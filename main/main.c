// main/main.c —— 观猹联名版：开机直接进身份卡页
#include "bsp_i2c.h"
#include "bsp_display.h"
#include "bsp_button.h"
#include "bsp_battery.h"
#include "bsp_pins.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static const char *TAG = "main";

#include "pages.h"
#include "power.h"
#include "storage.h"
#include "sfx.h"
#include "ble_sync.h"
#include "boot_anim.h"
static bool s_ready = false;
static int64_t s_skip_us = 0;   // 跳过开机动画的时刻：之后 600ms 内的按键（同一下的松开）丢掉，免得直接点进功能
static void card_ready(void) { page_open(PAGE_HOME); s_ready = true; }

// 按键回调跑在 esp_timer 任务里(iot_button 实现)，栈小且不该在里面碰 LVGL。
// 这里只把事件塞进队列；LVGL 任务里一个 10ms 的 lv_timer 取出来执行 page_key。
typedef struct { uint8_t btn, ev; } key_evt_t;
static QueueHandle_t s_keyq;

static void key_poll(lv_timer_t *t)
{
    (void)t;
    key_evt_t e;
    while (s_keyq && xQueueReceive(s_keyq, &e, 0) == pdTRUE)
        page_key((bsp_btn_t)e.btn, (bsp_btn_ev_t)e.ev);
}

// 省电策略都在 power.c 里；这里只负责把「开机动画是否结束」告诉它。
static void power_tick(lv_timer_t *t)
{
    (void)t;
    power_service(s_ready);
}

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    if (!s_keyq) return;
    if (!s_ready) { if (!s_skip_us) s_skip_us = esp_timer_get_time(); boot_anim_skip(); return; }
    int64_t now = esp_timer_get_time();
    if (s_skip_us && now - s_skip_us < 600000) return;
    // 调暗或已入睡时，第一下按键只用来叫醒；它的松开事件同样要丢掉。
    if (power_key_activity()) return;
    key_evt_t e = { (uint8_t)btn, (uint8_t)ev };
    xQueueSend(s_keyq, &e, 0);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Watcha x FoloToy AI Passport 启动");
    bsp_i2c_init();

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示初始化失败");
        return;
    }
    bsp_display_backlight(100);
    power_init();
    s_keyq = xQueueCreate(16, sizeof(key_evt_t));
    bsp_button_init(on_key, NULL);
    bsp_battery_init();
    storage_init();
    ble_sync_start();
    store_set_u32("boot_cnt", store_get_u32("boot_cnt", 0) + 1);   // 无时间同步时按开机次数算"天"
    sfx_init();

    if (bsp_lvgl_lock(1000)) {
        lv_timer_create(key_poll, 10, NULL);
        lv_timer_create(power_tick, 500, NULL);
        boot_anim_start(card_ready);
        bsp_lvgl_unlock();
    }
    ESP_LOGI(TAG, "就绪");
}
