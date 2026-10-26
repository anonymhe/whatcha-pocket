// main/ble_sync.h —— 观猹蓝牙同步（协议见《AI Passport 蓝牙同步协议 v1》）
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define TIER_USER   0   // 普通用户
#define TIER_MEMBER 1   // 观猹员
#define TIER_GOLD   2   // 金牌观猹员

typedef struct {
    bool     valid;          // 是否同步过
    char     nick[33];       // UTF-8，≤32 字节
    uint32_t tier;
    uint32_t reviews, posts, upvotes;
} profile_t;

void        ble_sync_start(void);          // main 里 storage_init 之后调一次，常驻广播
const char *ble_sync_device_id(void);      // 6 位大写十六进制
void        profile_load(profile_t *p);    // 从 NVS 读
bool        ble_sync_take_updated(void);
void        ble_sync_poke(void);           // 有人按键：蓝牙切回快速广播 2 分钟（在按键回调里调）；停播后第一下按键会自动恢复广播   // 有新同步返回 true 并清标志（LVGL 线程里轮询）
void        ble_sync_stop(void);           // 省电：长时间无按键后停掉广播（连接中不动，不打断同步）
void        ble_sync_resume(void);         // 省电：恢复广播并切回快速窗口（poke 里会自动调用）
