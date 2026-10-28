// main/apps/app_ui.c —— 见 app_ui.h
#include "app_ui.h"
#include "bsp_battery.h"
#include <stdio.h>

/* 电量读数与定时器的生命周期：定时器指针挂在标签的 user_data 上，
 * 标签随所属屏幕一起删除时在 LV_EVENT_DELETE 里一并删掉定时器，
 * 页面因此不需要再单独管理电量定时器（漏删会回调到已释放的标签）。 */
static void battery_refresh_cb(lv_timer_t *t)
{
    ui_battery_refresh((lv_obj_t *)lv_timer_get_user_data(t));
}

static void battery_cleanup_cb(lv_event_t *e)
{
    lv_timer_t *t = (lv_timer_t *)lv_obj_get_user_data(lv_event_get_target_obj(e));
    if (t) lv_timer_delete(t);
}

lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, uint32_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

lv_obj_t *ui_block(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

void ui_battery_refresh(lv_obj_t *label)
{
    if (!label) return;
    int soc = bsp_battery_soc();
    char buf[16];
    if (soc < 0) snprintf(buf, sizeof buf, "--%%");
    else         snprintf(buf, sizeof buf, "%d%%", soc);
    lv_label_set_text(label, buf);
}

lv_obj_t *ui_battery_add(lv_obj_t *parent)
{
    lv_obj_t *l = ui_label(parent, "--%", &font_ps8, C_MUTED);
    lv_obj_set_pos(l, UI_TOP_RIGHT_X, UI_TOP_RIGHT_Y);
    ui_battery_refresh(l);

    lv_timer_t *t = lv_timer_create(battery_refresh_cb, 30000, NULL);
    lv_timer_set_user_data(t, l);
    lv_obj_set_user_data(l, t);
    lv_obj_add_event_cb(l, battery_cleanup_cb, LV_EVENT_DELETE, NULL);
    return l;
}

void ui_topbar_ex(lv_obj_t *scr, const char *title, const lv_font_t *title_font)
{
    lv_obj_t *t = ui_label(scr, title, title_font, C_GREEN);
    lv_obj_set_pos(t, UI_TOP_TITLE_X, UI_TOP_TITLE_Y);
    ui_battery_add(scr);
    ui_block(scr, 0, UI_TOP_LINE_Y, UI_W, 1, C_LINE);
}

void ui_topbar(lv_obj_t *scr, const char *title)
{
    ui_topbar_ex(scr, title, &font_fusion12);
}

void ui_footer(lv_obj_t *scr, const char *hint)
{
    ui_block(scr, 0, 292, UI_W, 1, C_LINE);
    lv_obj_t *h = ui_label(scr, hint, &font_fusion12, C_MUTED);
    lv_obj_align(h, LV_ALIGN_TOP_MID, 0, 298);
}
