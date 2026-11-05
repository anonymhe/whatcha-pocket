// main/countdown.h —— 按截止时刻结算的倒计时（纯逻辑，可在主机上测试）
//
// 番茄钟原先每次 tick 回调把剩余秒数减 1，等于假设「回调次数 == 流逝秒数」。
// UI 卡顿、定时器迟到、待机补帧都会让两者偏离，25 分钟的专注于是可能跑成 26 分钟。
// 改成记一个截止时刻，每次只问「现在离截止还有多少」：真实流逝的时间一定算得进去。
//
// 时间由调用方传入（微秒），本模块不读时钟，因此主机测试可以喂任意时刻。
#pragma once

#include <stdint.h>

// 剩余时长（微秒）。已过期或未设定（deadline_us<=0）返回 0。
int64_t countdown_left_us(int64_t deadline_us, int64_t now_us);

// 剩余秒数，向上取整：整 25 分钟在开头完整显示 25:00 一秒，而不是立刻变 24:59。
// 归零/已过期返回 0。
int countdown_left_secs(int64_t deadline_us, int64_t now_us);

// 从 now_us 起算 secs 秒的截止时刻。secs<=0 返回 now_us（立即到期）。
int64_t countdown_deadline_from(int64_t now_us, int secs);

// 暂停：把剩余从截止时刻结算成秒，并给出新的「剩余秒」供恢复时重新起算。
// 下限钳到 1 秒，否则「暂停在 0 秒」会让恢复后立刻判定完成。
int countdown_pause_secs(int64_t deadline_us, int64_t now_us);
