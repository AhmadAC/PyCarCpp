
#include "main.h"
#include "driver/i2c.h"
#include <string.h>
#include <stdio.h>

#define I2C_MASTER_SCL_IO 23
#define I2C_MASTER_SDA_IO 25
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000
#define OLED_ADDR 0x3C

static uint8_t buffer[1024];

static void i2c_cmd(uint8_t cmd) {
    i2c_cmd_handle_t hc = i2c_cmd_link_create();
    i2c_master_start(hc);
    i2c_master_write_byte(hc, (OLED_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(hc, 0x00, true);
    i2c_master_write_byte(hc, cmd, true);
    i2c_master_stop(hc);
    i2c_master_cmd_begin(I2C_MASTER_NUM, hc, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(hc);
}

void display_init() {
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);

    const uint8_t init_cmds[] = {
        0xAE, 0x20, 0x00, 0x40, 0xA1, 0xA8, 0x3F, 0xC8, 0xD3, 0x00,
        0xDA, 0x12, 0xD5, 0x80, 0xD9, 0xF1, 0xDB, 0x30, 0x81, 0xFF,
        0xA4, 0xA6, 0x8D, 0x14, 0xAF
    };
    for (int i = 0; i < sizeof(init_cmds); i++) i2c_cmd(init_cmds[i]);
    memset(buffer, 0, sizeof(buffer));
}

static void draw_rect(int x, int y, int w, int h) {
    for (int i = x; i < x + w; i++) {
        for (int j = y; j < y + h; j++) {
            if (i >= 0 && i < 128 && j >= 0 && j < 64) buffer[i + (j / 8) * 128] |= (1 << (j % 8));
        }
    }
}

// Ultra-minimal 5x7 font data for textual replication of the Python UI
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* Space */
    {0x3e,0x51,0x49,0x45,0x3e}, /* 0 */
    {0x00,0x42,0x7f,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4b,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7f,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3c,0x4a,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1e}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x00,0x60,0x00,0x00}, /* . */
    {0x7f,0x09,0x09,0x09,0x01}, /* F */
    {0x7f,0x49,0x49,0x49,0x36}, /* B */
    {0x7f,0x40,0x40,0x40,0x40}, /* L */
    {0x7f,0x09,0x19,0x29,0x46}, /* R */
    {0x3e,0x41,0x41,0x41,0x22}, /* C */
    {0x7f,0x40,0x40,0x40,0x3f}, /* U */
    {0x7f,0x02,0x0c,0x02,0x7f}, /* M */
    {0x7f,0x49,0x49,0x49,0x41}, /* E */
    {0x7f,0x04,0x08,0x10,0x7f}, /* N */
    {0x3e,0x41,0x41,0x41,0x3e}, /* O */
    {0x01,0x02,0x7c,0x02,0x01}, /* T */
    {0x7f,0x20,0x10,0x20,0x7f}, /* W */
    {0x7f,0x09,0x09,0x09,0x06}, /* P */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x7f,0x08,0x14,0x22,0x41}, /* K */
    {0x3e,0x41,0x49,0x49,0x7a}, /* G */
    {0x7f,0x41,0x41,0x22,0x1c}, /* D */
    {0x7e,0x11,0x11,0x11,0x7e}, /* A */
    {0x7f,0x49,0x49,0x49,0x36}, /* B */
    {0x7f,0x08,0x14,0x22,0x41}, /* K */
    {0x7f,0x04,0x08,0x10,0x7f}, /* N */
    {0x01,0x02,0x7c,0x02,0x01}, /* T */
    {0x7f,0x10,0x28,0x44,0x00}, /* k (hack for lowercase) */
};

static void draw_char(int x, int y, char c) {
    int idx = 0;
    if (c >= '0' && c <= '9') idx = c - '0' + 1;
    else if (c == ':') idx = 11;
    else if (c == '.') idx = 12;
    else if (c == 'F') idx = 13; else if (c == 'B') idx = 14; else if (c == 'L') idx = 15; else if (c == 'R') idx = 16;
    else if (c == 'C') idx = 17; else if (c == 'U') idx = 18; else if (c == 'M') idx = 19; else if (c == 'E') idx = 20;
    else if (c == 'N') idx = 21; else if (c == 'O') idx = 22; else if (c == 'T') idx = 23; else if (c == 'W') idx = 24;
    else if (c == 'P') idx = 25; else if (c == 'S') idx = 26; else if (c == 'K') idx = 27; else if (c == 'G') idx = 28;
    else if (c == 'D') idx = 29; else if (c == 'm') idx = 19; else if (c == 'c') idx = 17;
    
    if (idx == 0 && c != ' ') return;
    for (int i = 0; i < 5; i++) {
        uint8_t line = font5x7[idx][i];
        for (int j = 0; j < 7; j++) {
            if (line & (1 << j)) {
                int px = x + i, py = y + j;
                if (px >= 0 && px < 128 && py >= 0 && py < 64) buffer[px + (py / 8) * 128] |= (1 << (py % 8));
            }
        }
    }
}

static void draw_str(int x, int y, const char* str) {
    while (*str) { draw_char(x, y, *str++); x += 6; }
}

void display_update(bool wifi_on, bool ble_on, bool line_on, bool light_on, float dist, float gap, bool fw, bool bw, bool lt, bool rt) {
    memset(buffer, 0, sizeof(buffer));

    // Replication of UI grid lines
    for(int i=0; i<64; i++) { buffer[16 + (i/8)*128] |= (1<<(i%8)); buffer[82 + (i/8)*128] |= (1<<(i%8)); }
    for(int i=82; i<122; i++) { buffer[i + (32/8)*128] |= (1<<(32%8)); }

    if (wifi_on) draw_str(0, 1, "WIFI");
    if (ble_on)  draw_str(0, 17, "BLE");
    if (line_on) draw_str(0, 31, "LINE");
    if (light_on)draw_str(1, 47, "LUM");

    draw_str(25, 27, bw ? "<" : " "); draw_str(49, 27, fw ? ">" : " ");
    draw_str(44, 7, lt ? "^" : " "); draw_str(44, 40, rt ? "v" : " ");

    char buf[16];
    draw_str(84, 8, "D:cm");
    snprintf(buf, sizeof(buf), "%.2f", dist); draw_str(84, 18, buf);
    
    draw_str(84, 38, "G:cm");
    snprintf(buf, sizeof(buf), "%.2f", gap); draw_str(84, 48, buf);

    i2c_cmd_handle_t hc = i2c_cmd_link_create();
    i2c_master_start(hc);
    i2c_master_write_byte(hc, (OLED_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(hc, 0x00, true);
    i2c_master_write_byte(hc, 0x21, true); i2c_master_write_byte(hc, 0, true); i2c_master_write_byte(hc, 127, true);
    i2c_master_write_byte(hc, 0x22, true); i2c_master_write_byte(hc, 0, true); i2c_master_write_byte(hc, 7, true);
    i2c_master_stop(hc);
    i2c_master_cmd_begin(I2C_MASTER_NUM, hc, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(hc);

    hc = i2c_cmd_link_create();
    i2c_master_start(hc);
    i2c_master_write_byte(hc, (OLED_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(hc, 0x40, true);
    i2c_master_write(hc, buffer, sizeof(buffer), true);
    i2c_master_stop(hc);
    i2c_master_cmd_begin(I2C_MASTER_NUM, hc, 1000 / portTICK_PERIOD_MS);
    i2c_cmd_link_delete(hc);
}