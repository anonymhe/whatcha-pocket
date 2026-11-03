<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Assets

This directory stores reusable fonts, images, music, and sound effects, organized by asset type.

Keep each asset in the matching subdirectory and document its destination, naming, integration method, and source/license. Do not mix binary assets with Markdown documentation.

## Fonts

Store reusable font files in `fonts/`.

- Use descriptive names that include the family, weight, size, and format when relevant.
- Document the source, license, character range, conversion command, and expected destination.
- Check Flash and internal-RAM impact before adding a font; the ESP32-C3 has no PSRAM.
- Do not commit fonts whose license does not permit redistribution.

The C sources generated from those typefaces are firmware code, so they live in the
`main` component instead: `tools/gen_fonts.py` writes `main/assets/fonts/font_*.c`
(`main/assets/images/` holds the equivalent LVGL image sources). Keeping the binaries
here and the generated code there means a font or image change never sits in the same
directory as the code that draws it.

## Images

Store reusable source images in `images/`.

- Use descriptive names and document dimensions, pixel format, conversion steps, and destination.
- Prefer formats suitable for the 240 × 320 RGB565 display and account for Flash and internal RAM.
- Preserve editable sources where licensing permits, and record the source and license.
- Never commit device QR secrets, credentials, or personal data in images.

## Battery profiles

Two CW2017 fuel-gauge profiles are kept here because they describe two different
cells, not two calibrations of one cell. They differ in 51 of 80 bytes, so the
state-of-charge curve changes completely.

| File | Cell | Source |
| --- | --- | --- |
| [`battery/utl-520mah-upstream-42f16ab9.txt`](battery/utl-520mah-upstream-42f16ab9.txt) | UTL 520 mAh | byte-for-byte from `WatchaAI/WATCHA-POCKET` (file as of 42f16ab9), also shipped by this fork up to v0.1.3 |
| [`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt) | Wanli New Energy 500 mAh, profile2 / XIL40X | supplied 2026-10-20, ported from ai-passport |

Format: 80 lines of two uppercase hex digits, one byte per line in register order.
Both are embedded in `components/bsp/src/bsp_battery.c`; only one is compiled, chosen by
`CONFIG_WATCHA_BATTERY_PROFILE_WANLI_500MAH` (`components/bsp/Kconfig`, default `n` = UTL
520 mAh). `tests/test_battery_profile.py` pins each archive to the driver and to the
FNV-1a fingerprints the boot log prints (520 = `0xEC96A1A5`, 500 = `0xB9287814`).

These are fuel-gauge profiles, not charging configurations and not interchangeable
between cells of the same nominal capacity. See the
[hardware guide](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.md#9-cw2017-fuel-gauge).

## Music and sound effects

Store reusable music and sound-effect sources in `music/`.

- Document the source, license, sample rate, bit depth, channels, conversion command, and destination.
- Prefer 16 kHz, 16-bit mono PCM when it matches the current BSP audio path.
- Check Flash and internal-RAM cost before embedding audio; stream or chunk long recordings.
- Do not commit media without redistribution permission.
