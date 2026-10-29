<p align="right">
  <a href="lvgl-chinese-fonts.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# LVGL Chinese Text: Fonts and Troubleshooting

Use this guide when adding Chinese UI text, choosing a font, or displaying dynamic (user or network) content. It describes the fonts this firmware already ships and how to stay inside their coverage, for the LVGL 9.5.0 version pinned in [`dependencies.lock`](../../../dependencies.lock).

Unlike the upstream baseline, this firmware does **not** rely on Montserrat or on generating a new font per application. It ships a fixed, pre-built pixel CJK font and enforces its coverage with a tool. Read this before reaching for `lv_font_conv`.

## 1. The fonts this firmware ships

| Font | Declared in | Covers | Use for |
| --- | --- | --- | --- |
| `font_fusion12` | `main/*.c` via `LV_FONT_DECLARE` (e.g. [`main/pages.c`](../../../main/pages.c)) | Full GB2312 (~7,300 Chinese characters plus the symbols and nickname variants the UI needs) plus ASCII, 12 px pixel style | All Chinese UI body text |
| `font_fusion20` | [`main/pages/page_home.c`](../../../main/pages/page_home.c), [`main/pages/page_fortune.c`](../../../main/pages/page_fortune.c), [`main/pages/page_type.c`](../../../main/pages/page_type.c), [`main/pages/page_guide.c`](../../../main/pages/page_guide.c) | A ~690-character headline subset, 20 px pixel style (an exact 2× of the 10 px artwork) | Headline/name/result text only, and only characters inside the subset |
| `font_fusion10` | On disk (`main/assets/fonts/font_fusion10.c`) but not compiled (kept as source to save Flash) | Full GB2312 plus ASCII, 10 px | None today; re-add it to the `WATCHA_FONT_SRCS` list in `main/CMakeLists.txt` only if a page needs a smaller body size |
| `font_ps8` / `font_ps16` | `main/apps/app_ui.h` | ASCII only (digits, Latin), 8/16 px pixel style | Scores, timers, numeric readouts |

The generated font sources live in [`main/assets/fonts/`](../../../main/assets/fonts/) beside the LVGL image sources in [`main/assets/images/`](../../../main/assets/images/); the editable TTF inputs stay in `assets/`. The `font_fusion*` fonts are rebuilt by [`tools/gen_fonts.py`](../../../tools/gen_fonts.py) from the Fusion Pixel TTFs in [`assets/fonts/`](../../../assets/fonts/): `font_fusion12` from the 12 px face, and `font_fusion10` / `font_fusion20` from the 10 px face (the 20 px headline is the 10 px artwork rendered at exactly 2×, so it stays crisp). The headline carries only a small subset to save Flash. The body coverage of `font_fusion12` is listed, as a readable inventory, in [`docs/watcha/fusion12_chars.txt`](../../watcha/fusion12_chars.txt); the headline subset is listed in [`docs/watcha/fusion20_chars.txt`](../../watcha/fusion20_chars.txt). `CONFIG_LV_FONT_UNSCII_8` and `CONFIG_LV_FONT_UNSCII_16` are enabled in [`sdkconfig.defaults`](../../../sdkconfig.defaults) as ASCII pixel fallbacks; the Montserrat defaults carry no CJK glyphs and are not the display font here.

Because `font_fusion20` covers only a subset, a character can exist in `font_fusion12` yet still box out when drawn at 20 px. Any text rendered with `font_fusion20` (headlines, the result table in `page_type.c`, the fortune headline and rank labels in `page_fortune.c`, and the synced nickname on the home screen) must stay inside that subset. The glyph gate below enforces this per font, per line.

A character outside the selected font renders as a missing-glyph box on the device. Changing a source file's encoding does not add a glyph the font never contained; neither does enlarging the LVGL memory pool.

### 1.1 Headline glyphs the 10 px face lacks (custom pixel glyphs)

A few headline characters — most importantly the mascot name **猹** — exist in the 12 px face but **not** the 10 px face the headline is built from. They cannot be rendered crisply at 20 px: the 10 px grid has no such glyph, and borrowing the 12 px glyph means scaling a 12 px design to 20 px (a non-integer 1.67×), which smears the strokes into thin, broken lines.

For these, `gen_fonts.py` splices a **hand-authored 10 px pixel glyph** into the generated `font_fusion20.c`, doubled to a clean 20 px (exact 2×, matching every other headline glyph). The designs live in the `HEADLINE_CUSTOM` map in [`tools/gen_fonts.py`](../../../tools/gen_fonts.py) as `#`/`.` art on a 10×10 grid; `patch_custom_glyphs()` replaces the glyph's bitmap and descriptor after `lv_font_conv` runs and re-flows every following `bitmap_index` so the font stays self-consistent. To add or edit one, draw the 10 px grid in `HEADLINE_CUSTOM` and rerun `gen_fonts.py`; the glyph must already be present in the font (from the 12 px fallback) so there is a slot to overwrite. Note that lv_font_conv emits bytes at minimal width (`0x0`, `0xf`, `0xff`), so any tool that reparses the bitmap array must match `0x[0-9a-fA-F]+`, not a fixed two-digit pattern.

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

   It reports each string literal character the font does not cover, with the file and line. The checker is font-aware **per line**: if a line names a specific `font_fusion*`, its literals are validated against that font; otherwise they fall back to the smallest `font_fusion*` the file references. This matters because the headline `font_fusion20` is a small subset — a line drawing text with `font_fusion20` is checked against the 20 px subset even in a file that also uses `font_fusion12`. Known gaps to substitute by hand: `…` (use `...`) and `◀`.
3. Keep dynamic content (names, network text) inside the covered set, or show a deliberate replacement for anything outside it. A fixed menu subset does not guarantee arbitrary user or server text; do not silently drop unknown characters.

`check_glyphs.py` inspects C string literals; it matches each literal to the font named on its line (or the file's smallest font), but it cannot follow text passed through variables (for example, table data read at runtime), nor does it prove on-device rendering or check layout. Treat a clean run as a necessary, not a sufficient, condition.

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

Only for a maintainer deliberately extending the shipped fonts (for example, adding characters to the body or headline set) — not for app development, which must stay inside existing coverage. All three fonts are rebuilt by one reproducible script, [`tools/gen_fonts.py`](../../../tools/gen_fonts.py), which drives a pinned [`lv_font_conv`](https://github.com/lvgl/lv_font_conv) (via `npx`) against the Fusion Pixel TTFs in [`assets/fonts/`](../../../assets/fonts/). Do not hand-run `lv_font_conv` for the shipped fonts; edit the inputs and rerun the script so every font and its coverage table stay consistent:

```bash
python3 tools/gen_fonts.py   # rebuilds font_fusion10/12/20 and fusion12_chars.txt
```

- **Body text (`font_fusion12`)** covers all of GB2312 plus `EXTRA_SYMBOLS` (symbols and nickname variants outside GB2312). To add a character, add it to `EXTRA_SYMBOLS` in the script; it must exist in the 12 px face.
- **Headline (`font_fusion20`)** covers the list in [`docs/watcha/fusion20_chars.txt`](../../watcha/fusion20_chars.txt). To add a headline character, add it to that file. If the 10 px face has it, it renders crisply at 2×; if only the 12 px face has it, the script pulls it as a fallback (scaled, slightly soft). For a brand-critical character that must be crisp (e.g. 猹), add a hand-drawn 10 px design to `HEADLINE_CUSTOM` instead — see [§1.1](#11-headline-glyphs-the-10-px-face-lacks-custom-pixel-glyphs).
- An absent glyph in the source face cannot be created by requesting its code point; the script drops uncovered characters so the output has no blank glyphs.

The script regenerates `docs/watcha/fusion12_chars.txt` automatically so `check_glyphs.py` stays accurate. Keep the TTFs, the character lists, and any `HEADLINE_CUSTOM` art under version control. Output matches the LVGL version pinned in `dependencies.lock` and is uncompressed to avoid a decoder mismatch. See the [LVGL 9.5 font overview](https://docs.lvgl.io/9.5/main-modules/fonts/overview.html).

### Source font and license

The fonts are generated from [Fusion Pixel Font](https://github.com/TakWolf/fusion-pixel-font) by TakWolf, a pan-CJK pixel font released under [SIL Open Font License 1.1](https://openfontlicense.org/) (`OFL-1.1`). The project ships native pixel designs at several sizes; this firmware uses the monospaced 10 px and 12 px zh_hans and latin faces (in [`assets/fonts/`](../../../assets/fonts/)). 12 px is a native size, so `font_fusion12` renders crisply; `font_fusion20` is the native 10 px artwork rendered at exactly 2×, so it is crisp too. OFL-1.1 permits embedding the generated bitmaps in this firmware; keep the license notice and do not sell the font files on their own. If you regenerate from a newer upstream release, record the release version (date-based, e.g. `2026.10.25`) alongside the font sources.

## 8. Validate without overstating the result

Run the [complete repository gate](build-and-test.md) and `check_glyphs.py`. Inspect `idf.py size-components` / `idf.py size-files` for Flash and static RAM impact; a font compiled into read-only Flash is not the same as allocating it on the heap, but drawing and caching can still need internal RAM.

On the device, inspect every page and state, mixed Chinese/Latin/digit text, long lines, and dynamic input at both sides of the covered-set boundary — after page recreation and restart, not just the first label. Without this check, Chinese rendering stays `Unverified`; neither the host tests nor a successful firmware build verifies on-screen output.
