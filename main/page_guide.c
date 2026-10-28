// main/page_guide.c —— 使用说明页：概述主页基本玩法 + 固件仓库链接。
// 长按确认返回首页（由 pages.c 统一拦截）；上/下键滚动正文。
#include "pages.h"
#include "app_ui.h"
#include "sfx.h"
#include "lvgl.h"

LV_FONT_DECLARE(font_fusion12);
LV_FONT_DECLARE(font_fusion20);
LV_FONT_DECLARE(font_ps8);

static lv_obj_t *s_scr;
static lv_obj_t *s_body;

static lv_obj_t *mk_label(lv_obj_t *par, const char *txt, const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    return l;
}

static void hline(lv_obj_t *par, int y)
{
    lv_obj_t *r = lv_obj_create(par);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 240, 1);
    lv_obj_set_pos(r, 0, y);
    lv_obj_set_style_bg_color(r, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
}

static lv_obj_t *para(lv_obj_t *par, const char *txt, const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = mk_label(par, txt, f, c);
    lv_obj_set_width(l, 208);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    return l;
}

static void page_enter(void)
{
    s_scr = page_screen_create();

    // 顶栏
    ui_topbar(s_scr, "使用说明");

    // 正文：可滚动容器，纵向 flex 排版
    s_body = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_body);
    lv_obj_set_pos(s_body, 16, 34);
    lv_obj_set_size(s_body, 208, 252);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_body, 8, 0);
    lv_obj_set_scroll_dir(s_body, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_body, LV_SCROLLBAR_MODE_AUTO);

    para(s_body,
         "主页上下翻页选功能，确认键进入。顶部是你的观猹身份卡：头像、昵称、等级，以及评价 / 发帖 / 获赞三项数值。",
         &font_fusion12, C_TEXT);

    para(s_body,
         "同步：在主页长按上键进入同步页，用微信扫码连接观猹小程序，即可把你的身份和数据同步到设备上。",
         &font_fusion12, C_TEXT);

    para(s_body,
         "额外玩法：好运猹抽签、番茄猹计时、吃瓜猹测反应、什么猹人格测试、木鱼猹积功德、1024 合成；列表最后一个是猹工坊，收录社区二创玩法。",
         &font_fusion12, C_TEXT);

    para(s_body,
         "长按确认键可随时返回上一级。",
         &font_fusion12, C_MUTED);

    hline(s_body, 0);   // flex 里当作一条分隔（宽度由容器裁剪）

    para(s_body, "本固件开源仓库", &font_fusion12, C_MUTED);
    para(s_body, "anonymhe/whatcha-pocket @ GitHub", &font_fusion20, C_GREEN);

    // 底部返回提示
    hline(s_scr, 292);
    lv_obj_t *back = mk_label(s_scr, "长按确认 \xE2\x96\xB6 返回", &font_fusion12, C_MUTED);
    lv_obj_align(back, LV_ALIGN_TOP_MID, 0, 298);

    lv_screen_load(s_scr);
}

static void page_exit(void)
{
    s_body = NULL;
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
}

static void page_key_cb(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK || !s_body) return;
    if (btn == BSP_BTN_UP)   { lv_obj_scroll_by(s_body, 0,  60, LV_ANIM_ON); sfx_play(SFX_NAV); }
    if (btn == BSP_BTN_DOWN) { lv_obj_scroll_by(s_body, 0, -60, LV_ANIM_ON); sfx_play(SFX_NAV); }
}

const page_t page_guide = { "page_guide", page_enter, page_exit, page_key_cb };
