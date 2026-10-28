// main/page_muyu.c —— 木鱼猹：按确认摸猹，功德 +1
#include "pages.h"
#include "app_ui.h"
#include "storage.h"
#include "sfx.h"
#include <stdio.h>

LV_FONT_DECLARE(font_fusion12);
LV_FONT_DECLARE(font_ps8);
LV_FONT_DECLARE(font_ps16);
LV_IMAGE_DECLARE(art_muyu_normal);
LV_IMAGE_DECLARE(art_muyu_combo);

// ---- 布局（对应 mock）----
#define AV_W       128
#define AV_H       128
#define AV_X       ((240 - AV_W) / 2)
#define AV_Y       52
#define LBL_Y      206
#define NUM_Y      224
#define COMBO_Y    250
#define BEST_Y     270
#define PLUS_Y0    (AV_Y - 6)
#define PLUS_RISE  22

#define NVS_KEY    "muyu_best"
#define SAVE_EVERY 50
#define COMBO_GAP  1000   // ms
#define COMBO_MIN  10

static lv_obj_t *s_scr, *s_av, *s_num, *s_combo, *s_plus, *s_best_lbl;
static lv_timer_t *s_combo_timer, *s_flash_timer;
static uint32_t s_count, s_best, s_saved;   // s_count 本次会话，s_best 历史最高（NVS）
static uint32_t s_last_tick;
static uint32_t s_best_enter;   // 进页时的历史最高
static int s_streak;

static lv_obj_t *label(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    return l;
}

static void save(void)
{
    if (s_best != s_saved && store_set_u32(NVS_KEY, s_best)) s_saved = s_best;
}

static void refresh_best(void)
{
    char buf[24];
    snprintf(buf, sizeof buf, "最高 %lu", (unsigned long)s_best);
    lv_label_set_text(s_best_lbl, buf);
    lv_obj_align(s_best_lbl, LV_ALIGN_TOP_MID, 0, BEST_Y);
}

static void refresh_num(void)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%lu", (unsigned long)s_count);
    lv_label_set_text(s_num, buf);
    lv_obj_align(s_num, LV_ALIGN_TOP_MID, 0, NUM_Y);
    bool hot = s_streak > COMBO_MIN;
    lv_obj_set_style_text_color(s_num, lv_color_hex(hot ? C_GREEN : C_TEXT), 0);
    if (hot) {
        snprintf(buf, sizeof buf, "连击 ×%d", s_streak);
        lv_label_set_text(s_combo, buf);
        lv_obj_align(s_combo, LV_ALIGN_TOP_MID, 0, COMBO_Y);
        lv_obj_remove_flag(s_combo, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_combo, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---- 动画 ----
static void anim_sx(void *o, int32_t v) { lv_image_set_scale_x(o, v); }
static void anim_sy(void *o, int32_t v) { lv_image_set_scale_y(o, v); }
static void anim_plus(void *o, int32_t v)   // v: 0..100
{
    lv_obj_set_y(o, PLUS_Y0 - PLUS_RISE * v / 100);
    lv_obj_set_style_opa(o, (lv_opa_t)(255 - 255 * v / 100), 0);
}

static void squash(void)
{
    lv_anim_delete(s_av, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_av);
    lv_anim_set_duration(&a, 40);
    lv_anim_set_playback_duration(&a, 60);
    lv_anim_set_values(&a, 256, 216);
    lv_anim_set_exec_cb(&a, anim_sy);
    lv_anim_start(&a);
    lv_anim_set_values(&a, 256, 280);
    lv_anim_set_exec_cb(&a, anim_sx);
    lv_anim_start(&a);
}

static void plus_fly(void)
{
    char buf[16];
    if (s_streak > COMBO_MIN) snprintf(buf, sizeof buf, "+1 ×%d", s_streak);
    else                      snprintf(buf, sizeof buf, "+1");
    lv_label_set_text(s_plus, buf);
    lv_obj_align(s_plus, LV_ALIGN_TOP_MID, 0, PLUS_Y0);
    lv_obj_remove_flag(s_plus, LV_OBJ_FLAG_HIDDEN);
    lv_anim_delete(s_plus, NULL);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_plus);
    lv_anim_set_duration(&a, 320);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_exec_cb(&a, anim_plus);
    lv_anim_start(&a);
}

static void flash_off(lv_timer_t *t)
{
    (void)t;
    lv_obj_set_style_border_width(s_scr, 0, 0);
    s_flash_timer = NULL;
}
static void flash_border(void)
{
    lv_obj_set_style_border_color(s_scr, lv_color_hex(C_GREEN), 0);
    lv_obj_set_style_border_width(s_scr, 3, 0);
    if (s_flash_timer) lv_timer_delete(s_flash_timer);
    s_flash_timer = lv_timer_create(flash_off, 220, NULL);
    lv_timer_set_repeat_count(s_flash_timer, 1);
}

// ---- 定时器 ----
static void combo_tick(lv_timer_t *t)   // 超过 1s 没按，连击归零
{
    (void)t;
    if (s_streak && lv_tick_elaps(s_last_tick) > COMBO_GAP) { s_streak = 0; refresh_num(); if (s_av) lv_image_set_src(s_av, &art_muyu_normal); }
}

// ---- 摸猹 ----
static void tap(void)
{
    uint32_t now = lv_tick_get();
    s_streak = (s_streak && lv_tick_elaps(s_last_tick) <= COMBO_GAP) ? s_streak + 1 : 1;
    s_last_tick = now;
    if (s_streak == COMBO_MIN + 1) lv_image_set_src(s_av, &art_muyu_combo);
    else if (s_streak == 1) lv_image_set_src(s_av, &art_muyu_normal);
    s_count++;
    refresh_num();
    if (s_count > s_best) { s_best = s_count; refresh_best(); }
    squash();
    plus_fly();
    sfx_gu(s_streak > COMBO_MIN ? s_streak - COMBO_MIN : 0);
    if (s_count == 100 || s_count == 1024 || s_count == 10000) flash_border();
    if (s_best - s_saved >= SAVE_EVERY) save();
}

// ---- page_t ----
static void page_enter(void)
{
    s_best = s_saved = store_get_u32(NVS_KEY, 0);
    s_best_enter = s_best;
    s_count = 0;                         // 每次进页面从 0 开始
    s_streak = 0;

    s_scr = page_screen_create();
    ui_topbar(s_scr, "木鱼猹");

    s_av = lv_image_create(s_scr);
    lv_image_set_src(s_av, &art_muyu_normal);
    lv_obj_set_pos(s_av, AV_X, AV_Y);
    lv_image_set_pivot(s_av, AV_W / 2, AV_H);      // 以底部中心为轴，压扁时脚不动

    lv_obj_t *l = label(s_scr, "功德", &font_fusion12, C_MUTED);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, LBL_Y);
    s_num   = label(s_scr, "0", &font_ps16, C_TEXT);
    s_combo = label(s_scr, "", &font_fusion12, C_GREEN);
    s_plus  = label(s_scr, "+1", &font_fusion12, C_GREEN);
    lv_obj_add_flag(s_plus, LV_OBJ_FLAG_HIDDEN);
    s_best_lbl = label(s_scr, "", &font_fusion12, C_MUTED);
    refresh_best();

    lv_obj_t *ln2 = lv_obj_create(s_scr);
    lv_obj_set_pos(ln2, 0, 292); lv_obj_set_size(ln2, 240, 1);
    lv_obj_set_style_bg_color(ln2, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_border_width(ln2, 0, 0); lv_obj_set_style_radius(ln2, 0, 0);
    lv_obj_t *h = label(s_scr, "小猹曰：功不唐捐 多多益善", &font_fusion12, C_MUTED);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 301);

    refresh_num();
    s_combo_timer = lv_timer_create(combo_tick, 200, NULL);
    lv_screen_load(s_scr);
}

static void page_exit(void)
{
    save();
    if (s_combo_timer) { lv_timer_delete(s_combo_timer); s_combo_timer = NULL; }
    if (s_flash_timer) { lv_timer_delete(s_flash_timer); s_flash_timer = NULL; }
    if (s_av)   lv_anim_delete(s_av, NULL);
    if (s_plus) lv_anim_delete(s_plus, NULL);
    if (s_scr)  { lv_obj_delete(s_scr); s_scr = NULL; }
    s_av = s_plus = s_num = s_combo = s_best_lbl = NULL;
}

static void page_key_cb(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn != BSP_BTN_OK) return;
    if (ev == BSP_BTN_PRESS) tap();                       // 按下即响应，手感最跟手
    if (ev == BSP_BTN_LONG && s_count) {                  // 长按返回：撤销刚才那次误计的 +1
        if (s_best == s_count && s_best > s_best_enter) s_best--;   // 那一下刚好刷新了最高，一起撤
        s_count--;
    }
}

const page_t page_muyu = { "muyu", page_enter, page_exit, page_key_cb };
