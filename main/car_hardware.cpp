#include "main.h"
#include "driver/mcpwm.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_attr.h"
#include <math.h>
#include <stdlib.h>

#define M1_P GPIO_NUM_14
#define M1_N GPIO_NUM_15
#define M2_P GPIO_NUM_16
#define M2_N GPIO_NUM_17
#define M3_P GPIO_NUM_18
#define M3_N GPIO_NUM_19
#define M4_P GPIO_NUM_21
#define M4_N GPIO_NUM_22

#define PIN_LIGHT GPIO_NUM_5
#define PIN_TRIG  GPIO_NUM_27
#define PIN_ECHO  GPIO_NUM_26
#define PIN_ENC1  GPIO_NUM_4
#define PIN_ENC2  GPIO_NUM_13

#define LINE_1 GPIO_NUM_33
#define LINE_2 GPIO_NUM_34
#define LINE_3 GPIO_NUM_35
#define LINE_4 GPIO_NUM_36
#define LINE_5 GPIO_NUM_39

float current_distance = 999.0;
float line_follower_gap = 0.0;
bool headlight_state = false;
bool line_follower_state = false;
bool s_forward = false, s_backward = false, s_left = false, s_right = false;
bool sonar_enabled = true;

volatile uint32_t count1 = 0;
volatile uint32_t count2 = 0;
uint32_t last_count1 = 0;
uint32_t last_count2 = 0;
int64_t last_encoder_time = 0;

float m1_pwm_trim = 1.0f;
float m2_pwm_trim = 1.0f;
int target_m1 = 0;
int target_m2 = 0;

float last_line_pos = 0.0f;
float last_error = 0.0f;

static void IRAM_ATTR enc1_isr(void* arg) { count1++; }
static void IRAM_ATTR enc2_isr(void* arg) { count2++; }

void car_set_headlight(bool enable) {
    headlight_state = enable;
    gpio_set_level(PIN_LIGHT, headlight_state ? 1 : 0);
}

void car_set_line_follower(bool enable) {
    line_follower_state = enable;
}

void car_set_motors(int m1, int m2, int m3, int m4) {
    if (m1 >= 0) { mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, m1 * 100.0 / 1023.0); mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, 0); }
    else         { mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, 0); mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_B, -m1 * 100.0 / 1023.0); }

    if (m2 >= 0) { mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_1, MCPWM_OPR_A, m2 * 100.0 / 1023.0); mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_1, MCPWM_OPR_B, 0); }
    else         { mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_1, MCPWM_OPR_A, 0); mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_1, MCPWM_OPR_B, -m2 * 100.0 / 1023.0); }

    if (m3 >= 0) { mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_2, MCPWM_OPR_A, m3 * 100.0 / 1023.0); mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_2, MCPWM_OPR_B, 0); }
    else         { mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_2, MCPWM_OPR_A, 0); mcpwm_set_duty(MCPWM_UNIT_0, MCPWM_TIMER_2, MCPWM_OPR_B, -m3 * 100.0 / 1023.0); }

    if (m4 >= 0) { mcpwm_set_duty(MCPWM_UNIT_1, MCPWM_TIMER_0, MCPWM_OPR_A, m4 * 100.0 / 1023.0); mcpwm_set_duty(MCPWM_UNIT_1, MCPWM_TIMER_0, MCPWM_OPR_B, 0); }
    else         { mcpwm_set_duty(MCPWM_UNIT_1, MCPWM_TIMER_0, MCPWM_OPR_A, 0); mcpwm_set_duty(MCPWM_UNIT_1, MCPWM_TIMER_0, MCPWM_OPR_B, -m4 * 100.0 / 1023.0); }

    s_forward  = (m1 > 0 && m2 > 0 && m3 > 0 && m4 > 0);
    s_backward = (m1 < 0 && m2 < 0 && m3 < 0 && m4 < 0);
    s_left     = (m1 < 0 && m4 > 0);
    s_right    = (m1 > 0 && m4 < 0);
}

float get_ultrasonic() {
    gpio_set_level(PIN_TRIG, 1);
    esp_rom_delay_us(20);
    gpio_set_level(PIN_TRIG, 0);
    
    int64_t t0 = esp_timer_get_time();
    while (gpio_get_level(PIN_ECHO) == 0) { if (esp_timer_get_time() - t0 > 30000) return -1.0; }
    
    int64_t t1 = esp_timer_get_time();
    while (gpio_get_level(PIN_ECHO) == 1) { if (esp_timer_get_time() - t1 > 30000) return -1.0; }
    
    int64_t t2 = esp_timer_get_time();
    return ((t2 - t1) * 170.0f) / 10000.0f;
}

void car_hardware_init() {
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A, M1_P); mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0B, M1_N);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM1A, M2_P); mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM1B, M2_N);
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM2A, M3_P); mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM2B, M3_N);
    mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM0A, M4_P); mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM0B, M4_N);
    
    mcpwm_config_t pwm_config = {};
    pwm_config.frequency = 1000;
    pwm_config.cmpr_a = 0; pwm_config.cmpr_b = 0;
    pwm_config.counter_mode = MCPWM_UP_COUNTER;
    pwm_config.duty_mode = MCPWM_DUTY_MODE_0;
    
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_1, &pwm_config);
    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_2, &pwm_config);
    mcpwm_init(MCPWM_UNIT_1, MCPWM_TIMER_0, &pwm_config);

    gpio_reset_pin(PIN_LIGHT); gpio_set_direction(PIN_LIGHT, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_TRIG);  gpio_set_direction(PIN_TRIG, GPIO_MODE_OUTPUT);
    gpio_reset_pin(PIN_ECHO);  gpio_set_direction(PIN_ECHO, GPIO_MODE_INPUT);
    
    gpio_config_t line_conf = {};
    line_conf.intr_type = GPIO_INTR_DISABLE;
    line_conf.mode = GPIO_MODE_INPUT;
    line_conf.pin_bit_mask = (1ULL<<LINE_1)|(1ULL<<LINE_2)|(1ULL<<LINE_3)|(1ULL<<LINE_4)|(1ULL<<LINE_5);
    gpio_config(&line_conf);

    gpio_install_isr_service(0);
    gpio_set_direction(PIN_ENC1, GPIO_MODE_INPUT); gpio_set_pull_mode(PIN_ENC1, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(PIN_ENC1, GPIO_INTR_NEGEDGE); gpio_isr_handler_add(PIN_ENC1, enc1_isr, NULL);

    gpio_set_direction(PIN_ENC2, GPIO_MODE_INPUT); gpio_set_pull_mode(PIN_ENC2, GPIO_PULLUP_ONLY);
    gpio_set_intr_type(PIN_ENC2, GPIO_INTR_NEGEDGE); gpio_isr_handler_add(PIN_ENC2, enc2_isr, NULL);
}

void car_hardware_loop() {
    static int64_t last_sonar = 0;
    int64_t now = esp_timer_get_time() / 1000;
    
    if (now - last_encoder_time > 100) {
        int spd1 = count1 - last_count1;
        int spd2 = count2 - last_count2;
        last_count1 = count1;
        last_count2 = count2;
        last_encoder_time = now;
        
        if (abs(target_m1) > 200 && target_m1 == target_m2) {
            if (spd1 > spd2 + 1) m1_pwm_trim -= 0.02f;
            else if (spd2 > spd1 + 1) m2_pwm_trim -= 0.02f;
            else if (spd1 == spd2) {
                if (m1_pwm_trim < 1.0f) m1_pwm_trim += 0.01f;
                if (m2_pwm_trim < 1.0f) m2_pwm_trim += 0.01f;
            }
            if (m1_pwm_trim < 0.5f) m1_pwm_trim = 0.5f;
            if (m2_pwm_trim < 0.5f) m2_pwm_trim = 0.5f;
        } else {
            if (m1_pwm_trim < 1.0f) m1_pwm_trim += 0.05f;
            if (m2_pwm_trim < 1.0f) m2_pwm_trim += 0.05f;
            if (m1_pwm_trim > 1.0f) m1_pwm_trim = 1.0f;
            if (m2_pwm_trim > 1.0f) m2_pwm_trim = 1.0f;
        }
    }

    if (now - last_sonar > 200) {
        float d = get_ultrasonic();
        current_distance = (d >= 0) ? d : 999.0f;
        line_follower_gap = current_distance; 
        last_sonar = now;
    }

    static bool last_light_btn = false;
    static bool last_line_btn = false;

    bool light_btn = (global_joy.btns & 0x20);
    if (light_btn && !last_light_btn) {
        car_set_headlight(!headlight_state);
    }
    last_light_btn = light_btn;

    bool line_btn = (global_joy.btns & 0x10);
    if (line_btn && !last_line_btn) car_set_line_follower(!line_follower_state);
    last_line_btn = line_btn;

    int turn_input = global_joy.lx - 128;
    // Inverted Y-axis calculations so Joystick UP gives positive drive (FORWARD) and DOWN gives negative drive (BACKWARD)
    int left_drive_input = 128 - global_joy.ly;
    int right_drive_input = 128 - global_joy.ry;

    int drive_input = (abs(left_drive_input) > abs(right_drive_input)) ? left_drive_input : right_drive_input;
    if (abs(turn_input) <= 15) turn_input = 0;
    if (abs(drive_input) <= 15) drive_input = 0;

    int throttle = (drive_input * 1023) / 128;
    int steering = (turn_input * 1023) / 128;
    if (throttle < 0) steering = -steering;

    int left_speed = throttle + steering;
    int right_speed = throttle - steering;

    int m1 = left_speed;
    int m4 = -left_speed;
    int m2 = right_speed;
    int m3 = right_speed;

    target_m1 = left_speed;
    target_m2 = right_speed;

    uint8_t dpad = global_joy.btns & 0x0F;
    if (dpad == 0)      { m1 = 1023; m2 = 1023; m3 = 1023; m4 = -1023; }
    else if (dpad == 4) { m1 = -1023; m2 = -1023; m3 = -1023; m4 = 1023; }

    bool manual_override = (abs(global_joy.lx - 128) > 15 || abs(global_joy.ly - 128) > 15 || abs(global_joy.ry - 128) > 15 || dpad != 8);

    if (line_follower_state && !manual_override) {
        int v1 = 1 - gpio_get_level(LINE_1);
        int v2 = 1 - gpio_get_level(LINE_2);
        int v3 = 1 - gpio_get_level(LINE_3);
        int v4 = 1 - gpio_get_level(LINE_4);
        int v5 = 1 - gpio_get_level(LINE_5);
        int active = v1 + v2 + v3 + v4 + v5;

        if (active > 0) {
            float pos = (-2.0f*v1 - 1.0f*v2 + 0.0f*v3 + 1.0f*v4 + 2.0f*v5) / active;
            float P = pos * 350.0f;
            float D = (pos - last_error) * 250.0f;
            last_error = pos;
            last_line_pos = pos;
            float str = P + D;
            float base = 500.0f - (fabs(pos) * 150.0f);
            left_speed = base + str;
            right_speed = base - str;
        } else {
            if (last_line_pos < -0.2f) { left_speed = -450; right_speed = 450; }
            else if (last_line_pos > 0.2f) { left_speed = 450; right_speed = -450; }
            else { left_speed = 0; right_speed = 0; }
        }
        m1 = left_speed; m4 = -m1; m2 = right_speed; m3 = m2;
    }

    if (sonar_enabled && current_distance <= 20.0f) {
        if (m1 > 0) m1 = 0;
        if (m2 > 0) m2 = 0;
        if (m3 > 0) m3 = 0;
        if (m4 < 0) m4 = 0;
    }

    m1 = (m1 > 1023) ? 1023 : (m1 < -1023) ? -1023 : m1;
    m2 = (m2 > 1023) ? 1023 : (m2 < -1023) ? -1023 : m2;
    m3 = (m3 > 1023) ? 1023 : (m3 < -1023) ? -1023 : m3;
    m4 = (m4 > 1023) ? 1023 : (m4 < -1023) ? -1023 : m4;

    car_set_motors(m1 * m1_pwm_trim, m2 * m2_pwm_trim, m3 * m2_pwm_trim, m4 * m1_pwm_trim);
}