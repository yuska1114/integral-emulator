from __future__ import annotations

import hashlib
import ssl
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
EXPECTED_SHA256 = "f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9"


class MacOSCABundleTests(unittest.TestCase):
    def test_mozilla_ca_bundle_is_valid_and_documented(self) -> None:
        bundle = ROOT / "c_client/ssl/cacert.pem"
        readme = ROOT / "c_client/ssl/README.md"
        license_text = ROOT / "c_client/ssl/MPL-2.0.txt"

        self.assertEqual(hashlib.sha256(bundle.read_bytes()).hexdigest(), EXPECTED_SHA256)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        context.load_verify_locations(cafile=str(bundle))
        self.assertGreaterEqual(len(context.get_ca_certs()), 100)
        self.assertIn(EXPECTED_SHA256, readme.read_text(encoding="utf-8"))
        self.assertIn(
            "https://curl.se/ca/cacert.pem", readme.read_text(encoding="utf-8")
        )
        self.assertIn(
            "Mozilla Public License Version 2.0",
            license_text.read_text(encoding="utf-8"),
        )

    def test_macos_release_uses_bundle_for_all_tls_clients(self) -> None:
        builder = (ROOT / "scripts/build_c_client_release_macos.sh").read_text(
            encoding="utf-8"
        )
        self.assertIn(
            'cp -f "${PROJECT_ROOT}/c_client/ssl/cacert.pem" "${SSL_DIR}/cert.pem"',
            builder,
        )
        self.assertIn('export SSL_CERT_FILE="${CA_FILE}"', builder)
        self.assertIn(
            'export INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_CA_FILE="${CA_FILE}"',
            builder,
        )
        self.assertIn(
            'export INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_CA_FILE="${CA_FILE}"',
            builder,
        )
        self.assertNotIn("/opt/homebrew/etc/ca-certificates", builder)
        self.assertIn("LICENSES/third-party/mozilla-ca/MPL-2.0.txt", builder)

        client = (ROOT / "c_client/login_client.c").read_text(encoding="utf-8")
        self.assertIn(
            'getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_CA_FILE")', client
        )
        self.assertIn(
            'getenv("INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_CA_FILE")', client
        )
        self.assertIn("integral_gb_runtime_fixed_host_ca_file()", client)
        self.assertIn("n64_runtime_media_ca_file()", client)


if __name__ == "__main__":
    unittest.main()
