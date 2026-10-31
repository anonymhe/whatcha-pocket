// main/apps/app_ui.h —— 写新玩法时常用的小工具（字体、文字、色块、顶栏、底栏）
#pragma once
#include "pages.h"   // 颜色常量 C_BG / C_GREEN / C_GOLD / C_TEXT / C_MUTED / C_CARD / C_LINE、page_t
#include "lvgl.h"
#include <stddef.h>
#include <stdint.h>

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

// 电量视图注册表。屏幕上任何显示电量的东西都该挂进来，这样 STANDBY 的周期唤醒
// 能把它们一起重绘，而不用每个视图各跑一个 lv_timer（那些 timer 会被唤醒时的
// tick 重定基准饿死，见 bsp_display_lvgl.c）。何时刷新由 power.c 决定。
//
// 用法：视图建好后调 ui_battery_register(root, paint, ctx)，root 是视图里最先
// 创建的对象（注销事件挂在它身上，屏一起销毁时自动从注册表摘除），paint 收到
// 本帧的电量读数（-1 = 读不到）。ui_topbar() 已内含右上角标签，手写顶栏时单独调
// ui_battery_add()。见 power.c / power-and-sleep.md。
typedef void (*ui_battery_paint_t)(void *ctx, int soc);

void      ui_battery_register(lv_obj_t *root, ui_battery_paint_t paint, void *ctx);
lv_obj_t *ui_battery_add(lv_obj_t *parent);      // 右上角标签，自带注册
void      ui_battery_refresh(lv_obj_t *label);   // 立即刷新单个顶栏标签
int       ui_battery_refresh_all(void);          // 重绘所有已注册视图（供 power.c 调用），返回本帧读数

// 读数格式化与配色：顶栏标签和首页血条共用同一套，避免两处规则漂移。
void     ui_battery_text(char *buf, size_t size, int soc);   // soc < 0 → "--%"
uint32_t ui_battery_color(int soc);                          // >=50 绿 / >=20 金 / 更低红
