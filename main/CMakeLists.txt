idf_component_register(SRCS "app_main.c"
    PRIV_REQUIRES i2c_lcd json wifi_connect mbedtls esp_http_client
    INCLUDE_DIRS "")