idf_component_register(
    SRCS "src/vl53l8cx_api.c"
         "src/vl53l8cx_platform.c"
         "src/vl53l8cx_i2c_glue.c"
    INCLUDE_DIRS "src"
    REQUIRES driver esp_rom
)
