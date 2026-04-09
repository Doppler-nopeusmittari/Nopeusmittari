idf_component_register(SRCS "ble_hb100.c" "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp-dsp esp_adc nvs_flash bt esp_hw_support)
