// main/apps/app_ui.c —— 见 app_ui.h
#include "app_ui.h"
#include "bsp_battery.h"
#include <stdio.h>

/* 电量标签注册表：每个屏幕的右上角电量标签都挂进这条单向链表。
 * 何时刷新不在这里决定——power.c 按设备姿态和唤醒节奏调 ui_battery_refresh_all()，
 * 本文件只负责「有哪些标签要重绘」。如此就没有每个标签各自跑一个定时器、
 * 而且还要跟 STANDBY 的唤醒周期恰巧对齐的耦合；刷新节奏只有 power.c 一个来源。
 * 生命周期：标签在 LV_EVENT_DELETE 里自动从链表摘除，页面无需再管。 */
typedef struct battery_label { lv_obj_t *label; struct battery_label *next; } battery_label_t;
static battery_label_t *s_battery_labels;

static void battery_unregister_cb(lv_event_t *e)
{
    const lv_obj_t *gone = lv_event_get_target_obj(e);
    for (battery_label_t **pp = &s_battery_labels; *pp; pp = &(*pp)->next) {
        if ((*pp)->label == gone) {
            battery_label_t *dead = *pp;
            *pp = dead->next;
            lv_free(dead);
            return;
        }
    }
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

    battery_label_t *node = lv_malloc(sizeof *node);
    if (node) {
        node->label = l;
        node->next  = s_battery_labels;
        s_battery_labels = node;
        lv_obj_add_event_cb(l, battery_unregister_cb, LV_EVENT_DELETE, NULL);
    }
    return l;
}

void ui_battery_refresh_all(void)
{
    for (battery_label_t *n = s_battery_labels; n; n = n->next)
        ui_battery_refresh(n->label);
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
