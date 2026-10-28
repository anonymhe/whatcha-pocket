// main/page_sync.c —— 同步页：显示本机设备信息 + 扫码连接观猹小程序。
// 规则（本页独有）：
//   · 蓝牙只在本页广播（进页 begin、离页 end），其它页面不开蓝牙。
//   · 90 秒无按键自动超时并返回首页；任意按键重置 90 秒计时。
//   · 收到有效同步数据后显示「同步完成」提示，约 2 秒后自动返回首页。
#include "pages.h"
#include "app_ui.h"
#include "sfx.h"
#include "ble_sync.h"
#include "lvgl.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(font_fusion12);
LV_FONT_DECLARE(font_ps8);
LV_FONT_DECLARE(font_ps16);

// 扫码直达观猹小程序的「AI Passport」中间页：二维码里放普通 https 链接，
// 由中间页再唤起小程序。s = 本机完整 MAC（12 位小写），k = 机身二维码上的出厂密钥
// （对跳转非必需；固件若未写入 NVS 则省略 k，仍可凭 s 定位到本机）。
#define WATCHA_SYNC_BASE "https://ai-passport.folotoy.cn/watcha-passport/"

#define SYNC_TIMEOUT_MS   90000   // 90 秒无按键超时返回首页
#define SYNC_DONE_MS      2000    // 同步完成提示停留后返回首页

static lv_obj_t  *s_scr;
static lv_obj_t  *s_status_lbl;
static lv_timer_t *s_poll_timer;     // 轮询同步结果
static lv_timer_t *s_timeout_timer;  // 90s 超时
static lv_timer_t *s_done_timer;     // 完成后延时返回
static bool        s_done;           // 已完成，忽略后续按键/超时

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

// 把二维码内容拼出来：带本机 MAC，若有出厂密钥再带 k。
static void build_url(char *out, size_t len)
{
    char key[40];
    // k 缺省也必须带上，否则中间页进不去；没有真实出厂密钥时用占位串冒充（装作隐私保护）。
    if (!ble_sync_pass_key(key, sizeof key)) snprintf(key, sizeof key, "redacted");
    snprintf(out, len, WATCHA_SYNC_BASE "?s=%s&k=%s", ble_sync_mac12(), key);
}

static void go_home(lv_timer_t *t) { (void)t; page_home(); }

// 轮询：收到有效同步数据 → 提示完成，SYNC_DONE_MS 后回首页。
static void poll_tick(lv_timer_t *t)
{
    (void)t;
    if (s_done) return;
    if (ble_sync_take_updated()) {
        s_done = true;
        lv_label_set_text(s_status_lbl, "同步完成");
        lv_obj_set_style_text_color(s_status_lbl, lv_color_hex(C_GREEN), 0);
        sfx_play(SFX_RECORD);
        if (s_timeout_timer) { lv_timer_delete(s_timeout_timer); s_timeout_timer = NULL; }
        if (!s_done_timer) { s_done_timer = lv_timer_create(go_home, SYNC_DONE_MS, NULL); lv_timer_set_repeat_count(s_done_timer, 1); }
    }
}

// 90s 无按键超时：回首页。
static void timeout_tick(lv_timer_t *t)
{
    (void)t;
    if (s_done) return;
    page_home();
}

static void page_enter(void)
{
    s_scr = page_screen_create();
    s_done = false;

    // 顶栏：标题 + 右上角电量
    ui_topbar(s_scr, "同步");

    // 说明
    lv_obj_t *hint = mk_label(s_scr, "微信扫码 连接观猹小程序", &font_fusion12, C_TEXT);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 36);

    // 普通链接二维码（白底黑码）
    char url[128];
    build_url(url, sizeof url);
    lv_obj_t *qr = lv_qrcode_create(s_scr);
    lv_qrcode_set_size(qr, 150);
    lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(qr, lv_color_hex(0xFFFFFF));
    lv_qrcode_update(qr, url, (uint32_t)strlen(url));
    lv_obj_set_style_border_color(qr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(qr, 6, 0);
    lv_obj_align(qr, LV_ALIGN_TOP_MID, 0, 58);

    // 设备编号：大号像素字只排 ASCII（font_ps16 无中文字形），中文说明放下一行 CJK 字体。
    char dev[32];
    snprintf(dev, sizeof dev, "#E6B93C No.%s#", ble_sync_device_id());
    lv_obj_t *dl = mk_label(s_scr, dev, &font_ps16, C_TEXT);
    lv_label_set_recolor(dl, true);
    lv_obj_align(dl, LV_ALIGN_TOP_MID, 0, 224);

    // 状态行：初始「等待连接」，完成时变「同步完成」
    s_status_lbl = mk_label(s_scr, "设备编号 · 等待扫码连接", &font_fusion12, C_MUTED);
    lv_obj_align(s_status_lbl, LV_ALIGN_TOP_MID, 0, 248);

    // 底部返回提示
    hline(s_scr, 292);
    lv_obj_t *back = mk_label(s_scr, "长按确认 ▶ 返回", &font_fusion12, C_MUTED);
    lv_obj_align(back, LV_ALIGN_TOP_MID, 0, 298);

    // 只在本页开蓝牙广播
    ble_sync_take_updated();   // 清掉进页前的旧标志
    ble_sync_adv_begin();

    s_poll_timer    = lv_timer_create(poll_tick, 200, NULL);
    s_timeout_timer = lv_timer_create(timeout_tick, SYNC_TIMEOUT_MS, NULL);
    lv_timer_set_repeat_count(s_timeout_timer, 1);
    power_keep_awake(true);   // 同步页全程亮屏，不被 30s 调暗打扰（超时由本页 90s 控制）
    lv_screen_load(s_scr);
}

static void page_exit(void)
{
    power_keep_awake(false);   // 离页恢复正常省电
    ble_sync_adv_end();   // 离页即关蓝牙
    if (s_poll_timer)    { lv_timer_delete(s_poll_timer);    s_poll_timer = NULL; }
    if (s_timeout_timer) { lv_timer_delete(s_timeout_timer); s_timeout_timer = NULL; }
    if (s_done_timer)    { lv_timer_delete(s_done_timer);    s_done_timer = NULL; }
    s_status_lbl = NULL;
    if (s_scr) { lv_obj_delete(s_scr); s_scr = NULL; }
}

static void page_key_cb(bsp_btn_t btn, bsp_btn_ev_t ev)
{
    (void)btn; (void)ev;   // 长按确认返回由 pages.c 统一拦截
    // 任意按键重置 90s 超时（完成后不再重置）
    if (!s_done && s_timeout_timer) lv_timer_reset(s_timeout_timer);
}

const page_t page_sync = { "page_sync", page_enter, page_exit, page_key_cb };
