#!/usr/bin/env python3
"""检查编进固件、却没有任何代码引用的「素材」（白占 Flash）。

素材 = 图片/图标/字体的 C 源，放在 main/assets/ 下：
    main/assets/images/  art_*.c、ic_*.c、cha_*.c、logo*.c
    main/assets/fonts/   font_*.c
每个这样的文件导出一个与文件名同名的符号（如 art_type.c 导出 art_type，
font_fusion20.c 导出 font_fusion20）。

一个素材算「被使用」，当它的符号在**定义它的文件之外**被引用过——
通常是某处 LV_IMAGE_DECLARE / LV_FONT_DECLARE 后再 &symbol 使用。
从未在别处出现的符号，就是编译进去却没人用的死素材，应从 main/CMakeLists.txt
里移除以省 Flash（或补上使用它的代码）。

编译范围：images/ 下的 .c 由 CMake 的 file(GLOB) 自动编入，fonts/ 下的字库在
main/CMakeLists.txt 的 WATCHA_FONT_SRCS 里逐个列出。

用法：python3 tools/check_material.py          # 扫描 main/assets/
      python3 tools/check_material.py --list   # 列出所有素材及其引用处

本脚本是静态启发式：它不解析宏展开，也不跟踪指针转发。把报告当作提示，
移除前请人工确认该素材确实无人使用。
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN = os.path.join(ROOT, 'main')
ASSETS = os.path.join(MAIN, 'assets')
IMAGE_DIR = os.path.join(ASSETS, 'images')
FONT_DIR = os.path.join(ASSETS, 'fonts')

MATERIAL_RE = re.compile(r'^(art_.*|ic_.*|cha_.*|font_.*|logo.*)\.c$')
WORD = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')


def material_files():
    """{文件名: 符号名}，符号名 == 去掉 .c 的文件名。"""
    out = {}
    for d in (IMAGE_DIR, FONT_DIR):
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if MATERIAL_RE.match(f):
                out[f] = os.path.splitext(f)[0]
    return out


def srcs_listed():
    """main/CMakeLists.txt 里显式列出的 .c 文件名（字库用这种方式编入）。"""
    txt = open(os.path.join(MAIN, 'CMakeLists.txt'), encoding='utf-8').read()
    return set(re.findall(r'"([A-Za-z0-9_]+\.c)"', txt))


def c_sources():
    """main/、main/apps/ 与 main/assets/ 下所有 .c/.h。"""
    paths = []
    for d in (MAIN, os.path.join(MAIN, 'apps'), IMAGE_DIR, FONT_DIR):
        if os.path.isdir(d):
            paths += [os.path.join(d, f) for f in sorted(os.listdir(d))
                      if f.endswith(('.c', '.h'))]
    return paths


def main():
    show_all = '--list' in sys.argv[1:]
    mats = material_files()
    sources = c_sources()
    # images/ 整个目录由 CMake glob 编入；fonts/ 只编入 CMakeLists 列出的那些。
    compiled = {f for f in mats
                if os.path.exists(os.path.join(IMAGE_DIR, f)) or f in srcs_listed()}

    # 预扫描：每个源文件里出现了哪些标识符（去掉注释，按词边界）
    tokens_by_file = {}
    for p in sources:
        src = open(p, encoding='utf-8', errors='ignore').read()
        src = re.sub(r'/\*.*?\*/', ' ', src, flags=re.S)
        src = re.sub(r'//[^\n]*', ' ', src)
        tokens_by_file[os.path.basename(p)] = set(WORD.findall(src))

    dead = []       # 已编译但无人引用：白占 Flash（致命）
    orphan = []     # 在磁盘上但未加入编译：孤立文件（仅提示）
    for fname, sym in mats.items():
        users = [bn for bn, toks in tokens_by_file.items()
                 if bn != fname and sym in toks]
        if show_all:
            built = '编译' if fname in compiled else '未编译'
            where = ', '.join(users) if users else '(无引用)'
            print(f'[{built}] {fname:28s} {sym:24s} <- {where}')
            continue
        if users:
            continue
        if fname in compiled:
            dead.append((fname, sym))
        else:
            orphan.append((fname, sym))

    if show_all:
        return

    if orphan:
        print('提示：以下素材在磁盘上但未加入编译，也无人引用：')
        for fname, sym in orphan:
            print(f'  {fname}  (符号 {sym})')
        print()

    if dead:
        print('以下素材编译进固件但没有任何代码引用（白占 Flash）：\n')
        for fname, sym in dead:
            print(f'  {fname}  (符号 {sym})')
        print('\n请从 main/CMakeLists.txt 里移除，或补上使用它的代码。'
              '\n（静态启发式，移除前请人工确认。）')
        sys.exit(1)

    print(f'OK：{len(compiled & set(mats))} 个已编译素材都有代码引用。')


if __name__ == '__main__':
    main()
