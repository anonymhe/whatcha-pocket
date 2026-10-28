// main/apps/app_intro.c —— 猹工坊说明：这里是什么、怎么加新玩法。
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

static lv_obj_t *s_scr, *s_body;

static lv_obj_t *para(lv_obj_t *par, const char *txt, const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = ui_label(par, txt, f, c);
    lv_obj_set_width(l, 208);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
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
         "猹工坊收录社区二创玩法：由大家照着开发指南写出来，再汇总到这个列表里。",
         &font_fusion12, C_TEXT);

    para(s_body,
         "上下翻页选玩法，确认键进入；在玩法里长按确认键随时返回这里。",
         &font_fusion12, C_TEXT);

    para(s_body,
         "想加新玩法只要两步：在 main/apps/ 下新建 app_xxx.c，导出 enter / exit / key 三个函数的页面；再到 apps.c 的玩法表里加一行名字、介绍和页面。",
         &font_fusion12, C_TEXT);

    para(s_body,
         "画风和主页一致：8-bit 像素、直角、无阴影。中文正文用 font_fusion12，写完跑 check_glyphs.py 查缺字。详细规则见 pocket-guide 开发文档。",
         &font_fusion12, C_TEXT);

    para(s_body,
         "下一个就是示例玩法「决策猹」，先读它的源码照着写。",
         &font_fusion12, C_MUTED);

    ui_footer(s_scr, "\xE2\x96\xB2\xE2\x96\xBC 滚动 · 长按返回");
    lv_screen_load(s_scr);
}

static void app_exit(void)
{
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
