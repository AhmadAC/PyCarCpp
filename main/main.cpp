#include "main.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

static const char* TAG = "MAIN";

volatile JoyMsg global_joy = {67, 128, 128, 128, 128, 8};
volatile bool has_espnow_peer = false;
volatile bool is_ap_mode_active = false;

extern float current_distance;
extern float line_follower_gap;
extern bool headlight_state;
extern bool line_follower_state;
extern bool s_forward, s_backward, s_left, s_right;

static int64_t timed_action_end = 0;

static void print_cheatsheet() {
    printf("\n");
    printf("================================================================================\n");
    printf("                       PYCAR ESP32 COMMAND CHEATSHEET                           \n");
    printf("================================================================================\n");
    printf("  MOVEMENT COMMANDS:\n");
    printf("    forward [sec]           - Drive forward for N sec (e.g., 'forward 3')\n");
    printf("    backward [sec]          - Drive backward for N sec (e.g., 'back 2.5')\n");
    printf("    left [sec]              - Turn left for N sec (e.g., 'left 2')\n");
    printf("    right [sec]             - Turn right for N sec (e.g., 'right 2')\n");
    printf("    circle [sec]            - Drive in a circle for N sec (e.g., 'circle 4')\n");
    printf("    stop                    - Immediately stop all motors\n\n");
    printf("  HARDWARE CONTROLS:\n");
    printf("    light [on|off|toggle]   - Control headlights (e.g., 'light on')\n");
    printf("    line [on|off|toggle]    - Control line follower (e.g., 'line on')\n\n");
    printf("  NETWORK CONTROLS:\n");
    printf("    wifi ap                 - Start Wi-Fi Access Point (pyCar_AP @ 192.168.4.1)\n");
    printf("    wifi sta <ssid> <pass>  - Connect to Wi-Fi router (e.g., 'wifi sta Home 1234')\n");
    printf("    ble                     - Enable Bluetooth LE Host (Device Name: pyCar)\n\n");
    printf("  SYSTEM:\n");
    printf("    status                  - Print real-time sensor & drive telemetry\n");
    printf("    help                    - Reprint this command cheat sheet\n");
    printf("================================================================================\n");
}

void process_serial_command(const char* cmd) {
    char buffer[128];
    strncpy(buffer, cmd, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    // Remove trailing newline / carriage return
    char* p = buffer + strlen(buffer) - 1;
    while (p >= buffer && (*p == '\r' || *p == '\n' || *p == ' ')) {
        *p = '\0';
        p--;
    }

    if (strlen(buffer) == 0) return;

    char command[32] = {0};
    char arg1[64] = {0};
    char arg2[64] = {0};
    int num_args = sscanf(buffer, "%31s %63s %63s", command, arg1, arg2);

    if (strcasecmp(command, "help") == 0) {
        print_cheatsheet();
    } 
    else if (strcasecmp(command, "forward") == 0 || strcasecmp(command, "fwd") == 0) {
        float sec = (num_args >= 2) ? atof(arg1) : 2.0f;
        if (sec <= 0) sec = 2.0f;
        global_joy.lx = 128; global_joy.ly = 255; global_joy.rx = 128; global_joy.ry = 128;
        timed_action_end = (esp_timer_get_time() / 1000) + (int64_t)(sec * 1000.0f);
        printf("[OK] Driving FORWARD for %.1f seconds...\n", sec);
    } 
    else if (strcasecmp(command, "backward") == 0 || strcasecmp(command, "back") == 0) {
        float sec = (num_args >= 2) ? atof(arg1) : 2.0f;
        if (sec <= 0) sec = 2.0f;
        global_joy.lx = 128; global_joy.ly = 0; global_joy.rx = 128; global_joy.ry = 128;
        timed_action_end = (esp_timer_get_time() / 1000) + (int64_t)(sec * 1000.0f);
        printf("[OK] Driving BACKWARD for %.1f seconds...\n", sec);
    } 
    else if (strcasecmp(command, "left") == 0) {
        float sec = (num_args >= 2) ? atof(arg1) : 2.0f;
        if (sec <= 0) sec = 2.0f;
        global_joy.lx = 0; global_joy.ly = 128; global_joy.rx = 128; global_joy.ry = 128;
        timed_action_end = (esp_timer_get_time() / 1000) + (int64_t)(sec * 1000.0f);
        printf("[OK] Turning LEFT for %.1f seconds...\n", sec);
    } 
    else if (strcasecmp(command, "right") == 0) {
        float sec = (num_args >= 2) ? atof(arg1) : 2.0f;
        if (sec <= 0) sec = 2.0f;
        global_joy.lx = 255; global_joy.ly = 128; global_joy.rx = 128; global_joy.ry = 128;
        timed_action_end = (esp_timer_get_time() / 1000) + (int64_t)(sec * 1000.0f);
        printf("[OK] Turning RIGHT for %.1f seconds...\n", sec);
    } 
    else if (strcasecmp(command, "circle") == 0) {
        float sec = (num_args >= 2) ? atof(arg1) : 3.0f;
        if (sec <= 0) sec = 3.0f;
        global_joy.lx = 220; global_joy.ly = 200; global_joy.rx = 128; global_joy.ry = 128;
        timed_action_end = (esp_timer_get_time() / 1000) + (int64_t)(sec * 1000.0f);
        printf("[OK] Driving in a CIRCLE for %.1f seconds...\n", sec);
    } 
    else if (strcasecmp(command, "stop") == 0) {
        timed_action_end = 0;
        global_joy.lx = 128; global_joy.ly = 128; global_joy.rx = 128; global_joy.ry = 128;
        printf("[OK] Motors STOPPED.\n");
    } 
    else if (strcasecmp(command, "light") == 0) {
        if (num_args >= 2 && strcasecmp(arg1, "on") == 0) car_set_headlight(true);
        else if (num_args >= 2 && strcasecmp(arg1, "off") == 0) car_set_headlight(false);
        else car_set_headlight(!headlight_state);
        printf("[OK] Headlight state: %s\n", headlight_state ? "ON" : "OFF");
    } 
    else if (strcasecmp(command, "line") == 0) {
        if (num_args >= 2 && strcasecmp(arg1, "on") == 0) car_set_line_follower(true);
        else if (num_args >= 2 && strcasecmp(arg1, "off") == 0) car_set_line_follower(false);
        else car_set_line_follower(!line_follower_state);
        printf("[OK] Line follower state: %s\n", line_follower_state ? "ACTIVE" : "OFF");
    } 
    else if (strcasecmp(command, "wifi") == 0) {
        if (num_args >= 2 && strcasecmp(arg1, "ap") == 0) {
            is_ap_mode_active = true;
            comms_wifi_ap_init();
            printf("[OK] Switched to Wi-Fi AP Mode (SSID: pyCar_AP, IP: 192.168.4.1)\n");
        } else if (num_args >= 3 && strcasecmp(arg1, "sta") == 0) {
            is_ap_mode_active = true;
            comms_wifi_sta_connect(arg2, (num_args >= 4) ? buffer + (arg2 - buffer) + strlen(arg2) + 1 : "");
            printf("[OK] Connecting to Wi-Fi SSID '%s'...\n", arg2);
        } else {
            printf("[ERROR] Usage: 'wifi ap' OR 'wifi sta <ssid> <password>'\n");
        }
    } 
    else if (strcasecmp(command, "ble") == 0) {
        comms_ble_init();
        printf("[OK] Bluetooth LE initialized (Device Name: pyCar).\n");
    } 
    else if (strcasecmp(command, "status") == 0) {
        printf("\n--- PYCAR STATUS & TELEMETRY ---\n");
        printf("  Distance Sensor:   %.2f cm\n", current_distance);
        printf("  Headlight:         %s\n", headlight_state ? "ON" : "OFF");
        printf("  Line Follower:     %s\n", line_follower_state ? "ACTIVE" : "OFF");
        printf("  Wi-Fi AP Active:   %s\n", is_ap_mode_active ? "YES" : "NO");
        printf("  ESP-NOW Direct:    %s\n", has_espnow_peer ? "CONNECTED" : "WAITING");
        printf("  Joystick Axes:     LX=%d LY=%d RX=%d RY=%d Btns=0x%02X\n", global_joy.lx, global_joy.ly, global_joy.rx, global_joy.ry, global_joy.btns);
        printf("--------------------------------\n\n");
    } 
    else {
        printf("[ERROR] Unknown command '%s'. Type 'help' for command cheat sheet.\n", command);
    }
}

void console_task(void *pv) {
    char rx_buf[128];
    int pos = 0;
    
    // Set non-blocking input for standard input
    fcntl(fileno(stdin), F_SETFL, O_NONBLOCK);

    // Print full command cheat sheet automatically upon connection/boot
    print_cheatsheet();
    printf("pycar> ");
    fflush(stdout);

    while (1) {
        int c = getchar();
        if (c != EOF) {
            if (c == '\r' || c == '\n') {
                if (pos > 0) {
                    rx_buf[pos] = '\0';
                    printf("\n");
                    process_serial_command(rx_buf);
                    pos = 0;
                    printf("pycar> ");
                    fflush(stdout);
                }
            } else if (c == '\b' || c == 127) {
                if (pos > 0) {
                    pos--;
                    printf("\b \b");
                    fflush(stdout);
                }
            } else if (pos < (int)sizeof(rx_buf) - 1) {
                rx_buf[pos++] = (char)c;
                putchar(c);
                fflush(stdout);
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

void car_control_task(void *pv) {
    int64_t last_log_time = 0;

    while (1) {
        int64_t now_ms = esp_timer_get_time() / 1000;

        // Check timed serial command duration
        if (timed_action_end > 0 && now_ms >= timed_action_end) {
            timed_action_end = 0;
            global_joy.lx = 128;
            global_joy.ly = 128;
            global_joy.rx = 128;
            global_joy.ry = 128;
            printf("\n[TIMED ACTION COMPLETE] Motors stopped.\npycar> ");
            fflush(stdout);
        }

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

        // Periodically log live status every 5 seconds
        if (now_ms - last_log_time >= 5000) {
            ESP_LOGI(TAG, "[TELEMETRY] Dist: %.1f cm | Mode: %s | Headlight: %s | LineFollow: %s",
                     current_distance,
                     is_ap_mode_active ? "AP/STA Network" : "ESP-NOW Direct",
                     headlight_state ? "ON" : "OFF",
                     line_follower_state ? "ACTIVE" : "OFF");
            last_log_time = now_ms;
        }
        
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
    
    // Launch Serial Console Task
    xTaskCreate(console_task, "console_task", 4096, NULL, 4, NULL);

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