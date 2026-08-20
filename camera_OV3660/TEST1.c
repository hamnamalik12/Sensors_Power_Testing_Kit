//CAMERA IS INITILAIZED AND DEINITIALIZED, AFTER EVERY 5 SEC IT CAPTURES THE IMAGE AND CAMERA TURNS OFF. CURRENT 113mA with esp32s3

#include "esp_log.h"
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "xiao_camera_capture";

#define PWDN_GPIO_NUM     -1
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM     10
#define SIOD_GPIO_NUM     40
#define SIOC_GPIO_NUM     39
#define Y9_GPIO_NUM       48
#define Y8_GPIO_NUM       11
#define Y7_GPIO_NUM       12
#define Y6_GPIO_NUM       14
#define Y5_GPIO_NUM       16
#define Y4_GPIO_NUM       18
#define Y3_GPIO_NUM       17
#define Y2_GPIO_NUM       15
#define VSYNC_GPIO_NUM    38
#define HREF_GPIO_NUM     47
#define PCLK_GPIO_NUM     13

// Change this to control how long the camera stays "off" between captures
#define OFF_INTERVAL_MS   5000

static camera_config_t camera_config = {
    .pin_pwdn     = PWDN_GPIO_NUM,
    .pin_reset    = RESET_GPIO_NUM,
    .pin_xclk     = XCLK_GPIO_NUM,
    .pin_sccb_sda = SIOD_GPIO_NUM,
    .pin_sccb_scl = SIOC_GPIO_NUM,
    .pin_d7       = Y9_GPIO_NUM,
    .pin_d6       = Y8_GPIO_NUM,
    .pin_d5       = Y7_GPIO_NUM,
    .pin_d4       = Y6_GPIO_NUM,
    .pin_d3       = Y5_GPIO_NUM,
    .pin_d2       = Y4_GPIO_NUM,
    .pin_d1       = Y3_GPIO_NUM,
    .pin_d0       = Y2_GPIO_NUM,
    .pin_vsync    = VSYNC_GPIO_NUM,
    .pin_href     = HREF_GPIO_NUM,
    .pin_pclk     = PCLK_GPIO_NUM,
    .xclk_freq_hz = 20000000,
    .ledc_timer   = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    .pixel_format = PIXFORMAT_JPEG,
    .frame_size   = FRAMESIZE_VGA,
    .jpeg_quality = 12,
    .fb_count     = 2,
    .fb_location  = CAMERA_FB_IN_PSRAM,
    .grab_mode    = CAMERA_GRAB_WHEN_EMPTY,
    .sccb_i2c_port = -1,
};

extern "C" void app_main(void)
{
    int frame_count = 0;

    while (1) {
        // --- Turn camera "on": init driver ---
        esp_err_t err = esp_camera_init(&camera_config);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
            vTaskDelay(pdMS_TO_TICKS(OFF_INTERVAL_MS));
            continue;   // retry next cycle instead of getting stuck
        }

        sensor_t *s = esp_camera_sensor_get();
        if (s != NULL) {
            ESP_LOGI(TAG, "Camera PID: 0x%02x", s->id.PID);
            s->set_vflip(s, 1);
            s->set_hmirror(s, 1);
        }

        // --- Capture one frame ---
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
        } else {
            frame_count++;
            ESP_LOGI(TAG, "Pic captured! #%d, size: %u bytes, res: %dx%d",
                     frame_count, (unsigned)fb->len, fb->width, fb->height);
            esp_camera_fb_return(fb);
        }

        // --- Turn camera "off" (soft): deinit driver, free buffers, stop XCLK ---
        err = esp_camera_deinit();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Camera deinit warning: 0x%x", err);
        } else {
            ESP_LOGI(TAG, "Camera soft-off (deinitialized)");
        }

        // --- Stay "off" for OFF_INTERVAL_MS before waking up again ---
        vTaskDelay(pdMS_TO_TICKS(OFF_INTERVAL_MS));
    }
}