// main/apps/app_intro.c —— 猹工坊说明：代码小猹写的一封说明书。
// 讲清这里是什么、怎么唤我（读 CHACKER.md）、我怎么跟你说话、怎么加新玩法。
// 仿照使用说明页：上/下键滚动正文，长按确认返回上一级（由 pages.c 统一拦截）。
//
// 11×11 图标（问号，社区二创入口）：
//   ...#####...
//   ..#.....#..
//   ..#.....#..
//   ......##...
//   .....##....
//   ....##.....
//   ....##.....
//   ...........
//   ....##.....
//   ....##.....
//   ...........
#include "app_ui.h"
#include "sfx.h"

static lv_obj_t *s_scr, *s_body, *s_cursor;
static lv_timer_t *s_blink;

static void blink_tick(lv_timer_t *t)
{
    (void)t;
    if (!s_cursor) return;
    bool on = lv_obj_get_style_bg_opa(s_cursor, 0) == LV_OPA_COVER;
    lv_obj_set_style_bg_opa(s_cursor, on ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
}

static lv_obj_t *para(lv_obj_t *par, const char *txt, const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = ui_label(par, txt, f, c);
    lv_obj_set_width(l, 208);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static lv_obj_t *inline_text(lv_obj_t *par, const char *txt, const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = ui_label(par, txt, f, c);
    lv_obj_set_width(l, LV_SIZE_CONTENT);
    return l;
}

static void app_enter(void)
{
    s_scr = page_screen_create();
    ui_topbar(s_scr, "猹工坊");

    s_body = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_body);
    lv_obj_set_pos(s_body, 16, 34);
    lv_obj_set_size(s_body, 208, 252);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_body, 8, 0);
    lv_obj_set_scroll_dir(s_body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_body, LV_SCROLLBAR_MODE_AUTO);

    para(s_body,
         "这封说明书是我写的。我是代码小猹，名字叫 CHACKER，住在这台机器里，专门教它新本领。",
         &font_fusion12, C_TEXT);

    para(s_body,
         "猹工坊收的是社区二创玩法：大家用 AI 照着开发指南写出来，再汇总到这个列表。上下翻页选，确认键进入，长按确认键随时回到这里。",
         &font_fusion12, C_TEXT);

    para(s_body, "怎么唤我", &font_fusion12, C_MUTED);

    para(s_body,
         "把仓库文件夹交给 AI 编程工具打开，对我说「读 CHACKER.md」，我就是那个样子。不会写代码也没关系：把想让它学会什么说出来，剩下的我来。",
         &font_fusion12, C_TEXT);

    para(s_body,
         "我说话直：先给判断再说理由，一次只问一个问题，说人话。做不到的当场说，并给一个能做到的替代。动手前先给你一张玩法卡，你点头我才开写；没在设备上验证过，我不会说搞定了。",
         &font_fusion12, C_TEXT);

    para(s_body, "怎么加新玩法", &font_fusion12, C_MUTED);

    para(s_body,
         "两步：在 main/apps/ 下新建 app_xxx.c，导出 enter / exit / key 三个函数；再到 apps.c 的玩法表里加一行名字、介绍和页面。这个目录的 .c 会自动编译，不用改 CMakeLists.txt。",
         &font_fusion12, C_TEXT);

    para(s_body,
         "只改 main/apps/，别碰口袋常驻的 6 个玩法、开机动画、蓝牙同步、分区表和 Recovery。画风跟主页一致：8-bit 像素、直角、无阴影；中文正文用 font_fusion12，写完跑 check_glyphs.py 查缺字。",
         &font_fusion12, C_TEXT);

    para(s_body,
         "写完我带你刷机：idf.py -p 端口 flash。刷完起不来别慌，关机后按住 ▲ 开机保持 5 秒进入恢复模式再刷一次，或用小程序刷回官方固件。Wi-Fi 还是实验性的，蓝牙常驻时内存做不动联网玩法，先做不联网的版本。",
         &font_fusion12, C_TEXT);

    para(s_body,
         "下一个「决策猹」是示例玩法，写新玩法先读它的源码。细节都在 pocket-guide 和 CHACKER.md 里。",
         &font_fusion12, C_MUTED);

    lv_obj_t *row = lv_obj_create(s_body);
    lv_obj_remove_style_all(row);
    lv_obj_set_width(row, 208);
    lv_obj_set_height(row, 24);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 2, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    inline_text(row, ">", &font_fusion20, C_GREEN);
    inline_text(row, "代码小猹 CHACKER", &font_fusion20, C_GREEN);
    s_cursor = ui_block(row, 0, 0, 10, 14, C_GREEN);
    s_blink = lv_timer_create(blink_tick, 500, NULL);

    ui_footer(s_scr, "\xE2\x96\xB2\xE2\x96\xBC 滚动 · 长按返回");
    lv_screen_load(s_scr);
}

static void app_exit(void)
{
    if (s_blink) { lv_timer_delete(s_blink); s_blink = NULL; }
    s_cursor = NULL;
    s_body = NULL;
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
}

static void app_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK || !s_body) return;
    if (btn == BSP_BTN_UP)   { lv_obj_scroll_by(s_body, 0,  60, LV_ANIM_ON); sfx_play(SFX_NAV); }
    if (btn == BSP_BTN_DOWN) { lv_obj_scroll_by(s_body, 0, -60, LV_ANIM_ON); sfx_play(SFX_NAV); }
}

const page_t app_intro = { "app_intro", app_enter, app_exit, app_key };
