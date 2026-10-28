// main/page_intro.c — 通用功能说明页
#include "pages.h"
#include "app_ui.h"
#include "storage.h"
#include "sfx.h"
#include <stdio.h>

LV_FONT_DECLARE(font_fusion12);
LV_FONT_DECLARE(font_ps8);
LV_IMAGE_DECLARE(art_intro_fortune);
LV_IMAGE_DECLARE(art_pomo_d25);
LV_IMAGE_DECLARE(art_intro_melon);
LV_IMAGE_DECLARE(art_intro_type);
LV_IMAGE_DECLARE(art_intro_muyu);
LV_IMAGE_DECLARE(art_intro_1024);
static const lv_image_dsc_t *const INTRO_IMG[6] = { &art_intro_fortune, &art_pomo_d25, &art_intro_melon, &art_intro_type, &art_intro_muyu, &art_intro_1024 };

/* ---- 配置 ---- */
typedef struct {
    const char *title;
    const char *desc;
    const char *key_up;     /* ▲键提示，NULL=不显示 */
    const char *key_down;   /* ▼键提示，NULL=不显示 */
    const char *key_ok;     /* 确认键提示 */
    const char *quote;      /* 底部小猹曰 */
} intro_cfg_t;

/*  按键从上到下: ▲(y=50)  ▼(y=155)  确认(y=272) */
static const intro_cfg_t CFG[6] = {
    { "好运猹", "每日运势 一键获取",
      NULL, NULL, "抽签 ▶",
      "小猹曰：信则灵 不信也灵" },
    { "番茄猹", "专注计时器",
      "加时 ▶", "减时 ▶", "开始 ▶",
      "小猹曰：逝者如斯夫" },
    { "吃瓜猹", "反应速度测试",
      NULL, NULL, "吃瓜 ▶",
      "小猹曰：迅雷不及掩耳" },
    { "什么猹", "AI人格测试",
      "选择 ▶", "选择 ▶", "开始 ▶",
      "小猹曰：未知猹 焉知己" },
    { "木鱼猹", "电子木鱼",
      NULL, NULL, "咚咚 ▶",
      "小猹曰：功不唐捐 多多益善" },
    { "1024", "合成1024",
      "上滑 ▶", "下滑 ▶", "左滑 ▶ 右滑 ▶▶",
      "小猹曰：千里之行始于合成" },
};

/* ---- 状态 ---- */
static lv_obj_t  *s_scr;
static int         s_target;      /* page_open 的目标 idx (1-6) */

/* ---- 工具 ---- */

static lv_obj_t *mk_label(lv_obj_t *par, const char *txt,
                           const lv_font_t *f, uint32_t c)
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

/* ---- 页面接口 ---- */

void page_intro_set(int target_idx) { s_target = target_idx; }

static void page_enter(void)
{
    s_scr = page_screen_create();
    int ci = s_target - 1;
    if (ci < 0 || ci > 5) ci = 0;
    const intro_cfg_t *c = &CFG[ci];

    /* 顶栏 */
    ui_topbar(s_scr, c->title);

    /* 猹形象 96x96 + 描述：整体在 27..291 之间上下居中；番茄猹页用同一个位置 */
#define STAGE_Y (ci == 2 ? 77 : 98)   /* 吃瓜猹多两行字，整组单独居中 */
    if (INTRO_IMG[ci]) {
        lv_obj_t *ph = lv_image_create(s_scr);
        lv_image_set_src(ph, INTRO_IMG[ci]);
        lv_obj_set_pos(ph, (240 - 96) / 2, STAGE_Y);
    }
    lv_obj_t *dl = mk_label(s_scr, c->desc, &font_fusion12, C_TEXT);
    lv_obj_align(dl, LV_ALIGN_TOP_MID, 0, INTRO_IMG[ci] ? STAGE_Y + 96 + 12 : 152);
    if (ci == 2) {   /* 吃瓜猹：说明页兼开始页，按确认直接进黑屏等瓜 */
        lv_obj_t *l2 = mk_label(s_scr, "等瓜出现，按下确认", &font_fusion12, C_TEXT);
        lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, STAGE_Y + 96 + 12 + 22);
        char buf[32];
        uint32_t best = store_get_u32("melon_best", 0);
        if (best) snprintf(buf, sizeof buf, "历史最快 %lums", (unsigned long)best);
        else      snprintf(buf, sizeof buf, "历史最快 --");
        lv_obj_t *l3 = mk_label(s_scr, buf, &font_fusion12, C_MUTED);
        lv_obj_align(l3, LV_ALIGN_TOP_MID, 0, STAGE_Y + 96 + 12 + 42);
    }

    /* 按键提示 — 右对齐，y 对应物理键位 */
    if (c->key_up) {
        lv_obj_t *ku = mk_label(s_scr, c->key_up, &font_fusion12, C_GREEN);
        lv_obj_align(ku, LV_ALIGN_TOP_RIGHT, -4, 50);
    }
    if (c->key_down) {
        lv_obj_t *kd = mk_label(s_scr, c->key_down, &font_fusion12, C_GREEN);
        lv_obj_align(kd, LV_ALIGN_TOP_RIGHT, -4, 155);
    }
    lv_obj_t *ko = mk_label(s_scr, c->key_ok, &font_fusion12, C_GREEN);
    lv_obj_align(ko, LV_ALIGN_TOP_RIGHT, -4, 272);

    /* 底部 */
    hline(s_scr, 292);
    lv_obj_t *ql = mk_label(s_scr, c->quote, &font_fusion12, C_MUTED);
    lv_obj_align(ql, LV_ALIGN_TOP_MID, 0, 298);

    lv_screen_load(s_scr);
}

static void page_exit(void)
{
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
}

static void page_key_cb(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_OK) {
        sfx_play(SFX_OK);
        page_open(s_target);
    }
}

const page_t page_intro = { "page_intro", page_enter, page_exit, page_key_cb };
