// main/sfx.c —— 8-bit 风格音效：方波/三角波/噪声实时合成，16kHz 单声道。
// 原创短旋律，只借鉴「方波 + 短音 + 上行=好事 / 下行=坏事」的老游戏风格。
// 音量整体偏小：改 GAIN_SQ / GAIN_TRI / GAIN_SINE 即可整体调节（10-13 实机反馈太响，三者一起 ×0.35）。
#include "sfx.h"
#include "bsp_audio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <math.h>
#include <stdint.h>
#include <stdbool.h>

static const char *TAG = "sfx";

#define SR         16000
#define GAIN_SQ    0.028f    // 方波/噪声（听感偏响，给小一点）
#define GAIN_TRI   0.053f
#define GAIN_SINE  0.10f     // 木鱼「咕」
#define GAIN_TICK  0.035f    // 首页切换「嗒」
#define GAIN_KEY   0.045f    // 打字音（再乘每个字的轻重）
#define CHUNK      256

enum { W_P12 = 0, W_P25, W_P50, W_TRI, W_NOISE, W_GU, W_TICK, W_KEY };

typedef struct { uint16_t f0, f1, ms; uint8_t wave; } note_t;
#define REST(ms) { 0, 0, ms, W_P25 }
#define END      { 0, 0, 0, 0 }

/* 音高（Hz） */
#define C5 523
#define D5 587
#define E5 659
#define F5 698
#define G5 784
#define A5 880
#define B5 988
#define C6 1047
#define D6 1175
#define E6 1319
#define G6 1568

static const note_t S_GU[]      = { { 520, 520, 110, W_GU }, END };
static const note_t S_NAV[]     = { { 1100, 760, 28, W_TICK }, END };   // 方波快衰减 + 音高下坠：干脆的「嗒」   // 柔和的三角波「嘟」，不尖
static const note_t S_OK[]      = { { G5, G5, 45, W_P25 }, { C6, C6, 80, W_P25 }, END };
static const note_t S_BACK[]    = { { C6, C6, 45, W_P25 }, { G5, G5, 80, W_P25 }, END };
static const note_t S_BOOT[]    = { { C5, C5, 80, W_P25 }, { E5, E5, 80, W_P25 }, { G5, G5, 80, W_P25 },
                                    REST(20), { C6, C6, 70, W_P25 }, { G5, G5, 70, W_P25 }, { C6, C6, 200, W_P50 }, END };
static const note_t S_BLIP[]    = { { 1200, 1200, 25, W_P25 }, END };
static const note_t S_PLINK[]   = { { A5, A5, 120, W_TRI }, END };
static const note_t S_ARP_UP[]  = { { C5, C5, 55, W_P25 }, { E5, E5, 55, W_P25 }, { G5, G5, 55, W_P25 },
                                    { C6, C6, 160, W_P25 }, END };
static const note_t S_FANFARE[] = { { G5, G5, 70, W_P25 }, { G5, G5, 70, W_P25 }, { G5, G5, 70, W_P25 },
                                    { C6, C6, 150, W_P50 }, REST(40), { A5, A5, 70, W_P25 }, { C6, C6, 70, W_P25 },
                                    { E6, E6, 260, W_P50 }, END };
static const note_t S_POP[]     = { { 500, 1500, 70, W_P25 }, END };
static const note_t S_RECORD[]  = { { E5, E5, 60, W_P25 }, { G5, G5, 60, W_P25 }, { C6, C6, 60, W_P25 },
                                    { E6, E6, 60, W_P25 }, { G6, G6, 240, W_P50 }, END };
static const note_t S_BAD[]     = { { 392, 349, 160, W_P50 }, REST(30), { 330, 294, 160, W_P50 }, REST(30),
                                    { 262, 196, 380, W_P50 }, END };
static const note_t S_POWERUP[] = { { 300, 1600, 260, W_P25 }, END };
static const note_t S_CLEAR[]   = { { C5, C5, 90, W_P25 }, { E5, E5, 90, W_P25 }, { G5, G5, 90, W_P25 },
                                    { A5, A5, 90, W_P25 }, { G5, G5, 90, W_P25 }, { C6, C6, 320, W_P50 }, END };
static const note_t S_REVEAL[]  = { { 120, 120, 22, W_NOISE }, REST(28), { 120, 120, 22, W_NOISE }, REST(28),
                                    { 120, 120, 22, W_NOISE }, REST(28), { 120, 120, 22, W_NOISE }, REST(40),
                                    { C5, C5, 50, W_P25 }, { E5, E5, 50, W_P25 }, { G5, G5, 50, W_P25 },
                                    { C6, C6, 240, W_P50 }, END };

static const note_t S_KEY[]     = { { 260, 200, 25, W_KEY }, END };

static const note_t *const SEQ[SFX__N] = {
    [SFX_GU] = S_GU, [SFX_GU_COMBO] = S_GU, [SFX_NAV] = S_NAV, [SFX_OK] = S_OK, [SFX_BACK] = S_BACK,
    [SFX_BOOT] = S_BOOT, [SFX_BLIP] = S_BLIP, [SFX_PLINK] = S_PLINK, [SFX_ARP_UP] = S_ARP_UP,
    [SFX_FANFARE] = S_FANFARE, [SFX_POP] = S_POP, [SFX_RECORD] = S_RECORD, [SFX_BAD] = S_BAD,
    [SFX_POWERUP] = S_POWERUP, [SFX_CLEAR] = S_CLEAR, [SFX_REVEAL] = S_REVEAL, [SFX_KEY] = S_KEY,
};

/* 动态音：木鱼连击、1024 合成 */
#define DYN_GU    100
#define DYN_MERGE 101
#define DYN_KEY   102
static note_t s_dyn[3];

static TaskHandle_t s_task;
static volatile bool s_ready;
static volatile int  s_status = 0;
static volatile int  s_req = -1;       // 待播：SFX id 或 DYN_*
static volatile int  s_req_arg;
static volatile int  s_pending = -1;   // 音频还没就绪时先记下（开机音）
static volatile bool s_sleep_req, s_resume_req;   // 省电：10 分钟无按键停 codec / 按键后重开（只由 codec 所属的 sfx 任务执行）
static bool s_slept;                              // codec 已下电（只在 sfx 任务里读写）
static int16_t s_buf[CHUNK];
static uint32_t s_lfsr = 0xACE1u;
static float s_vol = 1.0f;           // 动态音的音量系数（打字轻重）

/* 播放一个音符序列；中途有新请求就返回 true（被打断） */
static bool play_seq(const note_t *seq)
{
    for (const note_t *n = seq; n->ms; n++) {
        int total = SR * n->ms / 1000;
        float ph = 0.0f, nz = 1.0f, nz_ph = 0.0f;
        float g = (n->wave == W_GU) ? GAIN_SINE : (n->wave == W_TRI) ? GAIN_TRI
                : (n->wave == W_TICK) ? GAIN_TICK : (n->wave == W_KEY) ? GAIN_KEY : GAIN_SQ;
        g *= s_vol;
        float duty = (n->wave == W_P12) ? 0.125f : (n->wave == W_P25) ? 0.25f : 0.5f;
        int att = (n->wave == W_TICK || n->wave == W_KEY) ? 4 : SR * 2 / 1000, rel = SR * 8 / 1000;
        for (int i = 0; i < total; i += CHUNK) {
            int cnt = (total - i < CHUNK) ? total - i : CHUNK;
            for (int k = 0; k < cnt; k++) {
                int idx = i + k;
                float s = 0.0f;
                if (n->f0) {
                    float t = (float)idx / SR;
                    float f = n->f0 + (float)(n->f1 - n->f0) * idx / total;
                    ph += f / SR;
                    if (ph >= 1.0f) ph -= 1.0f;
                    float env = 1.0f;
                    if (idx < att) env = (float)idx / att;
                    if (total - idx < rel) env *= (float)(total - idx) / rel;
                    switch (n->wave) {
                    case W_TRI:   s = 4.0f * fabsf(ph - 0.5f) - 1.0f; env *= expf(-t * 6.0f); break;
                    case W_NOISE:
                        nz_ph += 4000.0f / SR;
                        if (nz_ph >= 1.0f) {
                            nz_ph -= 1.0f;
                            s_lfsr ^= s_lfsr << 7; s_lfsr ^= s_lfsr >> 9; s_lfsr ^= s_lfsr << 8;
                            nz = (s_lfsr & 1) ? 1.0f : -1.0f;
                        }
                        s = nz; env *= expf(-t * 60.0f); break;
                    case W_GU:
                        s = sinf(2.0f * 3.14159265f * ph) * 0.62f + sinf(4.0f * 3.14159265f * ph) * 0.25f
                          + sinf(2.0f * 3.14159265f * 2.76f * f * t) * expf(-t * 60.0f) * 0.25f;   // 木块的高频泛音，更脆
                        env *= expf(-t * 34.0f); break;
                    case W_TICK:  s = (ph < 0.5f) ? 1.0f : -1.0f; env *= expf(-t * 160.0f); break;
                    case W_KEY:   /* 键帽「咔」（白噪声，极快衰减）+ 触底「咚」（低频正弦） */
                        s_lfsr ^= s_lfsr << 7; s_lfsr ^= s_lfsr >> 9; s_lfsr ^= s_lfsr << 8;
                        s = ((s_lfsr & 1) ? 1.0f : -1.0f) * expf(-t * 700.0f) * 0.7f
                          + sinf(2.0f * 3.14159265f * ph) * expf(-t * 90.0f) * 0.9f;
                        break;
                    default:      s = (ph < duty) ? 1.0f : -1.0f; env *= expf(-t * 2.5f); break;
                    }
                    s *= env * g;
                }
                s_buf[k] = (int16_t)(s * 32767.0f);
            }
            bsp_audio_write(s_buf, cnt * sizeof(int16_t));
            if (s_req >= 0) return true;
        }
    }
    return false;
}

static void sfx_task(void *arg)
{
    (void)arg;
    if (bsp_audio_init() != ESP_OK) {
        ESP_LOGW(TAG, "audio init failed, sfx disabled");
        s_status = 1; vTaskDelete(NULL); return;
    }
    if (bsp_audio_set_format(SR, 16, 1) != ESP_OK) {
        ESP_LOGW(TAG, "set_format failed, sfx disabled");
        s_status = 2; vTaskDelete(NULL); return;
    }
    bsp_audio_set_volume(100);
    s_ready = true;
    s_status = 3;
    if (s_pending >= 0 && s_req < 0) { s_req = s_pending; }
    s_pending = -1;
    ESP_LOGI(TAG, "ready");
    for (;;) {
        if (s_req < 0 && !s_sleep_req && !s_resume_req) ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        // 省电：codec 下电 / 重开都在本任务里做,避免和播放写入 codec 抢同一个设备。
        if (s_resume_req) {
            s_resume_req = false;
            if (s_slept) {
                if (bsp_audio_set_format(SR, 16, 1) == ESP_OK) {
                    bsp_audio_set_volume(100);
                    s_slept = false;
                    ESP_LOGI(TAG, "resume");
                } else {
                    ESP_LOGW(TAG, "resume: set_format failed");
                }
            }
        }
        if (s_sleep_req) {
            s_sleep_req = false;
            if (!s_slept) { bsp_audio_stop(); s_slept = true; ESP_LOGI(TAG, "sleep"); }
        }
        int id = s_req, arg2 = s_req_arg;
        s_req = -1;
        if (id < 0) continue;
        if (s_slept) continue;   // codec 已下电：丢弃播放请求,等按键触发 sfx_resume 重开
        const note_t *seq = NULL;
        if (id == DYN_GU) {
            static const float SCALE[8] = { 1.0f, 1.125f, 1.25f, 1.3333f, 1.5f, 1.6667f, 1.875f, 2.0f };
            int st = arg2 < 0 ? 0 : (arg2 > 7 ? 7 : arg2);   // 爬一个八度（520→1040Hz）后保持最高音
            float f = 520.0f * SCALE[st];
            s_dyn[0] = (note_t){ (uint16_t)f, (uint16_t)f, 110, W_GU };
            s_dyn[1] = (note_t)END;
            seq = s_dyn;
        } else if (id == DYN_MERGE) {
            int lv = 0, v = arg2;
            while (v > 2 && lv < 10) { v >>= 1; lv++; }
            float f = 523.0f * powf(2.0f, lv * 2.0f / 12.0f);
            s_dyn[0] = (note_t){ (uint16_t)f, (uint16_t)f, 30, W_P25 };
            s_dyn[1] = (note_t){ (uint16_t)(f * 1.5f), (uint16_t)(f * 1.5f), 50, W_P25 };
            s_dyn[2] = (note_t)END;
            seq = s_dyn;
        } else if (id == DYN_KEY) {
            int hz = arg2 & 0xFFFF, v = (arg2 >> 16) & 0xFF;
            s_dyn[0] = (note_t){ (uint16_t)hz, (uint16_t)(hz * 3 / 4), 25, W_KEY };
            s_dyn[1] = (note_t)END;
            s_vol = v / 255.0f;
            seq = s_dyn;
        } else if (id < SFX__N) {
            seq = SEQ[id];
        }
        if (seq) play_seq(seq);
        s_vol = 1.0f;
    }
}

void sfx_init(void)
{
    if (!s_task) xTaskCreate(sfx_task, "sfx", 4096, NULL, 4, &s_task);
}

static void request(int id, int arg)
{
    if (!s_task) return;
    if (!s_ready) { s_pending = id; s_req_arg = arg; return; }
    s_req_arg = arg;
    s_req = id;
    xTaskNotifyGive(s_task);
}

void sfx_sleep(void)
{
    if (!s_task || !s_ready) return;
    s_sleep_req = true;
    xTaskNotifyGive(s_task);
}

void sfx_resume(void)
{
    if (!s_task || !s_ready) return;
    s_resume_req = true;
    xTaskNotifyGive(s_task);
}

void sfx_play(sfx_id_t id) { request((int)id, 0); }
void sfx_gu(int step)      { request(DYN_GU, step); }
void sfx_key(int vol, int hz) { request(DYN_KEY, ((vol & 0xFF) << 16) | (hz & 0xFFFF)); }
void sfx_merge(int value)  { request(DYN_MERGE, value); }
int  sfx_status(void)      { return s_status; }
