
c = """#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

#define SERVO1_GPIO 15
#define SERVO2_GPIO 16
#define SERVO_TIMER LEDC_TIMER_0
#define SERVO_MODE LEDC_LOW_SPEED_MODE
#define SERVO1_CHANNEL LEDC_CHANNEL_0
#define SERVO2_CHANNEL LEDC_CHANNEL_1

static void set_s1(int a) {
    if (a < 0) a = 0; if (a > 180) a = 180;
    uint32_t p = 500 + ((uint32_t)a * 2000) / 180;
    ledc_set_duty(SERVO_MODE, SERVO1_CHANNEL, (p * 16383) / 20000);
    ledc_update_duty(SERVO_MODE, SERVO1_CHANNEL);
    printf(">> M1 set to %d deg\\n", a);
}

static void set_s2(int a) {
    if (a < 0) a = 0; if (a > 180) a = 180;
    uint32_t p = 500 + ((uint32_t)a * 2000) / 180;
    ledc_set_duty(SERVO_MODE, SERVO2_CHANNEL, (p * 16383) / 20000);
    ledc_update_duty(SERVO_MODE, SERVO2_CHANNEL);
    printf(">> M2 set to %d deg\\n", a);
}

static void detach(void) {
    ledc_stop(SERVO_MODE, SERVO1_CHANNEL, 0);
    ledc_stop(SERVO_MODE, SERVO2_CHANNEL, 0);
    printf(">> DETACHED (0mA stall, gears relaxed)\\n");
}

void app_main(void) {
    gpio_config_t io = {.pin_bit_mask = (1ULL<<15)|(1ULL<<16), .mode = GPIO_MODE_OUTPUT, .pull_down_en = 1};
    gpio_config(&io);
    gpio_set_level(15, 0); gpio_set_level(16, 0);

    ledc_timer_config_t t = {.speed_mode = SERVO_MODE, .timer_num = SERVO_TIMER, .duty_resolution = LEDC_TIMER_14_BIT, .freq_hz = 50, .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&t);
    ledc_channel_config_t c1 = {.gpio_num = 15, .speed_mode = SERVO_MODE, .channel = SERVO1_CHANNEL, .timer_sel = SERVO_TIMER};
    ledc_channel_config(&c1);
    ledc_channel_config_t c2 = {.gpio_num = 16, .speed_mode = SERVO_MODE, .channel = SERVO2_CHANNEL, .timer_sel = SERVO_TIMER};
    ledc_channel_config(&c2);

    printf("\\n=== CALIBRATOR READY! Type SERVO1 45 or SERVO2 135 ===\\n\\n");

    char buf[64];
    while (1) {
        if (fgets(buf, sizeof(buf), stdin)) {
            int a1 = 0, a2 = 0;
            if (sscanf(buf, "SERVO1:%d", &a1) == 1 || sscanf(buf, "SERVO1 %d", &a1) == 1 || sscanf(buf, "M1 %d", &a1) == 1) set_s1(a1);
            else if (sscanf(buf, "SERVO2:%d", &a2) == 1 || sscanf(buf, "SERVO2 %d", &a2) == 1 || sscanf(buf, "M2 %d", &a2) == 1) set_s2(a2);
            else if (sscanf(buf, "BOTH %d %d", &a1, &a2) == 2) { set_s1(a1); set_s2(a2); }
            else if (strstr(buf, "DETACH")) detach();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
"""
with open("main/main.c", "w") as f:
    f.write(c)
print("Calibration file written successfully!")

