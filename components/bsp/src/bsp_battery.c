// components/bsp/src/bsp_battery.c
// CW2017 电量计驱动。
#include "bsp_battery.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <stdint.h>

static const char *TAG = "bsp_batt";

#define CW_REG_VERSION   0x00   // 版本号,上电应答即代表芯片在位
#define CW_REG_VCELL_H   0x02   // 14bit 电压,V(uV) = raw * 312.5
#define CW_REG_SOC_H     0x04   // 高字节 = 整数百分比;低字节(0x05)= 1/256 %
#define CW_REG_TEMP      0x06   // 电芯温度,厂商约定的编码,仅用于诊断
#define CW_REG_CONFIG    0x08   // 0xF0=睡眠 / 0x30=复位态 / 0x00=正常
#define CW_REG_SOC_ALERT 0x0B   // bit7=profile UPDATE_FLAG;bit6:0=SOC 告警阈值
#define CW_REG_PROFILE   0x10   // 80 字节电池 profile 起始地址

#define CW_CONFIG_ACTIVE  0x00
#define CW_CONFIG_RESTART 0x30
#define CW_CONFIG_SLEEP   0xF0
#define CW_UPDATE_FLAG    0x80
#define CW_PROFILE_SIZE   80

// 两份厂家 profile 择一编译，由 CONFIG_WATCHA_BATTERY_PROFILE_WANLI_500MAH 决定
// （见 sdkconfig.defaults）。它们不是同一块电芯的两种标定，而是两块不同电芯：
// 80 字节里有 51 字节不同，SOC 曲线因此完全不同。
//
// 优特利 520 mAh：WatchaAI/WATCHA-POCKET 开源固件一直携带的那一份（本 fork 的
// v0.1.3 及更早版本也是它）。实测该机在这份 profile 下能正常报到 100%。
// 原始字节: assets/battery/utl-520mah-upstream-42f16ab9.txt
static const uint8_t s_profile_utl_520mah[] = {
    0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAD, 0xC7, 0xC8, 0xCA, 0xBD, 0xB1, 0xC1, 0x94,
    0x88, 0xD1, 0xBD, 0x97, 0x88, 0x66, 0x56, 0x4A,
    0x3F, 0x33, 0x26, 0x5C, 0x37, 0xD1, 0x27, 0xD8,
    0xCC, 0xB7, 0xCF, 0xB3, 0xB2, 0xAE, 0xA6, 0x9E,
    0x99, 0x97, 0x9B, 0x86, 0x47, 0x1E, 0x17, 0x26,
    0x49, 0x96, 0xD9, 0xE1, 0xDD, 0xDC, 0xD4, 0x59,
    0x00, 0x00, 0x90, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5C,
};

// 万锂新能源 500 mAh，profile2 / XIL40X / 2026-10-20，来自上游 ai-passport。
// 原始字节: assets/battery/wanli-500mah-profile2-xil40x-20261020.txt
static const uint8_t s_profile_wanli_500mah[] = {
    0x5A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xAF, 0xCA, 0xCA, 0xC0, 0xB4, 0xA8, 0xCB, 0xAA,
    0x9F, 0xCF, 0xB5, 0x8A, 0x70, 0x56, 0x4B, 0x40,
    0x34, 0x2B, 0x22, 0x55, 0x41, 0xD2, 0x30, 0xD7,
    0xC9, 0xBE, 0xAE, 0xC8, 0xAE, 0x97, 0x9D, 0x98,
    0x94, 0x9F, 0xB6, 0x89, 0x73, 0x65, 0x57, 0x49,
    0x3D, 0x50, 0x74, 0x87, 0xA3, 0x9B, 0x5E, 0x38,
    0x00, 0x00, 0xAB, 0x02, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB7,
};

#if CONFIG_WATCHA_BATTERY_PROFILE_WANLI_500MAH
static const uint8_t *const s_battery_profile = s_profile_wanli_500mah;
static const char *const s_battery_profile_name = "万锂新能源 500mAh profile2 XIL40X 20261020";
#else
static const uint8_t *const s_battery_profile = s_profile_utl_520mah;
static const char *const s_battery_profile_name = "优特利 520mAh";
#endif

// 两份长度都要锁死：少写一个初始值会让 u8 数组静默变短，而写入时用的是
// CW_PROFILE_SIZE，读到的就是越界数据。
_Static_assert(sizeof(s_profile_utl_520mah) == CW_PROFILE_SIZE,
               "utl 520mAh CW2017 profile must contain exactly 80 bytes");
_Static_assert(sizeof(s_profile_wanli_500mah) == CW_PROFILE_SIZE,
               "wanli 500mAh CW2017 profile must contain exactly 80 bytes");

// 电量计里实际存的是哪一份 profile，只有把它读出来比对才知道。日志里报指纹而非
// 80 字节，一行就能分清「还是旧电芯的字节」和「已经是本固件这一份」。
// FNV-1a：够短、够稳定，只用于日志核对，不参与任何判定。
static uint32_t profile_fingerprint(const uint8_t *bytes) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < CW_PROFILE_SIZE; i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static i2c_master_dev_handle_t s_dev;

static esp_err_t cw_read(uint8_t reg, uint8_t *buf, size_t n) {
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 100);
}

static esp_err_t cw_write(uint8_t reg, uint8_t val) {
    if (!s_dev) return ESP_ERR_INVALID_STATE;
    uint8_t b[2] = { reg, val };
    return i2c_master_transmit(s_dev, b, 2, 100);
}

// CONFIG 的低 4 bit 为保留位。复位、睡眠和激活均按芯片规定的时序切换。
static int cw_enter_sleep(void) {
    if (cw_write(CW_REG_CONFIG, CW_CONFIG_RESTART) != 0) return -1;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (cw_write(CW_REG_CONFIG, CW_CONFIG_SLEEP) != 0) return -1;
    vTaskDelay(pdMS_TO_TICKS(10));
    return 0;
}

static int cw_enter_active(void) {
    if (cw_write(CW_REG_CONFIG, CW_CONFIG_RESTART) != 0) return -1;
    vTaskDelay(pdMS_TO_TICKS(20));
    if (cw_write(CW_REG_CONFIG, CW_CONFIG_ACTIVE) != 0) return -1;
    vTaskDelay(pdMS_TO_TICKS(10));
    return 0;
}

// 同时检查 UPDATE_FLAG 和 80 字节内容，避免只凭标志误用旧电芯参数。
static int cw_profile_matches(bool *matches) {
    uint8_t val = 0;
    *matches = false;

    if (cw_read(CW_REG_SOC_ALERT, &val, 1) != 0) return -1;
    if ((val & CW_UPDATE_FLAG) == 0) return 0;

    for (size_t i = 0; i < CW_PROFILE_SIZE; i++) {
        if (cw_read((uint8_t)(CW_REG_PROFILE + i), &val, 1) != 0) return -1;
        if (val != s_battery_profile[i]) return 0;
    }

    *matches = true;
    return 0;
}

// Profile 必须在睡眠态逐字节写入，读回校验后置 UPDATE_FLAG，再重启计算。
static int cw_update_profile(void) {
    uint8_t val = 0;
    if (cw_enter_sleep() != 0) return -1;

    for (size_t i = 0; i < CW_PROFILE_SIZE; i++) {
        if (cw_write((uint8_t)(CW_REG_PROFILE + i), s_battery_profile[i]) != 0) {
            ESP_LOGE(TAG, "写 profile 失败:index=%u", (unsigned)i);
            return -1;
        }
    }

    for (size_t i = 0; i < CW_PROFILE_SIZE; i++) {
        if (cw_read((uint8_t)(CW_REG_PROFILE + i), &val, 1) != 0) return -1;
        if (val != s_battery_profile[i]) {
            ESP_LOGE(TAG, "profile 校验失败:index=%u expected=0x%02X actual=0x%02X",
                     (unsigned)i, s_battery_profile[i], val);
            return -1;
        }
    }

    if (cw_read(CW_REG_SOC_ALERT, &val, 1) != 0) return -1;
    if (cw_write(CW_REG_SOC_ALERT, val | CW_UPDATE_FLAG) != 0) return -1;
    return cw_enter_active();
}

// 首次计算期间 SOC 可能暂时大于 100；最多等待 5 秒再判定初始化失败。
static int cw_wait_soc_ready(void) {
    for (int retry = 0; retry < 50; retry++) {
        uint8_t soc = 0;
        vTaskDelay(pdMS_TO_TICKS(100));
        if (cw_read(CW_REG_SOC_H, &soc, 1) == 0 && soc <= 100) return 0;
    }
    return -1;
}

// SOC 到不了 100 时，仅凭屏上的百分比无法判断是谁的问题。把电量计里真正存着的
// profile 指纹、SOC 的两个字节、电压、温度、CONFIG 和 SOC_ALERT 一起打出来：
//   - profile 指纹与本固件不一致 → 计里不是这份字节，读数与电芯无关；
//   - 指纹对、SOC 低字节（1/256%）也贴在 0x00 附近 → 芯片算到这儿就到顶了，
//     说明 profile 里的满充电压/容量与这块电芯对不上；
//   - SOC 低字节还在往上涨、电压已到 4.1V 以上 → 只是还没充到 profile 认的
//     满充点，属正常充电末端，等充满再看。
//
// 电压按原始 14 bit 打印（LSB=312.5uV）而不是整数 mV：充电末端每几分钟只涨几
// 个 LSB，换算成 mV 后取整会被整个抹平，看上去就像「纹丝不动」。
// CW2017 没有电流寄存器，也不接采样电阻——电流只能靠硬件（采样电阻/充电器状态
// 脚）测，主机侧无从读取。所以「还在充 / 已截止」只能由 dV/dt 和温度来推断。
static void cw_log_diagnostics(void) {
    uint8_t stored[CW_PROFILE_SIZE] = { 0 };
    bool readable = true;
    for (size_t i = 0; i < CW_PROFILE_SIZE; i++) {
        if (cw_read((uint8_t)(CW_REG_PROFILE + i), &stored[i], 1) != 0) {
            readable = false;
            break;
        }
    }

    uint8_t soc[2] = { 0 }, cfg = 0, alert = 0, temp = 0, vcell[2] = { 0 };
    const bool soc_ok = cw_read(CW_REG_SOC_H, soc, 2) == 0;
    const bool cfg_ok = cw_read(CW_REG_CONFIG, &cfg, 1) == 0;
    const bool alert_ok = cw_read(CW_REG_SOC_ALERT, &alert, 1) == 0;
    const bool temp_ok = cw_read(CW_REG_TEMP, &temp, 1) == 0;
    const bool vcell_ok = cw_read(CW_REG_VCELL_H, vcell, 2) == 0;

    const uint32_t stored_hash = profile_fingerprint(stored);
    const uint32_t expected_hash = profile_fingerprint(s_battery_profile);
    const char *profile_state = !readable ? "读取失败"
                                           : (stored_hash == expected_hash ? "匹配" : "不匹配");
    const unsigned vcell_raw = (unsigned)(((vcell[0] << 8) | vcell[1]) & 0x3FFF);
    const int t10 = (int)temp * 5 - 400;   // -40 + Value/2，单位 0.1 °C
    const int t_abs = t10 < 0 ? -t10 : t10;

    ESP_LOGI(TAG, "电量计诊断: profile=%s(0x%08X) 期望=0x%08X "
                  "SOC=%d%s%d/256 VCELL=0x%04X(%u uV) TEMP=0x%02X(%s%d.%d C) "
                  "CONFIG=0x%02X SOC_ALERT=0x%02X",
             profile_state, (unsigned)(readable ? stored_hash : 0),
             (unsigned)expected_hash,
             soc_ok ? soc[0] : -1, soc_ok ? "." : "?", soc_ok ? soc[1] : 0,
             vcell_ok ? vcell_raw : 0, vcell_ok ? vcell_raw * 3125 / 10 : 0,
             (unsigned)(temp_ok ? temp : 0xFF),
             t10 < 0 ? "-" : "", t_abs / 10, t_abs % 10,
             (unsigned)(cfg_ok ? cfg : 0xFF), (unsigned)(alert_ok ? alert : 0xFF));
}

esp_err_t bsp_battery_init(void) {
    if (s_dev) return ESP_OK;

    esp_err_t e = bsp_i2c_init();
    if (e != ESP_OK) return e;

    i2c_device_config_t dc = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = BSP_I2C_CW2017_ADDR,
        .scl_speed_hz    = 100000,
    };
    e = i2c_master_bus_add_device(bsp_i2c_bus(), &dc, &s_dev);
    if (e != ESP_OK) { ESP_LOGE(TAG, "添加 I2C 设备失败: %s", esp_err_to_name(e)); return e; }

    uint8_t ver = 0;
    if (cw_read(CW_REG_VERSION, &ver, 1) != 0) {
        ESP_LOGW(TAG, "CW2017 未应答 —— 用 bsp_i2c_scan() 确认 0x%02X 是否在线;"
                      "无电量计的板子可忽略本项", BSP_I2C_CW2017_ADDR);
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return ESP_ERR_NOT_FOUND;
    }
    ESP_LOGI(TAG, "检测到 CW2017 VERSION=0x%02X", ver);

    bool profile_matches = false;
    if (cw_profile_matches(&profile_matches) != 0) {
        ESP_LOGE(TAG, "读取电池 profile 失败");
        e = ESP_FAIL;
        goto fail;
    }

    if (!profile_matches) {
        ESP_LOGI(TAG, "写入%s profile", s_battery_profile_name);
        if (cw_update_profile() != 0) {
            ESP_LOGE(TAG, "自定义 profile 写入失败");
            e = ESP_FAIL;
            goto fail;
        }
    } else {
        uint8_t config = 0;
        if (cw_read(CW_REG_CONFIG, &config, 1) != 0) {
            e = ESP_FAIL;
            goto fail;
        }
        if (config != CW_CONFIG_ACTIVE && cw_enter_active() != 0) {
            e = ESP_FAIL;
            goto fail;
        }
        ESP_LOGI(TAG, "%s profile 已匹配", s_battery_profile_name);
    }

    if (cw_wait_soc_ready() != 0) {
        ESP_LOGE(TAG, "等待首次 SOC 计算超时");
        e = ESP_ERR_TIMEOUT;
        goto fail;
    }

    ESP_LOGI(TAG, "CW2017 ready: SOC=%d%%, voltage=%d mV",
             bsp_battery_soc(), bsp_battery_mv());
    cw_log_diagnostics();
    return ESP_OK;

fail:
    i2c_master_bus_rm_device(s_dev);
    s_dev = NULL;
    return e;
}

esp_err_t bsp_battery_sleep(void) {
    if (!s_dev) return ESP_OK;

    for (unsigned attempt = 1; attempt <= 2; attempt++) {
        uint8_t actual = 0;
        esp_err_t write_error = cw_write(CW_REG_CONFIG, CW_CONFIG_SLEEP);
        vTaskDelay(pdMS_TO_TICKS(5));
        esp_err_t read_error = cw_read(CW_REG_CONFIG, &actual, 1);
        if (write_error == ESP_OK && read_error == ESP_OK &&
            actual == CW_CONFIG_SLEEP) {
            ESP_LOGI(TAG, "CW2017 已进入休眠并通过寄存器校验");
            return ESP_OK;
        }

        ESP_LOGW(TAG, "CW2017 休眠失败 attempt=%u write=%s(0x%x) "
                      "read=%s(0x%x) actual=0x%02X",
                 attempt, esp_err_to_name(write_error), (unsigned)write_error,
                 esp_err_to_name(read_error), (unsigned)read_error, actual);
        if (attempt < 2) vTaskDelay(pdMS_TO_TICKS(5));
    }

    ESP_LOGE(TAG, "CW2017 连续两次未能进入休眠");
    return ESP_FAIL;
}

int bsp_battery_soc(void) {
    uint8_t b[2] = { 0 };
    if (cw_read(CW_REG_SOC_H, b, 2) != 0) return -1;
    int soc = b[0];                       // 高字节即整数百分比
    if (soc > 100) return -1;             // 芯片未就绪时可能读到 0xFF
    return soc;
}

int bsp_battery_mv(void) {
    uint8_t b[2] = { 0 };
    if (cw_read(CW_REG_VCELL_H, b, 2) != 0) return -1;
    uint32_t raw = ((uint32_t)b[0] << 8 | b[1]) & 0x3FFF;   // 14bit
    return (int)((raw * 3125) / 10000);                     // raw * 312.5uV → mV
}

// 原始值不换算：3150 uV 级的 LSB 换算成 mV 后取整就没了分辨率。
int bsp_battery_mv_raw(void) {
    uint8_t b[2] = { 0 };
    if (cw_read(CW_REG_VCELL_H, b, 2) != 0) return -1;
    return (int)(((uint32_t)b[0] << 8 | b[1]) & 0x3FFF);
}

// 高字节是整数百分比、低字节是 1/256%，合起来是芯片自己给出的完整读数。
// 不在这里判 >100：调用方要看的正是「是不是真的到顶了」。
int bsp_battery_soc_raw(void) {
    uint8_t b[2] = { 0 };
    if (cw_read(CW_REG_SOC_H, b, 2) != 0) return -1;
    return (int)(((unsigned)b[0] << 8) | b[1]);
}

// 数据手册 TEMP(0x06)：无符号 8 bit，LSB = 0.5 °C，量程 -40..87.5 °C，
// 换算 TEMP(°C) = -40 + Value/2。内核 cw2015 驱动写作 value*10/2-400（单位 0.1 °C）。
// 返回 0.1 °C 为单位，避免在主日志里做浮点。
int bsp_battery_temp_tenths(void) {
    uint8_t t = 0;
    if (cw_read(CW_REG_TEMP, &t, 1) != 0) return -1;
    return (int)t * 10 / 2 - 400;
}
