# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later

from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "verify_app_icon_assets", ROOT / "scripts" / "verify_app_icon_assets.py"
)
assert SPEC is not None and SPEC.loader is not None
ICONS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ICONS)


class AppIconAssetsTest(unittest.TestCase):
    def test_public_icon_set_has_all_valid_formats(self) -> None:
        root = ROOT / "assets" / "public"
        for kind, name in ICONS.NAMES.items():
            with self.subTest(kind=kind):
                ICONS.verify(root / name, kind)

    def test_wrong_format_and_missing_file_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "icon.ico"
            path.write_bytes(b"not an icon")
            with self.assertRaises(SystemExit):
                ICONS.verify(path, "ico")
            path.unlink()
            with self.assertRaises(SystemExit):
                ICONS.verify(path, "ico")


if __name__ == "__main__":
    unittest.main()
