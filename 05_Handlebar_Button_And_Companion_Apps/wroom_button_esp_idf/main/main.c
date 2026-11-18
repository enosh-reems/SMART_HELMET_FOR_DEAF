#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#define TAG "HANDLEBAR_BTN"

/* Hardware Configuration:
 * Push button connected between GPIO 4 and GND (Internal pull-up enabled)
 * Status LED on GPIO 2 (Blinks on button transmit)
 */
#define BUTTON_GPIO GPIO_NUM_4
#define LED_GPIO    GPIO_NUM_2

static const uint8_t s_broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static void espnow_send_cb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status)
{
    (void)tx_info;
    ESP_LOGI(TAG, "Visor toggle packet send status: %s", (status == ESP_NOW_SEND_SUCCESS) ? "SUCCESS" : "FAIL");
}

void app_main(void)
{
    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  SMART HELMET HANDLEBAR BUTTON SENDER (ESP32)   ");
    ESP_LOGI(TAG, "==================================================");

    // 1. Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Configure GPIOs
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&btn_cfg);

    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&led_cfg);
    gpio_set_level(LED_GPIO, 0);

    // 3. Initialize Wi-Fi Station Mode on Channel 1
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));

    // 4. Initialize ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));

    esp_now_peer_info_t peer = {
        .channel = 1,
        .ifidx = WIFI_IF_STA,
        .encrypt = false
    };
    memcpy(peer.peer_addr, s_broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    /* Ensure pull-up is active */
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);

    ESP_LOGI(TAG, "Ready! Press button on GPIO 4 (connect to GND) to toggle helmet visor.");

    // 5. Button Polling with strict debouncing (only triggers when solidly held LOW for 50ms)
    bool button_was_pressed = false;

    while (1) {
        int level = gpio_get_level(BUTTON_GPIO);

        if (level == 0) {
            // Check if it stays LOW for 5 consecutive samples (50 ms) to filter noise/antenna effect
            int stable_count = 0;
            for (int i = 0; i < 5; i++) {
                vTaskDelay(pdMS_TO_TICKS(10));
                if (gpio_get_level(BUTTON_GPIO) == 0) {
                    stable_count++;
                }
            }

            if (stable_count == 5 && !button_was_pressed) {
                button_was_pressed = true;
                ESP_LOGI(TAG, ">>> Stable Button Press Detected! Sending TOGGLE... <<<");

                // Blink LED
                gpio_set_level(LED_GPIO, 1);

                const char *msg = "TOGGLE";
                esp_err_t err = esp_now_send(s_broadcast_mac, (const uint8_t *)msg, strlen(msg));
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "esp_now_send failed: %s", esp_err_to_name(err));
                }

                vTaskDelay(pdMS_TO_TICKS(60));
                gpio_set_level(LED_GPIO, 0);

                // Lockout / Debounce: Must wait at least 500ms before allowing another press
                vTaskDelay(pdMS_TO_TICKS(500));
            }
        } else {
            // Button is released (HIGH)
            button_was_pressed = false;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}
