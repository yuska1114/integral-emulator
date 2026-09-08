from __future__ import annotations

import unittest

from integral_emulator.errors import ValidationError
from integral_emulator.gb_runtime_link_modes import normalize_link_mode


class GBRuntimeLinkModeTests(unittest.TestCase):
    def test_only_canonical_values_are_accepted(self) -> None:
        self.assertEqual(normalize_link_mode("battle"), "battle")
        self.assertEqual(normalize_link_mode("trade"), "trade")
        for mode in ("BATTLE", " trade ", "", None):
            with self.subTest(mode=mode), self.assertRaisesRegex(
                ValidationError, "link mode must be battle or trade"
            ):
                normalize_link_mode(mode)

if __name__ == "__main__":
    unittest.main()
