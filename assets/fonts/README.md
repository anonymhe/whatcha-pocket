<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Fonts

Source typefaces for the embedded LVGL pixel fonts.

## Files

- `fusion-pixel-10px-monospaced-latin.ttf`, `fusion-pixel-10px-monospaced-zh_hans.ttf`
- `fusion-pixel-12px-monospaced-latin.ttf`, `fusion-pixel-12px-monospaced-zh_hans.ttf`

Family: Fusion Pixel Font (monospaced). License: SIL Open Font License 1.1
(redistributable). Keep the license notice with the files if you add or replace them.

## Generated fonts

`tools/gen_fonts.py` converts these TTFs into the LVGL C sources under
`main/assets/fonts/`:

- `font_fusion10.c` (`font_fusion10`): GB2312 + UI symbols, 10 px body text.
  Checked in but not built by default; add it back to the `WATCHA_FONT_SRCS`
  list in `main/CMakeLists.txt` (and declare it) before using, since it is a
  large source.
- `font_fusion12.c` (`font_fusion12`): GB2312 + UI symbols, 12 px body text.
- `font_fusion20.c` (`font_fusion20`): headline subset, 20 px (10 px face at 2x).
- `font_ps8.c` / `font_ps16.c`: ASCII-only pixel faces (Press Start 2P), added
  by hand rather than by the generator.

ASCII is taken from the latin face and CJK from the zh_hans face; the generator
drops any glyph the face does not carry. It also rewrites
`docs/watcha/fusion12_chars.txt` (the table `tools/check_glyphs.py` checks) and
reads the headline subset from `docs/watcha/fusion20_chars.txt`.

Rebuild from the repository root (needs Node for `npx lv_font_conv`):

```bash
python3 tools/gen_fonts.py
```

The ESP32-C3 has no PSRAM, so every built font costs internal Flash; drop
unused sizes from `main/CMakeLists.txt` before shipping.
