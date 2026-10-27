#!/usr/bin/env python3
"""检查 main/ 下 C 代码字符串里有没有中文字库没收录的字。

用法：python3 tools/check_glyphs.py            # 检查 main/ 与 main/apps/ 下所有 .c
      python3 tools/check_glyphs.py main/xxx.c  # 检查指定文件

没收录的字在设备上会显示成方块，换成近义的常用字即可。

字库不止一套：font_fusion12 收录约 6,800 字（见 docs/watcha/fusion12_chars.txt），
而 font_fusion16 / font_fusion24 为节省 Flash 只收录约 650 字的子集。一个源文件里
如果用了较小的字号，它的所有中文字面量都必须落在那个较小子集内，否则 12px 看着正常、
24px 却是方块。本脚本对每个文件按它引用的最小字库校验。

各字库的收录字集直接从 main/font_fusion*.c 头部的 Opts 注释读取，字号表无需手工同步。
font_fusion12 另有 docs/watcha/fusion12_chars.txt 作为可读清单，与 Opts 一致。
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN = os.path.join(ROOT, 'main')
ASCII = {chr(c) for c in range(0x20, 0x7f)}


def font_symbols(c_path):
    """从 font_fusion*.c 头部 Opts 注释里解析 --symbols 的字集。"""
    src = open(c_path, encoding='utf-8').read()
    opts = re.search(r'\* Opts:(.*?)\n \*', src, flags=re.S)
    if not opts:
        return None
    sym = re.search(r'--symbols (.*?) -o ', opts.group(1), flags=re.S)
    if not sym:
        return None
    return set(sym.group(1).replace('\n', ''))


def load_fonts():
    """返回 {font_name: 收录字集}，字集含 ASCII。"""
    fonts = {}
    for f in sorted(os.listdir(MAIN)):
        m = re.fullmatch(r'(font_fusion\d+)\.c', f)
        if not m:
            continue
        syms = font_symbols(os.path.join(MAIN, f))
        if syms is not None:
            fonts[m.group(1)] = syms | ASCII
    return fonts


def font_size(name):
    m = re.search(r'(\d+)$', name)
    return int(m.group(1)) if m else 0


def pick_font(src, fonts):
    """源文件引用了哪些 font_fusion*，取其中最小字号的那套作为校验集。

    较小字号的字库是较大字号字库的子集，校验最小的即最严。
    文件若没引用任何 fusion 字体，退回 font_fusion12（最宽松）。
    """
    used = [n for n in fonts if re.search(r'\b%s\b' % re.escape(n), src)]
    if not used:
        used = [n for n in fonts if n == 'font_fusion12'] or list(fonts)
    name = min(used, key=font_size)
    return name, fonts[name]


def main():
    fonts = load_fonts()
    if not fonts:
        print('错误：在 main/ 下找不到 font_fusion*.c，无法读取字库。')
        sys.exit(2)

    paths = sys.argv[1:]
    if not paths:
        paths = []
        for d in (MAIN, os.path.join(MAIN, 'apps')):
            if os.path.isdir(d):
                paths += [os.path.join(d, f) for f in sorted(os.listdir(d))
                          if f.endswith('.c') and not f.startswith('font_')]

    bad = 0
    for p in paths:
        src = open(p, encoding='utf-8').read()
        font_name, have = pick_font(src, fonts)
        src = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'), src, flags=re.S)   # 去掉块注释，保留行号
        for lineno, line in enumerate(src.split('\n'), 1):
            if line.lstrip().startswith('//'):
                continue
            for s in re.findall(r'"((?:[^"\\]|\\.)*)"', line):
                miss = sorted({ch for ch in s if ord(ch) > 0x7f and ch not in have})
                if miss:
                    bad += 1
                    print(f'{os.path.relpath(p, ROOT)}:{lineno}: {font_name} 字库没有 {" ".join(miss)}  ->  "{s}"')

    if bad:
        print(f'\n共 {bad} 处会显示成方块，请换字。')
        sys.exit(1)
    print('OK：所有中文都在对应字库里。')


if __name__ == '__main__':
    main()
