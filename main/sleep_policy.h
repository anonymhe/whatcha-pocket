// main/sleep_policy.h —— 空闲超时的降级判据（纯逻辑，不依赖 ESP-IDF / LVGL）。
//
// 只回答一个问题：给定「多久没操作」和几个占用条件，现在应该停在哪一态。
// 真正调背光、停 codec、进 light/deep sleep 的是 power.c。
//
// 完整的五态阶梯（越往下越省，越难回到 ACTIVE）：
//
//   ACTIVE   正在使用
//     │ 30s 无操作
//   DIM      背光压暗，其余照常；任意按键立刻回 ACTIVE
//     │ 5min 无操作
//   STANDBY  MCU 进 light sleep，背光保持压暗可见；任意按键叫醒并回 ACTIVE
//     │ 更久无操作（默认关闭，见 power.c 的 POWER_ENABLE_DEEP_SLEEP）
//   SLEEP    deep sleep，外设逐个下电；唤醒即冷启动
//   OFF      用户长按机身电源键硬关机。该按键不在 MCU 上，软件既收不到也无法
//            主动进入，列在这里只为让状态集完整、文档有落点。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SLEEP_STAGE_ACTIVE = 0,
    SLEEP_STAGE_DIM,
    SLEEP_STAGE_STANDBY,
    SLEEP_STAGE_SLEEP,
    SLEEP_STAGE_OFF,       // 软件不可达，仅作状态枚举的完整定义
} sleep_stage_t;

typedef struct {
    int64_t dim_after_us;      // 空闲多久进入 DIM
    int64_t standby_after_us;  // 空闲多久进入 STANDBY
    int64_t sleep_after_us;    // 空闲多久进入 SLEEP
} sleep_policy_t;

// 一次判据判断所需的全部输入。拆成结构体是为了让调用点读起来像一句话，
// 也避免再多一个布尔参数后搞不清位置。
typedef struct {
    int64_t idle_us;      // 距上次按键/操作的时长
    bool    ready;        // 开机动画已结束；未就绪时任何降级都不做
    bool    caffeine;     // 页面要求常亮且及时刷新（番茄钟、计时类）
    bool    radio_busy;   // 蓝牙广播或连接中
} sleep_input_t;

// 阈值非法（负数、或后一段早于前一段）时按前一段的值修正，
// 绝不产生「永不降级」的配置。
void sleep_policy_init(sleep_policy_t *policy, int64_t dim_after_us,
                       int64_t standby_after_us, int64_t sleep_after_us);

// 判据顺序即优先级：未就绪 > caffeine > radio > 空闲时长。
//   ready=false     → ACTIVE（开机动画期间不暗屏）
//   caffeine=true   → ACTIVE（计时类页面既不能暗也不能睡）
//   radio_busy=true → 最多 DIM（蓝牙链路不跨 sleep，只能压暗）
sleep_stage_t sleep_policy_stage(const sleep_policy_t *policy, const sleep_input_t *in);
