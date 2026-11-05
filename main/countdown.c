// main/countdown.c —— 按截止时刻结算的倒计时（纯逻辑，无 ESP-IDF 依赖）
#include "countdown.h"

int64_t countdown_left_us(int64_t deadline_us, int64_t now_us)
{
    if (deadline_us <= 0) return 0;
    const int64_t left = deadline_us - now_us;
    return left > 0 ? left : 0;
}

int countdown_left_secs(int64_t deadline_us, int64_t now_us)
{
    const int64_t left = countdown_left_us(deadline_us, now_us);
    if (left <= 0) return 0;
    return (int)((left + 999999) / 1000000);
}

int64_t countdown_deadline_from(int64_t now_us, int secs)
{
    if (secs <= 0) return now_us;
    return now_us + (int64_t)secs * 1000000;
}

int countdown_pause_secs(int64_t deadline_us, int64_t now_us)
{
    const int left = countdown_left_secs(deadline_us, now_us);
    return left > 0 ? left : 1;
}
