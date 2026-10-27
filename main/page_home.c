// main/page_home.c —— 观猹首页（按 mock v14 布局）；身份区读蓝牙同步的资料
#include "pages.h"
#include "sfx.h"
#include "storage.h"
#include "ble_sync.h"
#include "bsp_battery.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(font_fusion12);
LV_FONT_DECLARE(font_ps8);
LV_FONT_DECLARE(font_ps16);
#include "cha_pool.h"
LV_IMAGE_DECLARE(ic_clover);
LV_IMAGE_DECLARE(ic_tomato);
LV_IMAGE_DECLARE(ic_melon);
LV_IMAGE_DECLARE(ic_type);
LV_IMAGE_DECLARE(ic_cha_mallet);
LV_IMAGE_DECLARE(ic_tiles);


// ---- 布局常量（屏幕像素，跟 mock 一一对应，调位置改这里）----
#define C1        48      // 第一列卡片中心
#define C23       156     // 第二三列中间
#define AV_BODY_C 36   /* 新图在 72 宽画布里水平居中 */
#define AV_X      (C1 - AV_BODY_C)
#define AV_Y      30
#define BADGE_H   15
#define BADGE_Y   (AV_Y + 64 + 6)

#define NAME_Y    50
#define NAME_W    136     // 昵称区宽度，超出显示省略号
#define ROW_X     (C23 - 29)
#define ROW1_Y    78
#define ROW2_Y    98
#define NUM_X     (ROW_X + 34)

#define GRID_X    18
#define GRID_Y    146
#define CARD_W    60
#define CARD_H    52
#define CARD_GX   72
#define CARD_GY   62

static lv_obj_t *s_scr;
static lv_obj_t *s_batt;
static lv_obj_t *s_av;
static lv_obj_t *s_badge, *s_badge_t;
static lv_obj_t *s_name;
static lv_obj_t *s_row1_t, *s_row2_t, *s_num1, *s_num2;
static lv_obj_t *s_cards[6];
static lv_obj_t *s_labels[6];
static lv_obj_t *s_icons[6];
static lv_timer_t *s_timer;
static lv_timer_t *s_sync_timer;
static int s_sel = 0;

static const struct { const char *name; const lv_image_dsc_t *icon; const lv_font_t *font; } ITEMS[6] = {
    {"好运猹", &ic_clover,     &font_fusion12},
    {"番茄猹", &ic_tomato,     &font_fusion12},
    {"吃瓜猹", &ic_melon,      &font_fusion12},
    {"什么猹", &ic_type,       &font_fusion12},
    {"木鱼猹", &ic_cha_mallet, &font_fusion12},
    {"1024",   &ic_tiles,      &font_ps8},
};

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

static lv_obj_t *label(lv_obj_t *p, const char *t, const lv_font_t *f, uint32_t c, int x, int y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, t);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(c), 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

static void fmt_count(char *buf, size_t n, uint32_t v)
{
    if (v < 1000)       snprintf(buf, n, "%lu", (unsigned long)v);
    else if (v < 10000) snprintf(buf, n, "%lu.%luk", (unsigned long)(v / 1000), (unsigned long)(v % 1000 / 100));
    else                snprintf(buf, n, "%luk", (unsigned long)(v / 1000));
}

static bool is_ascii(const char *s)
{
    for (; *s; s++) if ((uint8_t)*s >= 0x80) return false;
    return true;
}

// 原地去掉字库里没有字形的字符（emoji、花体字母、零宽字符等），免得显示成方框；ASCII 一律保留
static void strip_missing(char *s, const lv_font_t *f)
{
    char *w = s;
    const char *r = s;
    while (*r) {
        uint8_t c = (uint8_t)*r;
        int n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        uint32_t cp = n == 1 ? c : n == 2 ? (c & 0x1F) : n == 3 ? (c & 0x0F) : (c & 0x07);
        bool ok = !(n == 1 && c >= 0x80);              // 孤立的续字节/非法首字节丢掉
        for (int k = 1; k < n; k++) {
            uint8_t d = (uint8_t)r[k];
            if ((d & 0xC0) != 0x80) { ok = false; n = k; break; }
            cp = (cp << 6) | (d & 0x3F);
        }
        if (ok && cp >= 0x80) {
            lv_font_glyph_dsc_t g;
            ok = lv_font_get_glyph_dsc(f, &g, cp, 0) && !g.is_placeholder;
        }
        if (ok) { memmove(w, r, n); w += n; }
        r += n;
    }
    *w = 0;
}

// 把同步资料画到身份区（首次进页 + 收到新同步时调用）
static void apply_profile(void)
{
    profile_t p;
    profile_load(&p);

    // 身份标签：普通用户灰 USER / 观猹员绿 / 金牌观猹员金
    uint32_t col = C_MUTED;
    const char *tag = "USER";
    const lv_font_t *tf = &font_ps8;
    if (p.valid && p.tier == TIER_MEMBER) { col = C_GREEN; tag = "观猹员";     tf = &font_fusion12; }
    if (p.valid && p.tier == TIER_GOLD)   { col = C_GOLD;  tag = "金牌观猹员"; tf = &font_fusion12; }
    lv_label_set_text(s_badge_t, tag);
    lv_obj_set_style_text_font(s_badge_t, tf, 0);
    lv_obj_set_style_text_color(s_badge_t, lv_color_hex(col), 0);
    lv_obj_update_layout(s_badge_t);
    int bw = lv_obj_get_width(s_badge_t) + 10;
    lv_obj_set_size(s_badge, bw, BADGE_H);
    lv_obj_set_pos(s_badge, C1 - bw / 2, BADGE_Y);
    lv_obj_set_style_border_color(s_badge, lv_color_hex(col), 0);
    lv_obj_center(s_badge_t);

    // 昵称：纯英文短名用 ps16，长英文用 ps8，含中文用 fusion12
    uint32_t hi = (p.valid && p.tier == TIER_GOLD) ? C_GOLD : C_GREEN;
    if (!p.valid || !p.nick[0]) {
        lv_label_set_text(s_name, "未同步");
        lv_obj_set_style_text_font(s_name, &font_fusion12, 0);
        lv_obj_set_style_text_color(s_name, lv_color_hex(C_MUTED), 0);
    } else {
        char nick[sizeof p.nick];
        memcpy(nick, p.nick, sizeof nick);
        nick[sizeof nick - 1] = 0;
        if (!is_ascii(nick)) strip_missing(nick, &font_fusion12);
        if (!nick[0]) strcpy(nick, "--");                 // 全是 emoji 之类：去完就空了
        const lv_font_t *nf = &font_fusion12;
        if (is_ascii(nick)) nf = strlen(nick) <= 8 ? &font_ps16 : &font_ps8;
        lv_label_set_text(s_name, nick);
        lv_obj_set_style_text_font(s_name, nf, 0);
        lv_obj_set_style_text_color(s_name, lv_color_hex(p.tier == TIER_GOLD ? C_GOLD : C_TEXT), 0);
    }

    // 发言 = 猹评 + 猹馆；获赞
    char b1[16], b2[16];
    if (p.valid) { fmt_count(b1, sizeof b1, p.reviews + p.posts); fmt_count(b2, sizeof b2, p.upvotes); }
    else         { strcpy(b1, "--"); strcpy(b2, "--"); }
    lv_label_set_text(s_num1, b1);
    lv_label_set_text(s_num2, b2);
    lv_obj_set_style_text_color(s_num1, lv_color_hex(hi), 0);
    lv_obj_set_style_text_color(s_num2, lv_color_hex(hi), 0);
}

static void refresh_sel(void)
{
    for (int i = 0; i < 6; i++) {
        bool on = (i == s_sel);
        lv_obj_set_style_border_color(s_cards[i], lv_color_hex(on ? C_GREEN : C_LINE), 0);
        lv_obj_set_style_border_width(s_cards[i], on ? 2 : 1, 0);
        lv_obj_set_style_text_color(s_labels[i], lv_color_hex(on ? C_GREEN : C_TEXT), 0);
        lv_obj_set_style_image_recolor(s_icons[i], lv_color_hex(on ? C_GREEN : 0xFFFFFF), 0);
        lv_obj_set_style_image_recolor_opa(s_icons[i], LV_OPA_COVER, 0);
    }
}

static void tick(lv_timer_t *t)
{
    (void)t;
    int soc = bsp_battery_soc();
    char buf[16];
    if (soc < 0) snprintf(buf, sizeof buf, "--%%");
    else         snprintf(buf, sizeof buf, "%d%%", soc);
    lv_label_set_text(s_batt, buf);
}

static void sync_tick(lv_timer_t *t)
{
    (void)t;
    if (ble_sync_take_updated()) apply_profile();
}

static void home_enter(void)
{
    s_scr = lv_obj_create(NULL);
    lv_obj_remove_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);

    // 顶栏
    label(s_scr, "WATCHA", &font_ps8, C_GREEN, 18, 9);
    block(s_scr, 188, 11, 4, 4, C_GREEN);
    s_batt = label(s_scr, "--%", &font_ps8, C_MUTED, 198, 9);
    block(s_scr, 0, 26, 240, 1, C_LINE);

    // 头像 + 身份标签
    s_av = lv_image_create(s_scr);
    lv_image_set_src(s_av, cha_random_72());
    lv_obj_set_pos(s_av, AV_X, AV_Y);
    s_badge = block(s_scr, C1 - 24, BADGE_Y, 48, BADGE_H, C_BG);
    lv_obj_set_style_border_width(s_badge, 1, 0);
    s_badge_t = label(s_badge, "", &font_fusion12, C_MUTED, 0, 0);

    // 昵称 + 数据
    s_name = label(s_scr, "", &font_ps16, C_TEXT, C23 - NAME_W / 2, NAME_Y);
    lv_obj_set_width(s_name, NAME_W);
    lv_obj_set_style_text_align(s_name, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_name, LV_LABEL_LONG_DOT);
    s_row1_t = label(s_scr, "发言", &font_fusion12, C_MUTED, ROW_X, ROW1_Y);
    s_num1   = label(s_scr, "--",   &font_ps8,      C_GREEN, NUM_X, ROW1_Y + 3);
    s_row2_t = label(s_scr, "获赞", &font_fusion12, C_MUTED, ROW_X, ROW2_Y);
    s_num2   = label(s_scr, "--",   &font_ps8,      C_GREEN, NUM_X, ROW2_Y + 3);
    block(s_scr, 0, 134, 240, 1, C_LINE);
    ble_sync_take_updated();   // 进页即读最新，旧标志作废
    apply_profile();

    // 六宫格
    for (int i = 0; i < 6; i++) {
        int r = i / 3, c = i % 3;
        int x = GRID_X + c * CARD_GX, y = GRID_Y + r * CARD_GY;
        lv_obj_t *card = block(s_scr, x, y, CARD_W, CARD_H, C_CARD);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(C_LINE), 0);
        lv_obj_t *ic = lv_image_create(card);
        lv_image_set_src(ic, ITEMS[i].icon);
        lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 6);
        s_icons[i] = ic;
        lv_obj_t *lb = label(card, ITEMS[i].name, ITEMS[i].font, C_TEXT, 0, 0);
        lv_obj_align(lb, LV_ALIGN_BOTTOM_MID, 0, -5);
        s_cards[i] = card;
        s_labels[i] = lb;
    }
    refresh_sel();

    // 底部两行
    block(s_scr, 0, 276, 240, 1, C_LINE);
    /* 大会纪念票根：日期 + 本机编号（蓝牙地址后 6 位，每台不同），两端像素小方块当打孔 */
    char tk[64];
    snprintf(tk, sizeof tk, "观猹大会 10.24 · #E6B93C No.%s#", ble_sync_device_id());
    lv_obj_t *l1 = label(s_scr, tk, &font_fusion12, C_MUTED, 0, 0);
    lv_label_set_recolor(l1, true);
    lv_obj_align(l1, LV_ALIGN_TOP_MID, 0, 284);
    lv_obj_update_layout(l1);
    {
        int tx0 = lv_obj_get_x(l1), tx1 = tx0 + lv_obj_get_width(l1);
        block(s_scr, tx0 - 10, 289, 4, 4, C_GOLD);
        block(s_scr, tx1 + 6,  289, 4, 4, C_GOLD);
    }
    char rec[128];
    uint32_t mbest = store_get_u32("melon_best", 0);
    uint32_t muyu  = store_get_u32("muyu_best", 0);
    uint32_t g1024 = store_get_u32("g1024_best", 0);
    char ms[16], mu[16], gs[16];
    if (mbest) snprintf(ms, sizeof ms, "%lums", (unsigned long)mbest); else snprintf(ms, sizeof ms, "--");
    if (muyu)  snprintf(mu, sizeof mu, "%lu",   (unsigned long)muyu);  else snprintf(mu, sizeof mu, "--");
    if (g1024) snprintf(gs, sizeof gs, "%lu",   (unsigned long)g1024); else snprintf(gs, sizeof gs, "--");
    snprintf(rec, sizeof rec, "吃瓜 %s · 功德 %s · 合成 %s", ms, mu, gs);
    lv_obj_t *l2 = label(s_scr, rec, &font_fusion12, C_MUTED, 0, 0);
    lv_obj_align(l2, LV_ALIGN_TOP_MID, 0, 300);

    s_timer = lv_timer_create(tick, 30000, NULL);
    s_sync_timer = lv_timer_create(sync_tick, 300, NULL);
    tick(NULL);
    lv_screen_load(s_scr);
}

static void home_exit(void)
{
    if (s_timer)      { lv_timer_delete(s_timer);      s_timer = NULL; }
    if (s_sync_timer) { lv_timer_delete(s_sync_timer); s_sync_timer = NULL; }
    if (s_scr)        { lv_obj_delete(s_scr);          s_scr = NULL;   }
}

static void home_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (btn == BSP_BTN_UP && ev == BSP_BTN_LONG) {          // 首页长按 ▲：进口袋上新
        sfx_play(SFX_OK);
        page_open(PAGE_LAB);
        return;
    }
    if (btn == BSP_BTN_OK && ev == BSP_BTN_LONG) {          // 首页长按确认：换一只猹
        if (s_av) lv_image_set_src(s_av, cha_reroll_72());
        sfx_play(SFX_POP);
        return;
    }
    if (ev != BSP_BTN_CLICK) return;
    if (btn == BSP_BTN_UP)   { s_sel = (s_sel + 5) % 6; sfx_play(SFX_NAV); }
    if (btn == BSP_BTN_DOWN) { s_sel = (s_sel + 1) % 6; sfx_play(SFX_NAV); }
    if (btn == BSP_BTN_OK) { sfx_play(SFX_OK); page_intro_set(s_sel + PAGE_APP_BASE); page_open(PAGE_INTRO); return; }
    refresh_sel();
}

const page_t page_home_def = { "home", home_enter, home_exit, home_key };
