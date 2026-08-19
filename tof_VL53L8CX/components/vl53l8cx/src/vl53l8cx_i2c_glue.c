#include <string.h>
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "vl53l8cx_platform.h"

uint8_t esp_vl53l8cx_write(void *handle, uint16_t reg_addr, uint8_t *p_values, uint32_t size) {
    i2c_master_dev_handle_t dev = (i2c_master_dev_handle_t)handle;
    uint8_t *buf = malloc(size + 2);
    if (!buf) return 1;
    buf[0] = (reg_addr >> 8) & 0xFF;
    buf[1] = reg_addr & 0xFF;
    memcpy(&buf[2], p_values, size);
    esp_err_t ret = i2c_master_transmit(dev, buf, size + 2, -1);
    free(buf);
    return (ret == ESP_OK) ? 0 : 1;
}

uint8_t esp_vl53l8cx_read(void *handle, uint16_t reg_addr, uint8_t *p_values, uint32_t size) {
    i2c_master_dev_handle_t dev = (i2c_master_dev_handle_t)handle;
    uint8_t addr_buf[2] = { (reg_addr >> 8) & 0xFF, reg_addr & 0xFF };
    esp_err_t ret = i2c_master_transmit_receive(dev, addr_buf, 2, p_values, size, -1);
    return (ret == ESP_OK) ? 0 : 1;
}

uint8_t esp_vl53l8cx_wait(void *handle, uint32_t time_ms) {
    vTaskDelay(pdMS_TO_TICKS(time_ms));
    return 0;
}
