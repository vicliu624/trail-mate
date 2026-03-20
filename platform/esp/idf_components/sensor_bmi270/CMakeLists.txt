idf_component_register(
    SRCS
        "src/bmi2.c"
        "src/bmi270.c"
        "src/bmi270_context.c"
        "src/bmi270_legacy.c"
        "src/bmi270_maximum_fifo.c"
        "src/bmi2_ois.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_driver_i2c
        esp_driver_gpio
)

