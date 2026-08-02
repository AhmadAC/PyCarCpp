#pragma once
#include <stdint.h>
#include <stdbool.h>

// Shared Joystick Data matching the Python Controller Packet format
struct JoyMsg {
    uint8_t magic; // 67
    uint8_t lx;
    uint8_t ly;
    uint8_t rx;
    uint8_t ry;
    uint8_t btns;
};

extern volatile JoyMsg global_joy;
extern volatile bool has_espnow_peer;
extern volatile bool is_ap_mode_active;

// Export states so they can be accessed by the Wi-Fi remote controls
extern bool headlight_state;
extern bool line_follower_state;

// Subsystem initializers
void car_hardware_init();
void car_hardware_loop();
void car_set_headlight(bool enable);
void car_set_line_follower(bool enable);
void display_init();
void display_update(bool wifi_on, bool ble_on, bool line_on, bool light_on, float dist, float gap, bool fw, bool bw, bool lt, bool rt);

void comms_espnow_init();
void comms_ble_init();
void comms_wifi_ap_init();
void comms_wifi_sta_connect(const char* ssid, const char* pass);
void process_remote_command(const char* payload);
void process_serial_command(const char* cmd);