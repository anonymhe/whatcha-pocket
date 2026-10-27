<p align="right">
  <strong>简体中文</strong> · <a href="pocket-guide.md">English</a>
</p>

# 口袋上新开发规范（给 AI 编程助手看的）

WATCHA POCKET 的玩法分两类：

- **口袋常驻**：首页的 6 个出厂玩法，固定不变，不要改。
- **口袋上新**：持续更新的新玩法。**首页长按 ▲ 进入**，选一个按确认打开，在玩法里长按确认回到口袋上新。

所有新玩法都放进口袋上新。这份文档是写代码的规则；怎么和用户聊、怎么带用户做，见根目录的 [`CHACKER.md`](../../CHACKER.md)。

## 1. 加一个新玩法只要两步

1. 新建 `main/apps/app_<英文名>.c`，末尾导出一个页面：
   ```c
   const page_t app_xxx = { "app_xxx", app_enter, app_exit, app_key };
   ```
   `main/apps/` 下的 `.c` 文件会被自动编译，**不用改 CMakeLists.txt**。但新增文件后要先运行一次 `idf.py reconfigure`，构建系统才会发现它。
2. 在 `main/apps/apps.c` 里加一行 `extern` 和一行列表项：
   ```c
   extern const page_t app_xxx;
   { "某某猹", "一句话介绍（12 字以内）", &app_xxx },
   ```

**示例：** `main/apps/app_decide.c`（决策猹）。写新玩法前先读它，照它的结构写。

每个玩法文件开头用注释写一张玩法卡（名字、怎么玩、按键、一局流程、记住什么、音效、11×11 图标点阵），方便以后的人和 AI 接着改。

## 2. 改动范围

- **只改** `main/apps/` 下的文件。除非用户明确要求，不要碰 `main/` 下其他文件、`components/`、`partitions.csv`、`sdkconfig*`。
- 口袋常驻的 6 个玩法、开机动画、蓝牙同步、分区表、Recovery 都不要动。
- 名字统一叫「xx猹」，语气轻松，别用黑话。
- **不放任何第三方 logo、品牌图、已有动漫或游戏角色。**

## 3. 页面约定（page_t）

| 函数 | 做什么 | 注意 |
| --- | --- | --- |
| `app_enter()` | `s_scr = page_screen_create();` 建控件，最后 `lv_screen_load(s_scr);` | 每次进入都重新建，状态变量要重置 |
| `app_exit()` | 删掉自己建的所有 `lv_timer`，再 `lv_obj_delete(s_scr)` | **必须删干净**，否则返回后会崩 |
| `app_key(btn, ev)` | 处理按键 | 不能阻塞，不能 `vTaskDelay`，慢操作放后台任务 |

- 按键：`BSP_BTN_UP` / `BSP_BTN_DOWN` / `BSP_BTN_OK`；事件：`BSP_BTN_PRESS`（按下瞬间，游戏用）、`BSP_BTN_CLICK`、`BSP_BTN_DOUBLE`、`BSP_BTN_LONG`。
- **长按确认已被系统占用**（返回口袋上新），玩法里不要用 OK + LONG。
- 动画和游戏循环用 `lv_timer_create(cb, 毫秒, NULL)`，回调在 LVGL 线程里，可以直接改控件。

## 4. 视觉规范

和口袋常驻保持同一套视觉：8-bit 像素风，直角、无圆角、无阴影、无渐变。

**画面结构**
- 屏幕 240×320，深色背景。
- 顶栏：`ui_topbar(scr, "某某猹")`，标题在 (18, 9)，分隔线在 y=26。
- 底栏：`ui_footer(scr, "▲▼ 选择 · 确认开始 · 长按返回")`，分隔线在 y=292，提示文字在 y=298 居中。
- 内容区大约是 y=34 到 y=286。
- 工具函数在 `main/apps/app_ui.h`：`ui_label()` 文字、`ui_block()` 直角色块。

**颜色**（定义在 `main/pages.h`，界面以这套为主。玩法确实需要时，比如红绿灯、警告，可以在自己的玩法文件里定义一两个专用颜色，比如 `#define C_RED 0xE05A4F`，但不要改 `pages.h`）

| 常量 | 用途 |
| --- | --- |
| `C_BG` | 背景 |
| `C_GREEN` | 观猹绿：标题、强调、选中 |
| `C_GOLD` | 金色：结果、高光、破纪录 |
| `C_TEXT` | 正文 |
| `C_MUTED` | 次要文字、提示 |
| `C_CARD` | 卡片底色 |
| `C_LINE` | 分隔线、未选中边框 |

**卡片和图标**（与首页六宫格一致）
- 卡片 60×52，`C_CARD` 底，1px `C_LINE` 边框；选中时 2px `C_GREEN` 边框。
- 3 列排布，列距 72、行距 62，左边距 18。
- 图标是 11×11 的单色像素点阵，放大 2 倍显示（22×22），颜色用 `C_GREEN`，放在卡片顶部居中、距上 6px；名字在卡片底部居中、距底 5px。
- 口袋上新的列表接下来会改成这种方块卡片。新玩法请在玩法卡注释里附上自己的 11×11 图标点阵（`#` 表示亮，`.` 表示空）。

**字体**
- 中文只能用 `font_fusion12`；分数、数字可以用 `font_ps8` / `font_ps16`（只有 ASCII）。
- **字库只有约 6800 个常用字**。写完必须运行：
  ```bash
  python3 tools/check_glyphs.py
  ```
  报错的字换成常用字。已知缺：`…`（用 `...`）、`◀`。

## 5. 现成的素材

| 想要 | 用 |
| --- | --- |
| 一只猹的图（72×64） | `#include "cha_pool.h"`，`lv_image_set_src(img, cha_random_72());`，缩放用 `lv_image_set_scale(img, 128)`（256 = 原大） |
| 8-bit 音效 | `#include "sfx.h"`，`sfx_play(SFX_NAV)`；常用：`SFX_NAV` 选择、`SFX_OK` 确认、`SFX_BLIP` 轻点、`SFX_POP` 出现、`SFX_FANFARE` 大成功、`SFX_RECORD` 破纪录、`SFX_BAD` 失败、`SFX_CLEAR` 过关 |
| 记住最高分等 | `#include "storage.h"`，`store_get_u32("key", 默认值)` / `store_set_u32("key", 值)`；key 不超过 15 个字符，别在每帧里写 |
| 随机数 | `#include "esp_random.h"`，`esp_random()` |

猹的美术素材采用 CC BY-NC 4.0 授权，见 [`ASSETS_LICENSE.md`](../../ASSETS_LICENSE.md)。

## 6. 联网（实验性，暂不建议）

芯片带 Wi-Fi，但设备使用期间蓝牙同步一直在跑，内存很紧，HTTPS 请求目前还跑不稳，官方正在适配。用户想做联网玩法时，先说明这一点，建议先做不联网的版本。

如果一定要尝试：
- Wi-Fi 名称和密码写在 `main/apps/wifi_config.h`。这个文件在 `.gitignore` 里，**永远不要提交，也不要把密码写进其他文件**。没有这个文件时也要能正常编译。
- 联网放在**后台 FreeRTOS 任务**里做。后台任务**不碰任何 LVGL 控件**，只写全局变量；界面用 `lv_timer` 轮询状态再刷新。
- 拿完数据**立即 `esp_wifi_stop()` + `esp_wifi_deinit()`**。
- 观猹公开接口（不用登录）：`GET https://watcha.cn/api/v2/products?skip=0&limit=5` 返回最新上架的 AI 产品（`data.items[].name / slogan`），每条约 3 KB。

## 7. 编译、刷机和交付

```bash
. $IDF_PATH/export.sh        # 激活 ESP-IDF 5.5.3
python3 tools/check_glyphs.py
idf.py reconfigure           # 新增了 .c 文件时需要
idf.py build
idf.py -p <端口> flash        # USB 刷机
```

- 刷完起不来：关机后按住 ▲ 开机，保持 5 秒进入恢复模式，再刷一次，或用小程序刷回官方固件。
- 交付时告诉用户：新玩法在哪（首页长按 ▲ → 口袋上新 → 第几个）、怎么玩、改了哪些文件。
- 按 AGENTS.md 的格式报告 Build / Host tests / Device tests，**没在真机上试过就写 Device tests: NOT RUN**。
