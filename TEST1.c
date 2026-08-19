//8x8 grid => downsample, max freq 15Hz, default distance 3000cm, current 75mA

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

    // Hold sensor in reset/disabled state initially
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

// Downsample the real 8x8 zone grid into a virtual grid_size x grid_size grid
// by block-averaging. grid_size ranges 1..8. Works for non-integer divisions too.
static void downsample_8x8_to_grid(const int16_t *src_8x8, int grid_size, int16_t *out) {
    for (int r = 0; r < grid_size; r++) {
        int row_start = (r * 8) / grid_size;
        int row_end   = ((r + 1) * 8) / grid_size;
        if (row_end <= row_start) row_end = row_start + 1;

        for (int c = 0; c < grid_size; c++) {
            int col_start = (c * 8) / grid_size;
            int col_end   = ((c + 1) * 8) / grid_size;
            if (col_end <= col_start) col_end = col_start + 1;

            int32_t sum = 0;
            int count = 0;
            for (int rr = row_start; rr < row_end && rr < 8; rr++) {
                for (int cc = col_start; cc < col_end && cc < 8; cc++) {
                    sum += src_8x8[rr * 8 + cc];
                    count++;
                }
            }
            out[r * grid_size + c] = (count > 0) ? (int16_t)(sum / count) : 0;
        }
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

    // Sensor always physically ranges at real 8x8 — virtual grid sizes below
    // are software downsamples of this real data.
    vl53l8cx_set_resolution(&dev, VL53L8CX_RESOLUTION_8X8);
    vl53l8cx_set_ranging_frequency_hz(&dev, 15);

    status = vl53l8cx_start_ranging(&dev);
    if (status) {
        ESP_LOGE(TAG, "start_ranging failed: %d", status);
        return;
    }
    ESP_LOGI(TAG, "Ranging started (real 8x8)");

    VL53L8CX_ResultsData results;
    uint8_t data_ready = 0;

    const TickType_t PHASE_DURATION_TICKS = pdMS_TO_TICKS(20000);
    int16_t grid_buf[64]; // max size needed (8x8)

    for (int grid_size = 1; grid_size <= 8; grid_size++) {
        ESP_LOGI(TAG, "Virtual grid size: %dx%d (20s)", grid_size, grid_size);
        TickType_t phase_start = xTaskGetTickCount();

        while ((xTaskGetTickCount() - phase_start) < PHASE_DURATION_TICKS) {
            vl53l8cx_check_data_ready(&dev, &data_ready);
            if (data_ready) {
                vl53l8cx_get_ranging_data(&dev, &results);

                downsample_8x8_to_grid(results.distance_mm, grid_size, grid_buf);

                int n = grid_size * grid_size;
                printf("TOF,%d,", grid_size);
                for (int i = 0; i < n; i++) {
                    printf("%d", grid_buf[i]);
                    if (i < n - 1) printf(",");
                }
                printf("\n");
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    // Reached real 8x8 and its 20s phase is done -> power down sensor
    ESP_LOGI(TAG, "Sequence complete. Stopping ranging and powering down via LPn.");
    vl53l8cx_stop_ranging(&dev);
    lpn_disable_sensor();

    // Idle forever (sensor is off). Change this if you want the whole
    // 1x1..8x8 sequence to repeat automatically.
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}