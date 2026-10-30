// components/bsp/include/bsp_power.h
// 低功耗入口：light sleep（默认路径）与 deep sleep（关机路径）。
//
// 分工：本模块只管「让 MCU 睡着、醒来后把借出去的脚还给各自的外设」，
// 什么时候睡、睡多久、背光亮不亮由应用决定（main/power.c）。
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// 本次开机是怎么来的。deep sleep 唤醒是冷启动，应用据此决定是否跳过开机动画。
typedef enum {
    BSP_WAKE_CAUSE_POWER_ON = 0,   // 上电 / 复位 / 烧录
    BSP_WAKE_CAUSE_BUTTON,         // 按键把设备从 deep sleep 拉起
    BSP_WAKE_CAUSE_TIMER,          // 定时唤醒
    BSP_WAKE_CAUSE_OTHER,          // 唤醒了但原因未识别
} bsp_wake_cause_t;

// 读一次复位原因并缓存。应用启动时调一次即可。
bsp_wake_cause_t bsp_power_boot_cause(void);

// ---------------------------------------------------------------------------
// 唤醒源：light sleep 与 deep sleep 共用同一套描述。
//
// 板上唯一的输入是按键 ADC 节点（GPIO0），外部 10k 上拉让它常态为高，
// 按下任意键由分压电阻拉低，所以按钮唤醒条件是低电平。
// ---------------------------------------------------------------------------

typedef struct {
    bool     button;         // 任意按键（GPIO0 低电平）
    uint64_t timer_us;       // 定时唤醒间隔；0 = 不用定时器
} bsp_wake_source_t;

// 只有按键：空闲待机的默认配置。
#define BSP_WAKE_SOURCE_BUTTON  ((bsp_wake_source_t){ .button = true, .timer_us = 0 })

// 按键 + 周期性定时唤醒。用于「长期显示但需要定期刷新」的场景（见 power.c）。
static inline bsp_wake_source_t bsp_wake_source_button_and_timer(uint64_t timer_us)
{
    return (bsp_wake_source_t){ .button = true, .timer_us = timer_us };
}

// 返回 NULL 表示配置非法（既没有按键也没有定时器）。
esp_err_t bsp_power_wake_source_check(const bsp_wake_source_t *source);

// 装上/卸下唤醒源。arm 会把 GPIO0 从 ADC 借走，disarm 负责还给 ADC —— 必须成对，
// 否则按键电压会在下一次采样时整体偏移。
// 两者都幂等；arm 失败时不会留下半个配置。
esp_err_t bsp_power_wake_source_arm(const bsp_wake_source_t *source);
esp_err_t bsp_power_wake_source_disarm(void);

// ---------------------------------------------------------------------------
// light sleep：MCU 停下，RAM/外设/面板 GRAM 全部保留，醒来从调用点继续执行。
//
// ⚠ 只能在持有 CPU 的任务里调用，且调用前所有外设必须空闲：不能有正在进行的
//   SPI/I2S/I2C 传输。esp_light_sleep_start() 只处理 MCU 侧，停蓝牙、关 codec
//   都是调用方的责任（见 main/power.c）。
// ---------------------------------------------------------------------------

// slept_us 可为 NULL；非 NULL 时写回本次实际睡了多久（esp_light_sleep_start()
// 醒来会用 RTC 校准 esp_timer，所以这个差值就是真实睡眠时长）。
// 返回 ESP_OK 表示正常睡醒；ESP_ERR_SLEEP_REJECT 表示被拒（唤醒源冲突）。
esp_err_t bsp_power_light_sleep(const bsp_wake_source_t *source, int64_t *slept_us);

// ---------------------------------------------------------------------------
// deep sleep：MCU 断电，唤醒即冷启动。
//
// 顺序固定：先装唤醒源（失败返回错误，绝不能静默睡死），再逐个下电外设，
// 最后 start。3.3 V 轨在深睡时仍然带电，外设不点名就继续耗电。
// ---------------------------------------------------------------------------

// 装唤醒源并依次下电面板、背光、codec、电量计。失败返回错误，调用方应放弃
// 这次关机继续运行，而不是硬睡。
esp_err_t bsp_power_prepare_deep_sleep(const bsp_wake_source_t *source);

// 不返回。必须先成功调用 bsp_power_prepare_deep_sleep()。
void bsp_power_deep_sleep_start(void);
