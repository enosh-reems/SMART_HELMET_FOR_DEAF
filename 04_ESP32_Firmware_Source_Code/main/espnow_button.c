#include "espnow_button.h"
#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_timer.h"

#define TAG "ESPNOW_BTN"

static espnow_visor_cb_t s_visor_cb = NULL;
static int64_t s_last_packet_time = 0;

static void espnow_recv_callback(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len)
{
    (void)recv_info;
    if (data == NULL || data_len <= 0) return;

    int64_t now = esp_timer_get_time(); // microseconds
    if (now - s_last_packet_time < 1000000) { // 1 second cooldown between wireless triggers
        ESP_LOGW(TAG, "Ignoring rapid duplicate wireless packet (cooldown)");
        return;
    }
    s_last_packet_time = now;

    char cmd[32];
    int len = (data_len < (int)sizeof(cmd) - 1) ? data_len : (sizeof(cmd) - 1);
    memcpy(cmd, data, len);
    cmd[len] = '\0';

    ESP_LOGI(TAG, "Wireless button packet received: '%s'", cmd);

    if (s_visor_cb) {
        s_visor_cb(cmd);
    }
}

esp_err_t espnow_button_init(espnow_visor_cb_t cb)
{
    s_visor_cb = cb;

    /* Initialize TCP/IP and Event Loop if not already initialized */
    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    /* Set Wi-Fi channel 1 */
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_callback));

    ESP_LOGI(TAG, "ESP-NOW Handlebar Button receiver ready on Channel 1.");
    return ESP_OK;
}
