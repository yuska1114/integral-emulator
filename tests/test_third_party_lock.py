# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: GPL-3.0-or-later

from __future__ import annotations

import copy
import importlib.util
from pathlib import Path
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
