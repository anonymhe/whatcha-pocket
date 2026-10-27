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
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = os.path.join(ROOT, "assets", "font")
MAIN = os.path.join(ROOT, "main")

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


def conv(out_name, font_name, size, latin, zh, zh_symbols):
    out = os.path.join(MAIN, out_name)
    cmd = [
        NPX, "--yes", "lv_font_conv",
        "--bpp", "1",
        "--size", str(size),
        "--format", "lvgl",
        "--no-compress",
        "--font", rel(latin), "-r", ASCII_RANGE,
        "--font", rel(zh), "--symbols", zh_symbols,
        "-o", rel(out),
        "--lv-include", "lvgl.h",
        "--lv-font-name", font_name,
    ]
    run(cmd)


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
    print(f"body glyphs: {len(body)}  "
          f"10px: {len(gb10)}  12px: {len(gb12)}  headline: {len(head20)}")

    conv("font_fusion10.c", "font_fusion10", 10, LATIN10, ZH10, gb10)
    conv("font_fusion12.c", "font_fusion12", 12, LATIN12, ZH12, gb12)
    conv("font_fusion20.c", "font_fusion20", 20, LATIN10, ZH10, head20)

    # Keep the glyph-table used by tools/check_glyphs.py in sync with the
    # body font actually shipped (font_fusion12).
    chars = os.path.join(ROOT, "docs", "watcha", "fusion12_chars.txt")
    with open(chars, "w", encoding="utf-8") as fh:
        fh.write(gb12)
    print("done")


if __name__ == "__main__":
    main()
