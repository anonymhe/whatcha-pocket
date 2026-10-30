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
#define RC_STORE    0x04
#define MAX_JSON    512

static char        s_id[7];
static char        s_mac12[13];       // 12 位小写十六进制完整 MAC（链接 s= 用）
static char        s_name[16];        // "Watcha-XXXXXX"
static uint8_t     s_addr_type;
static uint16_t    s_res_handle;
static atomic_bool s_updated;

static volatile bool s_conn = false;      // 已连接
static volatile uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;  // 当前连接句柄，断链用
static volatile bool s_host_ready = false;  // NimBLE 主机已同步、地址就绪
static volatile bool s_adv = false;        // 同步页要求广播中（只在同步页为真）

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
const char *ble_sync_mac12(void) { return s_mac12; }

// 出厂密钥：存在 NVS watcha/pass_k（机身二维码上的 k）。没有写入则返回 false，
// 此时链接只带 s=<mac>，仍能定位到本机（k 对跳转页面不是必需）。
bool ble_sync_pass_key(char *buf, size_t len)
{
    return store_get_str("pass_k", buf, len) && buf[0];
}

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
    // 先清 p_ok 再写数据：这样任何一步失败都留在 p_ok=0（「未同步」），
    // 不会把旧资料与新字段混成一个「有效」资料。全部写成功后才最后置 p_ok=1。
    if (!store_set_u32("p_ok", 0) ||
        !store_set_str("p_nick", n) ||
        !store_set_u32("p_tier", tier) ||
        !store_set_u32("p_rev", rev) ||
        !store_set_u32("p_post", post) ||
        !store_set_u32("p_up", up) ||
        !store_set_u32("p_ok", 1)) {
        ESP_LOGE(TAG, "nvs write failed");
        rc = RC_STORE;
        goto out;
    }
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
        // 包超过 256 字节缓冲或长度不足 1 字节：本次传输无法恢复，
        // 立即回 RC_PACKET 并重置，否则客户端收不到结果只能超时。
        if (ble_hs_mbuf_to_flat(ctxt->om, pkt, sizeof pkt, &n) != 0 || n < 1) {
            notify_result(conn, RC_PACKET);
            s_len = 0; s_expect = 0; s_bad = false;
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
    // 安全权衡：资料写入特征值不要求配对/加密（无 F_WRITE_ENC/F_WRITE_AUTHEN），
    // 因此同步页开启期间（≤90 秒、且需用户亲自进页），范围内任何设备都能覆写
    // 资料。这是为了兼容小程序安装流程的有意取舍；暂无敏感数据，风险可接受。
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
        if (e->connect.status != 0) { if (s_adv) advertise(); }
        else { s_conn = true; s_conn_handle = e->connect.conn_handle; }
        s_len = 0; s_expect = 0; s_bad = false;
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnect");
        s_conn = false;
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        if (s_adv) advertise();   // 仍在同步页：继续等下一次连接；离页后 s_adv=false 不再重播
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (s_adv) advertise();
        break;
    default:
        break;
    }
    return 0;
}

static void advertise(void)
{
    if (!s_host_ready || !s_adv || s_conn) return;

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
    p.itvl_min  = BLE_GAP_ADV_ITVL_MS(1000);   // 同步页只开 ≤90 秒，固定快速广播
    p.itvl_max  = BLE_GAP_ADV_ITVL_MS(1000);
    if (rc == 0) rc = ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) ESP_LOGE(TAG, "advertise rc=%d", rc);
}

// 停广播 + 断当前连接。s_adv 置假后 GAP 的 disconnect/adv_complete 回调不再重新广播，
// 设备彻底不可见。NimBLE 主机仍常驻。分包重组状态（s_len/s_expect/s_bad）是 host 任务专有，
// 不在这里（UI 任务）清；新一次传输会在 host 任务的 CONNECT 与 seq==0 处自行复位。
static void stop_link(void)
{
    s_adv = false;
    ble_gap_adv_stop();
    if (s_conn) ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

// 进同步页：开始广播（只有此时蓝牙才可被发现/连接）。
void ble_sync_adv_begin(void)
{
    if (s_adv) return;
    s_adv = true;
    if (s_host_ready) advertise();
    ESP_LOGI(TAG, "adv begin");
}

// 同步成功后立刻断链：完成提示停留期间链路已无用，立即释放，不等离页。
void ble_sync_release_link(void)
{
    if (!s_adv && !s_conn) return;
    stop_link();
    ESP_LOGI(TAG, "link released");
}

// 离同步页：停广播并断开连接，之后蓝牙不可见。
void ble_sync_adv_end(void)
{
    if (!s_adv && !s_conn) return;
    stop_link();
    ESP_LOGI(TAG, "adv end");
}

// 同步页提示用：已连接 → CONN，主机就绪且在广播 → ADV，否则 INIT（蓝牙启动中）。
ble_sync_state_t ble_sync_state(void)
{
    if (s_conn) return BLE_SYNC_CONN;
    if (s_adv && s_host_ready) return BLE_SYNC_ADV;
    return BLE_SYNC_INIT;
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc == 0) rc = ble_hs_id_infer_auto(0, &s_addr_type);
    if (rc == 0) { s_host_ready = true; if (s_adv) advertise(); }   // 只有同步页开了广播才真正播
    else ESP_LOGE(TAG, "addr rc=%d", rc);
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset %d", reason);
    // 主机复位会丢掉所有连接且不走 DISCONNECT 回调：必须手动清掉陈旧状态，
    // 否则 on_sync 重新同步后 advertise() 会因 s_conn 仍为真而提前返回，永远不再广播。
    s_host_ready = false;
    s_conn = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
}

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

    // 扫码链接 s= 用出厂基础 MAC（12 位小写，与机身二维码一致）
    uint8_t base[6] = { 0 };
    esp_read_mac(base, ESP_MAC_WIFI_STA);
    snprintf(s_mac12, sizeof s_mac12, "%02x%02x%02x%02x%02x%02x",
             base[0], base[1], base[2], base[3], base[4], base[5]);

    if (nimble_port_init() != ESP_OK) { ESP_LOGE(TAG, "nimble init failed"); return; }
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(s_name);
    if (ble_gatts_count_cfg(SVCS) != 0 || ble_gatts_add_svcs(SVCS) != 0) {
        ESP_LOGE(TAG, "gatt register failed");
        return;
    }
    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "host ready as %s (adv only on sync page)", s_name);
}
