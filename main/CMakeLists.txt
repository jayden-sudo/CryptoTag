idf_component_register(SRCS "app_main.c"
    PRIV_REQUIRES i2c_lcd json wifi_connect http_request
    INCLUDE_DIRS "")