// main/apps/app_ui.h —— 写新玩法时常用的小工具（字体、文字、色块、顶栏、底栏）
#pragma once
#include "pages.h"   // 颜色常量 C_BG / C_GREEN / C_GOLD / C_TEXT / C_MUTED / C_CARD / C_LINE、page_t
#include "lvgl.h"

// 字体：中文正文用 font_fusion12（12px 像素字，GB2312 常用字 + ASCII），大标题用
//      font_fusion20。另有更小的 10px font_fusion10（main/assets/fonts/font_fusion10.c），
//      默认不编入；要用先把它加进 main/CMakeLists.txt 的 WATCHA_FONT_SRCS 再 LV_FONT_DECLARE。
//      没收录的字会显示成方块，写完跑 python3 tools/check_glyphs.py 检查。
//      font_ps8 / font_ps16 是英文数字像素字，只有 ASCII，适合分数、计时。
//      字体源用 python3 tools/gen_fonts.py 从 assets/fonts 的 TTF 重新生成。
LV_FONT_DECLARE(font_fusion12);
LV_FONT_DECLARE(font_fusion20);
LV_FONT_DECLARE(font_ps8);
LV_FONT_DECLARE(font_ps16);

// 屏幕 240×320。约定：顶栏标题 (18,9)、右上角电量 (198,9)，分隔线 y=26；
// 底栏分隔线 y=292，提示文字 y=298 居中。
#define UI_W 240
#define UI_H 320

// 顶栏规范：左边是页面名称，右边是电量（font_ps8）。只有口袋上新用位置计数占据右侧。
#define UI_TOP_TITLE_X 18
#define UI_TOP_TITLE_Y 9
#define UI_TOP_RIGHT_X 198
#define UI_TOP_RIGHT_Y 9
#define UI_TOP_LINE_Y  26

lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color);
lv_obj_t *ui_block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color);   // 直角色块
void      ui_topbar(lv_obj_t *scr, const char *title);   // 绿色标题 + 右上角电量 + 分隔线
void      ui_topbar_ex(lv_obj_t *scr, const char *title, const lv_font_t *title_font);
void      ui_footer(lv_obj_t *scr, const char *hint);    // 分隔线 + 灰色居中提示

// 电量读数：右上角电量标签。读值不可用（-1）时显示 “--%”。ui_topbar() 已内含，
// 手写顶栏时单独调用。何时刷新由 power.c 决定（它掌握设备姿态与唤醒节奏），
// 这里只负责「有哪些标签」和「怎么重绘」——每个加入的标签登记进内部清单，
// 随屏幕一起销毁时自动注销，页面无需自行管理。见 power.c / power-and-sleep.md。
lv_obj_t *ui_battery_add(lv_obj_t *parent);
void      ui_battery_refresh(lv_obj_t *label);   // 立即刷新单个标签
void      ui_battery_refresh_all(void);          // 重绘当前所有电量标签（供 power.c 调用）
