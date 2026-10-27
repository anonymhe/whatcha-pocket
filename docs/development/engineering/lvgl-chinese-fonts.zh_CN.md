<p align="right">
  <strong>简体中文</strong> · <a href="lvgl-chinese-fonts.md">English</a>
</p>

# LVGL 中文显示：字体与排查

添加中文文案、选择字体或显示动态（用户或网络）内容时使用本指南。它说明本固件已经内置的字体，以及如何停留在这些字体的覆盖范围内，适用于 [`dependencies.lock`](../../../dependencies.lock) 锁定的 LVGL 9.5.0。

与上游基线不同，本固件**不**依赖 Montserrat，也不要求每个应用各自生成新字体。它内置了一套固定、预先构建好的像素中文字体，并用工具强制校验其覆盖范围。动手用 `lv_font_conv` 之前先读本文。

## 1. 本固件内置的字体

| 字体 | 声明处 | 覆盖 | 用途 |
| --- | --- | --- | --- |
| `font_fusion12` | `main/*.c` 内 `LV_FONT_DECLARE`（如 [`main/pages.c`](../../../main/pages.c)） | 约 6,800 个常用汉字加 ASCII，12 px 像素风 | 所有中文 UI 文案 |
| `font_fusion24` | [`main/page_type.c`](../../../main/page_type.c)、[`main/page_fortune.c`](../../../main/page_fortune.c) | `font_fusion12` 的约 650 字子集，24 px 像素风 | 仅用于结果/标题文案，且只能用子集内的字 |
| `font_fusion16` | 文件在磁盘但未编译（作为死 Flash 已移出构建） | 同样的约 650 字子集，16 px | 无；`font_fusion16.c` 保留备查。仅当某页面开始使用时才重新加回 `main/CMakeLists.txt` |
| `font_ps8` / `font_ps16` | `main/apps/app_ui.h` | 只有 ASCII（数字、拉丁字母），8/16 px 像素风 | 分数、计时、数字读数 |

三套 `font_fusion*` 都从同一个 `fusion12.ttf` 生成；`font_fusion16` / `font_fusion24` 为节省 Flash 故意只收录一小撮符号子集。各自的子集记录在对应 `main/font_fusion*.c` 头部的 `Opts:` 注释里，`check_glyphs.py` 直接读取这些注释。`font_fusion12` 的收录字集另外以可读清单形式列在 [`docs/watcha/fusion12_chars.txt`](../../watcha/fusion12_chars.txt)。[`sdkconfig.defaults`](../../../sdkconfig.defaults) 启用了 `CONFIG_LV_FONT_UNSCII_8` 与 `CONFIG_LV_FONT_UNSCII_16` 作为 ASCII 像素兜底；Montserrat 默认字体不含中文字形，这里不是显示字体。

由于 `font_fusion24` 只收录子集，一个字可能在 `font_fusion12` 里有、用 24 px 画却是方块。凡用 `font_fusion24` 渲染的文本（目前是 `page_type.c` 的结果名/金句表和 `page_fortune.c` 的标题）都必须停留在该子集内。下文的字形门禁会按文件强制这一点。

不在 `font_fusion12` 内的字在设备上会显示成方块。改源文件编码不会补上字体原本就没有的字形，扩大 LVGL 内存池同样补不了字。

## 2. 区分编码、字形覆盖和样式选择

正确显示需要以下每一层都成立：

| 层次 | 必须满足 | 常见错误表现 |
| --- | --- | --- |
| 文本输入 | 有效 UTF-8，不能截断多字节字符 | 即使字体正确也显示乱码 |
| 字形覆盖 | 所选字体包含每个所需 Unicode 码点 | 英文正常，中文空白/方框 |
| 编译接入 | 字体源文件已编译，并用 `LV_FONT_DECLARE` 声明 | 字体符号未定义，或预期字体根本没选上 |
| 控件样式 | 当前状态下，实际绘制文字的部件选用了该字体 | 只有标题、弹窗或选中项异常 |
| 布局/绘制 | 颜色/透明度可见、空间足够、字体生命周期有效 | 字形存在，但文字被裁剪或不可见 |

UTF-8 只规定怎样把字节解码成码点，不会安装字库。源文件和文本资源都保存为 UTF-8，JSON 转义先解码再显示，不要按任意字节数截断 UTF-8——只在完整字符边界截断，并给结尾 NUL 留空间。

## 3. 停留在覆盖范围内（常规路径）

日常写玩法不需要添加或生成字体。你只要：

1. 中文标签用 `font_fusion12`，数字/拉丁读数用 `font_ps8` / `font_ps16`，并在标签上显式指定字体——见 [`main/apps/app_ui.h`](../../../main/apps/app_ui.h) 里的 `ui_label()`。
2. 运行覆盖门禁，把方块字换成收录了的字：

   ```bash
   python3 tools/check_glyphs.py            # 检查 main/ 与 main/apps/
   python3 tools/check_glyphs.py main/xxx.c # 检查指定文件
   ```

   它会报出每一处该字体没有收录的字符串字面量字符，带文件和行号。门禁按字体校验：每个文件对照它引用的**最小**那套 `font_fusion*`，所以 `font_fusion24` 文件里的字面量（例如 `page_type.c` 的结果表）是按 24 px 子集校验，而非完整 12 px 字集。已知需手动替换的缺字：`…`（用 `...`）和 `◀`。
3. 让动态内容（姓名、网络文本）停留在收录字集内，或为范围外的内容显示有意设计的替代。固定菜单子集不保证支持任意用户或服务器文本；不要悄悄丢弃未知字符。

`check_glyphs.py` 检查 C 字符串字面量；它按文件对照该文件用到的最小字体校验，但无法跟踪经由变量传入的文本（例如运行时读取的表数据），也不证明真机显示、不检查布局。把通过视为必要条件，而非充分条件。

## 4. 在控件上选择字体

以下调用只在 LVGL/显示初始化后、处于 LVGL 任务或持有 `bsp_lvgl_lock()` 时进行：

```c
#include "lvgl.h"

LV_FONT_DECLARE(font_fusion12);   /* 每个用到该字体的 .c 自行声明 */

lv_obj_t *label = lv_label_create(parent);
lv_obj_set_style_text_font(label, &font_fusion12, LV_PART_MAIN | LV_STATE_DEFAULT);
lv_label_set_text(label, "\u4E2D\u6587 ABC 123");   /* 中文 ABC 123 */
```

转义表示 U+4E2D、U+6587（即“中文”），用来让中英文文档示例保持一致；直接写 UTF-8 字面量也可以。设置 screen 或主题默认字体不会覆盖子控件上已显式指定的字体，所以要核对真正绘制文字的部件及其焦点、按下、选中、禁用状态，而不只是 screen 默认字体。

## 5. 验证覆盖，查明当前字体

以下辅助函数检查 Unicode 码点，而不是 UTF-8 单字节或 C 的多字符字符常量：

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

对已有标签，在 LVGL 锁内读取 `lv_obj_get_style_text_font(label, LV_PART_MAIN)`，对获取的实际字体检查，而不是验证一个无关的全局字体符号；各相关状态切换后都要重复检查。返回成功只说明描述符覆盖，不证明位图解码和屏幕输出成功。把固定文案解码为码点逐一检查，缺字记为 `U+XXXX`；保留一个已知缺字的反例，避免检查无条件通过。

## 6. 按顺序定位故障

| 现象 | 下一步 |
| --- | --- |
| ASCII 正常，中文空白/方框 | 跑 `check_glyphs.py`；确认标签用的是 `font_fusion12`；确认文本是 UTF-8 |
| 某个字是方块 | 它不在 `font_fusion12` 内；换成收录了的字（见 `fusion12_chars.txt`） |
| 正文正常，标题或弹窗异常 | 检查局部字体、控件部件、单独创建的弹窗及状态覆盖 |
| 字形检查通过但仍空白 | 检查文字颜色/透明度、隐藏标志、标签尺寸、字体生命周期和 LVGL 分配错误 |
| 字符重叠或上下被切掉 | 检查行高、基线、父容器裁剪和标签宽高 |
| 字体 undefined reference | 检查 `LV_FONT_DECLARE` 名称，以及字体 `.c` 是否在组件源文件列表中 |

## 7. 最后手段：添加或重新生成字体

仅用于维护者有意扩充已内置的字体（例如给 `font_fusion12` 加字），不用于应用开发——应用开发必须停留在现有覆盖范围内。准备授权允许且确实含所需字形的 TTF/OTF，安装固定版本的 [`lv_font_conv`](https://github.com/lvgl/lv_font_conv)，记录版本与命令。只生成子集而非整套字体家族，且按在用字号生成：

```bash
lv_font_conv \
  --font /path/to/licensed-cjk-font.otf \
  --range 0x20-0x7E,0x4E2D,0x6587,0xFF0C,0x3002 \
  --size 12 --bpp 1 --format lvgl --no-compress \
  --lv-font-name font_fusion12 --lv-include lvgl.h \
  --output main/font_fusion12.c
```

源字体没有的字形，不能靠填写码点生成。重新生成 `docs/watcha/fusion12_chars.txt` 以匹配新的覆盖，使 `check_glyphs.py` 保持准确；把字符清单与命令纳入版本管理；不要为补一个字就启用额外字号或整套 CJK 字体家族。若改动了 `font_fusion12` 收录的字，也要用同一 TTF 按各自字号重新生成 `font_fusion16` / `font_fusion24`，保持子集一致。输出要匹配 `dependencies.lock` 锁定的 LVGL 版本；首次先不压缩，避免解码器不匹配。参见 [LVGL 9.5 字体概览](https://docs.lvgl.io/9.5/main-modules/fonts/overview.html)。

### 源字体与许可

这些字体生成所用的 `fusion12.ttf` 是 TakWolf 的[缝合像素字体（Fusion Pixel Font）](https://github.com/TakWolf/fusion-pixel-font)，一套泛中日韩像素字体，以 [SIL 开放字体许可证 1.1](https://openfontlicense.org/)（`OFL-1.1`）发布。该项目原生提供 8/10/12 px 像素设计；12 px 是原生字号，所以 `font_fusion12` 显示清晰，而 `font_fusion16` / `font_fusion24` 是从该设计放大而来（24 px 恰好是 12 px 的整 2 倍，较干净；16 px 不是整数倍，会偏糊）。OFL-1.1 允许把生成的位图嵌入本固件；请保留许可声明，不要单独售卖字体文件。若从上游更新版重新生成，请在 `lv_font_conv` 命令旁记录发布版本（按日期编号，如 `2026.10.25`）。

## 8. 验证与交付边界

运行[完整仓库门禁](build-and-test.zh_CN.md)和 `check_glyphs.py`。用 `idf.py size-components` / `idf.py size-files` 检查 Flash 与静态 RAM 影响；只读 Flash 中的字体不等于把字库分配到堆上，但绘制与缓存仍可能需要内部 RAM。

真机逐页逐状态检查中英文/数字混排、长文本，以及覆盖字集边界内外的动态输入——页面重建、重启后也要测，不能只看第一个标签。未完成这些检查时，中文显示仍属于 `Unverified`；host tests 或固件编译通过都不能验证屏幕输出。
