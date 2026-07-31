
idf_component_register(SRCS "main.cpp"
                            "car_hardware.cpp"
                            "ssd1306.cpp"
                            "comms_wifi.cpp"
                            "comms_ble.cpp"
                            "comms_espnow.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_wifi esp_event driver esp_netif freertos bt json esp_http_server nimble esp_timer)