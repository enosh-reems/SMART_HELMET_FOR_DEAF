#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*visor_cmd_cb_t)(const char *cmd);

/**
 * @brief Initialize NimBLE BLE stack and start advertising as "SmartHelmet"
 * @param visor_cb Callback invoked when visor command ("OPEN", "CLOSE", "TOGGLE") is received
 */
esp_err_t ble_nav_init(visor_cmd_cb_t visor_cb);

/**
 * @brief Notify connected phone/PC about siren detection state
 */
void ble_nav_notify_siren(bool detected);

/**
 * @brief Notify connected phone/PC about visor open/closed state
 */
void ble_nav_notify_visor(bool is_open);

/**
 * @brief Send lightweight serial/telemetry log to connected phone/PC
 */
void ble_nav_send_log(const char *msg);

#ifdef __cplusplus
}
#endif
