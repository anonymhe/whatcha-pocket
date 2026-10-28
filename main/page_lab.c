// main/page_lab.c —— 口袋上新：二创玩法列表（首页长按 ▲ 进入；在玩法里长按确认回到这里）
#include "pages.h"
#include "sfx.h"
#include "apps.h"
#include "app_ui.h"
#include <stdio.h>

#define ROW_X  12
#define ROW_Y0 36
#define ROW_W  216
#define ROW_H  28
#define ROW_GAP 4
#define ROWS   7

static lv_obj_t *s_scr, *s_desc, *s_pos;
static lv_obj_t *s_rows[ROWS], *s_names[ROWS];
static int s_sel, s_top;

// 顶栏右侧是位置计数而不是电量：用 `0x??` 的十六进制口味标出当前选中第几个。
static void refresh_pos(void)
{
    if (!s_pos) return;
    char buf[8];
    if (APPS_N <= 0) snprintf(buf, sizeof buf, "--");
    else             snprintf(buf, sizeof buf, "0x%02X", (unsigned)(s_sel + 1));
    lv_label_set_text(s_pos, buf);
}

static void refresh(void)
{
    if (APPS_N <= 0) return;
    if (s_sel < s_top) s_top = s_sel;
    if (s_sel >= s_top + ROWS) s_top = s_sel - ROWS + 1;
    for (int i = 0; i < ROWS; i++) {
        int idx = s_top + i;
        if (idx >= APPS_N) { lv_obj_add_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(s_rows[i], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_names[i], APPS[idx].name);
        bool on = (idx == s_sel);
        lv_obj_set_style_border_color(s_rows[i], lv_color_hex(on ? C_GREEN : C_LINE), 0);
        lv_obj_set_style_border_width(s_rows[i], on ? 2 : 1, 0);
        lv_obj_set_style_text_color(s_names[i], lv_color_hex(on ? C_GREEN : C_TEXT), 0);
    }
    lv_label_set_text(s_desc, APPS[s_sel].desc ? APPS[s_sel].desc : "");
    refresh_pos();
}

static void lab_enter(void)
{
    s_scr = page_screen_create();
    // 顶栏：左名称、右位置计数（本页不显示电量，用 0x?? 十六进制计数代替）
    lv_obj_t *title = ui_label(s_scr, "口袋上新", &font_fusion12, C_GREEN);
    lv_obj_set_pos(title, UI_TOP_TITLE_X, UI_TOP_TITLE_Y);
    s_pos = ui_label(s_scr, "", &font_ps8, C_MUTED);
    lv_obj_set_pos(s_pos, UI_TOP_RIGHT_X, UI_TOP_RIGHT_Y);
    ui_block(s_scr, 0, UI_TOP_LINE_Y, UI_W, 1, C_LINE);

    for (int i = 0; i < ROWS; i++) {
        s_rows[i] = ui_block(s_scr, ROW_X, ROW_Y0 + i * (ROW_H + ROW_GAP), ROW_W, ROW_H, C_CARD);
        lv_obj_set_style_border_color(s_rows[i], lv_color_hex(C_LINE), 0);
        lv_obj_set_style_border_width(s_rows[i], 1, 0);
        s_names[i] = ui_label(s_rows[i], "", &font_fusion12, C_TEXT);
        lv_obj_align(s_names[i], LV_ALIGN_LEFT_MID, 10, 0);
    }
    s_desc = ui_label(s_scr, "", &font_fusion12, C_MUTED);
    lv_obj_align(s_desc, LV_ALIGN_TOP_MID, 0, 266);
    ui_footer(s_scr, "确认进入 · 长按返回");

    if (APPS_N <= 0) {
        lv_label_set_text(s_desc, "还没有玩法，照着 prompts 写一只吧");
        refresh_pos();
    } else {
        if (s_sel >= APPS_N) s_sel = 0;
        refresh();
    }
    lv_screen_load(s_scr);
}

static void lab_exit(void)
{
    s_pos = NULL;
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
}

static void lab_key(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    if (ev != BSP_BTN_CLICK || APPS_N <= 0) return;
    if (btn == BSP_BTN_UP)   { s_sel = (s_sel + APPS_N - 1) % APPS_N; sfx_play(SFX_NAV); refresh(); }
    if (btn == BSP_BTN_DOWN) { s_sel = (s_sel + 1) % APPS_N;          sfx_play(SFX_NAV); refresh(); }
    if (btn == BSP_BTN_OK)   { sfx_play(SFX_OK); page_open_app(APPS[s_sel].page); }
}

const page_t page_lab = { "lab", lab_enter, lab_exit, lab_key };
