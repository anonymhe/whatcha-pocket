// components/bsp/src/bsp_power.c
// 低功耗入口：light sleep 默认路径 + deep sleep 关机路径。
#include "bsp_power.h"
#include "bsp_display.h"
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_pins.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"

static const char *TAG = "bsp_power";

#define WAKE_PIN        BSP_BTN_ADC_GPIO
#define WAKE_PIN_MASK   (1ULL << WAKE_PIN)

static bool s_wake_armed;
static bool s_boot_cause_known;
static bsp_wake_cause_t s_boot_cause;

static esp_err_t wake_pin_as_input(void)
{
    const gpio_config_t io = {
        .pin_bit_mask = WAKE_PIN_MASK,
        .mode         = GPIO_MODE_INPUT,
        // 板上已有外部 10k 上拉；再叠内部上拉会把三键分压整体抬高。
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    return gpio_config(&io);
}

static bsp_wake_cause_t map_wakeup_cause(esp_sleep_wakeup_cause_t cause)
{
    switch (cause) {
    case ESP_SLEEP_WAKEUP_GPIO:      return BSP_WAKE_CAUSE_BUTTON;
    case ESP_SLEEP_WAKEUP_TIMER:     return BSP_WAKE_CAUSE_TIMER;
    case ESP_SLEEP_WAKEUP_UNDEFINED: return BSP_WAKE_CAUSE_POWER_ON;
    default:                         return BSP_WAKE_CAUSE_OTHER;
    }
}

bsp_wake_cause_t bsp_power_boot_cause(void)
{
    if (s_boot_cause_known) return s_boot_cause;

    s_boot_cause_known = true;
    s_boot_cause = map_wakeup_cause(esp_sleep_get_wakeup_cause());
    return s_boot_cause;
}

esp_err_t bsp_power_wake_source_check(const bsp_wake_source_t *source)
{
    if (!source) return ESP_ERR_INVALID_ARG;
    // 既没有按键也没有定时器 = 没有任何唤醒源的睡眠，按键永远叫不醒（静默睡死）。
    if (!source->button && source->timer_us == 0) {
        ESP_LOGE(TAG, "唤醒源为空：至少需要按键或定时器之一");
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

esp_err_t bsp_power_wake_source_arm(const bsp_wake_source_t *source)
{
    ESP_RETURN_ON_ERROR(bsp_power_wake_source_check(source), TAG, "唤醒源配置非法");
    if (s_wake_armed) return ESP_OK;

    esp_err_t err = ESP_OK;

    if (source->timer_us) {
        ESP_RETURN_ON_ERROR(esp_sleep_enable_timer_wakeup(source->timer_us), TAG,
                            "定时唤醒使能失败");
    }

    if (source->button) {
        // 任一步失败都要把已经装上的定时器和 GPIO 摘掉，否则调用方拿到错误码后
        // 不会再调 disarm，sleep 里就留下半个配置。
        err = wake_pin_as_input();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "GPIO%d 配成唤醒输入失败: %s", (int)WAKE_PIN, esp_err_to_name(err));
            goto rollback;
        }
        err = gpio_wakeup_enable(WAKE_PIN, GPIO_INTR_LOW_LEVEL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "GPIO%d 唤醒使能失败: %s", (int)WAKE_PIN, esp_err_to_name(err));
            goto rollback;
        }
        // 必须显式打开这个唤醒源：漏调就等于没装按钮唤醒。
        err = esp_sleep_enable_gpio_wakeup();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "GPIO 唤醒源使能失败: %s", esp_err_to_name(err));
            gpio_wakeup_disable(WAKE_PIN);
            goto rollback;
        }
    }

    s_wake_armed = true;
    return ESP_OK;

rollback:
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    return err;
}

esp_err_t bsp_power_wake_source_disarm(void)
{
    if (!s_wake_armed) return ESP_OK;

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_GPIO);
    gpio_wakeup_disable(WAKE_PIN);

    // gpio_config() 把这一脚从模拟功能上摘下来了，不还回去按键电压全是错的。
    const esp_err_t err = bsp_button_restore_adc();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "把 GPIO%d 还给 ADC 失败: %s", (int)WAKE_PIN, esp_err_to_name(err));
    }

    s_wake_armed = false;
    return err;
}

esp_err_t bsp_power_light_sleep(const bsp_wake_source_t *source, int64_t *slept_us,
                                bsp_wake_cause_t *cause)
{
    ESP_RETURN_ON_ERROR(bsp_power_wake_source_arm(source), TAG, "装唤醒源失败，放弃本次睡眠");

    // 按键组件的 5ms 周期定时器会让 esp_timer 在醒来后逐个补跑漏掉的 tick
    // （5 分钟 ≈ 6 万次），表现为唤醒后按键失灵。入睡前停掉，醒来立刻恢复。
    // 其他周期性 esp_timer（例如 LVGL 的 tick）由调用方负责，见 bsp_power.h。
    bsp_button_scan_stop();

    const int64_t started_us = esp_timer_get_time();
    const esp_err_t sleep_err = esp_light_sleep_start();
    const int64_t elapsed_us = esp_timer_get_time() - started_us;

    const esp_err_t restore_err = bsp_power_wake_source_disarm();
    bsp_button_scan_resume();
    if (slept_us) *slept_us = elapsed_us;

    const bsp_wake_cause_t woke_by = map_wakeup_cause(esp_sleep_get_wakeup_cause());
    if (cause) *cause = woke_by;

    if (sleep_err == ESP_ERR_SLEEP_REJECT) {
        ESP_LOGW(TAG, "light sleep 被拒绝（唤醒源冲突？）");
        return sleep_err;
    }
    if (sleep_err != ESP_OK) {
        ESP_LOGE(TAG, "light sleep 失败: %s", esp_err_to_name(sleep_err));
        return sleep_err;
    }

    ESP_LOGI(TAG, "light sleep 醒来：原因=%d，历时 %lld ms", (int)woke_by, elapsed_us / 1000);
    return restore_err;
}

esp_err_t bsp_power_prepare_deep_sleep(const bsp_wake_source_t *source)
{
    ESP_RETURN_ON_ERROR(bsp_power_wake_source_check(source), TAG, "唤醒源配置非法");

    if (source->timer_us) {
        ESP_RETURN_ON_ERROR(esp_sleep_enable_timer_wakeup(source->timer_us), TAG,
                            "定时唤醒使能失败");
    }

    if (source->button) {
        ESP_RETURN_ON_ERROR(wake_pin_as_input(), TAG, "GPIO%d 配成唤醒输入失败", (int)WAKE_PIN);
        // ESP32-C3 没有 EXT0/EXT1，只能用 GPIO 位掩码唤醒。第一个参数是【引脚位掩码】
        // 而不是引脚号：传 0 会被判非法，随后就是一次没有任何唤醒源的深睡。
        ESP_RETURN_ON_ERROR(
            esp_deep_sleep_enable_gpio_wakeup(WAKE_PIN_MASK, ESP_GPIO_WAKEUP_GPIO_LOW),
            TAG, "deep sleep GPIO 唤醒使能失败");
    }

    // 3.3 V 轨在深睡时仍然带电，外设必须逐个点名下电，否则待机电流下不来。
    if (bsp_display_sleep() != ESP_OK) ESP_LOGW(TAG, "面板未能进入睡眠（不影响流程）");
    bsp_display_backlight(0);
    bsp_audio_sleep();
    bsp_battery_sleep();
    return ESP_OK;
}

void bsp_power_deep_sleep_start(void)
{
    // 关掉睡眠期间的 GPIO 自动隔离：唤醒脚要保持数字输入配置才能被拉起。
    esp_sleep_enable_gpio_switch(false);
    ESP_LOGW(TAG, "进入 deep sleep");
    esp_deep_sleep_start();
}
