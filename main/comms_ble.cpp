
#include "main.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const ble_uuid128_t svc_uuid = BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E);
static const ble_uuid128_t rx_uuid  = BLE_UUID128_INIT(0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x02, 0x00, 0x40, 0x6E);

static int ble_rx_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    char buf[256] = {0};
    int len = OS_MBUF_PKTLEN(ctxt->om);
    if (len > 0 && len < sizeof(buf)) {
        os_mbuf_copydata(ctxt->om, 0, len, buf);
        process_remote_command(buf);
    }
    return 0;
}

static const struct ble_gatt_chr_def gatt_chrs[] = {
    { .uuid = (const ble_uuid_t *)&rx_uuid, .access_cb = ble_rx_cb, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
    {}
};

static const struct ble_gatt_svc_def gatt_svcs[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = (const ble_uuid_t *)&svc_uuid, .characteristics = gatt_chrs },
    {}
};

static void ble_app_on_sync(void) {
    uint8_t own_addr_type;
    ble_hs_id_infer_auto(0, &own_addr_type);
    
    struct ble_hs_adv_fields fields = {};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)"pyCar";
    fields.name_len = 5;
    fields.name_is_complete = 1;
    
    ble_uuid16_t adv_uuid16 = BLE_UUID16_INIT(0xABF0);
    fields.uuids16 = &adv_uuid16;
    fields.num_uuids16 = 1;
    
    ble_gap_adv_set_fields(&fields);
    
    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL);
}

void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void comms_ble_init() {
    nimble_port_init();
    ble_svc_gap_device_name_set("pyCar");
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);
    ble_hs_cfg.sync_cb = ble_app_on_sync;
    nimble_port_freertos_init(ble_host_task);
}