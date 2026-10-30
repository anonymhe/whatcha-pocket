// main/pages/page_pomo.c — 番茄猹：番茄钟
#include "pages.h"
#include "app_ui.h"
#include "sfx.h"
#include "esp_timer.h"
#include <stdio.h>

LV_FONT_DECLARE(font_fusion12);
LV_FONT_DECLARE(font_ps8);
LV_FONT_DECLARE(font_ps16);
LV_IMAGE_DECLARE(art_pomo_d15);
LV_IMAGE_DECLARE(art_pomo_d25);
LV_IMAGE_DECLARE(art_pomo_d45);
LV_IMAGE_DECLARE(art_pomo_focus);
LV_IMAGE_DECLARE(art_pomo_pause);
LV_IMAGE_DECLARE(art_pomo_done);
LV_IMAGE_DECLARE(art_pomo_rest);

typedef enum { ST_IDLE, ST_FOCUS, ST_PAUSE, ST_DONE, ST_REST } state_t;
static const lv_image_dsc_t *const POMO_IMG[5] = { &art_pomo_d25, &art_pomo_focus, &art_pomo_pause, &art_pomo_done, &art_pomo_rest };

/* ---- 配置 ---- */
static const int  DUR_OPTS[]   = { 15*60, 25*60, 45*60 };
#define DUR_COUNT  3
#define REST_SECS  (5 * 60)

/* 方框进度条几何（直角，无圆角） */
#define FR_CX   120
#define FR_CY   146          /* 猹 96x96 左上角 (72,98)，和说明页同位 */
#define FR_HALF  54          /* 108x108 框住 96 图 */
#define FR_T      3          /* 边框粗细 */
#define FR_X0   (FR_CX - FR_HALF)
#define FR_Y0   (FR_CY - FR_HALF)
#define FR_X1   (FR_CX + FR_HALF)
#define FR_Y1   (FR_CY + FR_HALF)
#define FR_W    (FR_HALF * 2)
#define FR_H    (FR_HALF * 2)
#define FR_PERIM (2 * (FR_W + FR_H))

/* 5 段周长：从顶部中心顺时针 */
static const int SEG_LEN[5] = { FR_HALF, FR_H, FR_W, FR_H, FR_HALF };

/* ---- 静态变量 ---- */
static lv_obj_t *s_scr;
static lv_obj_t *s_time_lbl, *s_state_lbl, *s_hint_lbl, *s_dur_lbl;
static lv_obj_t *s_avatar;
static lv_obj_t *s_fbg[4];   /* 背景框 4 边 */
static lv_obj_t *s_fpg[5];   /* 进度前景 5 段 */

static lv_timer_t *s_tick_timer, *s_done_timer, *s_phase2_timer;

static state_t s_state;
static int  s_dur_idx;       /* 0/1/2 → 15/25/45 分 */
static int  s_total_secs;
static int  s_remain;        /* 专注剩余秒 */
static int  s_rest_remain;   /* 休息剩余秒 */

/* ---- 工具函数 ---- */

static lv_obj_t *mk_label(lv_obj_t *par, const char *txt,
                           const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = lv_label_create(par);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    return l;
}

static lv_obj_t *mk_rect(lv_obj_t *par, int x, int y, int w, int h, uint32_t c)
{
    lv_obj_t *r = lv_obj_create(par);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, w > 0 ? w : 1, h > 0 ? h : 1);
    lv_obj_set_pos(r, x, y);
    lv_obj_set_style_bg_color(r, lv_color_hex(c), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    return r;
}

static void hline(lv_obj_t *par, int y)
{
    mk_rect(par, 0, y, 240, 1, C_LINE);
}

/* ---- 进度条更新 ---- */

static void progress_update(float pct, uint32_t color)
{
    int filled = (int)(pct * FR_PERIM);
    int consumed = 0;

    for (int i = 0; i < 5; i++) {
        int seg = SEG_LEN[i];
        int fill = filled - consumed;
        if (fill <= 0) {
            lv_obj_add_flag(s_fpg[i], LV_OBJ_FLAG_HIDDEN);
            consumed += seg;
            continue;
        }
        if (fill > seg) fill = seg;
        lv_obj_remove_flag(s_fpg[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_fpg[i], lv_color_hex(color), 0);

        switch (i) {
        case 0: /* 顶边右半：→ */
            lv_obj_set_pos(s_fpg[i], FR_CX, FR_Y0);
            lv_obj_set_size(s_fpg[i], fill, FR_T);
            break;
        case 1: /* 右边：↓ */
            lv_obj_set_pos(s_fpg[i], FR_X1 - FR_T, FR_Y0);
            lv_obj_set_size(s_fpg[i], FR_T, fill);
            break;
        case 2: /* 底边：← */
            lv_obj_set_pos(s_fpg[i], FR_X1 - fill, FR_Y1 - FR_T);
            lv_obj_set_size(s_fpg[i], fill, FR_T);
            break;
        case 3: /* 左边：↑ */
            lv_obj_set_pos(s_fpg[i], FR_X0, FR_Y1 - fill);
            lv_obj_set_size(s_fpg[i], FR_T, fill);
            break;
        case 4: /* 顶边左半：→ */
            lv_obj_set_pos(s_fpg[i], FR_X0, FR_Y0);
            lv_obj_set_size(s_fpg[i], fill, FR_T);
            break;
        }
        consumed += seg;
    }
}

static void progress_clear(void)
{
    for (int i = 0; i < 5; i++)
        lv_obj_add_flag(s_fpg[i], LV_OBJ_FLAG_HIDDEN);
}

/* ---- 时间显示 ---- */

static void show_time(int secs, uint32_t color)
{
    int m = secs / 60, s = secs % 60;
    char buf[16];
    snprintf(buf, sizeof buf, "%02d:%02d", m, s);
    lv_label_set_text(s_time_lbl, buf);
    lv_obj_set_style_text_color(s_time_lbl, lv_color_hex(color), 0);
}

/* ---- 状态文字 + 底部提示 ---- */

static void show_state(const char *txt, uint32_t color)
{
    lv_label_set_text(s_state_lbl, txt);
    lv_obj_set_style_text_color(s_state_lbl, lv_color_hex(color), 0);
    lv_obj_align(s_state_lbl, LV_ALIGN_TOP_MID, 0, 232);
}

static void show_hint(const char *txt)
{
    lv_label_set_text(s_hint_lbl, txt);
    lv_obj_align(s_hint_lbl, LV_ALIGN_TOP_MID, 0, 301);
}

/* 待机按时长换图（15/25/45 分），其他状态按状态换图 */
static const lv_image_dsc_t *pomo_img(int st)
{
    static const lv_image_dsc_t *const D[3] = { &art_pomo_d15, &art_pomo_d25, &art_pomo_d45 };
    if (st == ST_IDLE) return D[(s_dur_idx >= 0 && s_dur_idx < 3) ? s_dur_idx : 1];
    return POMO_IMG[st];
}

/* ---- 时长选择显示（仅待机） ---- */

static void show_dur(void)
{
    char buf[32];
    snprintf(buf, sizeof buf, "< %d 分 >", DUR_OPTS[s_dur_idx] / 60);
    lv_label_set_text(s_dur_lbl, buf);
    lv_obj_align(s_dur_lbl, LV_ALIGN_TOP_MID, 0, 254);
    lv_obj_remove_flag(s_dur_lbl, LV_OBJ_FLAG_HIDDEN);
    if (s_avatar && s_state == ST_IDLE) lv_image_set_src(s_avatar, pomo_img(ST_IDLE));
}

/* ---- 状态机 ---- */

static void enter_state(state_t st);

/* 每秒 tick */
static void tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_state == ST_FOCUS) {
        s_remain--;
        if (s_remain <= 0) {
            enter_state(ST_DONE);
            return;
        }
        show_time(s_remain, C_GREEN);
        float pct = 1.0f - (float)s_remain / s_total_secs;
        progress_update(pct, C_GREEN);
    } else if (s_state == ST_REST) {
        s_rest_remain--;
        if (s_rest_remain <= 0) {
            enter_state(ST_IDLE);
            return;
        }
        show_time(s_rest_remain, C_GOLD);
        float pct = 1.0f - (float)s_rest_remain / REST_SECS;
        progress_update(pct, C_GOLD);
    }
}

/* 完成闪烁结束 → 进入休息 */
static void done_flash_end(lv_timer_t *t)
{
    (void)t;
    s_done_timer = NULL;
    lv_obj_set_style_border_width(s_scr, 0, 0);
    enter_state(ST_REST);
}

static void enter_state(state_t st)
{
    s_state = st;
    /* 计时中需要常亮且每秒刷新：申请 caffeine 冻结空闲降级（不走 sleep 路径） */
    power_hold(POWER_CAFFEINE, st == ST_FOCUS || st == ST_DONE || st == ST_REST);
    if (s_avatar) lv_image_set_src(s_avatar, pomo_img(st));

    /* 停 tick（需要时重启） */
    if (s_tick_timer) lv_timer_pause(s_tick_timer);
    if (s_done_timer) { lv_timer_delete(s_done_timer); s_done_timer = NULL; }

    switch (st) {

    case ST_IDLE:
        s_remain = DUR_OPTS[s_dur_idx];
        s_total_secs = s_remain;
        show_time(s_remain, C_TEXT);
        show_state("等待开始", C_MUTED);
        show_hint("小猹曰：逝者如斯夫");
        show_dur();
        progress_clear();
        lv_obj_set_style_border_width(s_scr, 0, 0);
        break;

    case ST_FOCUS:
        lv_obj_add_flag(s_dur_lbl, LV_OBJ_FLAG_HIDDEN);
        show_time(s_remain, C_GREEN);
        show_state("专注中", C_GREEN);
        show_hint("小猹曰：逝者如斯夫");
        {
            float pct = 1.0f - (float)s_remain / s_total_secs;
            progress_update(pct, C_GREEN);
        }
        lv_timer_resume(s_tick_timer);
        break;

    case ST_PAUSE:
        show_time(s_remain, C_MUTED);
        show_state("已暂停", C_GOLD);
        show_hint("小猹曰：逝者如斯夫");
        {
            float pct = 1.0f - (float)s_remain / s_total_secs;
            progress_update(pct, C_MUTED);
        }
        /* tick 已 pause */
        break;

    case ST_DONE:
        /* 闪绿框 220ms → 进休息 */
        lv_obj_set_style_border_color(s_scr, lv_color_hex(C_GREEN), 0);
        lv_obj_set_style_border_width(s_scr, 2, 0);
        sfx_play(SFX_CLEAR);
        progress_update(1.0f, C_GREEN);
        show_time(0, C_GREEN);
        show_state("完成", C_GREEN);
        show_hint("");
        s_done_timer = lv_timer_create(done_flash_end, 800, NULL);
        lv_timer_set_repeat_count(s_done_timer, 1);
        break;

    case ST_REST:
        s_rest_remain = REST_SECS;
        progress_clear();
        show_time(s_rest_remain, C_GOLD);
        show_state("休息中", C_GOLD);
        show_hint("小猹曰：逝者如斯夫");
        lv_timer_resume(s_tick_timer);
        break;
    }
}

/* ---- 第二阶段初始化 ---- */

static void phase2_build(lv_timer_t *t)
{
    (void)t;
    s_phase2_timer = NULL;

    /* 进度前景 5 段（初始隐藏） */
    for (int i = 0; i < 5; i++) {
        s_fpg[i] = mk_rect(s_scr, 0, 0, 1, 1, C_GREEN);
        lv_obj_add_flag(s_fpg[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* 时间 */
    s_time_lbl = mk_label(s_scr, "25:00", &font_ps16, C_TEXT);
    lv_obj_align(s_time_lbl, LV_ALIGN_TOP_MID, 0, 210);

    /* 状态文字 */
    s_state_lbl = mk_label(s_scr, "", &font_fusion12, C_MUTED);

    /* 时长选择 */
    s_dur_lbl = mk_label(s_scr, "", &font_fusion12, C_MUTED);

    /* 底栏 */
    hline(s_scr, 292);
    s_hint_lbl = mk_label(s_scr, "", &font_fusion12, C_MUTED);

    /* tick timer（暂停，进状态时恢复） */
    s_tick_timer = lv_timer_create(tick_cb, 1000, NULL);
    lv_timer_pause(s_tick_timer);

    /* 进入待机状态 */
    enter_state(ST_IDLE);
}

/* ---- 页面接口 ---- */

static void page_enter(void)
{
    s_scr = page_screen_create();
    s_dur_idx = 1;  /* 默认 25 分 */
    s_state = ST_IDLE;   /* 先归位，避免第一帧按上次离开时的状态画图 */

    /* 第一阶段：顶栏 + 背景框 + 头像（~10 对象） */

    /* 顶栏 */
    ui_topbar(s_scr, "番茄猹");

    /* 背景框 4 边（直角） */
    s_fbg[0] = mk_rect(s_scr, FR_X0, FR_Y0,         FR_W, FR_T,  C_LINE); /* 顶 */
    s_fbg[1] = mk_rect(s_scr, FR_X1 - FR_T, FR_Y0,  FR_T, FR_H,  C_LINE); /* 右 */
    s_fbg[2] = mk_rect(s_scr, FR_X0, FR_Y1 - FR_T,  FR_W, FR_T,  C_LINE); /* 底 */
    s_fbg[3] = mk_rect(s_scr, FR_X0, FR_Y0,         FR_T, FR_H,  C_LINE); /* 左 */

    /* 头像居中在框内 */
    s_avatar = lv_image_create(s_scr);
    lv_image_set_src(s_avatar, pomo_img(s_state));
    lv_obj_set_pos(s_avatar, FR_CX - 48, FR_CY - 48);

    /* 第二阶段：延迟 50ms */
    s_tick_timer   = NULL;
    s_done_timer   = NULL;
    s_phase2_timer = lv_timer_create(phase2_build, 50, NULL);
    lv_timer_set_repeat_count(s_phase2_timer, 1);

    lv_screen_load(s_scr);
}

static void page_exit(void)
{
    power_hold(POWER_CAFFEINE, false);
    if (s_tick_timer)   { lv_timer_delete(s_tick_timer);   s_tick_timer = NULL; }
    if (s_done_timer)   { lv_timer_delete(s_done_timer);   s_done_timer = NULL; }
    if (s_phase2_timer) { lv_timer_delete(s_phase2_timer); s_phase2_timer = NULL; }
    s_time_lbl = s_state_lbl = s_hint_lbl = s_dur_lbl = NULL;
    s_avatar = NULL;
    for (int i = 0; i < 4; i++) s_fbg[i] = NULL;
    for (int i = 0; i < 5; i++) s_fpg[i] = NULL;
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
}

static void page_key_cb(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    /* 第二阶段还没建完，忽略按键 */
    if (!s_time_lbl) return;

    if (btn == BSP_BTN_OK && ev == BSP_BTN_CLICK) {
        switch (s_state) {
        case ST_IDLE:   sfx_play(SFX_POWERUP); enter_state(ST_FOCUS); break;
        case ST_FOCUS:  sfx_play(SFX_BLIP); enter_state(ST_PAUSE); break;
        case ST_PAUSE:  sfx_play(SFX_BLIP); enter_state(ST_FOCUS); break;
        case ST_DONE:   break;  /* 等闪完自动进休息 */
        case ST_REST:   enter_state(ST_IDLE);  break;
        }
    }
    else if (ev == BSP_BTN_CLICK && s_state == ST_IDLE) {
        if (btn == BSP_BTN_UP) {
            s_dur_idx = (s_dur_idx + 1) % DUR_COUNT;
            s_remain = DUR_OPTS[s_dur_idx];
            s_total_secs = s_remain;
            show_time(s_remain, C_TEXT);
            show_dur();
            sfx_play(SFX_NAV);
        }
        else if (btn == BSP_BTN_DOWN) {
            s_dur_idx = (s_dur_idx + DUR_COUNT - 1) % DUR_COUNT;
            s_remain = DUR_OPTS[s_dur_idx];
            s_total_secs = s_remain;
            show_time(s_remain, C_TEXT);
            show_dur();
            sfx_play(SFX_NAV);
        }
    }
    /* LONG: pages.c 拦截后调 page_home()，这里不处理 */
}

const page_t page_pomo = { "page_pomo", page_enter, page_exit, page_key_cb };
