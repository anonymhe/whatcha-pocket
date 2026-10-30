// main/power.c —— 省电策略的执行者。判据在 sleep_policy.c，这里只做动作。
#include "power.h"
#include "sleep_policy.h"

#include "bsp_display.h"
#include "bsp_power.h"
#include "bsp_battery.h"
#include "app_ui.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sfx.h"

static const char *TAG = "power";

// 降级时间表。STANDBY 从原来的「10 分钟关 codec」提前到 5 分钟进 light sleep：
// 屏幕压暗之后再等下去收益很小，真正省电的是 MCU 睡下去那一刻。
#define DIM_AFTER_US      (30LL * 1000000)
#define STANDBY_AFTER_US  (5LL * 60 * 1000000)
#define SLEEP_AFTER_US    (60LL * 60 * 1000000)

// 进入 SLEEP（deep sleep）默认关闭：唤醒即冷启动，界面状态全部丢失，不适合作为
// 空闲超时的默认归宿。需要「长时间关机」的应用把它改成 1 即可。
#define POWER_ENABLE_DEEP_SLEEP 0

#define BACKLIGHT_ACTIVE   100
// DIM 与 STANDBY 用同一个亮度：都是「屏幕还看得见，但明显暗下去」。
// 区别不在亮度而在代价——DIM 时 MCU 还醒着，STANDBY 时 MCU 已进 light sleep。
#define BACKLIGHT_LOW      10

// 叫醒后的这段时间里，按键事件（多半是同一下的松开）不再交给页面，
// 否则一次唤醒会顺带点进一个菜单项。
#define WAKE_SETTLE_US  (600LL * 1000)
// 留给 sfx 任务把 codec 关完，避免 MCU 停在 I2S 还有写入的时候。
#define CODEC_SETTLE_MS 300

// 电量读数的刷新节奏（唤醒与重绘公用同一套间隔）。
// STANDBY 期间按这个间隔定期唤醒重绘一帧；ACTIVE/DIM 期间按同一间隔就地重绘。
// 电量充足时刷得勤一点；掉到阈值以下就拉长间隔省电。
// 三个数都设计成可调：以后搬到设置页，或改阈值/间隔，只动这里即可。
#define BATTERY_REFRESH_US        (30LL * 1000000)   // 默认刷新间隔
#define BATTERY_REFRESH_LOW_US    (60LL * 1000000)   // 低电量时的刷新间隔
#define BATTERY_REFRESH_LOW_SOC   50                  // 电量(%)低于等于此值切到低电量间隔

static sleep_policy_t s_policy;
static uint32_t       s_holds;
static uint64_t       s_standby_refresh_us;
static int64_t        s_last_battery_refresh_us;   // 上次重绘电量读数的时刻，只在 LVGL 任务里读写
static sleep_stage_t  s_stage = SLEEP_STAGE_ACTIVE;
// 时间戳跨任务读写：按键回调在 button/esp_timer 任务，这里是 LVGL 任务。
// RV32 上 64 位读写不是单条指令，不加保护会读到撕裂值（超时忽长忽短）。
static portMUX_TYPE   s_time_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t        s_last_activity_us;   // 受 s_time_lock 保护
static int64_t        s_wake_us;            // 受 s_time_lock 保护
static bool           s_codec_down;

static bool holds_any(power_hold_t mask) { return (s_holds & (uint32_t)mask) != 0; }

static void mark_activity(void)
{
    const int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&s_time_lock);
    s_last_activity_us = now;
    portEXIT_CRITICAL(&s_time_lock);
}

static int64_t last_activity_us(void)
{
    portENTER_CRITICAL(&s_time_lock);
    const int64_t value = s_last_activity_us;
    portEXIT_CRITICAL(&s_time_lock);
    return value;
}

static void set_wake_us(int64_t value)
{
    portENTER_CRITICAL(&s_time_lock);
    s_wake_us = value;
    portEXIT_CRITICAL(&s_time_lock);
}

static void apply_stage(sleep_stage_t stage)
{
    s_stage = stage;

    // STANDBY 与 DIM 用同一个亮度：屏幕一直看得见，区别只在 MCU 有没有睡下。
    // 背光在 light sleep 期间照常输出（见 bsp_display.c），所以这里只需要定亮度。
    bsp_display_backlight(stage == SLEEP_STAGE_ACTIVE ? BACKLIGHT_ACTIVE : BACKLIGHT_LOW);

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

    portENTER_CRITICAL(&s_time_lock);
    const int64_t wake = s_wake_us;
    portEXIT_CRITICAL(&s_time_lock);
    if (wake && now - wake < WAKE_SETTLE_US) return true;

    mark_activity();

    if (s_stage == SLEEP_STAGE_ACTIVE) return false;

    set_wake_us(now);
    apply_stage(SLEEP_STAGE_ACTIVE);
    return true;   // 这一下只用来叫醒，不交给页面
}

// 电量刷新间隔：电量足够时 30s，掉到阈值以下改 60s。
// 读电量失败（-1）按「没掉到低电」处理，用默认间隔。
// 唤醒（STANDBY 定时）与屏上重绘（ACTIVE/DIM）共用这个间隔，只有一个来源。
static uint64_t battery_refresh_period_us(void)
{
    const int soc = bsp_battery_soc();
    if (soc >= 0 && soc <= BATTERY_REFRESH_LOW_SOC) return BATTERY_REFRESH_LOW_US;
    return BATTERY_REFRESH_US;
}

// 距上次重绘超过当前间隔就重绘一次屏上所有电量标签。
// 只在 LVGL 任务里调（power_service / enter_standby 都在该任务），故不需加锁。
static void maybe_refresh_battery(void)
{
    const int64_t now = esp_timer_get_time();
    if (now - s_last_battery_refresh_us < (int64_t)battery_refresh_period_us()) return;
    s_last_battery_refresh_us = now;
    ui_battery_refresh_all();
}

static bsp_wake_source_t standby_wake_source(void)
{
    // s_standby_refresh_us 非 0 时作为显式覆盖（保留给设置页/调试）；
    // 否则按电量自动选间隔，与屏上重绘用同一套。
    const uint64_t period = s_standby_refresh_us ? s_standby_refresh_us
                                                 : battery_refresh_period_us();
    if (period == 0) return BSP_WAKE_SOURCE_BUTTON;
    return bsp_wake_source_button_and_timer(period);
}

static void enter_standby(void)
{
    if (!s_codec_down) {
        sfx_sleep();
        s_codec_down = true;
        vTaskDelay(pdMS_TO_TICKS(CODEC_SETTLE_MS));
    }

    apply_stage(SLEEP_STAGE_STANDBY);
    ESP_LOGI(TAG, "进入 STANDBY：空闲 %lld s",
             (esp_timer_get_time() - last_activity_us()) / 1000000);

    // LVGL 的 5ms tick 和按键扫描是同一个坑：不先停掉，醒来后 esp_timer 会把整个
    // 待机期间欠下的每一拍补跑一遍，LVGL 任务被堵死，界面和按键一起失灵。
    bsp_lvgl_tick_stop();
    const bsp_wake_source_t wake = standby_wake_source();
    int64_t slept_us = 0;
    bsp_wake_cause_t cause = BSP_WAKE_CAUSE_OTHER;
    const esp_err_t err = bsp_power_light_sleep(&wake, &slept_us, &cause);
    bsp_lvgl_tick_resume(slept_us > 0 ? (uint32_t)(slept_us / 1000) : 0);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "本次未能入睡: %s", esp_err_to_name(err));
    } else if (cause == BSP_WAKE_CAUSE_TIMER) {
        ESP_LOGD(TAG, "定期唤醒，睡了 %lld ms", slept_us / 1000);
    }

    if (err == ESP_OK && cause == BSP_WAKE_CAUSE_TIMER) {
        // 定时唤醒就是为了重绘电量读数：刷一次再睡回去。不重置空闲计时，
        // 否则每醒一次就把自己续命成 ACTIVE，永远睡不回去。姿态和亮度维持原样，
        // 让 power_service() 下一次评估时照旧落在 STANDBY。
        maybe_refresh_battery();
        return;
    }

    // 按键叫醒（或本次没能入睡）：按「刚操作过」重新计时，别紧接着又睡下去。
    mark_activity();
    set_wake_us(last_activity_us());

    apply_stage(SLEEP_STAGE_ACTIVE);
}

void power_service(bool ready)
{
    const sleep_input_t in = {
        .idle_us    = esp_timer_get_time() - last_activity_us(),
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

    // ACTIVE/DIM 期间（MCU 没睡）按同一套间隔就地重绘电量，补上旧那个
    // 每标签 30s 定时器的职责；节奏由 maybe_refresh_battery() 自己节流。
    maybe_refresh_battery();
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
