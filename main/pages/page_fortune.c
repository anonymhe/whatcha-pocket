// main/pages/page_fortune.c —— 猹好运：一天一签（无时间时按开机次数）
#include "pages.h"
#include "app_ui.h"
#include "storage.h"
#include "sfx.h"
#include "esp_random.h"
#include <stdio.h>

LV_FONT_DECLARE(font_fusion12);
LV_FONT_DECLARE(font_ps8);
LV_FONT_DECLARE(font_fusion20);
LV_IMAGE_DECLARE(art_fortune_none);
LV_IMAGE_DECLARE(art_fortune_zhong);
LV_IMAGE_DECLARE(art_fortune_shang);
LV_IMAGE_DECLARE(art_fortune_ss);
static const lv_image_dsc_t *const FORT_IMG[3] = { &art_fortune_zhong, &art_fortune_shang, &art_fortune_ss };

// ---- 文案（宜 / 忌 两个独立池，同一件事只进一个池）----
static const char *YI[] = {
    "写猹评", "重构", "上线", "吃瓜", "摸猹", "早睡", "发版", "逛猹馆", "冲金牌", "调参", "提需求", "敲木鱼",
};
static const char *JI[] = {
    "改需求", "删库", "加班", "熬夜", "摸鱼", "开会", "回消息", "拖更", "硬刚", "抢跑", "空腹", "换模型",
};
#define N_YI (sizeof YI / sizeof YI[0])
#define N_JI (sizeof JI / sizeof JI[0])

enum { R_ZHONG = 0, R_SHANG = 1, R_SS = 2 };
static const char *RANK_TXT[3] = { "中签", "上签", "上上签" };
static const uint32_t RANK_COL[3] = { C_TEXT, C_GREEN, C_GOLD };

// ---- 布局 ----
#define AV_Y     48
#define RANK_Y   146
#define YI_Y     196
#define JI_Y     222
#define HINT_Y   200

// NVS：fortune_day = 抽签时的 boot_cnt；fortune_val = rank<<16 | yi<<8 | ji
#define K_DAY "fortune_day"
#define K_VAL "fortune_val"

static lv_obj_t *s_scr, *s_av, *s_rank_lbl, *s_yi, *s_ji, *s_hint, *s_foot;
static lv_obj_t *s_line_l, *s_line_r, *s_dia_l, *s_dia_r;
static lv_obj_t *s_spark[10];
static int s_nspark;
static bool s_drawn, s_busy;
static int s_rank, s_yi_i, s_ji_i;

static lv_obj_t *label(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t c)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    return l;
}
static lv_obj_t *block(lv_obj_t *p, int x, int y, int w, int h, uint32_t c)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
    return o;
}
static void set_opa(void *o, int32_t v) { lv_obj_set_style_opa(o, (lv_opa_t)v, 0); }
static void set_x(void *o, int32_t v)   { lv_obj_set_x(o, v); }

// 签级行：文字 + 两侧线 + 菱形，按文字宽度重新排
static void layout_rank(const char *txt, uint32_t col)
{
    lv_label_set_text(s_rank_lbl, txt);
    lv_obj_set_style_text_color(s_rank_lbl, lv_color_hex(col), 0);
    lv_obj_update_layout(s_rank_lbl);
    int w = lv_obj_get_width(s_rank_lbl);
    lv_obj_align(s_rank_lbl, LV_ALIGN_TOP_MID, 0, RANK_Y - 12);
    int lx = (240 - w) / 2 - 12, rx = (240 + w) / 2 + 12;
    lv_obj_set_pos(s_line_l, 28, RANK_Y); lv_obj_set_width(s_line_l, lx - 6 - 28);
    lv_obj_set_pos(s_line_r, rx + 6, RANK_Y); lv_obj_set_width(s_line_r, 211 - rx - 6);
    lv_obj_set_pos(s_dia_l, lx - 2, RANK_Y - 2);
    lv_obj_set_pos(s_dia_r, rx - 2, RANK_Y - 2);
    lv_obj_set_style_bg_color(s_dia_l, lv_color_hex(col), 0);
    lv_obj_set_style_bg_color(s_dia_r, lv_color_hex(col), 0);
}

static void set_line(lv_obj_t *l, const char *tag, uint32_t tagcol, const char *word, int y)
{
    char buf[48];
    snprintf(buf, sizeof buf, "#%06lX %s#  %s", (unsigned long)tagcol, tag, word);
    lv_label_set_text(l, buf);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
}

static void clear_sparks(void)
{
    for (int i = 0; i < s_nspark; i++) if (s_spark[i]) { lv_anim_delete(s_spark[i], NULL); lv_obj_delete(s_spark[i]); s_spark[i] = NULL; }
    s_nspark = 0;
}

// 星光：以签级为轴左右镜像，交替闪
static void make_sparks(int rank)
{
    clear_sparks();
    if (rank == R_ZHONG) return;
    uint32_t col = RANK_COL[rank];
    static const int P[3][2] = { {46, RANK_Y - 26}, {74, RANK_Y - 14}, {58, RANK_Y + 18} };
    static const int D[2][2] = { {96, RANK_Y - 32}, {112, RANK_Y + 24} };
    int np = (rank == R_SS) ? 3 : 2, nd = (rank == R_SS) ? 2 : 1;
    int k = 0;
    for (int i = 0; i < np; i++) for (int m = 0; m < 2; m++) {
        lv_obj_t *s = label(s_scr, "+", &font_fusion12, col);
        lv_obj_set_pos(s, (m ? 240 - 8 - P[i][0] : P[i][0] - 3), P[i][1] - 6);
        s_spark[k++] = s;
    }
    for (int i = 0; i < nd; i++) for (int m = 0; m < 2; m++) {
        lv_obj_t *s = label(s_scr, "·", &font_fusion12, col);
        lv_obj_set_pos(s, (m ? 240 - 6 - D[i][0] : D[i][0] - 2), D[i][1] - 6);
        s_spark[k++] = s;
    }
    s_nspark = k;
    for (int i = 0; i < k; i++) {
        lv_anim_t a; lv_anim_init(&a);
        lv_anim_set_var(&a, s_spark[i]);
        lv_anim_set_values(&a, 255, 40);
        lv_anim_set_duration(&a, 500);
        lv_anim_set_playback_duration(&a, 500);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        lv_anim_set_delay(&a, (i * 173) % 700);
        lv_anim_set_exec_cb(&a, set_opa);
        lv_anim_start(&a);
    }
}

static void show_result(bool animate)
{
    layout_rank(RANK_TXT[s_rank], RANK_COL[s_rank]);
    if (s_av) lv_image_set_src(s_av, FORT_IMG[s_rank]);
    set_line(s_yi, "宜", C_GREEN, YI[s_yi_i], YI_Y);
    set_line(s_ji, "忌", C_MUTED, JI[s_ji_i], JI_Y);
    lv_obj_remove_flag(s_yi, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_ji, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_foot, "小猹曰：信则灵 不信也灵");
    lv_obj_align(s_foot, LV_ALIGN_TOP_MID, 0, 301);
    make_sparks(s_rank);
    lv_obj_set_style_border_width(s_scr, s_rank == R_SS ? 2 : 0, 0);
    lv_obj_set_style_border_color(s_scr, lv_color_hex(C_GOLD), 0);

    if (!animate) {
        lv_obj_set_style_opa(s_rank_lbl, 255, 0);
        lv_obj_set_style_opa(s_yi, 255, 0);
        lv_obj_set_style_opa(s_ji, 255, 0);
        return;
    }
    lv_anim_t a; lv_anim_init(&a);
    lv_obj_set_style_opa(s_rank_lbl, 0, 0);
    lv_anim_set_var(&a, s_rank_lbl); lv_anim_set_values(&a, 0, 255); lv_anim_set_duration(&a, 220);
    lv_anim_set_exec_cb(&a, set_opa); lv_anim_start(&a);
    lv_obj_set_style_opa(s_yi, 0, 0); lv_obj_set_style_opa(s_ji, 0, 0);
    lv_anim_init(&a); lv_anim_set_values(&a, 0, 255); lv_anim_set_duration(&a, 250); lv_anim_set_exec_cb(&a, set_opa);
    lv_anim_set_var(&a, s_yi); lv_anim_set_delay(&a, 200); lv_anim_start(&a);
    lv_anim_set_var(&a, s_ji); lv_anim_set_delay(&a, 320); lv_anim_start(&a);
    sfx_play(s_rank == R_SS ? SFX_FANFARE : s_rank == R_SHANG ? SFX_ARP_UP : SFX_PLINK);
}

static void shake_done(lv_anim_t *a)
{
    (void)a;
    lv_obj_set_x(s_av, (240 - 72) / 2);
    s_busy = false;
    if (!s_drawn) {
        uint32_t r = esp_random() % 100;
        s_rank = (r < 5) ? R_SS : (r < 40) ? R_SHANG : R_ZHONG;
        s_yi_i = esp_random() % N_YI;
        s_ji_i = esp_random() % N_JI;
        s_drawn = true;
        store_set_u32(K_VAL, ((uint32_t)s_rank << 16) | ((uint32_t)s_yi_i << 8) | (uint32_t)s_ji_i);
        store_set_u32(K_DAY, store_get_u32("boot_cnt", 0));
        show_result(true);
    }
}

static void shake(int cycles)
{
    s_busy = true;
    lv_anim_delete(s_av, NULL);
    lv_anim_t a; lv_anim_init(&a);
    lv_anim_set_var(&a, s_av);
    lv_anim_set_values(&a, (240 - 72) / 2 - 4, (240 - 72) / 2 + 4);
    lv_anim_set_duration(&a, 60);
    lv_anim_set_playback_duration(&a, 60);
    lv_anim_set_repeat_count(&a, cycles);
    lv_anim_set_exec_cb(&a, set_x);
    lv_anim_set_completed_cb(&a, shake_done);
    lv_anim_start(&a);
}

static void page_enter(void)
{
    s_scr = page_screen_create();
    s_busy = false; s_nspark = 0;
    ui_topbar(s_scr, "猹好运");
    block(s_scr, 0, 292, 240, 1, C_LINE);

    s_av = lv_image_create(s_scr);
    lv_image_set_src(s_av, &art_fortune_none);
    lv_obj_set_pos(s_av, (240 - 72) / 2, AV_Y);

    s_line_l = block(s_scr, 28, RANK_Y, 10, 1, C_LINE);
    s_line_r = block(s_scr, 200, RANK_Y, 10, 1, C_LINE);
    s_dia_l = block(s_scr, 0, 0, 4, 4, C_MUTED);
    s_dia_r = block(s_scr, 0, 0, 4, 4, C_MUTED);

    s_rank = R_ZHONG;
    s_rank_lbl = label(s_scr, "今日猹运", &font_fusion20, C_MUTED);

    s_yi = label(s_scr, "", &font_fusion12, C_TEXT);
    s_ji = label(s_scr, "", &font_fusion12, C_TEXT);
    lv_label_set_recolor(s_yi, true); lv_label_set_recolor(s_ji, true);
    lv_obj_set_style_text_letter_space(s_yi, 2, 0); lv_obj_set_style_text_letter_space(s_ji, 2, 0);
    lv_obj_add_flag(s_yi, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(s_ji, LV_OBJ_FLAG_HIDDEN);

    s_hint = label(s_scr, "按确认抽签", &font_fusion12, C_MUTED);
    lv_obj_set_style_text_letter_space(s_hint, 1, 0);
    lv_obj_align(s_hint, LV_ALIGN_TOP_MID, 0, HINT_Y);
    s_foot = label(s_scr, "小猹曰：信则灵 不信也灵", &font_fusion12, C_MUTED);
    lv_obj_align(s_foot, LV_ALIGN_TOP_MID, 0, 301);

    // 今天抽过没有
    uint32_t today = store_get_u32("boot_cnt", 0);
    s_drawn = (store_get_u32(K_DAY, 0xFFFFFFFF) == today);
    if (s_drawn) {
        uint32_t v = store_get_u32(K_VAL, 0);
        s_rank = (v >> 16) & 0xFF; s_yi_i = (v >> 8) & 0xFF; s_ji_i = v & 0xFF;
        if (s_rank > R_SS) s_rank = R_ZHONG;
        if (s_yi_i >= (int)N_YI) s_yi_i = 0;
        if (s_ji_i >= (int)N_JI) s_ji_i = 0;
        show_result(false);
    } else {
        layout_rank("今日猹运", C_MUTED);
    }

    lv_screen_load(s_scr);
}

static void page_exit(void)
{
    if (s_av) lv_anim_delete(s_av, NULL);
    if (s_rank_lbl) lv_anim_delete(s_rank_lbl, NULL);
    if (s_yi) lv_anim_delete(s_yi, NULL);
    if (s_ji) lv_anim_delete(s_ji, NULL);
    clear_sparks();
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
    s_av = s_yi = s_ji = s_hint = s_foot = s_line_l = s_line_r = s_dia_l = s_dia_r = NULL;
    s_rank_lbl = NULL;
}

static void page_key_cb(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn != BSP_BTN_OK || ev != BSP_BTN_CLICK) return;
    if (s_busy) return;
    shake(s_drawn ? 1 : 4);      // 已抽：小抖一下；未抽：摇签筒
}

const page_t page_fortune = { "fortune", page_enter, page_exit, page_key_cb };
