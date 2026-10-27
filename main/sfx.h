// main/sfx.h —— 8-bit 风格短音效（方波/三角波实时合成）。sfx_* 只发信号不阻塞，播放在独立任务里。
#pragma once
typedef enum {
    SFX_GU = 0,      // 木鱼「咕」
    SFX_NAV,         // 上下选择：短「嘀」
    SFX_OK,          // 确认进入：两音上跳
    SFX_BACK,        // 返回：两音下落
    SFX_BOOT,        // 开机小旋律
    SFX_BLIP,        // 轻点：答题、暂停
    SFX_PLINK,       // 中签：单音
    SFX_ARP_UP,      // 上签：上行琶音
    SFX_FANFARE,     // 上上签：小号角
    SFX_POP,         // 「啵嘤」：瓜出现、换猹
    SFX_RECORD,      // 破纪录
    SFX_BAD,         // 失败：抢跑、1024 结束
    SFX_POWERUP,     // 番茄开始专注：上扫「变身」
    SFX_CLEAR,       // 过关：番茄完成、1024 通关
    SFX_REVEAL,      // 什么猹揭晓：鼓点 + 和弦
    SFX_KEY,         // 开机打字：很轻的「嗒」
    SFX__N
} sfx_id_t;

void sfx_init(void);
void sfx_sleep(void);        // 省电：下电音频 codec（可重启）。只发信号，重开由 sfx 任务做，非阻塞
void sfx_resume(void);       // 省电：按键后重新打开 codec，恢复发声。只发信号，非阻塞
void sfx_play(sfx_id_t id);
void sfx_gu(int step);       // 木鱼：step=0 基础音，连击时 1..7 沿大调音阶爬一个八度，之后保持最高音
void sfx_key(int vol, int hz);   // 开机打字：vol 0..255 轻重，hz「咚」的音高
void sfx_merge(int value);   // 1024：合出的数越大音越高
int  sfx_status(void);       // 0=pending 1=audio_init_fail 2=fmt_fail 3=ready
