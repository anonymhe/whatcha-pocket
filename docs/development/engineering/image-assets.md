<p align="right">
  <a href="image-assets.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Image Assets: Format Choice and Conversion

This firmware ships all 53 image sources as LVGL I8 indexed bitmaps. In RGB565A8 they occupied 881.9 KB; indexed they occupy 347.2 KB, which is 535 KB of the 3 MB application partition. This document records how the format works, when it is worth using, and the two defects that silently corrupt an indexed conversion.

## 1. Two storage formats

| | RGB565A8 | I8 indexed |
| --- | --- | --- |
| Bytes per pixel | 3 (2 RGB565 + 1 alpha) | 1 index |
| Fixed overhead | none | 1024 B palette (256 × `lv_color32_t`) |
| Colors | 65,536 × 256 alpha | 255 (index 0 is transparent) |
| Decode | pointer into flash, no RAM | line buffer, `w * 4` bytes |
| Cost for a 96 × 96 sprite | 27,648 B | 10,240 B |

`tools/gen_images.py` converts between them. Both formats are plain `lv_image_dsc_t` variables, so no page code changes when an asset switches.

## 2. The break-even point decides small icons

The 1024-byte palette is a fixed cost per file, so an indexed image is only smaller once its pixel count outweighs it. Solving `3n² = 1024 + n²` puts the crossover at roughly 22.6 px per side:

| Sprite | RGB565A8 | I8 | Result |
| --- | --- | --- | --- |
| 16 × 16 | 768 B | 1,280 B | indexed is **larger** |
| 22 × 22 | 1,452 B | 1,552 B | indexed is **larger** |
| 24 × 24 | 1,728 B | 1,600 B | indexed wins |
| 96 × 96 | 27,648 B | 10,240 B | indexed wins by 63% |

The six 22 × 22 home-screen icons are the awkward case: each grew by 100 B and the group nets about +0.6 KB. They are converted anyway, because a uniform format keeps the asset pipeline single-path and the loss is trivial next to the save on anything larger. Converting a *new* small icon below 24 px is not worth doing.

## 3. What the indexed data looks like

`bin_decoder` treats the map array as a palette followed by index rows. Three details are easy to get wrong and each one fails silently:

- **Palette first, exactly 256 entries.** `LV_COLOR_INDEXED_PALETTE_SIZE(I8)` is 256 regardless of how many entries are actually used, so the array always begins with 1024 bytes of palette and index data starts at offset 1024.
- **`lv_color32_t` is `{blue, green, red, alpha}`.** It is not RGBA. Writing RGBA puts every asset's colors through a red/blue swap that still renders as a plausible image, so the error survives a casual look.
- **Index rows are padded to `stride = (w + 3) & ~3`.** The row pitch in `emit()` must match what `LV_DRAW_BUF_STRIDE(w, cf)` computes from `CONFIG_LV_DRAW_BUF_STRIDE_ALIGN`. `sdkconfig.defaults` pins that option to 1 so an upstream change cannot shift every row.

Index 0 must be fully transparent (`0,0,0,0`), which is why a quantizer must never emit `0` for a visible pixel.

## 4. Decoding costs no RAM only with one option off

`LVGL` converts indexed images to ARGB8888 before the software renderer blends them. Which path it takes decides whether this is free:

- `CONFIG_LV_BIN_DECODER_RAM_LOAD=n` (this repo, pinned) decodes one row at a time through `get_area_cb`, needing only a `w * 4` line buffer. A 96 × 96 sprite costs 384 B of transient RAM.
- `CONFIG_LV_BIN_DECODER_RAM_LOAD=y` allocates `w * h * 4` per open. The same sprite costs 36.9 KB, and the 64 KB LVGL pool on this board cannot survive several such opens.

The option defaults to `n`, but nothing upstream guarantees it, so `sdkconfig.defaults` states it explicitly. A build that fits says nothing about this: the cost appears only at runtime, when a screen opens.

## 5. Quantizing artwork: keep the frequent colors exact

Palette art carries a few dominant colors plus a long tail of gradient in-between values along anti-aliased edges. A 96 × 96 sprite may hold 650 distinct colors of which 340 appear exactly once.

Sorting colors by pixel count and keeping the top 255 makes every high-frequency color exact, so flat regions and outlines are untouched; only the tail merges. This is the reason the conversion is acceptable at all: the error lands where the image is already transitioning.

## 6. Alpha must outweigh color when merging

Merging a tail color into its nearest palette entry needs a distance metric. Weighting all four channels equally lets a translucent edge pixel merge into a nearby *opaque* color, which discards its coverage: composited on the panel background those pixels jump by 218–224/255.

Weighting alpha four times higher than each color channel fixes most of it. Measured over the 38 lossy images, alpha error above 32/255 fell from 288 to 187 pixels and composite error from 258 to 229, at a small cost in flat RGB error, which is the right trade: a coverage jump is visible, a 1-pixel color shift is not. Removing alpha from the metric entirely makes both far worse (1,521 pixels), so the direction matters.

## 7. Two defects that corrupt a conversion silently

Both were found in this repository's first implementation and both produce an image that looks broken rather than wrong, so check for them in any new quantizer:

- **Long-tail colors defaulted to palette slot 0.** A zero-initialized `slot` array leaves unmerged colors at index 0, which is transparent. 833 pixels of one sprite were punched out of the artwork instead of taking an approximate color. Every visible color needs an explicit `slot >= 1`.
- **The distance sum overflowed in `int16`.** Squaring a channel difference of 253 gives 32,761 and adding three channels wraps negative, so `argmin` picked arbitrary entries. 424 of 490 merged colors landed on the wrong palette slot, which is why black pixels came out grey. Do this arithmetic in `int32`. Any such sum above 32,767 is suspect.

## 8. Measure what the user sees, not the raw channel

Comparing channel-by-channel against the original overstates the damage. What ships is the image composited on the panel background, so that is the metric to quote:

```bash
python3 tools/gen_images.py --verify
```

It reports, per image, the share of visible pixels whose RGB or alpha is off by more than 32/255, against the pre-conversion original recovered from git history. Current state: 15 of 53 sources convert pixel-identical; the other 38 composite with 229 of 164,432 visible pixels (0.14%) above 32/255, worst single image 0.87%, per-image mean error at most 2.2/255.

`--verify` is a difference report, not a pass/fail gate, because a merged palette is lossy by design. Wire it into CI only if the thresholds above become a hard budget.

## 9. What still needs a device check

A conversion can be numerically sound and still look wrong, and two cases deserve a look on hardware:

- **Scaled sprites.** `page_sync.c` draws the 19 `cha_*` sprites at half scale and `page_muyu.c` squashes `art_muyu_*` during its animation. LVGL's software transform interpolates neighbouring pixels, and the 1,503 source pixels that carry color under `alpha == 0` lose that color to palette slot 0. Bleeding is possible at the edge; only a screen check settles it.
- **The worst offenders.** `cha_duck` and `art_1024_win` carry the largest per-image error. If those two look clean, the rest are unlikely to show anything.

## 10. Adding an image

```bash
# drop the new .c into main/assets/images/ (images/ is picked up by file(GLOB))
python3 tools/gen_images.py --check    # size and color count, writes nothing
python3 tools/gen_images.py            # converts, refusing images above 255 colors
python3 tools/gen_images.py --force    # also converts pixel art with a long tail
python3 tools/gen_images.py --verify   # reports the difference per image
python3 tools/check_material.py        # confirms every asset has a reference
```

Images below 24 px per side are not worth indexing; leave them RGB565A8. A new source that exceeds 255 colors converts only when the long tail is genuinely gradient noise, which `--verify` and a look at the image both answer.
