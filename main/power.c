// main/power.c —— 省电策略的执行者。判据在 sleep_policy.c，这里只做动作。
#include "power.h"
#include "sleep_policy.h"

#include "bsp_display.h"
#include "bsp_power.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sfx.h"

static const char *TAG = "power";

// 降级时间表。STANDBY 从原来的「10 分钟关 codec」提前到 5 分钟进 light sleep：
// 屏幕灭掉之后再等下去没有收益，真正省电的是 MCU 睡下去那一刻。
#define DIM_AFTER_US      (30LL * 1000000)
#define STANDBY_AFTER_US  (5LL * 60 * 1000000)
#define SLEEP_AFTER_US    (60LL * 60 * 1000000)

// 进入 SLEEP（deep sleep）默认关闭：唤醒即冷启动，界面状态全部丢失，不适合作为
// 空闲超时的默认归宿。需要「长时间关机」的应用把它改成 1 即可。
#define POWER_ENABLE_DEEP_SLEEP 0

#define BACKLIGHT_ACTIVE  100
#define BACKLIGHT_DIM     3

// 叫醒后的这段时间里，按键事件（多半是同一下的松开）不再交给页面，
// 否则一次唤醒会顺带点进一个菜单项。
#define WAKE_SETTLE_US  (600LL * 1000)
// 留给 sfx 任务把 codec 关完，避免 MCU 停在 I2S 还有写入的时候。
#define CODEC_SETTLE_MS 300

static sleep_policy_t s_policy;
static uint32_t       s_holds;
static uint64_t       s_standby_refresh_us;
static sleep_stage_t  s_stage = SLEEP_STAGE_ACTIVE;
static int64_t        s_last_activity_us;
static int64_t        s_wake_us;
static bool           s_codec_down;

static bool holds_any(power_hold_t mask) { return (s_holds & (uint32_t)mask) != 0; }

static void mark_activity(void) { s_last_activity_us = esp_timer_get_time(); }

static void apply_stage(sleep_stage_t stage)
{
    s_stage = stage;
    bsp_display_backlight(stage == SLEEP_STAGE_ACTIVE ? BACKLIGHT_ACTIVE : BACKLIGHT_DIM);

    // 回到 ACTIVE 时把 codec 一起拉回来：休眠期间播放请求是被丢弃的。
    if (stage == SLEEP_STAGE_ACTIVE && s_codec_down) {
        sfx_resume();
        s_codec_down = false;
    }
}

void power_init(void)
{
    sleep_policy_init(&s_policy, DIM_AFTER_US, STANDBY_AFTER_US,
                      POWER_ENABLE_DEEP_SLEEP ? SLEEP_AFTER_US : INT64_MAX);
    mark_activity();
    s_stage = SLEEP_STAGE_ACTIVE;
    (void)bsp_power_boot_cause();   // 先记一次复位原因，串口里就能看出是不是被按键叫醒的
}

bool power_booted_from_sleep(void)
{
    const bsp_wake_cause_t cause = bsp_power_boot_cause();
    return cause == BSP_WAKE_CAUSE_BUTTON || cause == BSP_WAKE_CAUSE_TIMER;
}

void power_hold(power_hold_t hold, bool on)
{
    if (on) s_holds |= (uint32_t)hold;
    else    s_holds &= ~(uint32_t)hold;

    // 占用条件变化是一个状态边界：从这一刻重新计时，避免「番茄钟一结束就立刻入睡」。
    mark_activity();

    // 新增 caffeine 可能要把已经暗下去的屏幕立刻拉回来。
    if (on && s_stage != SLEEP_STAGE_ACTIVE) apply_stage(SLEEP_STAGE_ACTIVE);
}

void power_set_standby_refresh_us(uint64_t period_us)
{
    s_standby_refresh_us = period_us;
}

bool power_key_activity(void)
{
    const int64_t now = esp_timer_get_time();

    if (s_wake_us && now - s_wake_us < WAKE_SETTLE_US) return true;

    mark_activity();

    if (s_stage == SLEEP_STAGE_ACTIVE) return false;

    s_wake_us = now;
    apply_stage(SLEEP_STAGE_ACTIVE);
    return true;   // 这一下只用来叫醒，不交给页面
}

static bsp_wake_source_t standby_wake_source(void)
{
    if (s_standby_refresh_us == 0) return BSP_WAKE_SOURCE_BUTTON;
    return bsp_wake_source_button_and_timer(s_standby_refresh_us);
}

static void enter_standby(void)
{
    if (!s_codec_down) {
        sfx_sleep();
        s_codec_down = true;
        vTaskDelay(pdMS_TO_TICKS(CODEC_SETTLE_MS));
    }

    bsp_display_backlight(0);

    const bsp_wake_source_t wake = standby_wake_source();
    int64_t slept_us = 0;
    const esp_err_t err = bsp_power_light_sleep(&wake, &slept_us);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "本次未能入睡: %s", esp_err_to_name(err));
    } else if (s_standby_refresh_us) {
        ESP_LOGD(TAG, "定期唤醒，睡了 %lld ms", slept_us / 1000);
    }

    // 无论正常唤醒还是被拒，都按「刚操作过」重新计时，别紧接着又睡下去。
    mark_activity();
    s_wake_us = s_last_activity_us;
    apply_stage(SLEEP_STAGE_ACTIVE);
}

void power_service(bool ready)
{
    const sleep_input_t in = {
        .idle_us    = esp_timer_get_time() - s_last_activity_us,
        .ready      = ready,
        .caffeine   = holds_any(POWER_CAFFEINE),
        .radio_busy = holds_any(POWER_RADIO_ACTIVE),
    };
    const sleep_stage_t next = sleep_policy_stage(&s_policy, &in);

    if (next == SLEEP_STAGE_STANDBY) {
        enter_standby();   // 阻塞到被唤醒，醒来即回 ACTIVE
        return;
    }
    if (next == SLEEP_STAGE_SLEEP) {
        // deep sleep 不会返回；唤醒源装不上时降级为普通空闲，继续跑。
        (void)power_deep_sleep();
        return;
    }
    if (next != s_stage) apply_stage(next);
}

bool power_deep_sleep(void)
{
    if (bsp_power_prepare_deep_sleep(&BSP_WAKE_SOURCE_BUTTON) != ESP_OK) {
        ESP_LOGE(TAG, "唤醒源准备失败，放弃关机");
        return false;
    }
    bsp_power_deep_sleep_start();
    return true;   // deep sleep 唤醒是冷启动，不会执行到这里
}
