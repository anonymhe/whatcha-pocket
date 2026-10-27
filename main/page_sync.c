// main/page_sync.c —— 同步页：显示本机设备信息 + 连接观猹小程序的 deeplink 二维码
// 扫码直达观猹小程序的同步页（路径固定），scene=sync 并带上本机设备 ID，
// 小程序据此连回这台设备用蓝牙写入资料（见 ble_sync.c）。
#include "pages.h"
#include "sfx.h"
#include "ble_sync.h"
#include "bsp_battery.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(font_fusion12);
LV_FONT_DECLARE(font_ps8);
LV_FONT_DECLARE(font_ps16);

// 小程序 deeplink：t 定向到观猹小程序（同步用），cq 透传 scene=sync&d=<设备ID>。
// t 由小程序后台生成、与账号绑定，无法臆造；这里用参考链接给出的值，换账号时改这里。
#define WATCHA_MP_TOKEN "cQlWddcuWxi"
// cq 原文为 "scene=sync&d=<id>"，拼进 URL 时 '=' → %3D、'&' → %26（见参考链接）。
#define WATCHA_DEEPLINK_FMT "weixin://dl/business/?t=" WATCHA_MP_TOKEN "&cq=scene%%3Dsync%%26d%%3D%s"

static lv_obj_t  *s_scr;
static lv_obj_t  *s_batt_lbl;
static lv_timer_t *s_batt_timer;

static lv_obj_t *mk_label(lv_obj_t *par, const char *txt, const lv_font_t *f, uint32_t c)
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

static void batt_tick(lv_timer_t *t)
{
    (void)t;
    if (!s_batt_lbl) return;
    int soc = bsp_battery_soc();
    char buf[16];
    if (soc < 0) snprintf(buf, sizeof buf, "--%%");
    else         snprintf(buf, sizeof buf, "%d%%", soc);
    lv_label_set_text(s_batt_lbl, buf);
}

static void page_enter(void)
{
    s_scr = page_screen_create();
    const char *id = ble_sync_device_id();

    // 顶栏：标题 + 右上角电量
    lv_obj_t *tl = mk_label(s_scr, "同步", &font_fusion12, C_GREEN);
    lv_obj_set_pos(tl, 18, 9);
    s_batt_lbl = mk_label(s_scr, "", &font_ps8, C_MUTED);
    lv_obj_set_pos(s_batt_lbl, 198, 9);
    batt_tick(NULL);
    hline(s_scr, 26);

    // 说明
    lv_obj_t *hint = mk_label(s_scr, "微信扫码 连接观猹小程序", &font_fusion12, C_TEXT);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 36);

    // deeplink 二维码（白底黑码，扫码体验最好）
    char url[96];
    snprintf(url, sizeof url, WATCHA_DEEPLINK_FMT, id);
    lv_obj_t *qr = lv_qrcode_create(s_scr);
    lv_qrcode_set_size(qr, 150);
    lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(qr, lv_color_hex(0xFFFFFF));
    lv_qrcode_update(qr, url, (uint32_t)strlen(url));
    lv_obj_set_style_border_color(qr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(qr, 6, 0);
    lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 58);

    // 设备信息：本机编号（6 位十六进制，蓝牙 MAC 后 3 字节）
    char dev[32];
    snprintf(dev, sizeof dev, "#E6B93C 设备 No.%s#", id);
    lv_obj_t *dl = mk_label(s_scr, dev, &font_ps16, C_TEXT);
    lv_label_set_recolor(dl, true);
    lv_obj_align(dl, LV_ALIGN_TOP_MID, 0, 224);

    lv_obj_t *dl2 = mk_label(s_scr, "Watcha BLE · 蓝牙已就绪", &font_fusion12, C_MUTED);
    lv_obj_align(dl2, LV_ALIGN_TOP_MID, 0, 248);

    // 底部返回提示
    hline(s_scr, 292);
    lv_obj_t *back = mk_label(s_scr, "长按确认 ▶ 返回", &font_fusion12, C_MUTED);
    lv_obj_align(back, LV_ALIGN_TOP_MID, 0, 298);

    s_batt_timer = lv_timer_create(batt_tick, 30000, NULL);
    lv_screen_load(s_scr);
}

static void page_exit(void)
{
    if (s_batt_timer) { lv_timer_delete(s_batt_timer); s_batt_timer = NULL; }
    s_batt_lbl = NULL;
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
}

static void page_key_cb(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    (void)btn; (void)ev;   // 长按确认返回由 pages.c 统一拦截
}

const page_t page_sync = { "page_sync", page_enter, page_exit, page_key_cb };
