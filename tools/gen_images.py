#!/usr/bin/env python3
"""把 main/assets/images/ 里色彩数足够的 RGB565A8 图片源转成 LVGL I8 索引色源。

RGB565A8 每像素 3 字节（2 字节 RGB565 + 1 字节 alpha）；I8 每像素 1 字节
索引 + 固定 1024 字节调色板。只有「可见颜色 ≤255 种」的图才无损可转：
这类图主要是双色小图标（ic_*）与 logo。带抗锯齿渐变的美术图（art_*/
cha_* 大多数）唯一色达 300-750 种，截断到 256 会把 ~8% 像素指到色差
>96/255 的颜色上，视觉损伤不可接受，保持 RGB565A8 不动。

LVGL 9 对 RAM 中的 I8 变量图走 get_area_cb 逐行查表解码为 ARGB8888
（行缓冲 w*4 字节），不整图进 RAM，适合无 PSRAM 的 ESP32-C3。调色板
条目是 lv_color32_t（内存序 {blue,green,red,alpha}），alpha 存在调色
板里，索引 0 约定为透明（RGBA 全 0）。

用法：
    python3 tools/gen_images.py --check   # 只报告可转图与节省量，不改文件
    python3 tools/gen_images.py           # 原地重写可无损转换的图
    python3 tools/gen_images.py --force   # 超色图也有损转换（默认拒绝）
"""
from __future__ import annotations

import argparse
import os
import re

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
IMAGE_DIR = os.path.join(ROOT, 'main', 'assets', 'images')

MAX_PALETTE = 256          # I8 索引空间
VISIBLE_SLOTS = 255        # 索引 0 留给透明

MAP_RE = re.compile(r'(uint8_t\s+\w+_map\[\]\s*=\s*\{)')
DSC_RE = re.compile(
    r'const lv_image_dsc_t\s+(\w+)\s*=\s*\{.*?\};', re.S)
HEADER_RE = re.compile(
    r'\.magic = LV_IMAGE_HEADER_MAGIC,\s*'
    r'\.cf = (?P<cf>[\w]+),\s*'
    r'\.flags = (?P<flags>\w+),\s*'
    r'\.w = (?P<w>\d+),\s*'
    r'\.h = (?P<h>\d+),\s*'
    r'\.stride = (?P<stride>\d+),')


def parse_rgb565a8(path):
    """返回 (name, w, h, pixels[N,4] uint8 RGBA) 或 None（非 RGB565A8）。"""
    with open(path, 'r', errors='ignore') as f:
        src = f.read()
    dsc = DSC_RE.search(src)
    if not dsc:
        return None
    name = dsc.group(1)
    hdr = HEADER_RE.search(dsc.group(0))
    if not hdr or hdr.group('cf') != 'LV_COLOR_FORMAT_RGB565A8':
        return None
    w, h = int(hdr.group('w')), int(hdr.group('h'))
    n = w * h
    m = MAP_RE.search(src)
    if not m:
        return None
    body_start = src.find('{', m.start()) + 1
    body = src[body_start:src.find('};', body_start)]
    byts = [t.strip() for t in body.split(',') if t.strip().startswith('0x')]
    if len(byts) < n * 3:
        raise ValueError(f'{path}: expected {n * 3} bytes, got {len(byts)}')
    px = np.zeros((n, 4), dtype=np.uint8)
    rgb = np.array([int(t, 16) for t in byts[:n * 2]],
                   dtype=np.uint16).reshape(n, 2)
    v = (rgb[:, 1].astype(np.uint16) << 8) | rgb[:, 0]
    px[:, 0] = ((v >> 11) & 0x1F).astype(np.uint8) << 3    # R
    px[:, 1] = ((v >> 5) & 0x3F).astype(np.uint8) << 2     # G
    px[:, 2] = (v & 0x1F).astype(np.uint8) << 3            # B
    px[:, 3] = np.array([int(t, 16) for t in byts[n * 2:n * 3]],
                        dtype=np.uint8)                     # A
    return name, w, h, px


def count_visible_colors(px):
    """可见（alpha>0）像素的唯一 RGBA 组合数。"""
    vis = px[px[:, 3] > 0]
    if len(vis) == 0:
        return 0
    return len(np.unique(vis, axis=0))


def quantize(px):
    """无损量化：调色板 = 全部可见色（≤255）+ 透明索引 0。

    返回 (palette[256,4] uint8 RGBA, index[N] uint8)。调用方需先用
    count_visible_colors() 确认 <= VISIBLE_SLOTS，否则有损合并。"""
    n = len(px)
    index = np.zeros(n, dtype=np.uint8)
    vis_mask = px[:, 3] > 0
    vis = px[vis_mask]
    if len(vis) == 0:
        return np.zeros((MAX_PALETTE, 4), dtype=np.uint8), index
    colors, counts = np.unique(vis, axis=0, return_counts=True)
    order = np.argsort(-counts)
    keep = min(len(colors), VISIBLE_SLOTS)
    palette = np.zeros((MAX_PALETTE, 4), dtype=np.uint8)
    palette[1:keep + 1] = colors[order[:keep]]
    codes = np.zeros(len(colors), dtype=np.int64)
    for i, c in enumerate(colors):
        codes[i] = (int(c[0]) << 24) | (int(c[1]) << 16) | (int(c[2]) << 8) | int(c[3])
    slot = np.zeros(len(colors), dtype=np.uint8)
    for rank, i in enumerate(order[:keep]):
        slot[i] = rank + 1
    vis_codes = ((vis[:, 0].astype(np.int64) << 24) | (vis[:, 1].astype(np.int64) << 16)
                 | (vis[:, 2].astype(np.int64) << 8) | vis[:, 3].astype(np.int64))
    pos = np.searchsorted(codes, vis_codes)
    index[vis_mask] = slot[pos]
    return palette, index


def emit(name, w, h, palette, index):
    """生成与 lv_img_conv 等价的 I8 C 源。

    索引按 stride 行布局摆放（行尾 pad 到 4 字节对齐），与 LVGL 解码器的
    行寻址一致；调色板固定 256 项（LV_COLOR_INDEXED_PALETTE_SIZE(I8)）
    置于 data 开头。调色板条目按 lv_color32_t 的内存序写 {blue, green,
    red, alpha}，bin_decoder 直接把它当 lv_color32_t 查表。"""
    stride = (w + 3) & ~3
    pad = stride - w
    rows = np.concatenate([
        index.reshape(h, w),
        np.zeros((h, pad), dtype=np.uint8),
    ], axis=1)
    bgra = palette[:, [2, 1, 0, 3]].astype(np.uint8)
    payload = bgra.tobytes() + rows.tobytes()
    lines = []
    per_line = 12
    raw = payload
    for i in range(0, len(raw), per_line):
        chunk = raw[i:i + per_line]
        lines.append('    ' + ','.join(f'0x{b:02x}' for b in chunk) + ',')
    body = '\n'.join(lines).rstrip(',')
    total = len(raw)
    return f'''
#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
#include "lvgl.h"
#elif defined(LV_LVGL_H_INCLUDE_SYSTEM)
#include <lvgl.h>
#elif defined(LV_BUILD_TEST)
#include "../lvgl.h"
#else
#include "lvgl.h"
#endif

#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_{name.upper()}
#define LV_ATTRIBUTE_{name.upper()}
#endif

static const
LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_{name.upper()}
uint8_t {name}_map[] = {{
{body}
}};

const lv_image_dsc_t {name} = {{
  .header = {{
    .magic = LV_IMAGE_HEADER_MAGIC,
    .cf = LV_COLOR_FORMAT_I8,
    .flags = 0,
    .w = {w},
    .h = {h},
    .stride = {stride},
    .reserved_2 = 0,
  }},
  .data_size = sizeof({name}_map),
  .data = {name}_map,
  .reserved = NULL,
}};
''', total


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check', action='store_true',
                    help='只报告体积变化，不写文件')
    ap.add_argument('--force', action='store_true',
                    help='超过 255 色的图也有损转换（默认拒绝）')
    args = ap.parse_args()

    files = sorted(f for f in os.listdir(IMAGE_DIR)
                   if f.endswith('.c') and re.match(r'^(art_|ic_|cha_|logo)', f))
    before = after = 0
    converted = skipped = 0
    for fn in files:
        path = os.path.join(IMAGE_DIR, fn)
        parsed = parse_rgb565a8(path)
        if not parsed:
            continue
        name, w, h, px = parsed
        n_colors = count_visible_colors(px)
        if n_colors > VISIBLE_SLOTS and not args.force:
            skipped += 1
            continue
        palette, index = quantize(px)
        text, payload_len = emit(name, w, h, palette, index)
        old = len(px) * 3
        before += old
        after += payload_len
        converted += 1
        if args.check:
            print(f'  {fn:32s} {old / 1024:7.1f}K -> {payload_len / 1024:6.1f}K'
                  f'  (w={w} h={h} colors={n_colors + 1})')
        else:
            with open(path, 'w', newline='\n') as f:
                f.write(text)
            print(f'  rewrote {fn:32s} {old / 1024:7.1f}K -> {payload_len / 1024:6.1f}K'
                  f'  (colors={n_colors + 1})')
    print(f'\n可无损转换 {converted} 张，超 255 色跳过 {skipped} 张')
    print(f'转换部分: {before / 1024:.1f}K -> {after / 1024:.1f}K'
          f'  (省 {(before - after) / 1024:.1f}K, -{(1 - after / before) * 100:.0f}%)'
          if before else '没有可转换的图')


if __name__ == '__main__':
    main()
