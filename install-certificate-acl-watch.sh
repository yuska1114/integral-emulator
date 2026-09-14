#!/usr/bin/env bash
set -euo pipefail

SOURCE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_ROOT="/etc/integral-server"
CONFIG_FILE="${CONFIG_ROOT}/certificate-acl-watch.env"
HELPER_FILE="/usr/local/libexec/integral-server-certificate-acl-reapply"
SERVICE_FILE="/etc/systemd/system/integral-server-certificate-acl.service"
PATH_FILE="/etc/systemd/system/integral-server-certificate-acl.path"
SERVICE_USER="integral-server"

usage() {
  echo "usage: sudo ./install-certificate-acl-watch.sh [--service-user USER] CERT_FILE KEY_FILE" >&2
}

if [[ ${EUID} -ne 0 ]]; then
  echo "Certificate ACL watcher installer must be run with sudo." >&2
  exit 1
fi
if [[ ${1:-} == "--service-user" ]]; then
  if [[ $# -lt 2 ]]; then
    usage
    exit 2
  fi
  SERVICE_USER="$2"
  shift 2
fi
if [[ $# -ne 2 ]]; then
  usage
  exit 2
fi
if ! id "${SERVICE_USER}" >/dev/null 2>&1; then
  echo "Service user does not exist: ${SERVICE_USER}" >&2
  exit 1
fi
for command in flock getfacl realpath runuser setfacl systemctl; do
  if ! command -v "${command}" >/dev/null 2>&1; then
    echo "Required command is unavailable: ${command}" >&2
    exit 1
  fi
done

CERT_FILE="$(realpath -e -- "$1")"
KEY_FILE="$(realpath -e -- "$2")"
if [[ "${CERT_FILE}" == "${KEY_FILE}" ]]; then
  echo "Certificate and private key must be different files." >&2
  exit 1
fi
for target in "${CERT_FILE}" "${KEY_FILE}"; do
  if [[ ! -f "${target}" || -L "${target}" ]]; then
    echo "Certificate ACL target must be a regular non-symlink file: ${target}" >&2
    exit 1
  fi
  if [[ "${target}" =~ [[:space:]] ]]; then
    echo "Certificate ACL target path must not contain whitespace: ${target}" >&2
    exit 1
  fi
done

grant_traverse() {
  local parent
  parent="$(dirname -- "$1")"
  while [[ "${parent}" != "/" ]]; do
    setfacl -m "u:${SERVICE_USER}:--x" -- "${parent}"
    parent="$(dirname -- "${parent}")"
  done
}

grant_traverse "${CERT_FILE}"
grant_traverse "${KEY_FILE}"

install -d -o root -g root -m 0755 /usr/local/libexec
if [[ -e "${CONFIG_ROOT}" || -L "${CONFIG_ROOT}" ]]; then
  if [[ ! -d "${CONFIG_ROOT}" || -L "${CONFIG_ROOT}" ]]; then
    echo "Integral Server configuration path must be an existing non-symlink directory: ${CONFIG_ROOT}" >&2
    exit 1
  fi
else
  install -d -o root -g "${SERVICE_USER}" -m 0750 "${CONFIG_ROOT}"
fi
install -o root -g root -m 0755 \
  "${SOURCE_ROOT}/deploy/integral-server/certificate-acl-reapply" \
  "${HELPER_FILE}"
install -o root -g root -m 0644 \
  "${SOURCE_ROOT}/deploy/systemd/integral-server-certificate-acl.service" \
  "${SERVICE_FILE}"

CONFIG_TEMP=""
PATH_TEMP=""
cleanup() {
  [[ -n "${CONFIG_TEMP:-}" && -f "${CONFIG_TEMP}" ]] && rm -f -- "${CONFIG_TEMP}"
  [[ -n "${PATH_TEMP:-}" && -f "${PATH_TEMP}" ]] && rm -f -- "${PATH_TEMP}"
}
trap cleanup EXIT
CONFIG_TEMP="$(mktemp "${CONFIG_ROOT}/.certificate-acl-watch.env.XXXXXX")"
PATH_TEMP="$(mktemp "/etc/systemd/system/.integral-server-certificate-acl.path.XXXXXX")"

printf '%q=%q\n' \
  INTEGRAL_EMULATOR_CERTIFICATE_ACL_USER "${SERVICE_USER}" \
  INTEGRAL_EMULATOR_CERTIFICATE_ACL_CERT_FILE "${CERT_FILE}" \
  INTEGRAL_EMULATOR_CERTIFICATE_ACL_KEY_FILE "${KEY_FILE}" > "${CONFIG_TEMP}"
if [[ -L "${CONFIG_FILE}" ]]; then
  echo "Certificate ACL configuration must not be a symlink: ${CONFIG_FILE}" >&2
  exit 1
fi
install -o root -g root -m 0600 "${CONFIG_TEMP}" "${CONFIG_FILE}"
setfacl -b -- "${CONFIG_FILE}"
chown root:root "${CONFIG_FILE}"
chmod 0600 "${CONFIG_FILE}"

cat > "${PATH_TEMP}" <<EOF
[Unit]
Description=Watch Integral Server TLS certificate files for replacement

[Path]
PathChanged=${CERT_FILE}
PathChanged=${KEY_FILE}
Unit=integral-server-certificate-acl.service

[Install]
WantedBy=multi-user.target
EOF
install -o root -g root -m 0644 "${PATH_TEMP}" "${PATH_FILE}"

systemctl daemon-reload
systemctl start integral-server-certificate-acl.service
systemctl enable --now integral-server-certificate-acl.path

echo "Integral Server certificate ACL watcher installed."
echo "Certificate: ${CERT_FILE}"
echo "Private key: ${KEY_FILE}"
echo "Service user: ${SERVICE_USER}"
