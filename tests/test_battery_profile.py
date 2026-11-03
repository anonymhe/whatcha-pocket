#!/usr/bin/env python3
"""Keep the CW2017 supplier profile byte-exact and tied to the compiled driver.

The profile is opaque vendor data: the gauge's SOC curve is only valid for the cell
whose characterization produced these 80 bytes. This test pins all three profiles
that matter for the "SOC never reaches 100 %" investigation, so a later change
cannot quietly swap cells without the fingerprints in the boot log changing too:

* the archived Wanli 500 mAh profile (what the firmware must carry),
* the decoded bytes inside the driver (what actually gets written),
* the legacy 520 mAh bytes that still sit in git history and in the gauge of any
  device that has not yet been flashed with this firmware.

The FNV-1a fingerprints are printed by the boot-time diagnostic in
``bsp_battery.c``, so a serial log can be matched against this file.
"""

from __future__ import annotations

import re
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
WANLI_PROFILE = ROOT / "assets/battery/wanli-500mah-profile2-xil40x-20261020.txt"
UTL_PROFILE = ROOT / "assets/battery/utl-520mah-upstream-42f16ab9.txt"
DRIVER = ROOT / "components/bsp/src/bsp_battery.c"

# Pinned fingerprints; the boot log prints these so a serial capture can be matched here.
UTL_520_FNV1A = 0xEC96A1A5
WANLI_500_FNV1A = 0xB9287814

WANLI_FNV1A = 0xB9287814
UTL_520_FNV1A = 0xEC96A1A5

LEGACY_520_MAH = bytes(
    (
        0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xAD, 0xC7, 0xC8, 0xCA, 0xBD, 0xB1, 0xC1, 0x94,
        0x88, 0xD1, 0xBD, 0x97, 0x88, 0x66, 0x56, 0x4A,
        0x3F, 0x33, 0x26, 0x5C, 0x37, 0xD1, 0x27, 0xD8,
        0xCC, 0xB7, 0xCF, 0xB3, 0xB2, 0xAE, 0xA6, 0x9E,
        0x99, 0x97, 0x9B, 0x86, 0x47, 0x1E, 0x17, 0x26,
        0x49, 0x96, 0xD9, 0xE1, 0xDD, 0xDC, 0xD4, 0x59,
        0x00, 0x00, 0x90, 0x02, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5C,
    )
)


def parse_profile(text: str) -> bytes:
    lines = text.splitlines()
    if len(lines) != 80 or any(re.fullmatch(r"[0-9A-F]{2}", line) is None for line in lines):
        raise ValueError("profile must contain exactly 80 uppercase two-digit hex lines")
    return bytes(int(line, 16) for line in lines)


def fnv1a_32(payload: bytes) -> int:
    """Must match ``profile_fingerprint()`` in the driver."""
    value = 2166136261
    for byte in payload:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def fnv1a_32(payload: bytes) -> int:
    """Must match profile_fingerprint() in the driver."""
    value = 2166136261
    for byte in payload:
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return value


def driver_profile(name: str) -> bytes:
    """Pull one compiled profile initializer out of the driver by array name."""
    source = DRIVER.read_text(encoding="utf-8")
    match = re.search(
        rf"static\s+const\s+uint8_t\s+{name}\[\]\s*=\s*\{{([^}}]*)\}};",
        source,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"{name} must use an inferred array size {[]}")
    tokens = [token.strip() for token in match.group(1).strip().rstrip(",").split(",")]
    if not all(re.fullmatch(r"0x[0-9A-Fa-f]{2}", token) for token in tokens):
        raise AssertionError("profile initializer must contain only 0xNN byte tokens")
    return bytes(int(token, 16) for token in tokens)


class BatteryProfileTest(unittest.TestCase):
    def test_both_archives_are_80_bytes_and_match_the_driver(self) -> None:
        wanli = parse_profile(WANLI_PROFILE.read_text(encoding="ascii"))
        utl = parse_profile(UTL_PROFILE.read_text(encoding="ascii"))
        self.assertEqual(len(wanli), 80)
        self.assertEqual(len(utl), 80)
        self.assertEqual(driver_profile("s_profile_wanli_500mah"), wanli)
        self.assertEqual(driver_profile("s_profile_utl_520mah"), utl)

    def test_upstream_archive_is_the_520_cell_git_shipped(self) -> None:
        """The 520 archive must stay identical to what the upstream repo carries."""
        archived = parse_profile(UTL_PROFILE.read_text(encoding="ascii"))
        self.assertEqual(archived, LEGACY_520_MAH)

    def test_fingerprints_separate_the_two_cells(self) -> None:
        wanli = parse_profile(WANLI_PROFILE.read_text(encoding="ascii"))
        self.assertEqual(fnv1a_32(wanli), WANLI_FNV1A)
        self.assertEqual(fnv1a_32(LEGACY_520_MAH), UTL_520_FNV1A)
        self.assertNotEqual(WANLI_FNV1A, UTL_520_FNV1A)
        differing = sum(1 for a, b in zip(wanli, LEGACY_520_MAH) if a != b)
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
        self.assertIn(
            "#if CONFIG_WATCHA_BATTERY_PROFILE_WANLI_500MAH", source
        )

    def test_log_diagnostic_reports_the_same_fingerprint(self) -> None:
        """The boot log is the only place an end user can see which cell is loaded."""
        source = DRIVER.read_text(encoding="utf-8")
        self.assertIn("profile_fingerprint", source)
        self.assertIn("电量计诊断", source)
        self.assertIn("cw_log_diagnostics();", source)

    def test_fingerprints_match_what_the_boot_log_prints(self) -> None:
        self.assertEqual(fnv1a_32(parse_profile(UTL_PROFILE.read_text(encoding="ascii"))),
                         UTL_520_FNV1A)
        self.assertEqual(fnv1a_32(parse_profile(WANLI_PROFILE.read_text(encoding="ascii"))),
                         WANLI_500_FNV1A)

    def test_diagnostics_report_the_stored_profile_and_raw_voltage(self) -> None:
        """The boot line is the only place a user can tell which cell the gauge holds."""
        source = DRIVER.read_text(encoding="utf-8")
        self.assertIn("profile_fingerprint", source)
        self.assertIn("电量计诊断", source)
        self.assertIn("cw_log_diagnostics();", source)
        # Raw 14-bit VCELL is logged because integer mV hides charge movement.
        self.assertIn("VCELL=0x%04X", source)

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
