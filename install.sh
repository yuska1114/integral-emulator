#!/usr/bin/env bash
set -euo pipefail

SOURCE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INSTALL_ROOT="/opt/integral-server"
APP_ROOT="${INSTALL_ROOT}/app"
VENV_ROOT="${INSTALL_ROOT}/venv"
CONFIG_ROOT="/etc/integral-server"
CONFIG_FILE="${CONFIG_ROOT}/integral-server.env"
STORAGE_ROOT="/var/lib/integral-server"
SERVICE_FILE="/etc/systemd/system/integral-server.service"
MEDIA_RELAY_SERVICE_FILE="/etc/systemd/system/integral-server-media-relay.service"
UNINSTALL_HELPER="/usr/local/libexec/integral-server-uninstall"

if [[ ${EUID} -ne 0 ]]; then
  echo "install.sh must be run with sudo." >&2
  exit 1
fi

if [[ ! -f /etc/os-release ]]; then
  echo "Cannot identify the operating system." >&2
  exit 1
fi
# shellcheck disable=SC1091
source /etc/os-release
if [[ "${ID:-}" != "ubuntu" ]]; then
  echo "This installer currently supports Ubuntu only." >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1 \
  || ! dpkg-query -W -f='${Status}' python3-venv 2>/dev/null | grep -q 'ok installed'; then
  apt-get update
  DEBIAN_FRONTEND=noninteractive apt-get install -y python3 python3-venv
fi

python3 -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 11) else 1)' || {
  echo "Python 3.11 or newer is required." >&2
  exit 1
}

if ! id integral-server >/dev/null 2>&1; then
  useradd --system --home-dir "${STORAGE_ROOT}" --shell /usr/sbin/nologin integral-server
fi

install -d -m 0755 "${APP_ROOT}" "${APP_ROOT}/src" "${APP_ROOT}/config" "${APP_ROOT}/LICENSES"
install -d -m 0750 -o integral-server -g integral-server "${STORAGE_ROOT}"
install -d -m 0750 -o root -g integral-server "${CONFIG_ROOT}"

rm -rf "${APP_ROOT}/src/integral_emulator" "${APP_ROOT}/config/gb_mobile"
install -d -m 0755 "${APP_ROOT}/config"
cp -R "${SOURCE_ROOT}/src/integral_emulator" "${APP_ROOT}/src/"
cp -R "${SOURCE_ROOT}/config/." "${APP_ROOT}/config/"
install -m 0644 "${SOURCE_ROOT}/pyproject.toml" "${APP_ROOT}/pyproject.toml"
install -m 0644 "${SOURCE_ROOT}/LICENSES/AGPL-3.0-or-later.txt" "${APP_ROOT}/LICENSES/AGPL-3.0-or-later.txt"

if [[ ! -x "${VENV_ROOT}/bin/python3" ]]; then
  python3 -m venv "${VENV_ROOT}"
fi
install -m 0755 "${SOURCE_ROOT}/deploy/integral-server/integral-server" "${VENV_ROOT}/bin/integral-server"
install -d -m 0755 /usr/local/libexec
install -m 0755 "${SOURCE_ROOT}/deploy/integral-server/uninstall" "${UNINSTALL_HELPER}"
ln -sfn "${VENV_ROOT}/bin/integral-server" /usr/local/bin/integral-server

INTEGRAL_SERVER_CONFIG="${CONFIG_FILE}" integral-server init --storage-root "${STORAGE_ROOT}"
chown root:integral-server "${CONFIG_FILE}"
chmod 0640 "${CONFIG_FILE}"
chown -R root:root "${APP_ROOT}" "${VENV_ROOT}"
chown -R integral-server:integral-server "${STORAGE_ROOT}"
install -d -m 2750 -o root -g integral-server "${STORAGE_ROOT}/mobile-packages"

install -m 0644 "${SOURCE_ROOT}/deploy/systemd/integral-server.service" "${SERVICE_FILE}"
install -m 0644 "${SOURCE_ROOT}/deploy/systemd/integral-server-media-relay.service" "${MEDIA_RELAY_SERVICE_FILE}"
systemctl daemon-reload
systemctl enable integral-server.service integral-server-media-relay.service

echo "INTEGRAL EMULATOR server installed."
echo "Review network mode and media relay settings in ${CONFIG_FILE}."
echo "Run 'sudo integral-server start' to start the API and media relay."
echo "Run 'sudo integral-server doctor' to inspect the installation."
echo "Run 'sudo integral-server uninstall' to remove the application and keep persistent data."
