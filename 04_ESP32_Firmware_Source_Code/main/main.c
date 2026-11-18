#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2s_std.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_err.h"

#include "siren_detector.h"
#include "pixel_led.h"
#include "ble_nav.h"
#include "espnow_button.h"

#define TAG "SMART_HELMET"

#define SERVO1_GPIO 15
#define SERVO2_GPIO 16

#define SERVO_TIMER LEDC_TIMER_0
#define SERVO_MODE LEDC_LOW_SPEED_MODE
#define SERVO1_CHANNEL LEDC_CHANNEL_0
#define SERVO2_CHANNEL LEDC_CHANNEL_1
#define SERVO_FREQUENCY 50
#define SERVO_RESOLUTION LEDC_TIMER_14_BIT

#define SERVO1_CLOSE_ANGLE  30
#define SERVO1_OPEN_ANGLE   45   /* Exactly +15° restricted lift */

#define SERVO2_CLOSE_ANGLE  150
#define SERVO2_OPEN_ANGLE   135  /* Exactly -15° restricted lift */

static int s_visor_stroke_deg   = 15; /* 15° total travel stroke */
static int s_servo1_close_angle = SERVO1_CLOSE_ANGLE;
static int s_servo1_open_angle  = SERVO1_OPEN_ANGLE;
static int s_servo2_close_angle = SERVO2_CLOSE_ANGLE;
static int s_servo2_open_angle  = SERVO2_OPEN_ANGLE;

static int g_current_angle1 = SERVO1_CLOSE_ANGLE;
static int g_current_angle2 = SERVO2_CLOSE_ANGLE;
static bool g_servos_attached = false;
static bool s_visor_open = false;

/* =========================================================
 * SERVO CONTROL FUNCTIONS (Anti-Jam PWM Detach & Smooth Slew)
 * ========================================================= */

static void servos_detach(void)
{
    /* Setting duty to 0 completely stops PWM pulses while keeping LEDC channel configured */
    ledc_set_duty(SERVO_MODE, SERVO1_CHANNEL, 0);
    ledc_update_duty(SERVO_MODE, SERVO1_CHANNEL);
    ledc_set_duty(SERVO_MODE, SERVO2_CHANNEL, 0);
    ledc_update_duty(SERVO_MODE, SERVO2_CHANNEL);
    g_servos_attached = false;
    ESP_LOGI(TAG, "Servos DETACHED (PWM disabled, 0mA stall current, gears relaxed)");
}

static void servos_attach(void)
{
    g_servos_attached = true;
    ESP_LOGI(TAG, "Servos ATTACHED (PWM active)");
}

static void servo1_set_angle(int angle)
{
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    g_current_angle1 = angle;

    const uint32_t max_duty = (1 << 14) - 1;
    const uint32_t pulse_us = 500 + ((uint32_t)angle * 2000) / 180;
    const uint32_t duty = (pulse_us * max_duty) / 20000;

    ledc_set_duty(SERVO_MODE, SERVO1_CHANNEL, duty);
    ledc_update_duty(SERVO_MODE, SERVO1_CHANNEL);
}

static void servo2_set_angle(int angle)
{
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    g_current_angle2 = angle;

    const uint32_t max_duty = (1 << 14) - 1;
    const uint32_t pulse_us = 500 + ((uint32_t)angle * 2000) / 180;
    const uint32_t duty = (pulse_us * max_duty) / 20000;

    ledc_set_duty(SERVO_MODE, SERVO2_CHANNEL, duty);
    ledc_update_duty(SERVO_MODE, SERVO2_CHANNEL);
}

static void servos_move_smooth(int target1, int target2, int step_delay_ms)
{
    servos_attach();

    int start1 = g_current_angle1;
    int start2 = g_current_angle2;

    ESP_LOGI(TAG, "High-Torque Visor Drive: Left (%d->%d) & Right (%d->%d) [speed=%dms, stroke=15deg]",
             start1, target1, start2, target2, step_delay_ms);

    if (step_delay_ms <= 2) {
        /* Direct Full-Torque Drive: 100% maximum motor current & instantaneous acceleration */
        servo1_set_angle(target1);
        servo2_set_angle(target2);
        vTaskDelay(pdMS_TO_TICKS(350));
    } else {
        /* High-Torque Multi-Step Ramp: Larger step increments provide higher rotational momentum */
        int steps1 = abs(target1 - start1);
        int steps2 = abs(target2 - start2);
        int max_steps = (steps1 > steps2) ? steps1 : steps2;
        int step_increment = (step_delay_ms <= 6) ? 3 : 1;

        for (int i = step_increment; i < max_steps; i += step_increment) {
            int a1 = start1 + ((target1 - start1) * i) / max_steps;
            int a2 = start2 + ((target2 - start2) * i) / max_steps;
            servo1_set_angle(a1);
            servo2_set_angle(a2);
            vTaskDelay(pdMS_TO_TICKS(step_delay_ms));
        }
        servo1_set_angle(target1);
        servo2_set_angle(target2);
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    /* Continuous Holding Torque:
     * Keep PWM active so the internal PID loop of the MG90S maintains full 2.2 kg-cm
     * holding torque against gravity, preventing heavy acrylic visors from sagging or dropping.
     */
}

static int s_visor_speed_ms = 4; /* Default to High Torque / Snappy Lift */

static void on_visor_command(const char *cmd)
{
    if (cmd == NULL) return;

    if (strncmp(cmd, "SPEED:", 6) == 0) {
        int spd = atoi(cmd + 6);
        if (spd >= 0 && spd <= 60) {
            s_visor_speed_ms = spd;
            ESP_LOGI(TAG, "Visor Speed/Torque Mode Set: %d ms/step", s_visor_speed_ms);
        }
        return;
    }

    if (strncmp(cmd, "SET_TRAVEL:", 11) == 0 || strncmp(cmd, "TRAVEL:", 7) == 0) {
        const char *p = strchr(cmd, ':');
        int deg = p ? atoi(p + 1) : 15;
        if (deg >= 5 && deg <= 60) {
            s_visor_stroke_deg = deg;
            s_servo1_open_angle = s_servo1_close_angle + s_visor_stroke_deg;
            s_servo2_open_angle = s_servo2_close_angle - s_visor_stroke_deg;
            ESP_LOGI(TAG, "Visor Stroke Travel updated -> %d° (Open: Left=%d°, Right=%d°)",
                     s_visor_stroke_deg, s_servo1_open_angle, s_servo2_open_angle);
            if (s_visor_open) {
                servos_move_smooth(s_servo1_open_angle, s_servo2_open_angle, s_visor_speed_ms);
            }
        }
        return;
    }

    if (strncmp(cmd, "SET_CLOSE:", 10) == 0 || strncmp(cmd, "CLOSE_ANGLE:", 12) == 0) {
        const char *p = strchr(cmd, ':');
        int val = p ? atoi(p + 1) : 30;
        if (val >= 0 && val <= 70) {
            s_servo1_close_angle = val;
            s_servo2_close_angle = 180 - val;
            s_servo1_open_angle = s_servo1_close_angle + s_visor_stroke_deg;
            s_servo2_open_angle = s_servo2_close_angle - s_visor_stroke_deg;
            ESP_LOGI(TAG, "Configured Closed Angle -> Left=%d°, Right=%d° (Travel=%d°)",
                     s_servo1_close_angle, s_servo2_close_angle, s_visor_stroke_deg);
            if (!s_visor_open) {
                servo1_set_angle(s_servo1_close_angle);
                servo2_set_angle(s_servo2_close_angle);
            }
        }
        return;
    }

    if (strcasecmp(cmd, "MAX_TORQUE") == 0 || strcasecmp(cmd, "TURBO") == 0) {
        s_visor_speed_ms = 0;
        ESP_LOGI(TAG, "Visor Mode -> MAX TORQUE (Direct Drive)");
        return;
    }

    if (strcasecmp(cmd, "OPEN") == 0) {
        s_visor_open = true;
        ble_nav_notify_visor(true);
        ble_nav_send_log("[VISOR] OPEN (15deg)");
        ESP_LOGI(TAG, "Visor Command -> OPEN (M1=%d, M2=%d, stroke=%ddeg)", s_servo1_open_angle, s_servo2_open_angle, s_visor_stroke_deg);
        servos_move_smooth(s_servo1_open_angle, s_servo2_open_angle, s_visor_speed_ms);
    } else if (strcasecmp(cmd, "CLOSE") == 0) {
        s_visor_open = false;
        ble_nav_notify_visor(false);
        ble_nav_send_log("[VISOR] CLOSE");
        ESP_LOGI(TAG, "Visor Command -> CLOSE (M1=%d, M2=%d)", s_servo1_close_angle, s_servo2_close_angle);
        servos_move_smooth(s_servo1_close_angle, s_servo2_close_angle, s_visor_speed_ms);
    } else if (strcasecmp(cmd, "TOGGLE") == 0 || strcasecmp(cmd, "BUTTON") == 0 || strcmp(cmd, "1") == 0) {
        s_visor_open = !s_visor_open;
        ble_nav_notify_visor(s_visor_open);
        ble_nav_send_log(s_visor_open ? "[BUTTON] Visor Toggled -> OPEN (15deg)" : "[BUTTON] Visor Toggled -> CLOSE");
        ESP_LOGI(TAG, "Visor Toggle -> %s (stroke=%ddeg)", s_visor_open ? "OPEN" : "CLOSE", s_visor_stroke_deg);
        if (s_visor_open) {
            servos_move_smooth(s_servo1_open_angle, s_servo2_open_angle, s_visor_speed_ms);
        } else {
            servos_move_smooth(s_servo1_close_angle, s_servo2_close_angle, s_visor_speed_ms);
        }
    }
}

static void init_servo(void)
{
    ESP_LOGI(TAG, "Initializing dual MG90S servos...");

    ledc_timer_config_t timer_cfg = {
        .speed_mode       = SERVO_MODE,
        .timer_num        = SERVO_TIMER,
        .duty_resolution  = SERVO_RESOLUTION,
        .freq_hz          = SERVO_FREQUENCY,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch1 = {
        .gpio_num   = SERVO1_GPIO,
        .speed_mode = SERVO_MODE,
        .channel    = SERVO1_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = SERVO_TIMER,
        .duty       = 0,
        .hpoint     = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch1));

    ledc_channel_config_t ch2 = {
        .gpio_num   = SERVO2_GPIO,
        .speed_mode = SERVO_MODE,
        .channel    = SERVO2_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = SERVO_TIMER,
        .duty       = 0,
        .hpoint     = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch2));

    /* AT BOOT / FLASH: Immediately drive to calibrated CLOSED position (Left=30°, Right=150°) with full holding torque */
    ESP_LOGI(TAG, "Setting STARTUP CLOSED POSITION (Left=%d°, Right=%d°)...", s_servo1_close_angle, s_servo2_close_angle);
    servos_attach();
    servo1_set_angle(s_servo1_close_angle);
    servo2_set_angle(s_servo2_close_angle);
    vTaskDelay(pdMS_TO_TICKS(500));
    s_visor_open = false;
    ESP_LOGI(TAG, "Visor locked at CLOSED position with active holding torque (stroke=15deg).");
}

#include "driver/usb_serial_jtag.h"
#include "driver/uart.h"

static void process_serial_command(const char *cmd_line)
{
    if (!cmd_line || strlen(cmd_line) == 0) return;
    ESP_LOGI(TAG, "Serial Command Recv: '%s'", cmd_line);

    if (strncmp(cmd_line, "VISOR:", 6) == 0) {
        on_visor_command(cmd_line + 6);
    } else if (strncmp(cmd_line, "SIREN:", 6) == 0) {
        const char *s = cmd_line + 6;
        if (strcasecmp(s, "OFF") == 0 || strcasecmp(s, "CLEAR") == 0 || strcmp(s, "0") == 0) {
            pixel_led_set_siren_dir(SIREN_DIR_NONE);
            ble_nav_notify_siren(false);
            ESP_LOGI(TAG, "Siren Cleared / Turned OFF via Serial");
        }
    } else if (strncmp(cmd_line, "NAV:", 4) == 0) {
        char nav_data[64];
        strncpy(nav_data, cmd_line + 4, sizeof(nav_data) - 1);
        nav_data[sizeof(nav_data) - 1] = '\0';
        char turn[16] = {0};
        int dist = 0;
        char *comma = strchr(nav_data, ',');
        if (comma) {
            *comma = '\0';
            strncpy(turn, nav_data, sizeof(turn) - 1);
            dist = atoi(comma + 1);
        } else {
            strncpy(turn, nav_data, sizeof(turn) - 1);
        }
        pixel_led_set_nav_command(turn, dist);
    } else if (strcasecmp(cmd_line, "OPEN") == 0 || strcasecmp(cmd_line, "CLOSE") == 0 || strcasecmp(cmd_line, "TOGGLE") == 0) {
        on_visor_command(cmd_line);
    }
}

static void serial_receiver_task(void *pvParameters)
{
    (void)pvParameters;
    usb_serial_jtag_driver_config_t usb_serial_cfg = {
        .rx_buffer_size = 512,
        .tx_buffer_size = 512,
    };
    usb_serial_jtag_driver_install(&usb_serial_cfg);

    /* Also configure UART0 receiver for standard USB-UART / CP2102 COM ports */
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);

    uint8_t rx_buf[128];
    char line_buf[128];
    int line_len = 0;

    ESP_LOGI(TAG, "Dual Serial Receiver online (UART0 & USB-JTAG ready for Web Serial)");

    while (1) {
        /* Check USB-JTAG */
        int len = usb_serial_jtag_read_bytes(rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(15));
        if (len <= 0) {
            /* Check UART0 */
            len = uart_read_bytes(UART_NUM_0, rx_buf, sizeof(rx_buf) - 1, pdMS_TO_TICKS(15));
        }

        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = (char)rx_buf[i];
                if (c == '\n' || c == '\r') {
                    if (line_len > 0) {
                        line_buf[line_len] = '\0';
                        process_serial_command(line_buf);
                        line_len = 0;
                    }
                } else if (line_len < sizeof(line_buf) - 1) {
                    line_buf[line_len++] = c;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* Wireless Telemetry Forwarder (Streams all ESP_LOG to PC over BLE wirelessly) */
static vprintf_like_t s_original_vprintf = NULL;

static int wireless_log_vprintf(const char *fmt, va_list args)
{
    char buf[128];
    va_list args_copy;
    va_copy(args_copy, args);
    int ret = vsnprintf(buf, sizeof(buf), fmt, args_copy);
    va_end(args_copy);

    if (ret > 0) {
        char *nl = strpbrk(buf, "\r\n");
        if (nl) *nl = '\0';
        if (strlen(buf) > 0) {
            ble_nav_send_log(buf);
        }
    }

    if (s_original_vprintf) {
        return s_original_vprintf(fmt, args);
    }
    return ret;
}

/* =========================================================
 * MAIN ENTRY POINT
 * ========================================================= */
void app_main(void)
{
    /* 1. Hook Wireless Telemetry Streamer over BLE */
    s_original_vprintf = esp_log_set_vprintf(wireless_log_vprintf);

    ESP_LOGI(TAG, "==================================================");
    ESP_LOGI(TAG, "  SMART HELMET PRODUCTION FIRMWARE (ESP32-S3)     ");
    ESP_LOGI(TAG, "==================================================");

    /* 2. Initialize Dual Visor Servos (GPIO 15 & 16) */
    init_servo();

    /* 3. Initialize 3 Dedicated WS2811 Pixel LED Channels:
     * Right=GPIO 39, Center=GPIO 40, Left=GPIO 41 (Independent 28 FPS task on Core 1) */
    ESP_ERROR_CHECK(pixel_led_init());

    /* 4. Initialize BLE Navigation Service ("SmartHelmet" peripheral) */
    ESP_ERROR_CHECK(ble_nav_init(on_visor_command));

    /* 5. Initialize ESP-NOW Receiver for Bike Handlebar Button (Wi-Fi Ch 1) */
    esp_err_t espnow_err = espnow_button_init(on_visor_command);
    if (espnow_err != ESP_OK) {
        ESP_LOGW(TAG, "ESP-NOW init: %s (BLE & Web Control active)", esp_err_to_name(espnow_err));
    }

    /* 6. Start Dual Serial Command Listener for PC Cable Testing (Core 0, Priority 1) */
    xTaskCreatePinnedToCore(serial_receiver_task, "serial_receiver_task", 4096, NULL, 1, NULL, 0);

    /* 7. Start AI Siren Neural Network Detector on Core 1 (Mic 2 on GPIO 9, 10, 11) */
    esp_err_t siren_err = start_siren_detection();
    if (siren_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start siren detection: %s", esp_err_to_name(siren_err));
    }
    ESP_LOGI(TAG, "ALL HELMET SUBSYSTEMS ONLINE & SYNCHRONIZED!");
}