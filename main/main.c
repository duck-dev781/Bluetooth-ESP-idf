#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_bt_defs.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "driver/i2s.h"

static const char *TAG = "BT_SPEAKER";

/*
 * Freenove FNK0047 PCM5102A module wiring:
 * SCK/MCLK -> GPIO22, BCK -> GPIO26, DIN -> GPIO25, LCK/WS -> GPIO27.
 * If your module's silkscreen or Freenove diagram differs, change these pins.
 */
#define I2S_PORT       I2S_NUM_0
#define I2S_MCLK_PIN   GPIO_NUM_22
#define I2S_BCLK_PIN   GPIO_NUM_26
#define I2S_DOUT_PIN   GPIO_NUM_25
#define I2S_LRCK_PIN   GPIO_NUM_27

static SemaphoreHandle_t s_i2s_mutex;
static volatile bool s_i2s_ready = false;
static volatile uint32_t s_sample_rate = 44100;

static esp_err_t audio_i2s_init(uint32_t sample_rate)
{
    if (s_i2s_ready) {
        i2s_driver_uninstall(I2S_PORT);
        s_i2s_ready = false;
    }

    i2s_config_t cfg = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = sample_rate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S driver install failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_pin_config_t pins = {
        .mck_io_num = I2S_MCLK_PIN,
        .bck_io_num = I2S_BCLK_PIN,
        .ws_io_num = I2S_LRCK_PIN,
        .data_out_num = I2S_DOUT_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    err = i2s_set_pin(I2S_PORT, &pins);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S pin setup failed: %s", esp_err_to_name(err));
        i2s_driver_uninstall(I2S_PORT);
        return err;
    }

    err = i2s_set_clk(I2S_PORT, sample_rate,
                      I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S clock setup failed: %s", esp_err_to_name(err));
        i2s_driver_uninstall(I2S_PORT);
        return err;
    }

    s_sample_rate = sample_rate;
    s_i2s_ready = true;
    ESP_LOGI(TAG, "I2S ready: %" PRIu32 " Hz, 16-bit stereo", sample_rate);
    return ESP_OK;
}

static void bt_a2d_audio_data_cb(const uint8_t *data, uint32_t len)
{
    if (!s_i2s_ready || data == NULL || len == 0) {
        return;
    }

    size_t bytes_written = 0;
    esp_err_t err = i2s_write(I2S_PORT, data, len, &bytes_written, 0);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        ESP_LOGD(TAG, "I2S write: %s", esp_err_to_name(err));
    }
}

static void bt_a2d_connection_state_cb(esp_a2d_connection_state_t state,
                                        void *param)
{
    (void)param;
    switch (state) {
    case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
        ESP_LOGI(TAG, "Bluetooth audio disconnected");
        break;
    case ESP_A2D_CONNECTION_STATE_CONNECTING:
        ESP_LOGI(TAG, "Connecting to Bluetooth audio source...");
        break;
    case ESP_A2D_CONNECTION_STATE_CONNECTED:
        ESP_LOGI(TAG, "Bluetooth audio connected");
        break;
    case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
        ESP_LOGI(TAG, "Bluetooth audio disconnecting");
        break;
    default:
        ESP_LOGI(TAG, "Bluetooth connection state: %d", (int)state);
        break;
    }
}

static void bt_a2d_audio_state_cb(esp_a2d_audio_state_t state, void *param)
{
    (void)param;
    if (state == ESP_A2D_AUDIO_STATE_STARTED) {
        ESP_LOGI(TAG, "Audio streaming started");
    } else if (state == ESP_A2D_AUDIO_STATE_STOPPED) {
        ESP_LOGI(TAG, "Audio streaming stopped");
    } else if (state == ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND) {
        ESP_LOGI(TAG, "Audio suspended by source");
    } else {
        ESP_LOGI(TAG, "Audio state: %d", (int)state);
    }
}

static void bt_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    (void)param;
    if (event == ESP_BT_GAP_AUTH_CMPL_EVT) {
        ESP_LOGI(TAG, "Bluetooth pairing completed");
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_i2s_mutex = xSemaphoreCreateMutex();
    if (s_i2s_mutex == NULL) {
        ESP_LOGE(TAG, "Could not create I2S mutex");
        return;
    }

    ESP_ERROR_CHECK(audio_i2s_init(s_sample_rate));

    /* This target must use Classic Bluetooth for A2DP. */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_bt_gap_register_callback(bt_gap_cb));
    ESP_ERROR_CHECK(esp_bt_gap_set_device_name("FNK0047 Bluetooth Speaker"));
    ESP_ERROR_CHECK(esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE));

    ESP_ERROR_CHECK(esp_a2d_register_callback(bt_a2d_connection_state_cb));
    ESP_ERROR_CHECK(esp_a2d_sink_register_data_callback(bt_a2d_audio_data_cb));
    ESP_ERROR_CHECK(esp_a2d_sink_init());
    ESP_ERROR_CHECK(esp_a2d_sink_register_audio_state_callback(bt_a2d_audio_state_cb));

    const uint8_t *mac = esp_bt_dev_get_address();
    if (mac != NULL) {
        ESP_LOGI(TAG, "Bluetooth speaker ready; device address %02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    ESP_LOGI(TAG, "Pair Chromebook with: FNK0047 Bluetooth Speaker");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
