# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
MODULE_PATH = ROOT / "scripts" / "verify_third_party_lock.py"
SPEC = importlib.util.spec_from_file_location("verify_third_party_lock", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
LOCK_MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(LOCK_MODULE)


class ThirdPartyLockTests(unittest.TestCase):
    def test_repository_third_party_lock_verifies(self) -> None:
        lock = LOCK_MODULE.load_lock(ROOT / "THIRD_PARTY_LOCK.json")

        LOCK_MODULE.verify_local(ROOT, lock)

    def test_n64_build_patch_inventory_matches_lock(self) -> None:
        lock = LOCK_MODULE.load_lock(ROOT / "THIRD_PARTY_LOCK.json")
        build_script = (ROOT / "runtimes/n64/tools/build_source_stack.sh").read_text(
            encoding="utf-8"
        )
        applied = re.findall(
            r'apply_patch_once "\$(core|input|audio)_build" "\$project_root/patches/([^"/]+\.patch)"',
            build_script,
        )
        components = {
            "core": "mupen64plus-core",
            "input": "mupen64plus-input-sdl",
            "audio": "mupen64plus-audio-sdl",
        }
        for target, component_name in components.items():
            component = next(
                item for item in lock["components"] if item["component"] == component_name
            )
            declared = [
                Path(patch["path"]).name for patch in component["build_patches"]
            ]
            actual = [name for build_target, name in applied if build_target == target]
            if target == "audio":
                actual = re.findall(
                    r'apply_patch_once "\$component_build"\s*\\\s*"\$project_root/patches/([^"/]+\.patch)"',
                    build_script,
                )
            self.assertEqual(
                actual,
                declared,
                component_name,
            )

    def test_n64_change_notices_cover_each_upstream_patch_target(self) -> None:
        lock = LOCK_MODULE.load_lock(ROOT / "THIRD_PARTY_LOCK.json")
        components = {
            component["component"]: component
            for component in lock["components"]
            if component["component"] in {
                "mupen64plus-core",
                "mupen64plus-input-sdl",
                "mupen64plus-audio-sdl",
                "GLideN64",
            }
        }
        inventoried = {
            record["path"]
            for component in lock["components"]
            for group in ("build_patches", "materialized_patches")
            for record in component[group]
            if record["path"].startswith("runtimes/n64/patches/")
        }
        self.assertEqual(
            {str(path.relative_to(ROOT)) for path in (ROOT / "runtimes/n64/patches").glob("*.patch")},
            inventoried,
        )
        for name, component in components.items():
            records = component["materialized_patches"] if name == "GLideN64" else component["build_patches"]
            notice_records = [
                record for record in records
                if record["path"].endswith("-integral-change-notices.patch")
            ]
            self.assertEqual(len(notice_records), 1, name)
            self.assertIs(records[-1], notice_records[0], name)
            targets: dict[str, list[str]] = {}
            for record in records[:-1]:
                patch_name = Path(record["path"]).name
                patch = (ROOT / record["path"]).read_text(encoding="utf-8")
                for line in patch.splitlines():
                    if line.startswith("-") and not line.startswith("--- "):
                        self.assertNotRegex(
                            line,
                            r"Copyright|GNU General Public License|SPDX-License-Identifier",
                            patch_name,
                        )
                for target in re.findall(r"^\+\+\+ b/([^\t\n]+)", patch, re.MULTILINE):
                    targets.setdefault(target, []).append(patch_name)
            notice_patch = (ROOT / notice_records[0]["path"]).read_text(encoding="utf-8")
            sections = re.split(r"(?=^--- a/)", notice_patch, flags=re.MULTILINE)[1:]
            notice_by_target: dict[str, str] = {}
            for section in sections:
                match = re.search(r"^\+\+\+ b/([^\t\n]+)", section, re.MULTILINE)
                self.assertIsNotNone(match, name)
                target = match.group(1)
                self.assertNotIn(target, notice_by_target, name)
                notice_by_target[target] = section
                self.assertRegex(section, r"@@ -1,\d+ \+1,\d+ @@")
                self.assertFalse(
                    any(line.startswith("-") and not line.startswith("--- ") for line in section.splitlines()),
                    target,
                )
                self.assertRegex(section, r"Integral Emulator modification notice \(\d{4}-\d{2}-\d{2}\)")
            self.assertEqual(set(notice_by_target), set(targets), name)
            for target, patch_names in targets.items():
                for patch_name in patch_names:
                    self.assertRegex(
                        notice_by_target[target],
                        r"\* \d{4}-\d{2}-\d{2}: " + re.escape(patch_name) + r":",
                    )
                if "mupen64plus-core-current-rdram.patch" in patch_names:
                    self.assertIn("Partial backport of Mupen64Plus upstream", notice_by_target[target])
                    self.assertIn("b954248ad7944acdfc8d0f5edb4e752b76891394", notice_by_target[target])
                    self.assertIn("a70a6cca5a29b1711dcd3dc0ee15a9ffad8f45d0", notice_by_target[target])

    @unittest.skipUnless(shutil.which("patch"), "patch utility is needed for post-application notice check")
    def test_n64_applied_build_patches_keep_notices_and_upstream_headers(self) -> None:
        lock = LOCK_MODULE.load_lock(ROOT / "THIRD_PARTY_LOCK.json")
        for component in lock["components"]:
            if component["component"] not in {
                "mupen64plus-core",
                "mupen64plus-input-sdl",
                "mupen64plus-audio-sdl",
                "GLideN64",
            }:
                continue
            source = ROOT / component["source_path"]
            records = component["materialized_patches"] if component["component"] == "GLideN64" else component["build_patches"]
            with tempfile.TemporaryDirectory(prefix="integral-n64-notice-test-") as temporary:
                result_tree = Path(temporary) / component["component"]
                shutil.copytree(source, result_tree)
                if component["component"] != "GLideN64":
                    for record in records:
                        with (ROOT / record["path"]).open("rb") as patch_stream:
                            subprocess.run(
                                ["patch", "--batch", "-s", "-d", str(result_tree), "-p1"],
                                stdin=patch_stream,
                                check=True,
                                stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE,
                            )
                targets = {
                    target
                    for record in records[:-1]
                    for target in re.findall(
                        r"^\+\+\+ b/([^\t\n]+)",
                        (ROOT / record["path"]).read_text(encoding="utf-8"),
                        re.MULTILINE,
                    )
                }
                for target in targets:
                    original = (source / target).read_text(encoding="utf-8")
                    applied = (result_tree / target).read_text(encoding="utf-8")
                    self.assertTrue(applied.startswith("/*\n * Integral Emulator modification notice ("), target)
                    for line in original.splitlines()[:50]:
                        if re.search(r"Copyright|GNU General Public License|either version 2|any later version", line):
                            self.assertIn(line, applied, target)

    def test_patch_hash_tampering_is_rejected(self) -> None:
        lock = copy.deepcopy(LOCK_MODULE.load_lock(ROOT / "THIRD_PARTY_LOCK.json"))
        lock["components"][0]["materialized_patches"][0]["sha256"] = "0" * 64

        with self.assertRaisesRegex(LOCK_MODULE.LockError, "patch hash mismatch"):
            LOCK_MODULE.verify_local(ROOT, lock)

    def test_declared_checkout_crlf_is_normalized_for_inventory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "project.vcxproj"
            source.write_bytes(b"first\nsecond\n")
            expected = LOCK_MODULE.inventory_digest(root, {source.name: "file"})

            source.write_bytes(b"first\r\nsecond\r\n")
            actual = LOCK_MODULE.inventory_digest(
                root,
                {source.name: "file"},
                [source.name],
            )

            self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()
