idf_component_register(SRCS "app_main.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES esp32-camera esp_wifi esp_event nvs_flash esp_http_server)
