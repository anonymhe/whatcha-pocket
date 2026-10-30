// main/power.h —— 屏幕与休眠策略的应用侧入口。
//
// 默认阶梯（时间由 power.c 的常量决定）：
//
//   ACTIVE ──30s──▶ DIM ──5min──▶ STANDBY ──(默认关闭)──▶ SLEEP
//      ▲             ▲              │
//      └── 任意按键 ──┴──────────────┘
//
// CAFFEINE 不是阶梯上的一级，而是把阶梯「冻结」在 ACTIVE：
// 番茄钟这类需要常亮且及时刷新的页面申请它，期间既不调暗也不入睡；
// 释放后从当前时刻重新计时。
//
// STANDBY 走 light sleep：RAM 与面板 GRAM 都在，屏幕画面保持不动，按键叫醒后
// 从原处继续，不重建界面。SLEEP 是真正的 deep sleep（冷启动），默认不启用。
//
// OFF 是长按机身电源键的硬关机。该按键不在 MCU 上，软件收不到也进不去，
// 因此本模块不提供对应接口，只在 sleep_policy.h 状态集中保留这一态。
#pragma once

#include <stdbool.h>
#include <stdint.h>

// 页面在运行期间可以叠加申请的资源占用。退出时必须逐项释放。
typedef enum {
    // 需要常亮且及时刷新（番茄钟计时中）。等价于「暂停超时」，不走 sleep 路径。
    POWER_CAFFEINE = 1 << 0,
    // 蓝牙广播或连接中：可以调暗但绝不能入睡，链路不跨 sleep。
    POWER_RADIO_ACTIVE = 1 << 1,
} power_hold_t;

// 初始化计时与复位原因缓存。app_main 里尽早调用一次。
void power_init(void);

// 本次开机是否来自休眠唤醒（light sleep 唤醒来不到这里，只会是 deep sleep）。
bool power_booted_from_sleep(void);

// 申请/释放一项占用，可叠加。成对调用；页面退出时忘记释放会让设备再也睡不下去。
void power_hold(power_hold_t hold, bool on);

// 记录一次按键活动。
// 返回 true 表示这一下只用于把设备从 DIM/STANDBY 叫醒，不应再当页面操作处理。
bool power_key_activity(void);

// 由 LVGL 任务的周期定时器驱动：评估当前态并执行切换。
// ready=false（开机动画未结束）时不做任何降级。进入 STANDBY 会在这里真正调用
// light sleep 并阻塞到被唤醒。
void power_service(bool ready);

// 进入 STANDBY 时使用的定时唤醒间隔。默认 0（只靠按键）。
// 若以后要让长期显示的画面定期刷新（例如整分刷新时钟），把它设成刷新周期即可：
// 每次定时醒来会重绘一帧再睡回去，且**不重置空闲计时**，所以设备不会因为定期刷新
// 而永远回不到 STANDBY。LVGL 的 tick 会在唤醒时补齐睡眠时长，倒计时类画面不会漂移。
void power_set_standby_refresh_us(uint64_t period_us);

// 显式深睡（SLEEP 态）。默认没有代码路径调用它；失败时返回 false 并继续运行，
// 不会静默睡死。
bool power_deep_sleep(void);
