// main/ble_sync.c —— NimBLE GATT 外设：设备 ID(Read) / 用户资料(Write, 分包 JSON) / 同步结果(Notify)
// 回调都跑在 NimBLE host 任务里：不碰 LVGL，只写 NVS + 置标志，首页定时器来取。
#include "ble_sync.h"
#include "storage.h"

#include <string.h>
#include <stdio.h>
#include <stdatomic.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "ble_sync";

// a6f1000N-8c8e-41f9-9afd-e97a8af0ab62，小端字节序，第 12 字节是 N
#define WATCHA_UUID(n) BLE_UUID128_INIT(0x62, 0xab, 0xf0, 0x8a, 0x7a, 0xe9, 0xfd, 0x9a, \
                                        0xf9, 0x41, 0x8e, 0x8c, (n), 0x00, 0xf1, 0xa6)
static const ble_uuid128_t UUID_SVC = WATCHA_UUID(0x01);
static const ble_uuid128_t UUID_ID  = WATCHA_UUID(0x02);
static const ble_uuid128_t UUID_PRO = WATCHA_UUID(0x03);
static const ble_uuid128_t UUID_RES = WATCHA_UUID(0x04);

#define RC_OK       0x00
#define RC_BADJSON  0x01
#define RC_VERSION  0x02
#define RC_PACKET   0x03
#define MAX_JSON    512

static char        s_id[7];
static char        s_name[16];        // "Watcha-XXXXXX"
static uint8_t     s_addr_type;
static uint16_t    s_res_handle;
static atomic_bool s_updated;

// 省电：开机和每次按键后 30 秒内快速广播（1s，与亮屏时长一致），之后放慢到 10s；连接中不动广播
#define FAST_WINDOW_US (30LL * 1000000)
static volatile bool s_fast = true;
static volatile bool s_conn = false;
static volatile bool s_started = false;
static volatile bool s_stopped = false;   // 省电：10 分钟无按键后停了广播,按键后由 ble_sync_resume 恢复
static esp_timer_handle_t s_slow_timer;

static uint8_t  s_buf[MAX_JSON + 1];
static uint16_t s_len;
static uint8_t  s_expect;
static bool     s_bad;

static void advertise(void);

// ---------------- 资料存取 ----------------

void profile_load(profile_t *p)
{
    memset(p, 0, sizeof *p);
    p->valid   = store_get_u32("p_ok", 0) != 0;
    p->tier    = store_get_u32("p_tier", TIER_USER);
    p->reviews = store_get_u32("p_rev", 0);
    p->posts   = store_get_u32("p_post", 0);
    p->upvotes = store_get_u32("p_up", 0);
    if (!store_get_str("p_nick", p->nick, sizeof p->nick)) p->nick[0] = 0;
}

bool ble_sync_take_updated(void) { return atomic_exchange(&s_updated, false); }
const char *ble_sync_device_id(void) { return s_id; }

// 截到 ≤max 字节且不劈开 UTF-8 字符
static void utf8_copy(char *dst, const char *src, size_t max)
{
    size_t n = strlen(src);
    if (n > max) {
        n = max;
        while (n > 0 && ((uint8_t)src[n] & 0xC0) == 0x80) n--;
    }
    memcpy(dst, src, n);
    dst[n] = 0;
}

static bool get_count(const cJSON *root, const char *key, uint32_t *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(v) || v->valuedouble < 0) return false;
    *out = v->valuedouble > 4e9 ? 4000000000u : (uint32_t)v->valuedouble;
    return true;
}

static uint8_t apply_json(const uint8_t *buf, size_t len)
{
    cJSON *root = cJSON_ParseWithLength((const char *)buf, len);
    if (!root) return RC_BADJSON;
    uint8_t rc = RC_BADJSON;

    const cJSON *ver  = cJSON_GetObjectItemCaseSensitive(root, "schemaVersion");
    const cJSON *nick = cJSON_GetObjectItemCaseSensitive(root, "nickname");
    const cJSON *idt  = cJSON_GetObjectItemCaseSensitive(root, "identity");
    uint32_t rev, post, up, tier;

    if (!cJSON_IsNumber(ver)) goto out;
    if (ver->valueint != 1) { rc = RC_VERSION; goto out; }
    if (!cJSON_IsString(nick) || !cJSON_IsString(idt)) goto out;
    if      (!strcmp(idt->valuestring, "普通用户"))   tier = TIER_USER;
    else if (!strcmp(idt->valuestring, "观猹员"))     tier = TIER_MEMBER;
    else if (!strcmp(idt->valuestring, "金牌观猹员")) tier = TIER_GOLD;
    else goto out;
    if (!get_count(root, "reviewCount", &rev) || !get_count(root, "postCount", &post) ||
        !get_count(root, "upvoteCount", &up)) goto out;

    char n[33];
    utf8_copy(n, nick->valuestring, 32);
    store_set_str("p_nick", n);
    store_set_u32("p_tier", tier);
    store_set_u32("p_rev", rev);
    store_set_u32("p_post", post);
    store_set_u32("p_up", up);
    store_set_u32("p_ok", 1);
    atomic_store(&s_updated, true);
    ESP_LOGI(TAG, "synced: %s tier=%lu rev=%lu post=%lu up=%lu", n,
             (unsigned long)tier, (unsigned long)rev, (unsigned long)post, (unsigned long)up);
    rc = RC_OK;
out:
    cJSON_Delete(root);
    return rc;
}

static void notify_result(uint16_t conn, uint8_t code)
{
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&code, 1);
    if (om) ble_gatts_notify_custom(conn, s_res_handle, om);
    ESP_LOGI(TAG, "result 0x%02x", code);
}

// ---------------- GATT ----------------

static int on_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr;
    int which = (int)(intptr_t)arg;

    if (which == 2 && ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return os_mbuf_append(ctxt->om, s_id, 6) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if (which == 3 && ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t pkt[256];
        uint16_t n = 0;
        if (ble_hs_mbuf_to_flat(ctxt->om, pkt, sizeof pkt, &n) != 0 || n < 1) {
            s_bad = true;
            return 0;
        }
        uint8_t seq  = pkt[0] & 0x7F;
        bool    last = pkt[0] & 0x80;
        if (seq == 0) { s_len = 0; s_expect = 0; s_bad = false; }
        if (seq != s_expect || s_len + (n - 1) > MAX_JSON) s_bad = true;
        if (!s_bad) { memcpy(s_buf + s_len, pkt + 1, n - 1); s_len += n - 1; }
        s_expect = seq + 1;
        if (last) {
            notify_result(conn, s_bad ? RC_PACKET : apply_json(s_buf, s_len));
            s_len = 0; s_expect = 0; s_bad = false;
        }
        return 0;
    }
    return 0;   // 结果特征值只走 Notify
}

static const struct ble_gatt_svc_def SVCS[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SVC.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = &UUID_ID.u,  .access_cb = on_access, .arg = (void *)2, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = &UUID_PRO.u, .access_cb = on_access, .arg = (void *)3,
              .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = &UUID_RES.u, .access_cb = on_access, .arg = (void *)4,
              .flags = BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_res_handle },
            { 0 },
        },
    },
    { 0 },
};

// ---------------- GAP ----------------

static int gap_event(struct ble_gap_event *e, void *arg)
{
    (void)arg;
    switch (e->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "connect %s", e->connect.status == 0 ? "ok" : "fail");
        if (e->connect.status != 0) advertise(); else s_conn = true;
        s_len = 0; s_expect = 0; s_bad = false;
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect");
        s_conn = false;
        advertise();
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;
    default:
        break;
    }
    return 0;
}

static void advertise(void)
{
    struct ble_hs_adv_fields f = { 0 };
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.name = (const uint8_t *)s_name;
    f.name_len = strlen(s_name);
    f.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&f);

    struct ble_hs_adv_fields r = { 0 };   // 扫描响应里带服务 UUID，小程序可按服务过滤
    r.uuids128 = &UUID_SVC;
    r.num_uuids128 = 1;
    r.uuids128_is_complete = 1;
    if (rc == 0) rc = ble_gap_adv_rsp_set_fields(&r);

    struct ble_gap_adv_params p = { 0 };
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min  = s_fast ? BLE_GAP_ADV_ITVL_MS(1000) : BLE_GAP_ADV_ITVL_MS(10000);
    p.itvl_max  = s_fast ? BLE_GAP_ADV_ITVL_MS(1000) : BLE_GAP_ADV_ITVL_MS(10000);
    if (rc == 0) rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGE(TAG, "advertise rc=%d", rc);
}

// 下面三个都只在 esp_timer 任务里被调（定时器回调、按键回调），彼此不会并发
static void restart_adv(void)
{
    if (!s_started || s_conn) return;
    ble_gap_adv_stop();
    advertise();
}

static void slow_cb(void *arg)
{
    (void)arg;
    if (!s_fast) return;
    s_fast = false;
    restart_adv();
    ESP_LOGI(TAG, "adv slow");
}

// 省电：长时间无按键后停掉广播(连接中不动,免得打断正在进行的同步)。
// 与 restart_adv/slow_cb/poke 一样只在 esp_timer/LVGL 任务里被调,彼此不并发。
void ble_sync_stop(void)
{
    if (!s_started || s_conn || s_stopped) return;   // 连接中保留广播,不打断同步
    if (s_slow_timer) esp_timer_stop(s_slow_timer);
    ble_gap_adv_stop();
    s_stopped = true;
    ESP_LOGI(TAG, "adv stop");
}

// 省电：按键后恢复广播并切回快速窗口。
void ble_sync_resume(void)
{
    if (!s_stopped) return;
    s_stopped = false;
    s_fast = true;
    if (s_slow_timer) {
        esp_timer_stop(s_slow_timer);
        esp_timer_start_once(s_slow_timer, FAST_WINDOW_US);
    }
    restart_adv();
    ESP_LOGI(TAG, "adv resume");
}

void ble_sync_poke(void)
{
    if (s_stopped) { ble_sync_resume(); return; }   // 停播后第一下按键先把广播救回来(已切回快速窗口)
    if (s_slow_timer) {
        esp_timer_stop(s_slow_timer);
        esp_timer_start_once(s_slow_timer, FAST_WINDOW_US);
    }
    if (!s_fast) { s_fast = true; restart_adv(); ESP_LOGI(TAG, "adv fast"); }
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc == 0) { advertise(); s_started = true; }
    else ESP_LOGE(TAG, "addr rc=%d", rc);
}

static void on_reset(int reason) { ESP_LOGW(TAG, "host reset %d", reason); }

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_sync_start(void)
{
    uint8_t mac[6] = { 0 };
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(s_id, sizeof s_id, "%02X%02X%02X", mac[3], mac[4], mac[5]);
    snprintf(s_name, sizeof s_name, "Watcha-%s", s_id);

    if (nimble_port_init() != ESP_OK) { ESP_LOGE(TAG, "nimble init failed"); return; }
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(s_name);
    if (ble_gatts_count_cfg(SVCS) != 0 || ble_gatts_add_svcs(SVCS) != 0) {
        ESP_LOGE(TAG, "gatt register failed");
        return;
    }
    const esp_timer_create_args_t ta = { .callback = slow_cb, .name = "adv_slow" };
    if (esp_timer_create(&ta, &s_slow_timer) == ESP_OK) esp_timer_start_once(s_slow_timer, FAST_WINDOW_US);
    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "start as %s", s_name);
}
