// main/apps/app_ui.c —— 见 app_ui.h
#include "app_ui.h"
#include "bsp_battery.h"
#include <stdio.h>

// 低电量红：只属于电量配色规则，按项目约定不放进 pages.h 的公共调色板。
#define UI_BATTERY_RED 0xE05050

/* 电量视图注册表：屏幕上任何显示电量的东西都挂进这条单向链表——右上角标签、
 * 首页的 RPG 血条都走这里。何时刷新不在这里决定：power.c 按设备姿态和唤醒节奏调
 * ui_battery_refresh_all()，本文件只负责「有哪些视图要重绘、各自怎么重绘」。
 * 如此就没有每个视图各自跑一个定时器、而且还要跟 STANDBY 的唤醒周期恰巧对齐的
 * 耦合；刷新节奏只有 power.c 一个来源。
 * 生命周期：root 在 LV_EVENT_DELETE 里自动从链表摘除，页面无需再管。 */
typedef struct battery_view {
    lv_obj_t              *root;    // 视图里最先创建的那个对象，注销时用来比对
    ui_battery_paint_t     paint;
    void                  *ctx;
    struct battery_view   *next;
} battery_view_t;
static battery_view_t *s_battery_views;


static void battery_unregister_cb(lv_event_t *e)
{
    lv_obj_t *gone = lv_event_get_target_obj(e);
    for (battery_view_t **pp = &s_battery_views; *pp; pp = &(*pp)->next) {
        if ((*pp)->root == gone) {
            battery_view_t *dead = *pp;
            *pp = dead->next;
            lv_free(dead);
            return;
        }
    }
}

void ui_battery_register(lv_obj_t *root, ui_battery_paint_t paint, void *ctx)
{
    if (!root || !paint) return;

    battery_view_t *node = lv_malloc(sizeof *node);
    if (!node) return;
    node->root  = root;
    node->paint = paint;
    node->ctx   = ctx;
    node->next  = s_battery_views;
    s_battery_views = node;

    lv_obj_add_event_cb(root, battery_unregister_cb, LV_EVENT_DELETE, NULL);
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

void ui_battery_text(char *buf, size_t size, int soc)
{
    if (!buf || size < 4) return;
    if (soc < 0) snprintf(buf, size, "--%%");
    else         snprintf(buf, size, "%d%%", soc);
}

uint32_t ui_battery_color(int soc)
{
    if (soc < 0)   return C_GREEN;   // 读不到电量时不报警，按绿显示
    if (soc >= 50) return C_GREEN;
    if (soc >= 20) return C_GOLD;
    return UI_BATTERY_RED;
}

static void topbar_paint(void *ctx, int soc)
{
    lv_obj_t *label = ctx;
    if (!label) return;
    char buf[16];
    ui_battery_text(buf, sizeof buf, soc);
    lv_label_set_text(label, buf);
}

lv_obj_t *ui_battery_add(lv_obj_t *parent)
{
    lv_obj_t *l = ui_label(parent, "--%", &font_ps8, C_MUTED);
    lv_obj_set_pos(l, UI_TOP_RIGHT_X, UI_TOP_RIGHT_Y);
    ui_battery_register(l, topbar_paint, l);
    topbar_paint(l, bsp_battery_soc());
    return l;
}

int ui_battery_refresh_all(void)
{
    // 一次读取供所有视图共用：同一次重绘里各视图不该读出不同的电量。
    // 返回值就是本帧读数，调用方（power.c）拿它决定下一次刷新间隔，不必再读一次 I2C。
    const int soc = bsp_battery_soc();
    for (battery_view_t *n = s_battery_views; n; n = n->next)
        n->paint(n->ctx, soc);
    return soc;
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
