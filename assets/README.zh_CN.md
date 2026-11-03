<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

由这些字库生成的 C 源属于固件代码，因此放在 `main` 组件里：`tools/gen_fonts.py`
生成 `main/assets/fonts/font_*.c`（对应的 LVGL 图片源在 `main/assets/images/`）。
二进制素材留在这里、生成的代码放那边，字库或图片的改动就不会和绘制它的代码挤在同一个目录。

## 图片（images）

可复用的源图放在 `images/`。

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。
- 新图用 `tools/gen_images.py` 转换（LVGL I8 索引色，见[图片素材](../docs/development/engineering/image-assets.zh_CN.md)）；每边小于 24 px 的精灵不值得索引化。

## 电池 profile（battery）

这里保存两份 CW2017 电量计 profile，因为它们是**两块不同的电芯**，不是同一块电芯的两套标定。
80 字节里有 51 字节不同，SOC 曲线因此完全不同。

| 文件 | 电芯 | 来源 |
| --- | --- | --- |
| [`battery/utl-520mah-upstream-42f16ab9.txt`](battery/utl-520mah-upstream-42f16ab9.txt) | 优特利 520 mAh | 与 `WatchaAI/WATCHA-POCKET`（42f16ab9 时的文件）逐字节一致；本 fork 在 v0.1.3 及之前也是这一份 |
| [`battery/wanli-500mah-profile2-xil40x-20261020.txt`](battery/wanli-500mah-profile2-xil40x-20261020.txt) | 万锂新能源 500 mAh，profile2 / XIL40X | 2026-10-20 供应商提供，从 ai-passport 移植 |

格式：80 行、每行两个大写十六进制数，按寄存器顺序一字节一行。两份都内嵌在
`components/bsp/src/bsp_battery.c` 里，但只编译其中一份，由
`CONFIG_WATCHA_BATTERY_PROFILE_WANLI_500MAH`（`components/bsp/Kconfig`，默认 `n` = 优特利
520 mAh）决定。`tests/test_battery_profile.py` 把两份归档与驱动、以及开机日志里打印的
FNV-1a 指纹（520 = `0xEC96A1A5`，500 = `0xB9287814`）一起锁住。

这些是电量计 profile，不是充电配置，也不能在标称容量相同的不同电芯之间互换。详见
[硬件指南](../docs/hardware-design/AI_HARDWARE_DEVELOPMENT_GUIDE.zh_CN.md#9-cw2017-电池计)。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。
