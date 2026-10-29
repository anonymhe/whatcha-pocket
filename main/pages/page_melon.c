// main/pages/page_melon.c — 吃瓜猹：反应测试
#include "pages.h"
#include "app_ui.h"
#include "storage.h"
#include "sfx.h"
#include "esp_timer.h"
#include "esp_random.h"
#include <stdio.h>

LV_FONT_DECLARE(font_fusion12);
LV_FONT_DECLARE(font_ps8);
LV_IMAGE_DECLARE(art_melon_go);
LV_IMAGE_DECLARE(art_melon_result);
LV_IMAGE_DECLARE(art_melon_record);
LV_IMAGE_DECLARE(art_melon_toofast);

typedef enum { ST_IDLE, ST_WAIT, ST_GO, ST_RESULT, ST_TOOFAST } state_t;

#define NVS_KEY "melon_best"

static lv_obj_t *s_scr, *s_body;
static lv_timer_t *s_wait_timer;
static state_t s_state;
static int64_t  s_t0;
static uint32_t s_best, s_last_ms;
static bool     s_is_record;

/* ---- tiny helpers ---- */

static lv_obj_t *mk(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    return l;
}

static void hline(int y)
{
    lv_obj_t *r = lv_obj_create(s_body);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, 240, 1);
    lv_obj_set_style_bg_color(r, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_pos(r, 0, y);
}

static void topbar(void)
{
    ui_topbar(s_body, "吃瓜猹");
}

static void botbar(const char *hint)
{
    hline(292);
    lv_obj_t *l = mk(s_body, hint, &font_fusion12, C_MUTED);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 301);
}

static void avatar(int y, const lv_image_dsc_t *src)
{
    lv_obj_t *im = lv_image_create(s_body);
    lv_image_set_src(im, src);
    lv_obj_set_pos(im, (240 - 72) / 2, y);
}

/* ---- state machine ---- */

static void set_state(state_t st);

static void wait_fire(lv_timer_t *t)
{
    lv_timer_delete(t);
    s_wait_timer = NULL;
    set_state(ST_GO);
}

static void set_state(state_t st)
{
    s_state = st;
    if (s_wait_timer) { lv_timer_delete(s_wait_timer); s_wait_timer = NULL; }
    lv_obj_clean(s_body);
    lv_obj_set_style_border_width(s_scr, 0, 0);

    switch (st) {

    case ST_IDLE: {
        topbar();
        lv_obj_t *l = mk(s_body, "等瓜出现，按下确认", &font_fusion12, C_TEXT);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 156);
        char buf[32];
        if (s_best > 0)
            snprintf(buf, sizeof buf, "历史最快 %lums", (unsigned long)s_best);
        else
            snprintf(buf, sizeof buf, "历史最快 --");
        l = mk(s_body, buf, &font_fusion12, C_MUTED);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 200);
        botbar("小猹曰：迅雷不及掩耳");
        break;
    }

    case ST_WAIT:
        /* 纯黑：body 已清空，什么都不画 */
        s_wait_timer = lv_timer_create(wait_fire,
                        1000 + (esp_random() % 4001), NULL);
        lv_timer_set_repeat_count(s_wait_timer, 1);
        break;

    case ST_GO: {
        sfx_play(SFX_POP);
        lv_obj_set_style_border_color(s_scr, lv_color_hex(C_GREEN), 0);
        lv_obj_set_style_border_width(s_scr, 2, 0);
        lv_obj_t *im = lv_image_create(s_body);
        lv_image_set_src(im, &art_melon_go);
        lv_obj_align(im, LV_ALIGN_CENTER, 0, -20);
        lv_refr_now(NULL);
        s_t0 = esp_timer_get_time();
        break;
    }

    case ST_RESULT: {
        topbar();
        avatar(44, s_is_record ? &art_melon_record : &art_melon_result);
        char buf[32];
        lv_obj_t *l;
        if (s_is_record) {
            snprintf(buf, sizeof buf, "破纪录 %lums", (unsigned long)s_last_ms);
            l = mk(s_body, buf, &font_fusion12, C_GOLD);
        } else {
            snprintf(buf, sizeof buf, "本次 %lums", (unsigned long)s_last_ms);
            l = mk(s_body, buf, &font_fusion12, C_TEXT);
        }
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 150);
        snprintf(buf, sizeof buf, "历史最快 %lums", (unsigned long)s_best);
        l = mk(s_body, buf, &font_fusion12, C_MUTED);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 175);
        botbar("小猹曰：迅雷不及掩耳");
        sfx_play(s_is_record ? SFX_RECORD : SFX_BLIP);
        break;
    }

    case ST_TOOFAST: {
        sfx_play(SFX_BAD);
        topbar();
        avatar(64, &art_melon_toofast);
        lv_obj_t *l = mk(s_body, "瓜还没熟，请勿抢跑", &font_fusion12, C_TEXT);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 156);
        botbar("小猹曰：迅雷不及掩耳");
        break;
    }

    }
}

/* ---- page interface ---- */

static void page_enter(void)
{
    s_scr = page_screen_create();
    s_body = lv_obj_create(s_scr);
    lv_obj_remove_style_all(s_body);
    lv_obj_set_size(s_body, 240, 320);
    lv_obj_set_pos(s_body, 0, 0);

    s_best = store_get_u32(NVS_KEY, 0);
    s_wait_timer = NULL;

    set_state(ST_WAIT);   /* 说明页已承担待机页的作用 */
    lv_screen_load(s_scr);
}

static void page_exit(void)
{
    if (s_wait_timer) { lv_timer_delete(s_wait_timer); s_wait_timer = NULL; }
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; s_body = NULL; }
}

static void page_key_cb(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn != BSP_BTN_OK) return;

    if (ev == BSP_BTN_PRESS) {
        switch (s_state) {
        case ST_IDLE:    set_state(ST_WAIT); break;
        case ST_WAIT:    set_state(ST_TOOFAST); break;
        case ST_GO: {
            int64_t t1 = esp_timer_get_time();
            s_last_ms = (uint32_t)((t1 - s_t0) / 1000);
            s_is_record = (s_best == 0 || s_last_ms < s_best);
            if (s_is_record) {
                s_best = s_last_ms;
                store_set_u32(NVS_KEY, s_best);
            }
            set_state(ST_RESULT);
            break;
        }
        case ST_RESULT:  set_state(ST_WAIT); break;
        case ST_TOOFAST: set_state(ST_WAIT); break;
        }
    }
    /* LONG: pages.c 会先调这里再 page_home()。
       GO 态长按：前面的 PRESS 已经记了成绩并进 RESULT，
       这个成绩是真实反应，不撤销。 */
}

const page_t page_melon = { "page_melon", page_enter, page_exit, page_key_cb };
