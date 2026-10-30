// main/ble_sync.h —— 观猹蓝牙同步（协议见《AI Passport 蓝牙同步协议 v1》）
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TIER_USER   0   // 普通用户
#define TIER_MEMBER 1   // 观猹员
#define TIER_GOLD   2   // 金牌观猹员

typedef struct {
    bool     valid;          // 是否同步过
    char     nick[33];       // UTF-8，≤32 字节
    uint32_t tier;
    uint32_t reviews, posts, upvotes;
} profile_t;

void        ble_sync_start(void);          // main 里 storage_init 之后调一次：初始化 NimBLE 主机但不广播
const char *ble_sync_device_id(void);      // 6 位大写十六进制（蓝牙 MAC 后 3 字节，UI 显示用）
const char *ble_sync_mac12(void);          // 12 位小写十六进制完整 MAC（扫码链接 s= 参数用）
bool        ble_sync_pass_key(char *buf, size_t len);  // 读出厂密钥（NVS watcha/pass_k），没有返回 false
void        profile_load(profile_t *p);    // 从 NVS 读
bool        ble_sync_take_updated(void);   // 有新同步返回 true 并清标志（LVGL 线程里轮询）

// 广播只在同步页开启：进页调 begin 开始广播，离页调 end 停止广播并断开连接。
void        ble_sync_adv_begin(void);
void        ble_sync_adv_end(void);

// 同步成功后立刻断链：停止广播、断开当前连接，使设备不再可见；NimBLE 主机仍常驻。
// 与 adv_end 行为一致，但语义上用于「同步已完成、提示停留期间无需再占用链路」，
// 可在 adv_end（离页）之前先行调用，避免完成提示期间链路空占 2 秒。
void        ble_sync_release_link(void);

// 蓝牙当前状态（同步页用于提示用户）：
typedef enum {
    BLE_SYNC_INIT = 0,   // 主机未就绪（蓝牙还在启动）
    BLE_SYNC_ADV,        // 正在广播，等待扫码连接
    BLE_SYNC_CONN,       // 已连接，正在同步
} ble_sync_state_t;
ble_sync_state_t ble_sync_state(void);
