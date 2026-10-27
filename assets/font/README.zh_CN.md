<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 字体

内置 LVGL 像素字体的源字库。

## 文件

- `fusion-pixel-10px-monospaced-latin.ttf`、`fusion-pixel-10px-monospaced-zh_hans.ttf`
- `fusion-pixel-12px-monospaced-latin.ttf`、`fusion-pixel-12px-monospaced-zh_hans.ttf`

字族：Fusion Pixel Font（等宽）。许可：SIL Open Font License 1.1（可再分发）。
新增或替换文件时请一并保留许可声明。

## 生成的字体

`tools/gen_fonts.py` 把这些 TTF 转换为 `main/` 下的 LVGL C 源：

- `font_fusion10.c`（`font_fusion10`）：GB2312 + 界面符号，10px 正文。
  已入库但默认不编译；因源文件较大，使用前先加回 `main/CMakeLists.txt` 并声明。
- `font_fusion12.c`（`font_fusion12`）：GB2312 + 界面符号，12px 正文。
- `font_fusion20.c`（`font_fusion20`）：大标题子集，20px（10px 字面 2 倍）。

ASCII 取自 latin 字面，中文取自 zh_hans 字面；字面缺失的字形会被跳过。脚本还会
重写 `docs/watcha/fusion12_chars.txt`（`tools/check_glyphs.py` 校验用的字表），
并从 `docs/watcha/fusion20_chars.txt` 读取大标题子集。

在仓库根目录重建（需要 Node 以运行 `npx lv_font_conv`）：

```bash
python3 tools/gen_fonts.py
```

ESP32-C3 没有 PSRAM，每个编入的字体都占用内部 Flash；发布前请在
`main/CMakeLists.txt` 中删除用不到的字号。
