# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later

from __future__ import annotations

import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "build_artifact_provenance", ROOT / "scripts/build_artifact_provenance.py"
)
assert SPEC is not None and SPEC.loader is not None
PROVENANCE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROVENANCE)
LICENSE_SPEC = importlib.util.spec_from_file_location(
    "verify_c_client_release_licenses", ROOT / "scripts/verify_c_client_release_licenses.py"
)
assert LICENSE_SPEC is not None and LICENSE_SPEC.loader is not None
LICENSES = importlib.util.module_from_spec(LICENSE_SPEC)
LICENSE_SPEC.loader.exec_module(LICENSES)


class ReleaseProvenanceTest(unittest.TestCase):
    def make_checkout(self, root: Path) -> str:
        subprocess.run(["git", "init", "-q"], cwd=root, check=True)
        subprocess.run(["git", "config", "user.name", "test"], cwd=root, check=True)
        subprocess.run(["git", "config", "user.email", "test@example.invalid"], cwd=root, check=True)
        (root / "tracked.txt").write_text("source\n", encoding="utf-8")
        subprocess.run(["git", "add", "tracked.txt"], cwd=root, check=True)
        subprocess.run(["git", "commit", "-qm", "source"], cwd=root, check=True)
        return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()

    def test_clean_record_verifies_and_detects_artifact_changes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            checkout = base / "checkout"
            artifacts = base / "artifacts"
            checkout.mkdir()
            artifacts.mkdir()
            commit = self.make_checkout(checkout)
            binary = artifacts / "client.bin"
            binary.write_bytes(b"binary")
            PROVENANCE.write_record(artifacts, checkout, "linux")
            expected_source = PROVENANCE.source_identity(checkout)
            record = PROVENANCE.load_and_verify(artifacts, "linux", expected_source, True)
            self.assertEqual(record["source"]["source_commit"], commit)
            self.assertFalse(record["source"]["dirty"])
            binary.write_bytes(b"changed")
            with self.assertRaises(SystemExit):
                PROVENANCE.load_and_verify(artifacts, "linux", expected_source, True)

    def test_dirty_and_commit_mismatch_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            checkout = base / "checkout"
            artifacts = base / "artifacts"
            checkout.mkdir()
            artifacts.mkdir()
            commit = self.make_checkout(checkout)
            (checkout / "tracked.txt").write_text("dirty\n", encoding="utf-8")
            (artifacts / "client.bin").write_bytes(b"binary")
            PROVENANCE.write_record(artifacts, checkout, "windows")
            with self.assertRaises(SystemExit):
                PROVENANCE.load_and_verify(artifacts, "windows", None, True)
            mismatched = PROVENANCE.source_identity(checkout)
            mismatched["source_commit"] = "0" * 40
            with self.assertRaises(SystemExit):
                PROVENANCE.load_and_verify(artifacts, "windows", mismatched, False)

    def test_missing_record_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            artifacts = Path(directory)
            (artifacts / "client.bin").write_bytes(b"binary")
            with self.assertRaises(SystemExit):
                PROVENANCE.load_and_verify(artifacts, "macos", None, True)

    def test_integrated_record_keeps_verified_build_identity(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            checkout = base / "checkout"
            source = base / "source"
            integrated = base / "integrated"
            checkout.mkdir()
            source.mkdir()
            integrated.mkdir()
            commit = self.make_checkout(checkout)
            (source / "raw.bin").write_bytes(b"raw")
            source_record = PROVENANCE.write_record(source, checkout, "linux")
            expected_source = PROVENANCE.source_identity(checkout)
            verified = PROVENANCE.load_and_verify(source, "linux", expected_source, True)
            self.assertEqual(source_record.name, "BUILD_PROVENANCE.json")
            (integrated / "client" ).mkdir()
            (integrated / "client" / "client.bin").write_bytes(b"raw")
            PROVENANCE.write_record(integrated, checkout, "linux", verified)
            result = PROVENANCE.load_and_verify(integrated, "linux", expected_source, True)
            self.assertEqual(result["source"]["source_commit"], commit)
            self.assertIn("client/client.bin", result["files"])

    def test_fixed_release_license_allowlist(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            required = LICENSES.COMMON_REQUIRED + LICENSES.MACOS_REQUIRED
            for relative in required:
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("license\n", encoding="utf-8")
            notices = root / "THIRD_PARTY_NOTICES.md"
            notices.write_text("\n".join(LICENSES.NOTICE_REFERENCES), encoding="utf-8")
            (root / "RUNTIME_DEPENDENCIES.md").write_text(
                "\n".join(LICENSES.MACOS_REQUIRED), encoding="utf-8"
            )
            LICENSES.verify(root, "macos")
            (root / LICENSES.MACOS_REQUIRED[0]).unlink()
            with self.assertRaises(SystemExit):
                LICENSES.verify(root, "macos")


if __name__ == "__main__":
    unittest.main()
