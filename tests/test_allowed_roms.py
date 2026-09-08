# SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114)
# SPDX-License-Identifier: AGPL-3.0-or-later
from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

from integral_emulator.allowed_roms import AllowedRomCatalogError, find_allowed_rom, load_allowed_roms
from integral_emulator.errors import ValidationError
from integral_emulator.roms import find_allowed_registration


ROOT = Path(__file__).resolve().parents[1]


def entry(
    content_id: str,
    *,
    game_type: str = "sample_game",
    region: str = "GLOBAL",
    digit: str = "1",
    sha256: bool = True,
) -> dict:
    hashes = {
        "crc32": digit * 8,
        "md5": digit * 32,
        "sha1": digit * 40,
    }
    if sha256:
        hashes["sha256"] = digit * 64
    return {
        "content_id": content_id,
        "game_type": game_type,
        "platform": "gb",
        "display_name": f"Display {content_id}",
        "canonical_name": f"Canonical {content_id}",
        "region": region,
        "size": 32768,
        "hashes": hashes,
    }


def catalog(catalog_id: str, roms: list[dict], *, role: str = "primary") -> dict:
    return {
        "schema_version": 1,
        "catalog_id": catalog_id,
        "catalog_role": role,
        "roms": roms,
    }


class AllowedRomCatalogTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)

    def tearDown(self) -> None:
        self.temp_dir.cleanup()

    def write(self, name: str, payload: object) -> None:
        (self.root / name).write_text(json.dumps(payload), encoding="utf-8")

    def test_public_default_catalog_is_empty_and_game_independent(self) -> None:
        payload = json.loads(
            (ROOT / "config" / "allowed_roms" / "default.json").read_text(
                encoding="utf-8"
            )
        )
        self.assertEqual(
            payload,
            {
                "schema_version": 1,
                "catalog_id": "default",
                "catalog_role": "primary",
                "roms": [],
            },
        )

    def test_empty_catalog_allows_default_mode_and_rejects_strict_mode(self) -> None:
        import integral_emulator.allowed_roms as module

        previous = module._ALLOWED_ROMS
        try:
            module._ALLOWED_ROMS = ()
            self.assertIsNone(
                find_allowed_registration(
                    "gb",
                    "GLOBAL",
                    "1" * 64,
                    None,
                    allow_unlisted_roms=True,
                )
            )
            with self.assertRaisesRegex(
                ValidationError, "ROM is not in the enabled ROM catalog"
            ):
                find_allowed_registration(
                    "gb",
                    "GLOBAL",
                    "1" * 64,
                    None,
                    allow_unlisted_roms=False,
                )
        finally:
            module._ALLOWED_ROMS = previous

    def test_loads_multiple_catalogs_in_deterministic_catalog_order(self) -> None:
        self.write("z.json", catalog("catalog_z", [entry("item_z", digit="2")]))
        self.write("a.json", catalog("catalog_a", [entry("item_a", digit="1")]))
        loaded = load_allowed_roms(self.root)
        self.assertEqual([(item.catalog_id, item.content_id) for item in loaded], [
            ("catalog_a", "item_a"),
            ("catalog_z", "item_z"),
        ])

    def test_allows_an_explicit_empty_catalog_for_a_packless_installation(self) -> None:
        self.write("empty.json", catalog("empty_installation", []))
        self.assertEqual(load_allowed_roms(self.root), ())

    def test_normalizes_search_inputs_and_requires_platform_and_region_match(self) -> None:
        self.write("one.json", catalog("sample", [entry("release_one", game_type="sample_game")]))
        loaded = load_allowed_roms(self.root)
        rom = loaded[0]
        self.assertEqual(rom.sha256, "1" * 64)

        # The process-global search uses production catalogs; exercise equivalent
        # normalization through a temporary module-level catalog in a focused way.
        import integral_emulator.allowed_roms as module

        previous = module._ALLOWED_ROMS
        try:
            module._ALLOWED_ROMS = loaded
            self.assertIs(find_allowed_rom("gb", "global", "A" * 64), None)
            self.assertIs(find_allowed_rom("gb", "global", "A" * 64, "1" * 40), None)
            self.assertIs(find_allowed_rom("GB", "global", "1" * 64), rom)
            with self.assertRaisesRegex(AllowedRomCatalogError, "conflicts"):
                find_allowed_rom("n64", "GLOBAL", "1" * 64)
            with self.assertRaisesRegex(AllowedRomCatalogError, "conflicts"):
                find_allowed_rom("gb", "OTHER", "1" * 64)
        finally:
            module._ALLOWED_ROMS = previous

    def test_optional_rom_header_title_is_normalized_and_validated(self) -> None:
        rom = entry("with_header")
        rom["rom_header_title"] = "  pocket demo  "
        self.write("one.json", catalog("sample", [rom]))
        self.assertEqual(load_allowed_roms(self.root)[0].rom_header_title, "POCKET DEMO")

        rom["rom_header_title"] = "bad\x01header"
        self.write("one.json", catalog("sample", [rom]))
        with self.assertRaisesRegex(AllowedRomCatalogError, "rom_header_title"):
            load_allowed_roms(self.root)

    def test_rejects_empty_directory_and_invalid_json(self) -> None:
        with self.assertRaisesRegex(AllowedRomCatalogError, "no ROM catalogs"):
            load_allowed_roms(self.root)
        (self.root / "bad.json").write_text("{", encoding="utf-8")
        with self.assertRaisesRegex(AllowedRomCatalogError, "cannot parse JSON"):
            load_allowed_roms(self.root)

    def test_rejects_missing_field_bad_hash_and_nonpositive_size(self) -> None:
        cases = []
        missing = entry("missing")
        missing.pop("display_name")
        cases.append((missing, "display_name"))
        bad_hash = entry("bad_hash")
        bad_hash["hashes"]["sha256"] = "f" * 63
        cases.append((bad_hash, "hashes.sha256"))
        bad_size = entry("bad_size")
        bad_size["size"] = -1
        cases.append((bad_size, "size must"))

        for index, (rom, message) in enumerate(cases):
            with self.subTest(index=index):
                case_root = self.root / str(index)
                case_root.mkdir()
                (case_root / "catalog.json").write_text(
                    json.dumps(catalog(f"catalog_{index}", [rom])), encoding="utf-8"
                )
                with self.assertRaisesRegex(AllowedRomCatalogError, message):
                    load_allowed_roms(case_root)

    def test_rejects_duplicate_catalog_content_and_hashes(self) -> None:
        duplicate_cases = [
            (
                catalog("same", [entry("one", digit="1")]),
                catalog("same", [entry("two", digit="2")]),
                "duplicate catalog_id",
            ),
            (
                catalog("first", [entry("same", digit="1")]),
                catalog("second", [entry("same", digit="2")]),
                "duplicate content_id",
            ),
            (
                catalog("first", [entry("one", digit="1")]),
                catalog("second", [entry("two", digit="1")]),
                "duplicate crc32",
            ),
        ]
        for index, (first, second, message) in enumerate(duplicate_cases):
            with self.subTest(message=message):
                case_root = self.root / str(index)
                case_root.mkdir()
                (case_root / "a.json").write_text(json.dumps(first), encoding="utf-8")
                (case_root / "b.json").write_text(json.dumps(second), encoding="utf-8")
                with self.assertRaisesRegex(AllowedRomCatalogError, message):
                    load_allowed_roms(case_root)

    def test_catalog_accepts_sha1_as_an_explicit_hash(self) -> None:
        self.write("primary.json", catalog("primary", [entry("primary", sha256=False)]))
        loaded = load_allowed_roms(self.root)
        self.assertIsNone(loaded[0].sha256)
        self.assertEqual(loaded[0].sha1, "1" * 40)

        import integral_emulator.allowed_roms as module

        previous = module._ALLOWED_ROMS
        try:
            module._ALLOWED_ROMS = loaded
            self.assertIs(find_allowed_rom("gb", "GLOBAL", "A" * 64, "1" * 40), loaded[0])
        finally:
            module._ALLOWED_ROMS = previous

    def test_optional_game_pack_accepts_only_enabled_sha256_entries(self) -> None:
        enabled = entry("enabled", digit="1")
        disabled = entry("disabled", digit="2", sha256=False)
        disabled["enabled"] = False
        self.write(
            "optional.json",
            catalog("optional", [disabled, enabled], role="optional_game_pack"),
        )
        loaded = load_allowed_roms(self.root)
        self.assertEqual([item.content_id for item in loaded], ["enabled"])
        self.assertEqual(loaded[0].catalog_role, "optional_game_pack")

if __name__ == "__main__":
    unittest.main()
