#include "pixel_led.h"
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/gpio.h"
#include "esp_log.h"

#define TAG "PIXEL_LED"

/* WS2811 Standard RGB byte order (0 = RGB, 1 = GRB) */
#define COLOR_ORDER_GRB 0

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb_color_t;

/* Comfort Brightness Palette (~35% intensity for eye safety and glare reduction) */
static const rgb_color_t COLOR_OFF    = {0, 0, 0};
static const rgb_color_t COLOR_RED    = {70, 0, 0};    // Soft Red
static const rgb_color_t COLOR_BLUE   = {0, 0, 75};    // Soft Blue
static const rgb_color_t COLOR_GREEN  = {0, 65, 0};    // Soft Green
static const rgb_color_t COLOR_ORANGE = {75, 30, 0};   // Soft Amber/Orange turn signal

/* 3 Separate Hardware RMT Channels & Encoders */
static rmt_channel_handle_t s_chan_right  = NULL; // Module 1: GPIO 39
static rmt_channel_handle_t s_chan_center = NULL; // Module 2: GPIO 40
static rmt_channel_handle_t s_chan_left   = NULL; // Module 3: GPIO 41

static rmt_encoder_handle_t s_encoder_right  = NULL;
static rmt_encoder_handle_t s_encoder_center = NULL;
static rmt_encoder_handle_t s_encoder_left   = NULL;

/* 3 LEDs per physical module */
static rgb_color_t s_pixels_right[LEDS_PER_MODULE];
static rgb_color_t s_pixels_center[LEDS_PER_MODULE];
static rgb_color_t s_pixels_left[LEDS_PER_MODULE];

static uint8_t s_raw_right[LEDS_PER_MODULE * 3];
static uint8_t s_raw_center[LEDS_PER_MODULE * 3];
static uint8_t s_raw_left[LEDS_PER_MODULE * 3];

static nav_state_t s_current_state = NAV_STATE_IDLE;
static siren_direction_t s_siren_dir = SIREN_DIR_NONE;
static inline bool is_siren_active(void) { return (s_siren_dir != SIREN_DIR_NONE); }
static SemaphoreHandle_t s_mutex = NULL;

#define RMT_RESOLUTION_HZ 10000000

static void encode_pixels(const rgb_color_t *pixels, uint8_t *raw_buf, int count)
{
    for (int i = 0; i < count; i++) {
#if COLOR_ORDER_GRB
        raw_buf[i * 3 + 0] = pixels[i].g;
        raw_buf[i * 3 + 1] = pixels[i].r;
        raw_buf[i * 3 + 2] = pixels[i].b;
#else
        raw_buf[i * 3 + 0] = pixels[i].r;
        raw_buf[i * 3 + 1] = pixels[i].g;
        raw_buf[i * 3 + 2] = pixels[i].b;
#endif
    }
}

static esp_err_t send_all_modules(void)
{
    if (!s_chan_right || !s_chan_center || !s_chan_left || 
        !s_encoder_right || !s_encoder_center || !s_encoder_left) {
        return ESP_ERR_INVALID_STATE;
    }

    encode_pixels(s_pixels_right, s_raw_right, LEDS_PER_MODULE);
    encode_pixels(s_pixels_center, s_raw_center, LEDS_PER_MODULE);
    encode_pixels(s_pixels_left, s_raw_left, LEDS_PER_MODULE);

    rmt_transmit_config_t tx_config = { .loop_count = 0 };

    rmt_transmit(s_chan_right, s_encoder_right, s_raw_right, sizeof(s_raw_right), &tx_config);
    rmt_transmit(s_chan_center, s_encoder_center, s_raw_center, sizeof(s_raw_center), &tx_config);
    rmt_transmit(s_chan_left, s_encoder_left, s_raw_left, sizeof(s_raw_left), &tx_config);

    rmt_tx_wait_all_done(s_chan_right, portMAX_DELAY);
    rmt_tx_wait_all_done(s_chan_center, portMAX_DELAY);
    rmt_tx_wait_all_done(s_chan_left, portMAX_DELAY);

    /* 80µs low latch signal to commit WS2811/WS2812 pixel data */
    esp_rom_delay_us(80);

    return ESP_OK;
}

static void clear_all_pixels(void)
{
    for (int i = 0; i < LEDS_PER_MODULE; i++) {
        s_pixels_right[i]  = COLOR_OFF;
        s_pixels_center[i] = COLOR_OFF;
        s_pixels_left[i]   = COLOR_OFF;
    }
}

static void set_right_module(rgb_color_t color)
{
    for (int i = 0; i < LEDS_PER_MODULE; i++) s_pixels_right[i] = color;
}

static void set_center_module(rgb_color_t color)
{
    for (int i = 0; i < LEDS_PER_MODULE; i++) s_pixels_center[i] = color;
}

static void set_left_module(rgb_color_t color)
{
    for (int i = 0; i < LEDS_PER_MODULE; i++) s_pixels_left[i] = color;
}

static inline rgb_color_t scale_color(rgb_color_t c, float scale)
{
    if (scale <= 0.0f) return COLOR_OFF;
    if (scale >= 1.0f) return c;
    rgb_color_t out = {
        .r = (uint8_t)(c.r * scale),
        .g = (uint8_t)(c.g * scale),
        .b = (uint8_t)(c.b * scale)
    };
    return out;
}

/* Directional Sequential Flow Wave across 3 LEDs in a module:
 * For direction < 0 (LEFT flow): sweeps 2 -> 1 -> 0
 * For direction > 0 (RIGHT flow): sweeps 0 -> 1 -> 2
 */
static void render_sequential_flow_3led(rgb_color_t *dest, rgb_color_t color, int direction, uint32_t tick)
{
    int step = (tick / 3) % 4; // 4-step sequential cycle (~105ms per step)
    
    for (int i = 0; i < LEDS_PER_MODULE; i++) {
        dest[i] = COLOR_OFF;
    }

    if (direction < 0) {
        // Sweeping towards LEFT: LED 2 -> LED 1 -> LED 0 -> Pause
        if (step == 0) {
            dest[2] = color;
        } else if (step == 1) {
            dest[2] = scale_color(color, 0.4f);
            dest[1] = color;
        } else if (step == 2) {
            dest[1] = scale_color(color, 0.4f);
            dest[0] = color;
        } else if (step == 3) {
            dest[0] = scale_color(color, 0.3f);
        }
    } else {
        // Sweeping towards RIGHT: LED 0 -> LED 1 -> LED 2 -> Pause
        if (step == 0) {
            dest[0] = color;
        } else if (step == 1) {
            dest[0] = scale_color(color, 0.4f);
            dest[1] = color;
        } else if (step == 2) {
            dest[1] = scale_color(color, 0.4f);
            dest[2] = color;
        } else if (step == 3) {
            dest[2] = scale_color(color, 0.3f);
        }
    }
}

/* Continuous 6-LED cascade across two modules (Center into Left or Right) */
static void render_sequential_cascade_6led(rgb_color_t *mod_start, rgb_color_t *mod_end, rgb_color_t color, int direction, uint32_t tick)
{
    int step = (tick / 2) % 8; // 8-step cascade
    for (int i = 0; i < LEDS_PER_MODULE; i++) {
        mod_start[i] = COLOR_OFF;
        mod_end[i]   = COLOR_OFF;
    }

    if (direction < 0) {
        // Center (2 -> 1 -> 0) into Left (2 -> 1 -> 0)
        if (step == 0) mod_start[2] = color;
        else if (step == 1) { mod_start[2] = scale_color(color, 0.4f); mod_start[1] = color; }
        else if (step == 2) { mod_start[1] = scale_color(color, 0.4f); mod_start[0] = color; }
        else if (step == 3) { mod_start[0] = scale_color(color, 0.4f); mod_end[2]   = color; }
        else if (step == 4) { mod_end[2]   = scale_color(color, 0.4f); mod_end[1]   = color; }
        else if (step == 5) { mod_end[1]   = scale_color(color, 0.4f); mod_end[0]   = color; }
        else if (step == 6) { mod_end[0]   = scale_color(color, 0.3f); }
    } else {
        // Center (0 -> 1 -> 2) into Right (0 -> 1 -> 2)
        if (step == 0) mod_start[0] = color;
        else if (step == 1) { mod_start[0] = scale_color(color, 0.4f); mod_start[1] = color; }
        else if (step == 2) { mod_start[1] = scale_color(color, 0.4f); mod_start[2] = color; }
        else if (step == 3) { mod_start[2] = scale_color(color, 0.4f); mod_end[0]   = color; }
        else if (step == 4) { mod_end[0]   = scale_color(color, 0.4f); mod_end[1]   = color; }
        else if (step == 5) { mod_end[1]   = scale_color(color, 0.4f); mod_end[2]   = color; }
        else if (step == 6) { mod_end[2]   = scale_color(color, 0.3f); }
    }
}

static void pixel_led_anim_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t tick = 0;

    while (1) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        clear_all_pixels();

        /* Navigation Animations Across 3 Dedicated Hardware Lines */
        switch (s_current_state) {
            /* IDLE STANDBY: Smooth subtle pilot breathing glow so rider knows system is active */
            case NAV_STATE_IDLE:
                {
                    uint8_t breath = (uint8_t)(10 + 8 * sinf(tick * 0.08f));
                    rgb_color_t standby_col = { .r = 0, .g = (uint8_t)(breath / 2), .b = breath };
                    if (!is_siren_active()) {
                        set_center_module(standby_col);
                    }
                }
                break;

            /* RIGHT TURN (Module 1 - GPIO 39) */
            case NAV_STATE_RIGHT_100M:
                /* Gentle slow pulse Blue at 100m */
                if ((tick / 8) % 2 == 0) {
                    set_right_module(COLOR_BLUE);
                }
                break;

            case NAV_STATE_RIGHT_50M:
                /* Flowing wave Green at 50m */
                render_sequential_flow_3led(s_pixels_right, COLOR_GREEN, +1, tick);
                break;

            case NAV_STATE_RIGHT_TURN:
                /* Smooth continuous flowing Amber wave sweeping right (0 -> 1 -> 2) */
                render_sequential_flow_3led(s_pixels_right, COLOR_ORANGE, +1, tick);
                break;

            /* LEFT TURN (Module 3 - GPIO 41) */
            case NAV_STATE_LEFT_100M:
                /* Gentle slow pulse Blue at 100m */
                if ((tick / 8) % 2 == 0) {
                    set_left_module(COLOR_BLUE);
                }
                break;

            case NAV_STATE_LEFT_50M:
                /* Flowing wave Green at 50m */
                render_sequential_flow_3led(s_pixels_left, COLOR_GREEN, -1, tick);
                break;

            case NAV_STATE_LEFT_TURN:
                /* Smooth continuous flowing Amber wave sweeping left (2 -> 1 -> 0) */
                render_sequential_flow_3led(s_pixels_left, COLOR_ORANGE, -1, tick);
                break;

            /* LEFT U-TURN (Center Module 2 & Left Module 3) */
            case NAV_STATE_UTURN_LEFT_100M:
                if ((tick / 8) % 2 == 0) {
                    if (!is_siren_active()) set_center_module(COLOR_BLUE);
                    set_left_module(COLOR_BLUE);
                }
                break;

            case NAV_STATE_UTURN_LEFT_50M:
                render_sequential_cascade_6led(s_pixels_center, s_pixels_left, COLOR_GREEN, -1, tick);
                break;

            case NAV_STATE_UTURN_LEFT_TURN:
                /* Continuous 6-pixel cascade sweeping from Center into Left */
                render_sequential_cascade_6led(s_pixels_center, s_pixels_left, COLOR_ORANGE, -1, tick);
                break;

            /* RIGHT U-TURN (Center Module 2 & Right Module 1) */
            case NAV_STATE_UTURN_RIGHT_100M:
                if ((tick / 8) % 2 == 0) {
                    set_right_module(COLOR_BLUE);
                    if (!is_siren_active()) set_center_module(COLOR_BLUE);
                }
                break;

            case NAV_STATE_UTURN_RIGHT_50M:
                render_sequential_cascade_6led(s_pixels_center, s_pixels_right, COLOR_GREEN, +1, tick);
                break;

            case NAV_STATE_UTURN_RIGHT_TURN:
                /* Continuous 6-pixel cascade sweeping from Center into Right */
                render_sequential_cascade_6led(s_pixels_center, s_pixels_right, COLOR_ORANGE, +1, tick);
                break;

            /* SLIGHT RIGHT (Center Module 2 - GPIO 40) */
            case NAV_STATE_SLIGHT_RIGHT_100M:
                if (!is_siren_active() && (tick / 8) % 2 == 0) {
                    set_center_module(COLOR_BLUE);
                }
                break;

            case NAV_STATE_SLIGHT_RIGHT_50M:
                if (!is_siren_active()) {
                    render_sequential_flow_3led(s_pixels_center, COLOR_GREEN, +1, tick);
                }
                break;

            case NAV_STATE_SLIGHT_RIGHT_TURN:
                if (!is_siren_active()) {
                    render_sequential_flow_3led(s_pixels_center, COLOR_ORANGE, +1, tick);
                }
                break;

            /* SLIGHT LEFT (Center Module 2 - GPIO 40) */
            case NAV_STATE_SLIGHT_LEFT_100M:
                if (!is_siren_active() && (tick / 8) % 2 == 0) {
                    set_center_module(COLOR_BLUE);
                }
                break;

            case NAV_STATE_SLIGHT_LEFT_50M:
                if (!is_siren_active()) {
                    render_sequential_flow_3led(s_pixels_center, COLOR_GREEN, -1, tick);
                }
                break;

            case NAV_STATE_SLIGHT_LEFT_TURN:
                if (!is_siren_active()) {
                    render_sequential_flow_3led(s_pixels_center, COLOR_ORANGE, -1, tick);
                }
                break;

            /* RE-ROUTING (All 3 separate modules pulse RED in unison) */
            case NAV_STATE_REROUTE:
                if ((tick / 7) % 2 == 0) {
                    set_right_module(COLOR_RED);
                    if (!is_siren_active()) set_center_module(COLOR_RED);
                    set_left_module(COLOR_RED);
                }
                break;
        }

        /* Directional Siren Emergency Strobe */
        if (s_siren_dir != SIREN_DIR_NONE) {
            bool strobe_phase = ((tick / 4) % 2 == 0);
            rgb_color_t color = strobe_phase ? COLOR_RED : COLOR_BLUE;

            if (s_siren_dir == SIREN_DIR_LEFT || s_siren_dir == SIREN_DIR_BOTH) {
                set_left_module(color);
            }
            if (s_siren_dir == SIREN_DIR_RIGHT || s_siren_dir == SIREN_DIR_BOTH) {
                set_right_module(color);
            }
            if (s_siren_dir == SIREN_DIR_BOTH) {
                set_center_module(strobe_phase ? COLOR_BLUE : COLOR_RED);
            }
        }

        send_all_modules();
        xSemaphoreGive(s_mutex);

        tick++;
        vTaskDelay(pdMS_TO_TICKS(35)); // ~28 FPS smooth animation
    }
}

esp_err_t pixel_led_init(void)
{
    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "Initializing 3 Independent WS2811 RMT Channels: Right=GPIO%d, Center=GPIO%d, Left=GPIO%d...",
             PIXEL_RIGHT_GPIO, PIXEL_CENTER_GPIO, PIXEL_LEFT_GPIO);

    /* 1. Encoder Config for WS2811 / WS2812B universal timings (10 MHz, 1 tick = 100ns) */
    rmt_bytes_encoder_config_t encoder_cfg = {
        .bit0 = {
            .level0 = 1,
            .duration0 = 3,  // 300ns HIGH
            .level1 = 0,
            .duration1 = 9,  // 900ns LOW
        },
        .bit1 = {
            .level0 = 1,
            .duration0 = 9,  // 900ns HIGH
            .level1 = 0,
            .duration1 = 3,  // 300ns LOW
        },
        .flags.msb_first = 1
    };
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&encoder_cfg, &s_encoder_right));
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&encoder_cfg, &s_encoder_center));
    ESP_ERROR_CHECK(rmt_new_bytes_encoder(&encoder_cfg, &s_encoder_left));

    /* 2. Configure Channel Right (GPIO 39) */
    rmt_tx_channel_config_t cfg_r = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = (gpio_num_t)PIXEL_RIGHT_GPIO,
        .mem_block_symbols = 48,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&cfg_r, &s_chan_right));
    gpio_set_drive_capability((gpio_num_t)PIXEL_RIGHT_GPIO, GPIO_DRIVE_CAP_3);
    ESP_ERROR_CHECK(rmt_enable(s_chan_right));

    /* 3. Configure Channel Center (GPIO 40) */
    rmt_tx_channel_config_t cfg_c = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = (gpio_num_t)PIXEL_CENTER_GPIO,
        .mem_block_symbols = 48,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&cfg_c, &s_chan_center));
    gpio_set_drive_capability((gpio_num_t)PIXEL_CENTER_GPIO, GPIO_DRIVE_CAP_3);
    ESP_ERROR_CHECK(rmt_enable(s_chan_center));

    /* 4. Configure Channel Left (GPIO 41) */
    rmt_tx_channel_config_t cfg_l = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = (gpio_num_t)PIXEL_LEFT_GPIO,
        .mem_block_symbols = 48,
        .resolution_hz = RMT_RESOLUTION_HZ,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&cfg_l, &s_chan_left));
    gpio_set_drive_capability((gpio_num_t)PIXEL_LEFT_GPIO, GPIO_DRIVE_CAP_3);
    ESP_ERROR_CHECK(rmt_enable(s_chan_left));

    /* POWER-ON SELF-TEST (POST):
     * 1. Illuminates each separate GPIO line in sequence:
     * Module 1 (Right  - GPIO 39): Soft RED
     * Module 2 (Center - GPIO 40): Soft GREEN
     * Module 3 (Left   - GPIO 41): Soft BLUE
     */
    ESP_LOGI(TAG, "Running 3-Pin POST: Right(Red), Center(Green), Left(Blue)...");
    set_right_module(COLOR_RED);
    set_center_module(COLOR_GREEN);
    set_left_module(COLOR_BLUE);
    send_all_modules();
    vTaskDelay(pdMS_TO_TICKS(600));

    /* 2. Sequential Flow Startup Preview across all 3 modules */
    ESP_LOGI(TAG, "Running Sequential Flow Startup Test...");
    for (int t = 0; t < 24; t++) {
        clear_all_pixels();
        render_sequential_flow_3led(s_pixels_left, COLOR_ORANGE, -1, t);
        render_sequential_flow_3led(s_pixels_center, COLOR_GREEN, +1, t);
        render_sequential_flow_3led(s_pixels_right, COLOR_ORANGE, +1, t);
        send_all_modules();
        vTaskDelay(pdMS_TO_TICKS(35));
    }

    clear_all_pixels();
    send_all_modules();

    BaseType_t res = xTaskCreatePinnedToCore(
        pixel_led_anim_task,
        "pixel_anim",
        3072,
        NULL,
        2,
        NULL,
        1
    );

    ESP_LOGI(TAG, "3-Channel WS2811 System Ready (Right=GPIO39, Center=GPIO40, Left=GPIO41).");
    return (res == pdPASS) ? ESP_OK : ESP_FAIL;
}

void pixel_led_set_nav_state(nav_state_t state)
{
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_current_state = state;
        xSemaphoreGive(s_mutex);
    }
}

void pixel_led_set_nav_command(const char *turn_type, int dist_m)
{
    if (!turn_type) return;

    nav_state_t state = NAV_STATE_IDLE;

    if (strcmp(turn_type, "REROUTE") == 0) {
        state = NAV_STATE_REROUTE;
    } else if (strcmp(turn_type, "CLEAR") == 0 || strcmp(turn_type, "IDLE") == 0) {
        state = NAV_STATE_IDLE;
    } else if (strcmp(turn_type, "R") == 0) {
        if (dist_m > 70) state = NAV_STATE_RIGHT_100M;
        else if (dist_m > 20) state = NAV_STATE_RIGHT_50M;
        else state = NAV_STATE_RIGHT_TURN;
    } else if (strcmp(turn_type, "L") == 0) {
        if (dist_m > 70) state = NAV_STATE_LEFT_100M;
        else if (dist_m > 20) state = NAV_STATE_LEFT_50M;
        else state = NAV_STATE_LEFT_TURN;
    } else if (strcmp(turn_type, "UR") == 0) {
        if (dist_m > 70) state = NAV_STATE_UTURN_RIGHT_100M;
        else if (dist_m > 20) state = NAV_STATE_UTURN_RIGHT_50M;
        else state = NAV_STATE_UTURN_RIGHT_TURN;
    } else if (strcmp(turn_type, "UL") == 0) {
        if (dist_m > 70) state = NAV_STATE_UTURN_LEFT_100M;
        else if (dist_m > 20) state = NAV_STATE_UTURN_LEFT_50M;
        else state = NAV_STATE_UTURN_LEFT_TURN;
    } else if (strcmp(turn_type, "SR") == 0) {
        if (dist_m > 70) state = NAV_STATE_SLIGHT_RIGHT_100M;
        else if (dist_m > 20) state = NAV_STATE_SLIGHT_RIGHT_50M;
        else state = NAV_STATE_SLIGHT_RIGHT_TURN;
    } else if (strcmp(turn_type, "SL") == 0) {
        if (dist_m > 70) state = NAV_STATE_SLIGHT_LEFT_100M;
        else if (dist_m > 20) state = NAV_STATE_SLIGHT_LEFT_50M;
        else state = NAV_STATE_SLIGHT_LEFT_TURN;
    }

    ESP_LOGI(TAG, "Nav command '%s' (dist=%dm) -> state %d", turn_type, dist_m, state);
    pixel_led_set_nav_state(state);
}

void pixel_led_set_siren_dir(siren_direction_t dir)
{
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        if (s_siren_dir != dir) {
            s_siren_dir = dir;
            const char *name = "NONE";
            if (dir == SIREN_DIR_LEFT) name = "LEFT (Module 3 Red-Blue Strobe)";
            else if (dir == SIREN_DIR_RIGHT) name = "RIGHT (Module 1 Red-Blue Strobe)";
            else if (dir == SIREN_DIR_BOTH) name = "BOTH (Modules 1 & 3 Red-Blue Strobe)";
            ESP_LOGW(TAG, "Siren Visual Alert: %s", name);
        }
        xSemaphoreGive(s_mutex);
    }
}

siren_direction_t pixel_led_get_siren_dir(void)
{
    return s_siren_dir;
}

void pixel_led_set_siren_alert(bool active)
{
    pixel_led_set_siren_dir(active ? SIREN_DIR_BOTH : SIREN_DIR_NONE);
}

bool pixel_led_is_siren_alert(void)
{
    return (s_siren_dir != SIREN_DIR_NONE);
}
