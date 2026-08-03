// PyCarCpp/main/comms_wifi.cpp
#include "main.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/sockets.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "COMMS_WIFI";
static esp_netif_t *ap_netif = NULL;
static esp_netif_t *sta_netif = NULL;

static const char* HTML_PAGE = R"raw_html(<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1">
<title>pyCar Dashboard</title>
<style>
  :root { --primary: #0ea5e9; --bg: #0f172a; --card: #1e293b; --text: #f1f5f9; }
  body { font-family: -apple-system, sans-serif; background: var(--bg); color: var(--text); padding: 15px; text-align: center; margin:0;}
  .card { background: var(--card); padding: 25px; border-radius: 20px; border: 1px solid #334155; margin-top: 15px; }
  .btn { padding: 15px; margin: 5px; font-size: 18px; border-radius: 10px; border: none; background: #3b82f6; color: white; width: 45%; font-weight: bold; }
  .btn:active { transform: scale(0.96); opacity: 0.9; }
  .btn-red { background: #ef4444; }
  .btn-green { background: #10b981; }
  .status-bar { padding: 12px; border-radius: 10px; font-weight: bold; text-transform: uppercase; background: #172554; color: #93c5fd; border: 1px solid #3b82f6; }
</style>
</head><body>
<div class="status-bar">pyCar Web Dashboard</div>
<div class="card">
  <button class="btn" onmousedown="s('forward')" onmouseup="s('stop')" ontouchstart="s('forward')" ontouchend="s('stop')">Forward</button><br>
  <button class="btn" onmousedown="s('left')" onmouseup="s('stop')" ontouchstart="s('left')" ontouchend="s('stop')">Left</button>
  <button class="btn" onmousedown="s('right')" onmouseup="s('stop')" ontouchstart="s('right')" ontouchend="s('stop')">Right</button><br>
  <button class="btn" onmousedown="s('backward')" onmouseup="s('stop')" ontouchstart="s('backward')" ontouchend="s('stop')">Backward</button>
</div>
<div class="card">
  <button class="btn btn-green" onclick="t('light')">Toggle Light</button>
  <button class="btn btn-red" onclick="t('line')">Line Follower</button>
</div>
<div class="card" style="font-size: 14px; color: #94a3b8;">
  <div id="gp-info">Waiting for Gamepad... Press any button to connect.</div>
</div>
<script>
  let ws;
  function initWS() {
    ws = new WebSocket("ws://" + window.location.host + "/ws");
    ws.onopen = () => document.getElementById('gp-info').innerText = "WS Connected. Waiting for Gamepad...";
    ws.onclose = () => setTimeout(initWS, 2000);
  }
  initWS();
  function s(act) { if(ws && ws.readyState === 1) ws.send(JSON.stringify({action: act})); }
  function t(act) {
    if(ws && ws.readyState === 1) {
      ws.send(JSON.stringify({action: act}));
      setTimeout(() => ws.send(JSON.stringify({action: 'stop'})), 100);
    }
  }
  let lastBtn = {}; let lastAxis = {};
  function updateGamepad() {
    const gps = navigator.getGamepads ? navigator.getGamepads() : [];
    let gp = gps[0];
    if (gp) {
      document.getElementById('gp-info').innerText = "Gamepad: " + gp.id;
      let lx = Math.floor((gp.axes[0] + 1) * 127.5);
      let ly = Math.floor((gp.axes[1] + 1) * 127.5);
      let rx = Math.floor((gp.axes[2] + 1) * 127.5);
      let ry = Math.floor((gp.axes[3] + 1) * 127.5);
      let btns = 8;
      if(gp.buttons[12] && gp.buttons[12].pressed) btns = 0;
      if(gp.buttons[13] && gp.buttons[13].pressed) btns = 4;
      if(gp.buttons[14] && gp.buttons[14].pressed) btns = 6;
      if(gp.buttons[15] && gp.buttons[15].pressed) btns = 2;
      if(gp.buttons[0] && gp.buttons[0].pressed) btns |= 0x20;
      if(gp.buttons[3] && gp.buttons[3].pressed) btns |= 0x10;
      let axesChanged = (Math.abs(lx - (lastAxis.lx||128)) > 5) || (Math.abs(ly - (lastAxis.ly||128)) > 5);
      if (axesChanged || btns !== lastBtn.btns) {
        if(ws && ws.readyState === 1) ws.send(JSON.stringify({lx:lx, ly:ly, rx:rx, ry:ry, btns:btns}));
        lastAxis = {lx, ly, rx, ry}; lastBtn = {btns};
      }
    }
    requestAnimationFrame(updateGamepad);
  }
  requestAnimationFrame(updateGamepad);
</script>
</body></html>)raw_html";

void process_remote_command(const char* payload) {
    if (!payload) return;
    last_remote_cmd_time = esp_timer_get_time() / 1000;
    
    cJSON *json = cJSON_Parse(payload);
    if (!json) {
        ESP_LOGE(TAG, "Failed to parse JSON!");
        return;
    }
    
    const char* a = NULL;
    cJSON *act = cJSON_GetObjectItem(json, "action");
    cJSON *a_num = cJSON_GetObjectItem(json, "a");
    
    // Map BLE numerical codes back to English words for backwards compatibility & REPL clarity
    if (act && act->valuestring) {
        a = act->valuestring;
    } else if (a_num && cJSON_IsNumber(a_num)) {
        switch(a_num->valueint) {
            case 1: a = "stop"; break;
            case 2: a = "forward"; break;
            case 3: a = "backward"; break;
            case 4: a = "left"; break;
            case 5: a = "right"; break;
            case 6: a = "light"; break;
            case 7: a = "line"; break;
        }
    }
    
    if (a) {
        ESP_LOGI(TAG, "Parsed Action: %s", a); // Display the English translation to the REPL Console
        
        // When using explicit buttons/actions, completely reset the gamepad state so no leftover controller rotation interrupts it
        if (strcmp(a, "forward") == 0)                                 { global_joy.lx = 128; global_joy.ly = 0;   global_joy.rx = 128; global_joy.ry = 128; global_joy.btns = 8; }
        else if (strcmp(a, "backward") == 0 || strcmp(a, "back") == 0) { global_joy.lx = 128; global_joy.ly = 255; global_joy.rx = 128; global_joy.ry = 128; global_joy.btns = 8; }
        else if (strcmp(a, "left") == 0)                               { global_joy.lx = 0;   global_joy.ly = 128; global_joy.rx = 128; global_joy.ry = 128; global_joy.btns = 8; }
        else if (strcmp(a, "right") == 0)                              { global_joy.lx = 255; global_joy.ly = 128; global_joy.rx = 128; global_joy.ry = 128; global_joy.btns = 8; }
        else if (strcmp(a, "stop") == 0)                               { global_joy.lx = 128; global_joy.ly = 128; global_joy.rx = 128; global_joy.ry = 128; global_joy.btns = 8; }
        else if (strcmp(a, "light") == 0)                              { car_set_headlight(!headlight_state); }
        else if (strcmp(a, "line") == 0)                               { car_set_line_follower(!line_follower_state); }
    } else {
        // Fallback parsers check for both explicit ("lx") and shortened BLE keys ("x") 
        cJSON *lx = cJSON_GetObjectItem(json, "lx"); if (!lx) lx = cJSON_GetObjectItem(json, "x");
        if (lx) global_joy.lx = lx->valueint;
        
        cJSON *ly = cJSON_GetObjectItem(json, "ly"); if (!ly) ly = cJSON_GetObjectItem(json, "y");
        if (ly) global_joy.ly = ly->valueint;
        
        cJSON *rx = cJSON_GetObjectItem(json, "rx"); if (!rx) rx = cJSON_GetObjectItem(json, "z");
        if (rx) global_joy.rx = rx->valueint;
        
        cJSON *ry = cJSON_GetObjectItem(json, "ry"); if (!ry) ry = cJSON_GetObjectItem(json, "r");
        if (ry) global_joy.ry = ry->valueint;
        
        cJSON *btns = cJSON_GetObjectItem(json, "btns"); if (!btns) btns = cJSON_GetObjectItem(json, "b");
        if (btns) global_joy.btns = btns->valueint;
    }
    cJSON_Delete(json);
}

static esp_err_t ws_handler(httpd_req_t *req) {
    if (req->method == HTTP_GET) return ESP_OK;
    httpd_ws_frame_t ws_pkt = {};
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    if (httpd_ws_recv_frame(req, &ws_pkt, 0) == ESP_OK && ws_pkt.len) {
        uint8_t *buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        ws_pkt.payload = buf;
        if (httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len) == ESP_OK) process_remote_command((char*)buf);
        free(buf);
    }
    return ESP_OK;
}

// HTTP POST Handler for HTTP App Controls
static esp_err_t http_post_handler(httpd_req_t *req) {
    char buf[512] = {0};
    int total_len = req->content_len;
    int cur_len = 0;

    if (total_len >= sizeof(buf)) total_len = sizeof(buf) - 1;

    while (cur_len < total_len) {
        int received = httpd_req_recv(req, buf + cur_len, total_len - cur_len);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_408(req);
            }
            return ESP_FAIL;
        }
        cur_len += received;
    }
    buf[cur_len] = '\0';

    if (cur_len > 0) process_remote_command(buf);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

// HTTP GET Action Handler
static esp_err_t http_get_action_handler(httpd_req_t *req) {
    char qbuf[256] = {0};
    if (httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf)) == ESP_OK) {
        char param[64] = {0};
        if (httpd_query_key_value(qbuf, "action", param, sizeof(param)) == ESP_OK ||
            httpd_query_key_value(qbuf, "cmd", param, sizeof(param)) == ESP_OK) {
            char jbuf[128];
            snprintf(jbuf, sizeof(jbuf), "{\"action\":\"%s\"}", param);
            process_remote_command(jbuf);
        }
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    return ESP_OK;
}

static esp_err_t captive_portal_handler(httpd_req_t *req) {
    char host[64] = {0};
    httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host));
    if (strcmp(host, "192.168.4.1") != 0 && strcmp(host, "192.168.4.1:80") != 0) {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, HTML_PAGE, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static void dns_server_task(void *pvParameters) {
    char rx_buffer[128];
    struct sockaddr_in dest_addr = {};
    dest_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(53);
    
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    bind(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));

    while (1) {
        struct sockaddr_storage source_addr;
        socklen_t socklen = sizeof(source_addr);
        int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&source_addr, &socklen);
        
        if (len > 12) {
            rx_buffer[2] = 0x81; rx_buffer[3] = 0x80;
            rx_buffer[6] = rx_buffer[4]; rx_buffer[7] = rx_buffer[5];
            rx_buffer[8] = 0; rx_buffer[9] = 0; rx_buffer[10] = 0; rx_buffer[11] = 0; 
            int pos = len;
            rx_buffer[pos++] = 0xC0; rx_buffer[pos++] = 0x0C;
            rx_buffer[pos++] = 0x00; rx_buffer[pos++] = 0x01;
            rx_buffer[pos++] = 0x00; rx_buffer[pos++] = 0x01;
            rx_buffer[pos++] = 0x00; rx_buffer[pos++] = 0x00;
            rx_buffer[pos++] = 0x00; rx_buffer[pos++] = 0x3C;
            rx_buffer[pos++] = 0x00; rx_buffer[pos++] = 0x04;
            rx_buffer[pos++] = 192; rx_buffer[pos++] = 168; rx_buffer[pos++] = 4; rx_buffer[pos++] = 1;
            sendto(sock, rx_buffer, pos, 0, (struct sockaddr *)&source_addr, sizeof(source_addr));
        }
    }
}

void comms_wifi_ap_init() {
    esp_netif_init();
    esp_event_loop_create_default();
    if (!ap_netif) {
        ap_netif = esp_netif_create_default_wifi_ap();
    }
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.gw, 192, 168, 4, 1);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_dhcps_stop(ap_netif);
    esp_netif_set_ip_info(ap_netif, &ip_info);
    esp_netif_dhcps_start(ap_netif);

    wifi_config_t ap_config = {};
    strcpy((char*)ap_config.ap.ssid, "pyCar_AP");
    ap_config.ap.ssid_len = strlen("pyCar_AP");
    ap_config.ap.authmode = WIFI_AUTH_OPEN;
    ap_config.ap.max_connection = 4;
    
    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_config);
    esp_wifi_start();

    xTaskCreate(dns_server_task, "dns_task", 4096, NULL, 5, NULL);

    httpd_config_t http_cfg = HTTPD_DEFAULT_CONFIG();
    http_cfg.uri_match_fn = httpd_uri_match_wildcard;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &http_cfg) == ESP_OK) {
        httpd_uri_t uri_ws = {};
        uri_ws.uri = "/ws";
        uri_ws.method = HTTP_GET;
        uri_ws.handler = ws_handler;
        uri_ws.is_websocket = true;

        httpd_uri_t uri_action = {};
        uri_action.uri = "/action";
        uri_action.method = HTTP_GET;
        uri_action.handler = http_get_action_handler;

        httpd_uri_t uri_claw = {};
        uri_claw.uri = "/claw";
        uri_claw.method = HTTP_GET;
        uri_claw.handler = http_get_action_handler;

        httpd_uri_t uri_post = {};
        uri_post.uri = "/*";
        uri_post.method = HTTP_POST;
        uri_post.handler = http_post_handler;

        httpd_uri_t uri_fallback = {};
        uri_fallback.uri = "/*";
        uri_fallback.method = HTTP_GET;
        uri_fallback.handler = captive_portal_handler;

        httpd_register_uri_handler(server, &uri_ws);
        httpd_register_uri_handler(server, &uri_action);
        httpd_register_uri_handler(server, &uri_claw);
        httpd_register_uri_handler(server, &uri_post);
        httpd_register_uri_handler(server, &uri_fallback);
    }
    ESP_LOGI(TAG, "Wi-Fi Access Point 'pyCar_AP' started at 192.168.4.1");
}

void comms_wifi_sta_connect(const char* ssid, const char* pass) {
    esp_netif_init();
    esp_event_loop_create_default();
    if (!sta_netif) {
        sta_netif = esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    wifi_config_t sta_config = {};
    strncpy((char*)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char*)sta_config.sta.password, pass, sizeof(sta_config.sta.password) - 1);
    sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_stop();
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_start();
    esp_wifi_connect();

    ESP_LOGI(TAG, "Connecting to Wi-Fi Station Network SSID: '%s' ...", ssid);
}