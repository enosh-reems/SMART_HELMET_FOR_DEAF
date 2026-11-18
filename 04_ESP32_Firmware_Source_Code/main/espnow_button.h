#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*espnow_visor_cb_t)(const char *cmd);

/**
 * @brief Initialize ESP-NOW wireless receiver for bike handlebar button
 * @param cb Callback invoked when button command is received ("TOGGLE", "OPEN", "CLOSE")
 */
esp_err_t espnow_button_init(espnow_visor_cb_t cb);

#ifdef __cplusplus
}
#endif
