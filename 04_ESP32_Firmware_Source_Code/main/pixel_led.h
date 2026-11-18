#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 3 Dedicated Hardware Output Pins for Independent WS2811 Modules:
 * Module 1 (Right):  GPIO 41 (3 LEDs)
 * Module 2 (Center): GPIO 40 (3 LEDs)
 * Module 3 (Left):   GPIO 39 (3 LEDs)
 */
#define PIXEL_RIGHT_GPIO   41
#define PIXEL_CENTER_GPIO  40
#define PIXEL_LEFT_GPIO    39

#define NUM_PIXEL_MODULES  3
#define LEDS_PER_MODULE    3
#define TOTAL_PIXEL_LEDS   (NUM_PIXEL_MODULES * LEDS_PER_MODULE)

typedef enum {
    NAV_STATE_IDLE = 0,
    NAV_STATE_RIGHT_100M,
    NAV_STATE_RIGHT_50M,
    NAV_STATE_RIGHT_TURN,
    NAV_STATE_LEFT_100M,
    NAV_STATE_LEFT_50M,
    NAV_STATE_LEFT_TURN,
    NAV_STATE_UTURN_LEFT_100M,
    NAV_STATE_UTURN_LEFT_50M,
    NAV_STATE_UTURN_LEFT_TURN,
    NAV_STATE_UTURN_RIGHT_100M,
    NAV_STATE_UTURN_RIGHT_50M,
    NAV_STATE_UTURN_RIGHT_TURN,
    NAV_STATE_SLIGHT_RIGHT_100M,
    NAV_STATE_SLIGHT_RIGHT_50M,
    NAV_STATE_SLIGHT_RIGHT_TURN,
    NAV_STATE_SLIGHT_LEFT_100M,
    NAV_STATE_SLIGHT_LEFT_50M,
    NAV_STATE_SLIGHT_LEFT_TURN,
    NAV_STATE_REROUTE
} nav_state_t;

/**
 * @brief Initialize WS2811 / WS2812 RMT driver and start animation task
 */
esp_err_t pixel_led_init(void);

/**
 * @brief Set current turn-by-turn navigation state
 */
void pixel_led_set_nav_state(nav_state_t state);

/**
 * @brief Set navigation state by string code and distance
 * @param turn_type "R", "L", "UR", "UL", "SR", "SL", "REROUTE", "CLEAR"
 * @param dist_m Distance in meters (e.g. 100, 50, 0)
 */
void pixel_led_set_nav_command(const char *turn_type, int dist_m);

typedef enum {
    SIREN_DIR_NONE = 0,
    SIREN_DIR_LEFT,   /* Left Mic detected siren -> Module 3 (Left, LEDs 6,7,8) blinks Red-Blue */
    SIREN_DIR_RIGHT,  /* Right Mic detected siren -> Module 1 (Right, LEDs 0,1,2) blinks Red-Blue */
    SIREN_DIR_BOTH    /* Both Mics detected siren -> Modules 1 & 3 blink Red-Blue */
} siren_direction_t;

/**
 * @brief Set directional emergency siren alert
 */
void pixel_led_set_siren_dir(siren_direction_t dir);

/**
 * @brief Get current siren alert direction
 */
siren_direction_t pixel_led_get_siren_dir(void);

/**
 * @brief Set emergency siren alert (backward-compatible: true sets SIREN_DIR_BOTH)
 */
void pixel_led_set_siren_alert(bool active);

/**
 * @brief Get whether any siren alert is currently active
 */
bool pixel_led_is_siren_alert(void);

#ifdef __cplusplus
}
#endif
