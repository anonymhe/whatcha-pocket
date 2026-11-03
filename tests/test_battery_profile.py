#!/usr/bin/env python3
"""Keep both CW2017 supplier profiles byte-exact and tied to the compiled driver.

The profiles are opaque vendor data: the gauge's SOC curve is only valid for the cell
whose characterization produced these 80 bytes. Two cells are kept here because the
hardware changed between firmware releases:

* UTL 520 mAh, the profile the public WatchaAI/WATCHA-POCKET firmware carries and this
  fork shipped up to v0.1.3,
* Wanli New Energy 500 mAh (profile2 / XIL40X), introduced in v0.1.4 from ai-passport.

Exactly one is compiled, selected by CONFIG_WATCHA_BATTERY_PROFILE_WANLI_500MAH. The
test also asserts that the config symbol is backed by a real Kconfig definition: a
symbol that only exists in sdkconfig.defaults is silently ignored by the build, which
would make the option look like it works while changing nothing.
"""

from __future__ import annotations

import re
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
WANLI_PROFILE = ROOT / "assets/battery/wanli-500mah-profile2-xil40x-20261020.txt"
UTL_PROFILE = ROOT / "assets/battery/utl-520mah-upstream-42f16ab9.txt"
DRIVER = ROOT / "components/bsp/src/bsp_battery.c"


def parse_profile(text: str) -> bytes:
    lines = text.splitlines()
    if len(lines) != 80 or any(re.fullmatch(r"[0-9A-F]{2}", line) is None for line in lines):
        raise ValueError("profile must contain exactly 80 uppercase two-digit hex lines")
    return bytes(int(line, 16) for line in lines)


def driver_profile(name: str) -> bytes:
    """Pull one compiled profile initializer out of the driver by array name."""
    source = DRIVER.read_text(encoding="utf-8")
    match = re.search(
        rf"static\s+const\s+uint8_t\s+{name}\[\]\s*=\s*\{{([^}}]*)\}};",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"{name} must use an inferred array size so truncation is caught")
    tokens = [token.strip() for token in match.group(1).strip().rstrip(",").split(",")]
    if not all(re.fullmatch(r"0x[0-9A-Fa-f]{2}", token) for token in tokens):
        raise AssertionError("profile initializer must contain only 0xNN byte tokens")
    return bytes(int(token, 16) for token in tokens)


class BatteryProfileTest(unittest.TestCase):
    def test_both_archives_are_80_bytes_and_match_the_driver(self) -> None:
        self.assertEqual(driver_profile("s_profile_wanli_500mah"),
                         parse_profile(WANLI_PROFILE.read_text(encoding="ascii")))
        self.assertEqual(driver_profile("s_profile_utl_520mah"),
                         parse_profile(UTL_PROFILE.read_text(encoding="ascii")))

    def test_profiles_describe_different_cells(self) -> None:
        wanli = parse_profile(WANLI_PROFILE.read_text(encoding="ascii"))
        utl = parse_profile(UTL_PROFILE.read_text(encoding="ascii"))
        differing = sum(1 for a, b in zip(wanli, utl) if a != b)
        self.assertGreater(differing, 40, "the two cells must not look like one calibration")

    def test_driver_has_the_static_size_assertions(self) -> None:
        source = DRIVER.read_text(encoding="utf-8")
        self.assertRegex(source, r"#define\s+CW_PROFILE_SIZE\s+80\b")
        for name in ("s_profile_utl_520mah", "s_profile_wanli_500mah"):
            self.assertRegex(
                source,
                rf"_Static_assert\(sizeof\({name}\)\s*==\s*CW_PROFILE_SIZE",
            )

    def test_profile_choice_is_a_real_kconfig_symbol(self) -> None:
        """A config symbol with no Kconfig definition silently selects nothing."""
        kconfig = (ROOT / "components/bsp/Kconfig").read_text(encoding="utf-8")
        self.assertIn("config WATCHA_BATTERY_PROFILE_WANLI_500MAH", kconfig)
        defaults = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")
        self.assertIn("CONFIG_WATCHA_BATTERY_PROFILE_WANLI_500MAH", defaults)
        source = DRIVER.read_text(encoding="utf-8")
        self.assertIn("#if CONFIG_WATCHA_BATTERY_PROFILE_WANLI_500MAH", source)

    def test_parser_rejects_truncation_extra_bytes_and_bad_tokens(self) -> None:
        valid = ["00"] * 80
        cases = [valid[:-1], valid + ["00"], valid + [""]]
        for invalid in ("", "0", "000", "0x00", "GG", "ff", " 00", "00 ", "00 # comment"):
            cases.append(valid[:40] + [invalid] + valid[41:])
        for lines in cases:
            with self.subTest(lines=len(lines), token=lines[40]):
                with self.assertRaises(ValueError):
                    parse_profile("\n".join(lines) + "\n")


if __name__ == "__main__":
    unittest.main()
