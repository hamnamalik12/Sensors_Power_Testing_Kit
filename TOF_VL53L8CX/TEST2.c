//4x4 , then 8x8, freq 15Hz, 4x4 25mA, 8X8 75mA 

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "vl53l8cx_api.h"

#define I2C_SDA_PIN     5
#define I2C_SCL_PIN     6
#define VL53L8CX_ADDR   0x29
#define TOF_LPN_PIN     GPIO_NUM_4   // D3 on XIAO ESP32S3

static const char *TAG = "TOF_VL53L8CX";

extern uint8_t esp_vl53l8cx_write(void *handle, uint16_t reg_addr, uint8_t *p_values, uint32_t size);
extern uint8_t esp_vl53l8cx_read(void *handle, uint16_t reg_addr, uint8_t *p_values, uint32_t size);
extern uint8_t esp_vl53l8cx_wait(void *handle, uint32_t time_ms);

i2c_master_bus_handle_t bus_handle;
i2c_master_dev_handle_t tof_dev_handle;
VL53L8CX_Configuration dev;

static void lpn_gpio_init(void) {
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << TOF_LPN_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    gpio_set_level(TOF_LPN_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void lpn_enable_sensor(void) {
    gpio_set_level(TOF_LPN_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void lpn_disable_sensor(void) {
    gpio_set_level(TOF_LPN_PIN, 0);
}

static void i2c_bus_init(void) {
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = VL53L8CX_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &tof_dev_handle));
}

// Runs one resolution phase for the given number of milliseconds,
// printing "TOF,<res>,<v0>,<v1>,...,<vN-1>" for each ready frame.
static void run_resolution_phase(uint8_t resolution, int num_zones, uint32_t duration_ms) {
    uint8_t status;

    status = vl53l8cx_stop_ranging(&dev);
    if (status) {
        ESP_LOGW(TAG, "stop_ranging warning: %d", status);
    }

    status = vl53l8cx_set_resolution(&dev, resolution);
    if (status) {
        ESP_LOGE(TAG, "set_resolution(%d) failed: %d", resolution, status);
        return;
    }

    status = vl53l8cx_start_ranging(&dev);
    if (status) {
        ESP_LOGE(TAG, "start_ranging failed: %d", status);
        return;
    }

    int res_label = (resolution == VL53L8CX_RESOLUTION_8X8) ? 8 : 4;
    ESP_LOGI(TAG, "Ranging at %dx%d for %lu ms", res_label, res_label, (unsigned long)duration_ms);

    VL53L8CX_ResultsData results;
    uint8_t data_ready = 0;
    TickType_t phase_start = xTaskGetTickCount();
    TickType_t phase_ticks = pdMS_TO_TICKS(duration_ms);

    while ((xTaskGetTickCount() - phase_start) < phase_ticks) {
        vl53l8cx_check_data_ready(&dev, &data_ready);
        if (data_ready) {
            vl53l8cx_get_ranging_data(&dev, &results);

            printf("TOF,%d,", res_label);
            for (int i = 0; i < num_zones; i++) {
                printf("%d", results.distance_mm[i]);
                if (i < num_zones - 1) printf(",");
            }
            printf("\n");
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void app_main(void) {
    // 1. Set up LPn pin FIRST, before I2C, so the sensor is in a known state
    lpn_gpio_init();

    // 2. Bring up I2C bus
    i2c_bus_init();

    // 3. Enable the sensor via LPn
    lpn_enable_sensor();

    dev.platform.address = VL53L8CX_ADDR;
    dev.platform.handle = tof_dev_handle;
    dev.platform.Write = esp_vl53l8cx_write;
    dev.platform.Read = esp_vl53l8cx_read;
    dev.platform.Wait = esp_vl53l8cx_wait;

    uint8_t status, is_alive;
    status = vl53l8cx_is_alive(&dev, &is_alive);
    if (!is_alive || status) {
        ESP_LOGE(TAG, "VL53L8CX not detected. status=%d", status);
        return;
    }
    ESP_LOGI(TAG, "VL53L8CX detected, starting init (uploads firmware, ~1-2s)...");

    status = vl53l8cx_init(&dev);
    if (status) {
        ESP_LOGE(TAG, "vl53l8cx_init failed: %d", status);
        return;
    }
    ESP_LOGI(TAG, "Init OK");

    vl53l8cx_set_ranging_frequency_hz(&dev, 15);

    // Phase 1: 4x4 for 20s (16 zones)
    run_resolution_phase(VL53L8CX_RESOLUTION_4X4, 16, 20000);

    // Phase 2: 8x8 for 20s (64 zones)
    run_resolution_phase(VL53L8CX_RESOLUTION_8X8, 64, 20000);

    // Done -> stop ranging and power down sensor via LPn
    ESP_LOGI(TAG, "Sequence complete. Stopping ranging and powering down via LPn.");
    vl53l8cx_stop_ranging(&dev);
    lpn_disable_sensor();

    // Idle forever with sensor powered off.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}