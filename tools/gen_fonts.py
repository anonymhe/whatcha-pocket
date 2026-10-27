#!/usr/bin/env python3
"""Regenerate the Fusion Pixel LVGL C fonts from the TTF sources in assets/font.

Fonts produced (see AGENTS.md and assets/README.md):

  font_fusion10.c  GB2312 + ASCII, 10 px body text (fusion-pixel 10px)
  font_fusion12.c  GB2312 + ASCII, 12 px body text (fusion-pixel 12px)
  font_fusion20.c  large-headline subset, 20 px (fusion-pixel 10px rendered 2x)

ASCII comes from the "latin" face and CJK from the "zh_hans" face of the
matching pixel size; lv_font_conv merges them. font_fusion20 reuses the small
headline subset that the former font_fusion24 carried, rendered at 20 px from
the 10 px face so the pixel grid stays an exact 2x of font_fusion10.

Requires Node (npx lv_font_conv). Run from the repository root:

    python3 tools/gen_fonts.py
"""
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = os.path.join(ROOT, "assets", "font")
MAIN = os.path.join(ROOT, "main")

# Hand-authored 10 px glyphs for headline characters the pixel TTFs cannot
# render crisply at 20 px (e.g. the mascot name 猹, which only exists on the
# 12 px grid and smears when scaled to 20 px). Each pattern is drawn on the
# 10 px grid ('#' = ink, '.' = empty) and doubled to 20 px — an exact 2x like
# the rest of font_fusion20 — then spliced into the generated font after
# lv_font_conv runs, replacing whatever that glyph would otherwise carry.
HEADLINE_CUSTOM = {
    "猹": [
        "#..#..#...",
        ".##.#####.",
        "..#..###..",
        ".##.#.#.#.",
        "#.#.#####.",
        "..#.#...#.",
        ".##.#####.",
        "#.#.#...#.",
        "..#.#####.",
        ".#.#######",
    ],
}

LATIN10 = os.path.join(FONT_DIR, "fusion-pixel-10px-monospaced-latin.ttf")
ZH10 = os.path.join(FONT_DIR, "fusion-pixel-10px-monospaced-zh_hans.ttf")
LATIN12 = os.path.join(FONT_DIR, "fusion-pixel-12px-monospaced-latin.ttf")
ZH12 = os.path.join(FONT_DIR, "fusion-pixel-12px-monospaced-zh_hans.ttf")

ASCII_RANGE = "0x20-0x7E"

# Symbols the UI relies on that fall outside GB2312: the middot and play/triangle
# marks used across the pages, plus traditional-variant hanzi and emoji that
# appear in nicknames and headings. Kept so existing strings still render.
EXTRA_SYMBOLS = (
    "´·Æä—•⁷™╹█░▶▼▽◡☀☹⛢ㅤ乂冇刅勥咲啰喆囧垚妘嫑學峯愛憉敗旸昇歸洺澔烎"
    "無煙爺猲玥珝珮瓓矞祎義聴菓菻蘭衝覺蹅鋒雲風飍麥點龍\uf8ff🇨🇳"
)

NPX = shutil.which("npx") or shutil.which("npx.cmd")
if not NPX:
    sys.exit("npx not found on PATH (Node.js is required for lv_font_conv)")


def gb2312_chars():
    """Every printable character in the GB2312-80 standard (symbols + hanzi)."""
    seen = set()
    uniq = []
    for hi in range(0xA1, 0xF8):
        for lo in range(0xA1, 0xFF):
            try:
                ch = bytes([hi, lo]).decode("gb2312")
            except UnicodeDecodeError:
                continue
            if ch.isspace() or ch in seen:
                continue
            seen.add(ch)
            uniq.append(ch)
    return "".join(uniq)


def face_coverage(path):
    """Codepoints the TTF actually carries, or None if fontTools is absent."""
    try:
        from fontTools.ttLib import TTFont
    except ImportError:
        return None
    return set(TTFont(path).getBestCmap().keys())


def covered(chars, path):
    """Drop characters the face lacks so the output has no blank glyphs."""
    cov = face_coverage(path)
    if cov is None:
        return chars
    return "".join(ch for ch in chars if ord(ch) in cov)


def headline_subset():
    """The small headline glyph set the former font_fusion24 shipped."""
    path = os.path.join(ROOT, "docs", "watcha", "fusion20_chars.txt")
    syms = open(path, encoding="utf-8").read()
    # Keep only the CJK/symbol part; ASCII is pulled from the latin face.
    return "".join(ch for ch in syms if ord(ch) > 0x7E)


def run(cmd):
    print("+", " ".join(cmd[:6]), "...")
    subprocess.run(cmd, check=True, cwd=ROOT)


def rel(path):
    """Forward-slash path relative to the repo root (keeps headers portable)."""
    return os.path.relpath(path, ROOT).replace(os.sep, "/")


def conv(out_name, font_name, size, latin, zh, zh_symbols, extra_font=None, extra_symbols=""):
    out = os.path.join(MAIN, out_name)
    cmd = [
        NPX, "--yes", "lv_font_conv",
        "--bpp", "1",
        "--size", str(size),
        "--format", "lvgl",
        "--no-compress",
        "--font", rel(latin), "-r", ASCII_RANGE,
        "--font", rel(zh), "--symbols", zh_symbols,
    ]
    # Headline glyphs the primary CJK face lacks (e.g. the mascot name 猹) are
    # pulled from the fallback face so the brand name renders at headline size.
    if extra_font and extra_symbols:
        cmd += ["--font", rel(extra_font), "--symbols", extra_symbols]
    cmd += [
        "-o", rel(out),
        "--lv-include", "lvgl.h",
        "--lv-font-name", font_name,
    ]
    run(cmd)


def patch_custom_glyphs(out_name, scale, custom):
    """Replace chosen glyphs in a generated LVGL font .c with hand-drawn pixel
    art, doubled by `scale`. Rewrites the glyph bitmap, its descriptor box, and
    every following bitmap_index so the font stays self-consistent."""
    if not custom:
        return
    path = os.path.join(MAIN, out_name)
    src = open(path, encoding="utf-8").read()

    # --- parse the glyph bitmap byte array ---
    bm = re.search(r"(glyph_bitmap\[\]\s*=\s*\{)(.*?)(\};)", src, re.S)
    # Bytes are emitted with minimal width (0x0, 0xf, 0xff), so match 1+ hex digits.
    data = bytearray(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", bm.group(2)))

    # --- parse glyph descriptors ---
    dsc_blk = re.search(r"(glyph_dsc\[\]\s*=\s*\{)(.*?)(\};)", src, re.S)
    dsc_rx = re.compile(
        r"\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), "
        r"\.box_h = (\d+), \.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}"
    )
    entries = [list(map(int, m)) for m in dsc_rx.findall(dsc_blk.group(2))]

    # --- map codepoint -> glyph id via the sparse unicode_list / cmap ---
    def glyph_id(cp):
        cm = re.search(
            r"\.range_start = (\d+), \.range_length = \d+, \.glyph_id_start = (\d+),"
            r"\s*\.unicode_list = (unicode_list_\d+|NULL)",
            src,
        )
        # Walk every cmap block to find the one containing cp.
        for m in re.finditer(
            r"\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+),"
            r"\s*\.unicode_list = (unicode_list_\d+|NULL)",
            src,
        ):
            rs, rl, gid0, ul = int(m[1]), int(m[2]), int(m[3]), m[4]
            if not (rs <= cp < rs + rl):
                continue
            if ul == "NULL":
                return gid0 + (cp - rs)
            lst = re.search(ul + r"\[\]\s*=\s*\{(.*?)\};", src, re.S).group(1)
            offs = [int(x, 0) for x in re.findall(r"(0x[0-9a-fA-F]+|\d+)", lst)]
            return gid0 + offs.index(cp - rs)
        raise KeyError(cp)

    def pack(rows, scale):
        bits = []
        for row in rows:
            for _ in range(scale):
                for ch in row:
                    bits += [1 if ch == "#" else 0] * scale
        out = bytearray()
        for i in range(0, len(bits), 8):
            b = 0
            for j in range(8):
                if i + j < len(bits) and bits[i + j]:
                    b |= 1 << (7 - j)
            out.append(b)
        return out

    for ch, rows in custom.items():
        gid = glyph_id(ord(ch))
        w, h = len(rows[0]) * scale, len(rows) * scale
        new_bm = pack(rows, scale)
        bi = entries[gid][0]
        nbi = entries[gid + 1][0] if gid + 1 < len(entries) else len(data)
        old_len = nbi - bi
        data[bi:nbi] = new_bm
        delta = len(new_bm) - old_len
        # shift every following glyph's bitmap_index
        for e in entries[gid + 1:]:
            e[0] += delta
        # adv_w is in 1/16 px; use the ink width so spacing matches the box.
        entries[gid][1] = w * 16
        entries[gid][2] = w
        entries[gid][3] = h
        entries[gid][4] = 0
        entries[gid][5] = -2

    # --- re-emit bitmap array ---
    def fmt_bytes(b):
        lines = []
        for i in range(0, len(b), 16):
            lines.append("    " + ", ".join(f"0x{x:02x}" for x in b[i:i + 16]) + ",")
        return "\n" + "\n".join(lines) + "\n"
    src = src[:bm.start(2)] + fmt_bytes(data) + src[bm.end(2):]

    # re-locate dsc block (offsets shifted) and re-emit descriptors
    dsc_blk = re.search(r"(glyph_dsc\[\]\s*=\s*\{)(.*?)(\};)", src, re.S)
    body = []
    for idx, e in enumerate(entries):
        suffix = " /* id = 0 reserved */" if idx == 0 else ""
        body.append(
            f"    {{.bitmap_index = {e[0]}, .adv_w = {e[1]}, .box_w = {e[2]}, "
            f".box_h = {e[3]}, .ofs_x = {e[4]}, .ofs_y = {e[5]}}}{suffix}"
        )
    src = src[:dsc_blk.start(2)] + "\n" + ",\n".join(body) + "\n" + src[dsc_blk.end(2):]

    open(path, "w", encoding="utf-8").write(src)
    print(f"patched {len(custom)} custom glyph(s) into {out_name}: {''.join(custom)}")


def main():
    for p in (LATIN10, ZH10, LATIN12, ZH12):
        if not os.path.exists(p):
            sys.exit(f"missing font source: {p}")

    gb = gb2312_chars()
    head = headline_subset()

    # Merge GB2312 with the extra symbols, de-duplicating while keeping order.
    body = gb
    seen = set(gb)
    for ch in EXTRA_SYMBOLS:
        if ch not in seen:
            seen.add(ch)
            body += ch

    gb10 = covered(body, ZH10)
    gb12 = covered(body, ZH12)
    head20 = covered(head, ZH10)
    # Headline glyphs the 10px face lacks but the 12px face has (e.g. the mascot
    # name 猹) — render them from the 12px face so the headline is complete.
    head20_set = set(head20)
    head20_fallback = covered(
        "".join(ch for ch in head if ord(ch) > 0x7E and ch not in head20_set),
        ZH12,
    )
    print(f"body glyphs: {len(body)}  "
          f"10px: {len(gb10)}  12px: {len(gb12)}  "
          f"headline: {len(head20)}+{len(head20_fallback)} fallback")

    conv("font_fusion10.c", "font_fusion10", 10, LATIN10, ZH10, gb10)
    conv("font_fusion12.c", "font_fusion12", 12, LATIN12, ZH12, gb12)
    conv("font_fusion20.c", "font_fusion20", 20, LATIN10, ZH10, head20,
         extra_font=ZH12, extra_symbols=head20_fallback)
    # Splice hand-drawn 10 px glyphs (scaled 2x) over the ones the TTF cannot
    # render crisply at 20 px.
    patch_custom_glyphs("font_fusion20.c", 2, HEADLINE_CUSTOM)

    # Keep the glyph-table used by tools/check_glyphs.py in sync with the
    # body font actually shipped (font_fusion12).
    chars = os.path.join(ROOT, "docs", "watcha", "fusion12_chars.txt")
    with open(chars, "w", encoding="utf-8") as fh:
        fh.write(gb12)
    print("done")


if __name__ == "__main__":
    main()
