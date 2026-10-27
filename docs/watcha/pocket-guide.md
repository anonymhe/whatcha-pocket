<p align="right">
  <a href="pocket-guide.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pocket New Development Spec (for AI coding agents)

WATCHA POCKET apps come in two groups:

- **Pocket Classics (口袋常驻)**: the 6 built-in apps on the home screen. Fixed; do not modify.
- **Pocket New (口袋上新)**: the continuously updated app list. **Long-press ▲ on the home screen** to open it, press OK to launch an app, long-press OK inside an app to return.

Every new app goes into Pocket New. This file holds the coding rules; how to talk to and guide the user is in [`CHACKER.md`](../../CHACKER.md).

## 1. Adding an app takes two steps

1. Create `main/apps/app_<name>.c` and export a page at the end:
   ```c
   const page_t app_xxx = { "app_xxx", app_enter, app_exit, app_key };
   ```
   Files in `main/apps/` are compiled automatically; **do not edit CMakeLists.txt**. Run `idf.py reconfigure` once after adding a file so the build system picks it up.
2. Add one `extern` line and one entry to `main/apps/apps.c`:
   ```c
   extern const page_t app_xxx;
   { "某某猹", "one-line description (≤12 chars)", &app_xxx },
   ```

**Example:** `main/apps/app_decide.c`. Read it first and follow its structure.

Start every app file with a comment block describing the app (name, how to play, keys, round flow, what is persisted, sounds, and an 11×11 icon bitmap).

## 2. Scope

- **Only edit files in `main/apps/`.** Do not touch the rest of `main/`, `components/`, `partitions.csv` or `sdkconfig*` unless the user explicitly asks.
- Leave the 6 home apps, boot animation, BLE sync, partition table and Recovery alone.
- Name apps "xx猹", keep the tone light, avoid jargon.
- **No third-party logos, brand images or existing anime/game characters.**

## 3. Page contract (page_t)

| Function | Does | Notes |
| --- | --- | --- |
| `app_enter()` | `s_scr = page_screen_create();`, build widgets, then `lv_screen_load(s_scr);` | Rebuilt on every entry; reset state |
| `app_exit()` | Delete every `lv_timer` you created, then `lv_obj_delete(s_scr)` | **Clean up completely** or the device crashes on return |
| `app_key(btn, ev)` | Handle buttons | Never block or `vTaskDelay`; slow work goes to a worker task |

- Buttons: `BSP_BTN_UP` / `BSP_BTN_DOWN` / `BSP_BTN_OK`; events: `BSP_BTN_PRESS` (instant, for games), `BSP_BTN_CLICK`, `BSP_BTN_DOUBLE`, `BSP_BTN_LONG`.
- **Long-press OK is reserved** (back to Pocket New).
- Use `lv_timer_create(cb, ms, NULL)` for animation and game loops; callbacks run in the LVGL task.

## 4. Visual spec

Same language as the home screen: 8-bit pixel style, square corners, no shadows, no gradients.

- 240×320, dark background. `ui_topbar(scr, title)` (title at (18,9), line at y=26) and `ui_footer(scr, hint)` (line at y=292, hint at y=298). Content area ≈ y=34–286. Helpers live in `main/apps/app_ui.h`.
- Colors (from `main/pages.h`; use these for the UI. If gameplay truly needs one or two extra colors, e.g. a red light, define them inside your own app file such as `#define C_RED 0xE05A4F`, never in `pages.h`): `C_BG` background, `C_GREEN` titles/accent/selection, `C_GOLD` results/highlights, `C_TEXT` body, `C_MUTED` secondary, `C_CARD` card fill, `C_LINE` dividers/unselected borders.
- Cards (same as the home grid): 60×52, `C_CARD` fill, 1px `C_LINE` border, 2px `C_GREEN` when selected; 3 columns, column pitch 72, row pitch 62, left margin 18. Icons are 11×11 one-color bitmaps drawn at 2× (22×22) in `C_GREEN`, 6px from the card top; the name sits 5px from the bottom. Pocket New is moving to this card grid, so include your 11×11 icon (`#` on, `.` off) in the app's header comment.
- Fonts: Chinese text must use `font_fusion12`; `font_ps8` / `font_ps16` are ASCII-only. The font holds about 6,800 common characters; always run `python3 tools/check_glyphs.py`. Known missing: `…` (use `...`), `◀`.

## 5. Ready-made assets

| Need | Use |
| --- | --- |
| A cha image (72×64) | `#include "cha_pool.h"`, `lv_image_set_src(img, cha_random_72());`, scale with `lv_image_set_scale(img, 128)` |
| 8-bit sound | `#include "sfx.h"`, `sfx_play(SFX_NAV)`; common: `SFX_NAV`, `SFX_OK`, `SFX_BLIP`, `SFX_POP`, `SFX_FANFARE`, `SFX_RECORD`, `SFX_BAD`, `SFX_CLEAR` |
| Persist a value | `#include "storage.h"`, `store_get_u32("key", def)` / `store_set_u32("key", v)`; keys ≤15 chars, never write every frame |
| Random numbers | `#include "esp_random.h"`, `esp_random()` |

Cha artwork is licensed CC BY-NC 4.0; see [`ASSETS_LICENSE.md`](../../ASSETS_LICENSE.md).

## 6. Networking (experimental, not recommended yet)

The chip has Wi-Fi, but with BLE sync running while the device is in use, heap is too tight for reliable HTTPS. Tell the user and suggest an offline version first. If you must try: credentials go in the gitignored `main/apps/wifi_config.h` (never commit or copy them elsewhere; the build must work without it), network work runs in a worker task that never touches LVGL, and Wi-Fi is stopped and deinitialized right after fetching. Public Watcha API: `GET https://watcha.cn/api/v2/products?skip=0&limit=5`.

## 7. Build, flash, deliver

```bash
. $IDF_PATH/export.sh
python3 tools/check_glyphs.py
idf.py reconfigure     # after adding a .c file
idf.py build
idf.py -p <port> flash
```

If the device does not boot, power it off, then hold ▲ while powering on for 5 seconds to enter Recovery and reflash. Report Build / Host tests / Device tests in the AGENTS.md format (**Device tests: NOT RUN** unless tested on hardware).
