
idf_component_register(
    SRCS "m5stack_tab5.c" "esp_lcd_st7123.c" "trail_mate_tab5_runtime.cpp"
    INCLUDE_DIRS "include" "include/bsp"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES
        driver
        esp_lcd
        esp_codec_dev
        esp_lvgl_port
        esp_lcd_ili9881c
        esp_lcd_touch_st7123
        esp_timer
    PRIV_REQUIRES
        esp_app_format
        usb
        spiffs
        fatfs
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-deprecated-declarations
    -Wno-unused-function
)
