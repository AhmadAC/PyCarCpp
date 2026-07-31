
#include "main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char* TAG = "MAIN";

volatile JoyMsg global_joy = {67, 128, 128, 128, 128, 8};
volatile bool has_espnow_peer = false;
volatile bool is_ap_mode_active = false;

extern float current_distance;
extern float line_follower_gap;
extern bool headlight_state;
extern bool line_follower_state;
extern bool s_forward, s_backward, s_left, s_right;

void car_control_task(void *pv) {
    while (1) {
        car_hardware_loop();
        
        display_update(
            is_ap_mode_active,
            !is_ap_mode_active, // Show BLE indicator if not AP (Fallback)
            line_follower_state,
            headlight_state,
            current_distance,
            line_follower_gap,
            s_forward, s_backward, s_left, s_right
        );
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

extern "C" void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    display_init();
    car_hardware_init();
    comms_espnow_init();
    
    ESP_LOGI(TAG, "Waiting 3 seconds for ESP-NOW Peer...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    if (!has_espnow_peer) {
        ESP_LOGW(TAG, "ESP-NOW Timeout! Booting BLE and Wi-Fi Captive Portal.");
        is_ap_mode_active = true;
        comms_ble_init();
        comms_wifi_ap_init();
    } else {
        ESP_LOGI(TAG, "ESP-NOW Connected! Disabling heavy networks to save power.");
    }

    xTaskCreatePinnedToCore(car_control_task, "car_ctrl", 4096, NULL, 5, NULL, 1);
}