#include "main.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include <string.h>

static uint8_t controller_mac[6];

static void espnow_recv_cb(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    if (len == 14 && memcmp(data, "pyCAR_DISCOVER", 14) == 0) {
        if (!has_espnow_peer) {
            memcpy(controller_mac, info->src_addr, 6);
            esp_now_peer_info_t peerInfo = {};
            memcpy(peerInfo.peer_addr, controller_mac, 6);
            peerInfo.channel = 1;
            peerInfo.ifidx = WIFI_IF_STA;
            esp_now_add_peer(&peerInfo);
            has_espnow_peer = true;
        }
        esp_now_send(controller_mac, (const uint8_t*)"pyCAR_ACK", 9);
    } 
    else if (len == 6 && data[0] == 67) {
        last_remote_cmd_time = esp_timer_get_time() / 1000;
        global_joy.lx = data[1];
        // Invert Y-axes specifically for ESP-NOW pyController so UP drives forward
        global_joy.ly = 255 - data[2];
        global_joy.rx = data[3];
        global_joy.ry = 255 - data[4];
        global_joy.btns = data[5];
    }
}

void comms_espnow_init() {
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    
    esp_now_init();
    esp_now_register_recv_cb(espnow_recv_cb);
}