<p align="right">
  <a href="lvgl-chinese-fonts.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# LVGL Chinese Text: Fonts and Troubleshooting

Use this guide when adding Chinese UI text, choosing a font, or displaying dynamic (user or network) content. It describes the fonts this firmware already ships and how to stay inside their coverage, for the LVGL 9.5.0 version pinned in [`dependencies.lock`](../../../dependencies.lock).

Unlike the upstream baseline, this firmware does **not** rely on Montserrat or on generating a new font per application. It ships a fixed, pre-built pixel CJK font and enforces its coverage with a tool. Read this before reaching for `lv_font_conv`.

## 1. The fonts this firmware ships

| Font | Declared in | Covers | Use for |
| --- | --- | --- | --- |
| `font_fusion12` | `main/*.c` via `LV_FONT_DECLARE` (e.g. [`main/pages.c`](../../../main/pages.c)) | ~6,800 common Chinese characters plus ASCII, 12 px pixel style | All Chinese UI text |
| `font_fusion24` | [`main/page_type.c`](../../../main/page_type.c), [`main/page_fortune.c`](../../../main/page_fortune.c) | A ~650-character subset of `font_fusion12`, 24 px pixel style | Result/headline text only, and only characters inside the subset |
| `font_fusion16` | On disk but not compiled (removed from the build as dead Flash) | The same ~650-character subset, 16 px | None; `font_fusion16.c` is kept for reference. Re-add it to `main/CMakeLists.txt` only if a page starts using it |
| `font_ps8` / `font_ps16` | `main/apps/app_ui.h` | ASCII only (digits, Latin), 8/16 px pixel style | Scores, timers, numeric readouts |

All three `font_fusion*` fonts are generated from the same `fusion12.ttf`; `font_fusion16` and `font_fusion24` deliberately carry only a small symbol subset to save Flash. The exact subset of each is recorded in the `Opts:` header comment of its `main/font_fusion*.c` file, and `check_glyphs.py` reads those headers directly. The covered character set for `font_fusion12` is also listed, as a readable inventory, in [`docs/watcha/fusion12_chars.txt`](../../watcha/fusion12_chars.txt). `CONFIG_LV_FONT_UNSCII_8` and `CONFIG_LV_FONT_UNSCII_16` are enabled in [`sdkconfig.defaults`](../../../sdkconfig.defaults) as ASCII pixel fallbacks; the Montserrat defaults carry no CJK glyphs and are not the display font here.

Because `font_fusion24` covers only a subset, a character can exist in `font_fusion12` yet still box out when drawn at 24 px. Any text rendered with `font_fusion24` (today, the result name/quote table in `page_type.c` and the headline in `page_fortune.c`) must stay inside that subset. The glyph gate below enforces this per file.

A character outside `font_fusion12` renders as a missing-glyph box on the device. Changing a source file's encoding does not add a glyph the font never contained; neither does enlarging the LVGL memory pool.

## 2. Keep encoding, coverage, and style selection separate

Correct rendering needs every one of these layers to hold:

| Layer | Required condition | Typical failure |
| --- | --- | --- |
| Text input | Valid UTF-8, no truncated multibyte characters | Corrupted text even with the right font |
| Font coverage | Every Unicode code point exists in the selected font | ASCII works, Chinese is blank or boxed |
| Build integration | The font source is compiled and declared with `LV_FONT_DECLARE` | Undefined font symbol, or the intended font never selected |
| Widget style | The text-drawing part selects that font in its active state | Only titles, popups, or selected items fail |
| Layout/rendering | Visible color/opacity, enough space, valid font lifetime | Clipped or invisible text despite correct coverage |

UTF-8 only describes how to decode bytes into code points; it does not install a font. Save every source file and text resource as UTF-8, decode JSON escapes before display, and never truncate a UTF-8 string by an arbitrary byte count — cut only at a character boundary and leave room for the terminating NUL.

## 3. Staying inside coverage (the normal path)

For ordinary app work you do not add or generate a font. You:

1. Write Chinese labels with `font_fusion12` and numeric/Latin readouts with `font_ps8` / `font_ps16`, selecting the font explicitly on the label — see `ui_label()` in [`main/apps/app_ui.h`](../../../main/apps/app_ui.h).
2. Run the coverage gate and fix any boxed characters by substituting a covered character:

   ```bash
   python3 tools/check_glyphs.py            # scans main/ and main/apps/
   python3 tools/check_glyphs.py main/xxx.c # scans one file
   ```

   It reports each string literal character the font does not cover, with the file and line. The checker is font-aware: it validates each file against the *smallest* `font_fusion*` the file references, so a literal in a `font_fusion24` file (such as the result table in `page_type.c`) is checked against the 24 px subset, not the full 12 px set. Known gaps to substitute by hand: `…` (use `...`) and `◀`.
3. Keep dynamic content (names, network text) inside the covered set, or show a deliberate replacement for anything outside it. A fixed menu subset does not guarantee arbitrary user or server text; do not silently drop unknown characters.

`check_glyphs.py` inspects C string literals; it validates each file against the smallest font it uses, but it cannot follow text passed through variables (for example, table data read at runtime), nor does it prove on-device rendering or check layout. Treat a clean run as a necessary, not a sufficient, condition.

## 4. Selecting a font on a widget

Call these only after LVGL/display initialization, in the LVGL task or while holding `bsp_lvgl_lock()`:

```c
#include "lvgl.h"

LV_FONT_DECLARE(font_fusion12);   /* each .c that uses the font declares it */

lv_obj_t *label = lv_label_create(parent);
lv_obj_set_style_text_font(label, &font_fusion12, LV_PART_MAIN | LV_STATE_DEFAULT);
lv_label_set_text(label, "\u4E2D\u6587 ABC 123");   /* 中文 ABC 123 */
```

The escapes identify U+4E2D and U+6587 so the example reads identically in both document languages; a plain UTF-8 literal works too. Setting a screen or theme default font does not override a font already set explicitly on a child widget, so audit the part that actually draws the text and its focused, pressed, checked, and disabled states — not just the screen default.

## 5. Prove coverage and identify the active font

This helper checks a Unicode code point, not a UTF-8 byte or a multicharacter C literal:

```c
#include "lvgl.h"

bool app_font_has_glyph(const lv_font_t *font, uint32_t codepoint)
{
    if (font == NULL) return false;
    lv_font_glyph_dsc_t glyph = {0};
    return lv_font_get_glyph_dsc(font, &glyph, codepoint, 0)
        && !glyph.is_placeholder;
}
```

For an existing label, read `lv_obj_get_style_text_font(label, LV_PART_MAIN)` under the LVGL lock and check that font rather than an unrelated global symbol; repeat after each relevant state change. A positive result confirms descriptor coverage, not successful bitmap decoding or final screen output. Decode fixed text into code points and check each one, logging misses as `U+XXXX`; keep a known-missing negative case so the check cannot pass unconditionally.

## 6. Diagnose failures in order

| Symptom | Next action |
| --- | --- |
| ASCII works, Chinese is blank or boxed | Run `check_glyphs.py`; confirm the label uses `font_fusion12`; confirm the text is UTF-8 |
| A specific character is a box | It is outside `font_fusion12`; substitute a covered character (see `fusion12_chars.txt`) |
| Body text works, title or popup fails | Inspect local fonts, widget parts, separately created popups, and state overrides |
| Glyph check passes but text is blank | Check text color/opacity, hidden flags, label size, font lifetime, and LVGL allocation errors |
| Characters overlap or lose their top/bottom | Check line height, baseline, parent clipping, and label width/height |
| Undefined reference to a font | Check the `LV_FONT_DECLARE` name and that the font `.c` is in the component's sources |

## 7. Last resort: adding or regenerating a font

Only for a maintainer deliberately extending the shipped fonts (for example, adding characters to `font_fusion12`) — not for app development, which must stay inside existing coverage. Prepare a licensed TTF/OTF that actually contains the glyphs, install a pinned [`lv_font_conv`](https://github.com/lvgl/lv_font_conv), and record its version and command. Generate a subset, never a whole family, for the sizes in use:

```bash
lv_font_conv \
  --font /path/to/licensed-cjk-font.otf \
  --range 0x20-0x7E,0x4E2D,0x6587,0xFF0C,0x3002 \
  --size 12 --bpp 1 --format lvgl --no-compress \
  --lv-font-name font_fusion12 --lv-include lvgl.h \
  --output main/font_fusion12.c
```

An absent glyph in the source font cannot be created by requesting its code point. Regenerate `docs/watcha/fusion12_chars.txt` to match the new coverage so `check_glyphs.py` stays accurate, keep the character inventory and command under version control, and avoid enabling extra sizes or a full CJK family to fix one missing character. If you change which characters `font_fusion12` covers, regenerate `font_fusion16` / `font_fusion24` from the same TTF at their sizes too, so the subsets stay consistent. Match the output to the LVGL version pinned in `dependencies.lock`; start uncompressed to avoid a decoder mismatch. See the [LVGL 9.5 font overview](https://docs.lvgl.io/9.5/main-modules/fonts/overview.html).

### Source font and license

The `fusion12.ttf` these fonts are generated from is [Fusion Pixel Font](https://github.com/TakWolf/fusion-pixel-font) by TakWolf, a pan-CJK pixel font released under [SIL Open Font License 1.1](https://openfontlicense.org/) (`OFL-1.1`). The project ships native pixel designs at 8/10/12 px; 12 px is a native size, so `font_fusion12` renders crisply, while `font_fusion16` / `font_fusion24` are scaled from that artwork (24 px benefits from being a clean 2× of 12 px; 16 px is not an integer multiple and is softer). OFL-1.1 permits embedding the generated bitmaps in this firmware; keep the license notice and do not sell the font files on their own. If you regenerate from a newer upstream release, record the release version (date-based, e.g. `2026.10.25`) alongside the `lv_font_conv` command.

## 8. Validate without overstating the result

Run the [complete repository gate](build-and-test.md) and `check_glyphs.py`. Inspect `idf.py size-components` / `idf.py size-files` for Flash and static RAM impact; a font compiled into read-only Flash is not the same as allocating it on the heap, but drawing and caching can still need internal RAM.

On the device, inspect every page and state, mixed Chinese/Latin/digit text, long lines, and dynamic input at both sides of the covered-set boundary — after page recreation and restart, not just the first label. Without this check, Chinese rendering stays `Unverified`; neither the host tests nor a successful firmware build verifies on-screen output.
