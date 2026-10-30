// components/bsp/src/bsp_display_lvgl.c
// LVGL 接入单独成文件:不用 LVGL 的开发者删掉本文件 + idf_component.yml 里的两条依赖即可。
#include "bsp_display.h"
#include "bsp_pins.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"

static const char *TAG = "bsp_lvgl";

static lv_display_t *s_disp;

lv_display_t *bsp_lvgl_init(void) {
    if (s_disp) return s_disp;
    if (!bsp_display_panel()) {
        ESP_LOGE(TAG, "请先成功调用 bsp_display_init()");
        return NULL;
    }

    lvgl_port_cfg_t pc = ESP_LVGL_PORT_INIT_CONFIG();
    pc.task_stack = 10240;   // 页面 enter / 按键处理都在 LVGL 任务里跑，默认 6KB 不够
    if (lvgl_port_init(&pc) != ESP_OK) {
        ESP_LOGE(TAG, "lvgl_port_init 失败");
        return NULL;
    }

    const lvgl_port_display_cfg_t dc = {
        .panel_handle = bsp_display_panel(),
        .io_handle    = bsp_display_io(),
        // ⚠ C3 无 PSRAM,DMA 只能用内部 RAM(总共约 150KB)。
        // 20 行单缓冲 ≈ 9.6KB;若改成 40 行双缓冲(≈37.5KB)会把 I2S 等外设的
        // DMA 描述符挤到 NO_MEM。刷新略慢但稳。
        .buffer_size   = (uint32_t)BSP_LCD_W * 20,
        .double_buffer = false,
        .hres = BSP_LCD_W, .vres = BSP_LCD_H,
        // 旋转/镜像必须在这里配:esp_lvgl_port 注册显示时会重新下发 MADCTL,
        // 覆盖 bsp_display.c 里 esp_lcd_panel_mirror() 的设置。
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        // swap_bytes:LVGL 输出小端 RGB565,ST7789 走 SPI 要大端 → 需交换高低字节。
        .flags = { .buff_dma = true, .swap_bytes = true },
    };
    s_disp = lvgl_port_add_disp(&dc);
    if (!s_disp) { ESP_LOGE(TAG, "lvgl_port_add_disp 失败"); return NULL; }

    ESP_LOGI(TAG, "LVGL 就绪");
    return s_disp;
}

bool bsp_lvgl_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }
void bsp_lvgl_unlock(void)         { lvgl_port_unlock(); }

// LVGL 的 tick 也是 5ms 周期的 esp_timer，和按键扫描是同一个坑：light sleep 醒来后
// esp_timer 会把睡过去的每一拍都补跑一遍（5 分钟 ≈ 6 万次 lv_tick_inc），把 LVGL
// 任务堵住，界面和按键一起卡死。入睡前 stop，醒来 resume；resume 会重新起一个周期的
// 定时器，不补历史。lv_timer_enable(false) 顺带让 lv_timer_handler 空转等待。
void bsp_lvgl_tick_stop(void) { lvgl_port_stop(); }

// slept_ms 是本次实际睡了多久。定时器停着的时候 lv_tick 不会走，补这一次跳变后
// 画面上的时钟/倒计时才和真实时间对得上。
//
// 但只补 tick 还不够：lv_tick 一下跳过去一个小时，调度器下一拍会发现每个 lv_timer
// 都「欠跑了一个小时」，于是在同一次 lv_timer_handler 里把每个周期性 timer 反复补跑
// 到追平当前时间为止（10ms 周期欠一小时 ≈ 36 万次）。这会把 LVGL 任务堵死数秒，表现
// 为睡得越久、唤醒越慢。把每个 timer 的 last_run 重置到「现在」，调度器就从当下重新
// 计时，不再试图重放整段睡眠；时钟/倒计时的正确性已由上面的 lv_tick_inc 保住。
void bsp_lvgl_tick_resume(uint32_t slept_ms) {
    if (slept_ms) lv_tick_inc(slept_ms);
    for (lv_timer_t *t = lv_timer_get_next(NULL); t; t = lv_timer_get_next(t)) {
        lv_timer_reset(t);
    }
    lvgl_port_resume();
}
