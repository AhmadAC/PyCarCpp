#include "main.h"
#include "esp_log.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/ble_hs_util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char* TAG = "COMMS_BLE";

// Matching Service and Characteristic 128-bit Little Endian UUIDs expected by the Controller App
static const ble_uuid128_t svc_uuid = BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0xf0, 0xab, 0x00, 0x00); // 0000abf0-0000-1000-8000-00805f9b34fb
static const ble_uuid128_t rx_uuid  = BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0xf1, 0xab, 0x00, 0x00); // 0000abf1-0000-1000-8000-00805f9b34fb
static const ble_uuid128_t ip_uuid  = BLE_UUID128_INIT(0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0xf3, 0xab, 0x00, 0x00); // 0000abf3-0000-1000-8000-00805f9b34fb

static void ble_app_on_sync(void);

static int ble_rx_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        char buf[256] = {0};
        int len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > 0 && len < (int)sizeof(buf)) {
            os_mbuf_copydata(ctxt->om, 0, len, buf);
            buf[len] = '\0';
            process_remote_command(buf);
        }
    }
    return 0;
}

static int ble_ip_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    const char *ip = "192.168.4.1";
    os_mbuf_append(ctxt->om, ip, strlen(ip));
    return 0;
}

static const struct ble_gatt_chr_def gatt_chrs[] = {
    {
        .uuid = (const ble_uuid_t *)&rx_uuid,
        .access_cb = ble_rx_cb,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
    },
    {
        .uuid = (const ble_uuid_t *)&ip_uuid,
        .access_cb = ble_ip_cb,
        .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
    },
    { 0 }
};

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = (const ble_uuid_t *)&svc_uuid,
        .characteristics = gatt_chrs
    },
    { 0 }
};

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "BLE Connection %s", event->connect.status == 0 ? "established" : "failed");
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE Disconnected. Restarting advertising...");
            ble_app_on_sync();
            break;
    }
    return 0;
}

static void ble_app_on_sync(void) {
    ble_hs_util_ensure_addr(0);
    uint8_t own_addr_type;
    ble_hs_id_infer_auto(0, &own_addr_type);
    
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)"ESPRobot";
    fields.name_len = 8;
    fields.name_is_complete = 1;
    
    ble_uuid16_t adv_uuid16 = BLE_UUID16_INIT(0xABF0);
    fields.uuids16 = &adv_uuid16;
    fields.num_uuids16 = 1;
    
    ble_gap_adv_set_fields(&fields);
    
    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
    ESP_LOGI(TAG, "BLE Advertising started as 'ESPRobot'");
}

void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void comms_ble_init() {
    static bool initialized = false;
    if (initialized) return;
    initialized = true;

    nimble_port_init();
    ble_svc_gap_device_name_set("ESPRobot");
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    nimble_port_freertos_init(ble_host_task);
}