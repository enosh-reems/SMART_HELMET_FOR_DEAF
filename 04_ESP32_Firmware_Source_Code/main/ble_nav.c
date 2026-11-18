#include "ble_nav.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "pixel_led.h"

#define TAG "BLE_NAV"

#define DEVICE_NAME "SmartHelmet"

/* 128-bit UUIDs:
 * Service: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
 * Nav Char: beb5483e-36e1-4688-b7f5-ea07361b26a8
 * Visor Char: beb5483e-36e1-4688-b7f5-ea07361b26a9
 * Siren Char: beb5483e-36e1-4688-b7f5-ea07361b26aa
 */
static const ble_uuid128_t s_svc_uuid =
    BLE_UUID128_INIT(0x4b, 0x91, 0x31, 0xc3, 0xc9, 0xc5, 0xcc, 0x8f,
                     0x9e, 0x45, 0xb5, 0x1f, 0x01, 0xc2, 0xaf, 0x4f);

static const ble_uuid128_t s_chr_nav_uuid =
    BLE_UUID128_INIT(0xa8, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
                     0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);

static const ble_uuid128_t s_chr_visor_uuid =
    BLE_UUID128_INIT(0xa9, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
                     0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);

static const ble_uuid128_t s_chr_siren_uuid =
    BLE_UUID128_INIT(0xaa, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
                     0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);

static const ble_uuid128_t s_chr_telemetry_uuid =
    BLE_UUID128_INIT(0xab, 0x26, 0x1b, 0x36, 0x07, 0xea, 0xf5, 0xb7,
                     0x88, 0x46, 0xe1, 0x36, 0x3e, 0x48, 0xb5, 0xbe);

static uint16_t s_siren_val_handle = 0;
static uint16_t s_visor_val_handle = 0;
static uint16_t s_telemetry_val_handle = 0;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static visor_cmd_cb_t s_visor_cb = NULL;
static bool s_last_visor_open = false;

static int gatt_chr_access_nav(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        char buf[64];
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        os_mbuf_copydata(ctxt->om, 0, len, buf);
        buf[len] = '\0';

        ESP_LOGI(TAG, "Received BLE Nav Command: %s", buf);

        /* Format: <TURN_TYPE>,<DISTANCE> e.g. "R,100" or "REROUTE" */
        char turn[16] = {0};
        int dist = 0;
        char *comma = strchr(buf, ',');
        if (comma) {
            *comma = '\0';
            strncpy(turn, buf, sizeof(turn) - 1);
            dist = atoi(comma + 1);
        } else {
            strncpy(turn, buf, sizeof(turn) - 1);
        }

        pixel_led_set_nav_command(turn, dist);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int gatt_chr_access_visor(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t st = s_last_visor_open ? 1 : 0;
        os_mbuf_append(ctxt->om, &st, sizeof(st));
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        char buf[32];
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        os_mbuf_copydata(ctxt->om, 0, len, buf);
        buf[len] = '\0';

        ESP_LOGI(TAG, "Received BLE Visor Command: %s", buf);
        if (s_visor_cb) {
            s_visor_cb(buf);
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int gatt_chr_access_siren(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t alert = pixel_led_is_siren_alert() ? 1 : 0;
        os_mbuf_append(ctxt->om, &alert, sizeof(alert));
        return 0;
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        char buf[32];
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        os_mbuf_copydata(ctxt->om, 0, len, buf);
        buf[len] = '\0';
        ESP_LOGI(TAG, "Received BLE Siren Command: %s", buf);
        if (strcasecmp(buf, "OFF") == 0 || strcasecmp(buf, "0") == 0 || strcasecmp(buf, "CLEAR") == 0) {
            pixel_led_set_siren_dir(SIREN_DIR_NONE);
            ble_nav_notify_siren(false);
            ESP_LOGI(TAG, "Siren Cleared / Turned OFF via BLE");
        }
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int gatt_chr_access_telemetry(uint16_t conn_handle, uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const char *heartbeat = "OK";
        os_mbuf_append(ctxt->om, heartbeat, strlen(heartbeat));
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &s_chr_nav_uuid.u,
                .access_cb = gatt_chr_access_nav,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &s_chr_visor_uuid.u,
                .access_cb = gatt_chr_access_visor,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_visor_val_handle,
            },
            {
                .uuid = &s_chr_siren_uuid.u,
                .access_cb = gatt_chr_access_siren,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_siren_val_handle,
            },
            {
                .uuid = &s_chr_telemetry_uuid.u,
                .access_cb = gatt_chr_access_telemetry,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_telemetry_val_handle,
            },
            {0}
        },
    },
    {0}
};

static void ble_advertise(void);

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "BLE Client Connected (handle=%d)", s_conn_handle);
            } else {
                s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
                ble_advertise();
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE Client Disconnected");
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ble_advertise();
            break;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            ble_advertise();
            break;

        default:
            break;
    }
    return 0;
}

static void ble_advertise(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields adv_fields;
    struct ble_hs_adv_fields rsp_fields;

    /* Advertising Data (Flags + Device Name) = ~16 bytes (< 31 bytes) */
    memset(&adv_fields, 0, sizeof(adv_fields));
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.name = (uint8_t *)DEVICE_NAME;
    adv_fields.name_len = strlen(DEVICE_NAME);
    adv_fields.name_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv fields: rc=%d", rc);
        return;
    }

    /* Scan Response Data (128-bit Service UUID) = ~18 bytes (< 31 bytes) */
    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.uuids128 = (ble_uuid128_t *)&s_svc_uuid;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set scan rsp fields: rc=%d", rc);
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising: rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising as '%s'...", DEVICE_NAME);
    }
}

static void on_sync(void)
{
    ESP_LOGI(TAG, "NimBLE host synched.");
    ble_advertise();
}

static void nimble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "NimBLE host task started.");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_nav_init(visor_cmd_cb_t visor_cb)
{
    s_visor_cb = visor_cb;

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init nimble port: %d", ret);
        return ret;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to count GATT svcs: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to add GATT svcs: %d", rc);
        return ESP_FAIL;
    }

    ble_svc_gap_device_name_set(DEVICE_NAME);

    ble_hs_cfg.sync_cb = on_sync;

    nimble_port_freertos_init(nimble_host_task);

    ESP_LOGI(TAG, "BLE Nav & Visor Service initialized successfully.");
    return ESP_OK;
}

void ble_nav_notify_siren(bool detected)
{
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE && s_siren_val_handle != 0) {
        uint8_t alert = detected ? (uint8_t)pixel_led_get_siren_dir() : 0;
        if (detected && alert == 0) alert = 1;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(&alert, sizeof(alert));
        if (om) {
            ble_gatts_notify_custom(s_conn_handle, s_siren_val_handle, om);
        }
    }
}

void ble_nav_notify_visor(bool is_open)
{
    s_last_visor_open = is_open;
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE && s_visor_val_handle != 0) {
        uint8_t st = is_open ? 1 : 0;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(&st, sizeof(st));
        if (om) {
            ble_gatts_notify_custom(s_conn_handle, s_visor_val_handle, om);
        }
    }
}

void ble_nav_send_log(const char *msg)
{
    if (msg == NULL) return;
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE && s_telemetry_val_handle != 0) {
        size_t len = strlen(msg);
        if (len > 120) len = 120;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, len);
        if (om) {
            ble_gatts_notify_custom(s_conn_handle, s_telemetry_val_handle, om);
        }
    }
}

