#!/usr/bin/env python3
"""把 main/assets/images/ 里的 RGB565A8 图片源转成 LVGL I8 索引色源。

RGB565A8 每像素 3 字节（2 字节 RGB565 + 1 字节 alpha）；I8 每像素 1 字节
索引 + 固定 1024 字节调色板，整体省约 60%。

可见颜色 ≤255 的图（双色图标 ic_*、logo）完全无损。超出 255 的美术图按
出现次数保留最高频的 255 色（逐色零误差），长尾色并入最近的保留色：实测
整批 38 张有损图里，RGB 通道误差 >32/255 的像素占 0.00%~1.04%（加权
0.04%），且集中在半透明抗锯齿边缘；内部区域 MAE ≤1.1/255。像素画长尾色
本就是渐变的中间过渡值，这一级别的合并肉眼不可辨。

LVGL 9 对 RAM 中的 I8 变量图走 get_area_cb 逐行查表解码为 ARGB8888
（行缓冲 w*4 字节），不整图进 RAM，适合无 PSRAM 的 ESP32-C3 —— 前提是
CONFIG_LV_BIN_DECODER_RAM_LOAD 为 n（已在 sdkconfig.defaults 钉死）。
调色板条目是 lv_color32_t（内存序 {blue,green,red,alpha}），alpha 存在
调色板里，索引 0 约定为透明（RGBA 全 0）。

用法：
    python3 tools/gen_images.py --check    # 只报告体积变化，不改文件
    python3 tools/gen_images.py            # 只转换无损可转的图
    python3 tools/gen_images.py --force    # 含长尾色的图也转换（有损，视觉可接受）
    python3 tools/gen_images.py --verify   # 与 git 历史里的 RGB565A8 原版逐像素比对
"""
from __future__ import annotations

import argparse
import os
import re
import sys

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
    """量化到 <=255 个调色板项，索引 0 保留给透明。

    可见色不超过 255 时完全无损（每个颜色占一格）。超出时保留出现次数最多
    的 255 种（零误差），长尾颜色并入最近的保留色 —— 绝不落到索引 0，否则
    那部分像素会变成全透明，在画面上表现为「被打孔的噪点」。

    返回 (palette[256,4] uint8 RGBA, index[N] uint8, lossless bool)。"""
    n = len(px)
    index = np.zeros(n, dtype=np.uint8)
    vis_mask = px[:, 3] > 0
    vis = px[vis_mask]
    if len(vis) == 0:
        return np.zeros((MAX_PALETTE, 4), dtype=np.uint8), index, True
    colors, counts = np.unique(vis, axis=0, return_counts=True)
    order = np.argsort(-counts)
    keep = min(len(colors), VISIBLE_SLOTS)
    palette = np.zeros((MAX_PALETTE, 4), dtype=np.uint8)
    palette[1:keep + 1] = colors[order[:keep]]
    slot = np.zeros(len(colors), dtype=np.uint8)
    for rank, i in enumerate(order[:keep]):
        slot[i] = rank + 1
    if keep < len(colors):
        rest = order[keep:]
        d = ((colors[rest][:, None, :].astype(np.int32)
              - palette[1:keep + 1][None, :, :].astype(np.int32)) ** 2).sum(axis=2)
        slot[rest] = d.argmin(axis=1) + 1
    codes = np.zeros(len(colors), dtype=np.int64)
    for i, c in enumerate(colors):
        codes[i] = (int(c[0]) << 24) | (int(c[1]) << 16) | (int(c[2]) << 8) | int(c[3])
    vis_codes = ((vis[:, 0].astype(np.int64) << 24) | (vis[:, 1].astype(np.int64) << 16)
                 | (vis[:, 2].astype(np.int64) << 8) | vis[:, 3].astype(np.int64))
    pos = np.searchsorted(codes, vis_codes)
    index[vis_mask] = slot[pos]
    return palette, index, keep == len(colors)


def parse_i8(path):
    """读回生成的 I8 源，返回 (name, w, h, palette[256,4] RGBA, index[N] uint8)。

    调色板按 lv_color32_t 内存序 {blue,green,red,alpha} 存储，这里转回
    RGBA 便于与原 RGB565A8 像素直接比对。"""
    with open(path, 'r', errors='ignore') as f:
        src = f.read()
    dsc = DSC_RE.search(src)
    if not dsc:
        return None
    name = dsc.group(1)
    hdr = HEADER_RE.search(dsc.group(0))
    if not hdr or hdr.group('cf') != 'LV_COLOR_FORMAT_I8':
        return None
    w, h = int(hdr.group('w')), int(hdr.group('h'))
    stride = int(hdr.group('stride'))
    m = MAP_RE.search(src)
    if not m:
        return None
    body_start = src.find('{', m.start()) + 1
    body = src[body_start:src.find('};', body_start)]
    byts = [int(t.strip(), 16) for t in body.split(',') if t.strip().startswith('0x')]
    if len(byts) != MAX_PALETTE * 4 + stride * h:
        raise ValueError(f'{path}: payload {len(byts)} != 1024 + {stride}*{h}')
    pal = np.array(byts[:MAX_PALETTE * 4], dtype=np.uint8).reshape(MAX_PALETTE, 4)[:, [2, 1, 0, 3]]
    idx = np.array(byts[MAX_PALETTE * 4:], dtype=np.uint8).reshape(h, stride)[:, :w]
    return name, w, h, pal, idx.reshape(-1)


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


def verify():
    """把每个 I8 源与转换提交前的 RGB565A8 原版逐像素比对，报告误差。

    基准是「git log 里最早的、该文件仍是 RGB565A8 的版本」：从当前分支
    历史回溯，找到最近一次内容不含 LV_COLOR_FORMAT_I8 的提交。"""
    import subprocess
    files = sorted(f for f in os.listdir(IMAGE_DIR)
                   if f.endswith('.c') and re.match(r'^(art_|ic_|cha_|logo)', f))
    checked = unchanged = mismatched = 0
    for fn in files:
        path = os.path.join(IMAGE_DIR, fn)
        rel = fn if len(os.path.commonpath([ROOT, path])) > 0 else path
        repo_rel = os.path.relpath(path, ROOT).replace(os.sep, '/')
        if not parse_i8(path):
            continue
        base = subprocess.run(
            ['git', 'log', '--format=%H', '--', repo_rel],
            capture_output=True, text=True).stdout.split()
        old = ''
        for commit in base:
            blob = subprocess.run(['git', 'show', f'{commit}:{repo_rel}'],
                                  capture_output=True, text=True).stdout
            if blob and 'LV_COLOR_FORMAT_I8' not in blob:
                old = blob
                break
        if not old:
            print(f'  {fn}: git 历史里找不到 RGB565A8 原版，跳过')
            continue
        tmp = path + '.orig'
        with open(tmp, 'w', newline='\n') as f:
            f.write(old)
        parsed = parse_rgb565a8(tmp)
        os.remove(tmp)
        if not parsed:
            print(f'  {fn}: HEAD 原版不是 RGB565A8，跳过')
            continue
        _, w, h, orig = parsed
        orig = orig.reshape(h, w, 4)
        _, _, _, pal, idx = parse_i8(path)
        dec = pal[idx].reshape(h, w, 4)
        diff = np.abs(dec.astype(np.int16) - orig.astype(np.int16))
        mask = (orig[:, :, 3] > 0) | (dec[:, :, 3] > 0)
        d = diff[mask]
        checked += 1
        if len(d) == 0 or d.max() == 0:
            print(f'  OK       {fn}')
        else:
            mismatched += 1
            print(f'  MISMATCH {fn}  max={d.max()}  mean={d.mean():.2f}')
    others = len(files) - checked
    print(f'\n比对了 {checked} 张 I8，其余 {others} 张保持 RGB565A8 未动')
    return 1 if mismatched else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check', action='store_true',
                    help='只报告体积变化，不写文件')
    ap.add_argument('--force', action='store_true',
                    help='含长尾色的图也转换：保留最高频 255 色（零误差），'
                         '长尾色并入最近色，误差集中在抗锯齿边缘')
    ap.add_argument('--verify', action='store_true',
                    help='重读已转换的 I8 源，与 git 历史里的 RGB565A8 原版逐像素比对')
    args = ap.parse_args()
    if args.verify:
        sys.exit(verify())

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
        palette, index, lossless = quantize(px)
        text, payload_len = emit(name, w, h, palette, index)
        old = len(px) * 3
        before += old
        after += payload_len
        converted += 1
        tag = 'lossless' if lossless else 'LOSSY'
        if args.check:
            print(f'  {fn:32s} {old / 1024:7.1f}K -> {payload_len / 1024:6.1f}K'
                  f'  (w={w} h={h} colors={n_colors + 1} {tag})')
        else:
            with open(path, 'w', newline='\n') as f:
                f.write(text)
            print(f'  rewrote {fn:32s} {old / 1024:7.1f}K -> {payload_len / 1024:6.1f}K'
                  f'  (colors={n_colors + 1} {tag})')
    print(f'\n可无损转换 {converted} 张，超 255 色跳过 {skipped} 张')
    print(f'转换部分: {before / 1024:.1f}K -> {after / 1024:.1f}K'
          f'  (省 {(before - after) / 1024:.1f}K, -{(1 - after / before) * 100:.0f}%)'
          if before else '没有可转换的图')


if __name__ == '__main__':
    main()
