#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "dl_fbank.hpp"
#include "dl_model_base.hpp"
#include "dl_tensor_base.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pixel_led.h"
#include "ble_nav.h"

#define SIREN_TAG "SIREN_DETECT"

/* Dual Directional Siren Microphones:
 * Left Mic:  I2S_NUM_0 -> BCLK=GPIO 5, WS=GPIO 6,  DATA=GPIO 7  (Alerts Module 3 - Left)
 * Right Mic: I2S_NUM_1 -> BCLK=GPIO 9, WS=GPIO 10, DATA=GPIO 11 (Alerts Module 1 - Right)
 */
#define SIREN_LEFT_I2S_PORT   I2S_NUM_0
#define SIREN_LEFT_BCLK_GPIO  GPIO_NUM_5
#define SIREN_LEFT_WS_GPIO    GPIO_NUM_6
#define SIREN_LEFT_DATA_GPIO  GPIO_NUM_7

#define SIREN_RIGHT_I2S_PORT  I2S_NUM_1
#define SIREN_RIGHT_BCLK_GPIO GPIO_NUM_9
#define SIREN_RIGHT_WS_GPIO   GPIO_NUM_10
#define SIREN_RIGHT_DATA_GPIO GPIO_NUM_11

#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_WINDOW_SAMPLES (AUDIO_SAMPLE_RATE * 10)
#define MEL_BINS 64
#define MEL_IMAGE_SIZE 128
#define FRAMES_PER_READ 256

extern "C" {
extern const uint8_t model_start[] asm("_binary_light_siren_realworld_v2_s3_w8a8_espdl_start");
extern const uint8_t model_end[] asm("_binary_light_siren_realworld_v2_s3_w8a8_espdl_end");
}

static i2s_chan_handle_t siren_left_rx_handle = nullptr;
static i2s_chan_handle_t siren_right_rx_handle = nullptr;

static void resize_mel_to_model_input(const float *source,
                                      int source_frames,
                                      float *destination)
{
    for (int out_mel = 0; out_mel < MEL_IMAGE_SIZE; ++out_mel) {
        float mel_pos = (out_mel + 0.5f) * MEL_BINS / MEL_IMAGE_SIZE - 0.5f;
        mel_pos = fminf(fmaxf(mel_pos, 0.0f), MEL_BINS - 1.0f);
        int mel0 = static_cast<int>(floorf(mel_pos));
        int mel1 = (mel0 + 1 < MEL_BINS) ? mel0 + 1 : mel0;
        float mel_weight = mel_pos - mel0;

        for (int out_frame = 0; out_frame < MEL_IMAGE_SIZE; ++out_frame) {
            float frame_pos = (out_frame + 0.5f) * source_frames / MEL_IMAGE_SIZE - 0.5f;
            frame_pos = fminf(fmaxf(frame_pos, 0.0f), source_frames - 1.0f);
            int frame0 = static_cast<int>(floorf(frame_pos));
            int frame1 = (frame0 + 1 < source_frames) ? frame0 + 1 : frame0;
            float frame_weight = frame_pos - frame0;

            float a = source[frame0 * MEL_BINS + mel0] * (1.0f - mel_weight) +
                      source[frame0 * MEL_BINS + mel1] * mel_weight;
            float b = source[frame1 * MEL_BINS + mel0] * (1.0f - mel_weight) +
                      source[frame1 * MEL_BINS + mel1] * mel_weight;
            destination[out_mel * MEL_IMAGE_SIZE + out_frame] =
                a * (1.0f - frame_weight) + b * frame_weight;
        }
    }
}

static bool configure_single_microphone(i2s_port_t port, gpio_num_t bclk, gpio_num_t ws, gpio_num_t sd,
                                        i2s_chan_handle_t *handle_out)
{
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(port, I2S_ROLE_MASTER);
    channel_config.dma_desc_num = 8;
    channel_config.dma_frame_num = FRAMES_PER_READ;

    esp_err_t err = i2s_new_channel(&channel_config, nullptr, handle_out);
    if (err != ESP_OK) {
        ESP_LOGE(SIREN_TAG, "I2S%d channel create failed: %s", port, esp_err_to_name(err));
        return false;
    }

    i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = bclk,
            .ws = ws,
            .dout = I2S_GPIO_UNUSED,
            .din = sd,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(*handle_out, &standard_config);
    if (err != ESP_OK) {
        ESP_LOGE(SIREN_TAG, "I2S%d setup failed: %s", port, esp_err_to_name(err));
        return false;
    }

    err = i2s_channel_enable(*handle_out);
    if (err != ESP_OK) {
        ESP_LOGE(SIREN_TAG, "I2S%d enable failed: %s", port, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(SIREN_TAG, "I2S%d Mic ready: BCLK=%d WS=%d SD=%d (16 kHz)", port, bclk, ws, sd);
    return true;
}

static void siren_detection_task(void *argument)
{
    (void)argument;
    const size_t model_size = static_cast<size_t>(model_end - model_start);
    ESP_LOGI(SIREN_TAG, "Loading ESP-DL model (%u bytes) for Dual-Mic Directional Siren AI", static_cast<unsigned>(model_size));

    char *aligned_model = static_cast<char *>(heap_caps_aligned_alloc(16, model_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (aligned_model == nullptr) {
        aligned_model = static_cast<char *>(heap_caps_aligned_alloc(16, model_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (aligned_model == nullptr) {
        ESP_LOGE(SIREN_TAG, "Failed to allocate 16-byte aligned buffer for siren model!");
        vTaskDelete(nullptr);
        return;
    }
    memcpy(aligned_model, model_start, model_size);

    dl::Model model(aligned_model, fbs::MODEL_LOCATION_IN_FLASH_RODATA);
    dl::TensorBase *model_input = model.get_input("mel_spectrogram");
    dl::TensorBase *model_output = model.get_output("logit");
    if (model_input == nullptr || model_output == nullptr || model_input->get_size() != MEL_IMAGE_SIZE * MEL_IMAGE_SIZE) {
        ESP_LOGE(SIREN_TAG, "Model tensors unavailable or unexpected input size.");
        heap_caps_free(aligned_model);
        vTaskDelete(nullptr);
        return;
    }

    dl::audio::SpeechFeatureConfig feature_config;
    feature_config.sample_rate = AUDIO_SAMPLE_RATE;
    feature_config.frame_length = 64;
    feature_config.frame_shift = 16;
    feature_config.num_mel_bins = MEL_BINS;
    feature_config.preemphasis = 0.0f;
    feature_config.window_type = dl::audio::WinType::HANN;
    feature_config.low_freq = 0.0f;
    feature_config.high_freq = AUDIO_SAMPLE_RATE / 2.0f;
    feature_config.log_epsilon = 1e-6f;
    feature_config.use_log_fbank = 2;
    feature_config.use_power = true;
    feature_config.use_energy = false;
    feature_config.remove_dc_offset = true; // Crucial for INMP441 DC offset rejection

    dl::audio::Fbank fbank(feature_config);
    const int frame_count = fbank.get_output_shape(AUDIO_WINDOW_SAMPLES)[0];

    int16_t *audio_left = static_cast<int16_t *>(heap_caps_malloc(
        AUDIO_WINDOW_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    int16_t *audio_right = static_cast<int16_t *>(heap_caps_malloc(
        AUDIO_WINDOW_SAMPLES * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    int32_t *i2s_words_left = static_cast<int32_t *>(heap_caps_malloc(
        FRAMES_PER_READ * 2 * sizeof(int32_t), MALLOC_CAP_8BIT));
    int32_t *i2s_words_right = static_cast<int32_t *>(heap_caps_malloc(
        FRAMES_PER_READ * 2 * sizeof(int32_t), MALLOC_CAP_8BIT));

    float *mel_frames = static_cast<float *>(heap_caps_malloc(
        frame_count * MEL_BINS * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    float *model_features = static_cast<float *>(heap_caps_malloc(
        MEL_IMAGE_SIZE * MEL_IMAGE_SIZE * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (!audio_left || !audio_right || !i2s_words_left || !i2s_words_right || !mel_frames || !model_features) {
        ESP_LOGE(SIREN_TAG, "Not enough memory for dual-mic audio buffers.");
        if (audio_left) heap_caps_free(audio_left);
        if (audio_right) heap_caps_free(audio_right);
        if (i2s_words_left) heap_caps_free(i2s_words_left);
        if (i2s_words_right) heap_caps_free(i2s_words_right);
        if (mel_frames) heap_caps_free(mel_frames);
        if (model_features) heap_caps_free(model_features);
        heap_caps_free(aligned_model);
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(SIREN_TAG, "Dual Siren AI Detector active: 10s Circular Context with 1.0s Sliding Window Hop");

    static int left_hits = 0, left_misses = 0;
    static int right_hits = 0, right_misses = 0;
    static bool left_active = false;
    static bool right_active = false;
    static siren_direction_t current_dir = SIREN_DIR_NONE;

    const float MIN_SIREN_RMS = 450.0f;
    const float DETECTION_THRESHOLD = 0.82f;
    const int HOP_SAMPLES = AUDIO_SAMPLE_RATE * 1; // 1.0s hop = inference every 1 second!

    bool buffer_initialized = false;

    while (true) {
        int samples_to_capture = buffer_initialized ? HOP_SAMPLES : AUDIO_WINDOW_SAMPLES;
        int captured = 0;

        /* If already initialized, shift previous audio left by HOP_SAMPLES */
        if (buffer_initialized) {
            memmove(audio_left, audio_left + HOP_SAMPLES, (AUDIO_WINDOW_SAMPLES - HOP_SAMPLES) * sizeof(int16_t));
            memmove(audio_right, audio_right + HOP_SAMPLES, (AUDIO_WINDOW_SAMPLES - HOP_SAMPLES) * sizeof(int16_t));
        }

        int write_offset = buffer_initialized ? (AUDIO_WINDOW_SAMPLES - HOP_SAMPLES) : 0;

        while (captured < samples_to_capture) {
            size_t bytes_left = 0, bytes_right = 0;

            if (siren_left_rx_handle) {
                esp_err_t err = i2s_channel_read(
                    siren_left_rx_handle, i2s_words_left, FRAMES_PER_READ * 2 * sizeof(int32_t),
                    &bytes_left, pdMS_TO_TICKS(50));
                if (err != ESP_OK) bytes_left = 0;
            }

            if (siren_right_rx_handle) {
                esp_err_t err = i2s_channel_read(
                    siren_right_rx_handle, i2s_words_right, FRAMES_PER_READ * 2 * sizeof(int32_t),
                    &bytes_right, pdMS_TO_TICKS(50));
                if (err != ESP_OK) bytes_right = 0;
            }

            size_t max_bytes = (bytes_left > bytes_right) ? bytes_left : bytes_right;
            if (max_bytes == 0) {
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }

            int frames_read = static_cast<int>(max_bytes / (2 * sizeof(int32_t)));
            int frames_to_copy = (frames_read < samples_to_capture - captured)
                                     ? frames_read
                                     : samples_to_capture - captured;

            for (int i = 0; i < frames_to_copy; ++i) {
                /* Left Mic Sample */
                if (bytes_left > 0) {
                    int16_t l0 = static_cast<int16_t>(i2s_words_left[i * 2] >> 16);
                    int16_t l1 = static_cast<int16_t>(i2s_words_left[i * 2 + 1] >> 16);
                    int16_t sl = (abs(l0) >= abs(l1)) ? l0 : l1;
                    audio_left[write_offset + captured + i] = sl;
                } else {
                    audio_left[write_offset + captured + i] = 0;
                }

                /* Right Mic Sample */
                if (bytes_right > 0) {
                    int16_t r0 = static_cast<int16_t>(i2s_words_right[i * 2] >> 16);
                    int16_t r1 = static_cast<int16_t>(i2s_words_right[i * 2 + 1] >> 16);
                    int16_t sr = (abs(r0) >= abs(r1)) ? r0 : r1;
                    audio_right[write_offset + captured + i] = sr;
                } else {
                    audio_right[write_offset + captured + i] = 0;
                }
            }
            captured += frames_to_copy;
        }

        buffer_initialized = true;

        /* Calculate RMS of recent 2-second audio to check current sound presence */
        double recent_sq_left = 0.0, recent_sq_right = 0.0;
        int recent_samples = AUDIO_SAMPLE_RATE * 2;
        int recent_start = AUDIO_WINDOW_SAMPLES - recent_samples;
        int16_t peak_left = 0, peak_right = 0;

        for (int i = recent_start; i < AUDIO_WINDOW_SAMPLES; ++i) {
            int16_t sl = audio_left[i];
            int16_t sr = audio_right[i];
            if (abs(sl) > peak_left) peak_left = abs(sl);
            if (abs(sr) > peak_right) peak_right = abs(sr);
            recent_sq_left += static_cast<double>(sl) * sl;
            recent_sq_right += static_cast<double>(sr) * sr;
        }

        float rms_left = sqrtf(static_cast<float>(recent_sq_left / recent_samples));
        float rms_right = sqrtf(static_cast<float>(recent_sq_right / recent_samples));

        /* ------------------ Left Mic Inference ------------------ */
        float prob_left = 0.0f;
        float logit_left = -10.0f;
        if (siren_left_rx_handle && rms_left >= MIN_SIREN_RMS) {
            esp_err_t err = fbank.process(audio_left, AUDIO_WINDOW_SAMPLES, mel_frames);
            if (err == ESP_OK) {
                resize_mel_to_model_input(mel_frames, frame_count, model_features);
                dl::TensorBase float_input({1, 1, MEL_IMAGE_SIZE, MEL_IMAGE_SIZE}, model_features, 0, dl::DATA_TYPE_FLOAT, false);
                if (model_input->assign(&float_input)) {
                    model.run();
                    int8_t quantized_logit = model_output->get_element_ptr<int8_t>()[0];
                    logit_left = dl::dequantize<int8_t>(quantized_logit, DL_SCALE(static_cast<int>(model_output->exponent)));
                    prob_left = 1.0f / (1.0f + expf(-logit_left));
                }
            }
        }

        /* ------------------ Right Mic Inference ------------------ */
        float prob_right = 0.0f;
        float logit_right = -10.0f;
        if (siren_right_rx_handle && rms_right >= MIN_SIREN_RMS) {
            esp_err_t err = fbank.process(audio_right, AUDIO_WINDOW_SAMPLES, mel_frames);
            if (err == ESP_OK) {
                resize_mel_to_model_input(mel_frames, frame_count, model_features);
                dl::TensorBase float_input({1, 1, MEL_IMAGE_SIZE, MEL_IMAGE_SIZE}, model_features, 0, dl::DATA_TYPE_FLOAT, false);
                if (model_input->assign(&float_input)) {
                    model.run();
                    int8_t quantized_logit = model_output->get_element_ptr<int8_t>()[0];
                    logit_right = dl::dequantize<int8_t>(quantized_logit, DL_SCALE(static_cast<int>(model_output->exponent)));
                    prob_right = 1.0f / (1.0f + expf(-logit_right));
                }
            }
        }

        /* ------------------ Fast Sliding Window Hysteresis ------------------ */
        bool left_hit_raw = (prob_left >= DETECTION_THRESHOLD);
        bool right_hit_raw = (prob_right >= DETECTION_THRESHOLD);

        if (left_hit_raw) {
            left_hits++;
            left_misses = 0;
            if (left_hits >= 2) left_active = true; // Confirms in ~2 seconds!
        } else {
            left_misses++;
            if (left_misses >= 3) {
                left_hits = 0;
                left_active = false;
            }
        }

        if (right_hit_raw) {
            right_hits++;
            right_misses = 0;
            if (right_hits >= 2) right_active = true; // Confirms in ~2 seconds!
        } else {
            right_misses++;
            if (right_misses >= 3) {
                right_hits = 0;
                right_active = false;
            }
        }

        /* ------------------ Directional Decision ------------------ */
        siren_direction_t new_dir = SIREN_DIR_NONE;
        if (left_active && right_active) {
            new_dir = SIREN_DIR_BOTH;
        } else if (left_active) {
            new_dir = SIREN_DIR_LEFT;
        } else if (right_active) {
            new_dir = SIREN_DIR_RIGHT;
        }

        if (new_dir != current_dir) {
            current_dir = new_dir;
            pixel_led_set_siren_dir(new_dir);
            ble_nav_notify_siren(new_dir != SIREN_DIR_NONE);

            if (new_dir == SIREN_DIR_LEFT) {
                ble_nav_send_log(">>> [SIREN] LEFT Siren Detected -> Module 3 Strobe <<<");
                ESP_LOGW(SIREN_TAG, ">>> SIREN CONFIRMED: LEFT MIC (Prob: %.3f, RMS: %.1f) -> MODULE 3 RED-BLUE STROBE <<<", prob_left, rms_left);
            } else if (new_dir == SIREN_DIR_RIGHT) {
                ble_nav_send_log(">>> [SIREN] RIGHT Siren Detected -> Module 1 Strobe <<<");
                ESP_LOGW(SIREN_TAG, ">>> SIREN CONFIRMED: RIGHT MIC (Prob: %.3f, RMS: %.1f) -> MODULE 1 RED-BLUE STROBE <<<", prob_right, rms_right);
            } else if (new_dir == SIREN_DIR_BOTH) {
                ble_nav_send_log(">>> [SIREN] DUAL Siren Detected -> Modules 1 & 3 Strobe <<<");
                ESP_LOGW(SIREN_TAG, ">>> SIREN CONFIRMED: BOTH MICS (L: %.3f, R: %.3f) -> MODULES 1 & 3 RED-BLUE STROBE <<<", prob_left, prob_right);
            } else {
                ble_nav_send_log("[SIREN] Siren Cleared (Normal)");
                ESP_LOGI(SIREN_TAG, "Siren cleared: returned to normal.");
            }
        } else if (current_dir != SIREN_DIR_NONE) {
            ESP_LOGW(SIREN_TAG, ">>> SIREN SUSTAINED (Dir: %d, L_Prob: %.3f, R_Prob: %.3f) <<<",
                     current_dir, prob_left, prob_right);
        } else {
            ESP_LOGI(SIREN_TAG, "Monitoring: Left(P=%.3f, RMS=%.1f) | Right(P=%.3f, RMS=%.1f)",
                     prob_left, rms_left, prob_right, rms_right);
        }
    }
}

extern "C" esp_err_t start_siren_detection(void)
{
    ESP_LOGI(SIREN_TAG, "==================================================");
    ESP_LOGI(SIREN_TAG, "STARTING DUAL-MIC DIRECTIONAL SIREN AI SUBSYSTEM  ");
    ESP_LOGI(SIREN_TAG, "  Left Mic  (I2S0: BCLK=5, WS=6, SD=7)   -> Module 3 (Left)");
    ESP_LOGI(SIREN_TAG, "  Right Mic (I2S1: BCLK=9, WS=10, SD=11) -> Module 1 (Right)");
    ESP_LOGI(SIREN_TAG, "==================================================");

    bool ok_left = configure_single_microphone(
        SIREN_LEFT_I2S_PORT, SIREN_LEFT_BCLK_GPIO, SIREN_LEFT_WS_GPIO, SIREN_LEFT_DATA_GPIO,
        &siren_left_rx_handle);

    bool ok_right = configure_single_microphone(
        SIREN_RIGHT_I2S_PORT, SIREN_RIGHT_BCLK_GPIO, SIREN_RIGHT_WS_GPIO, SIREN_RIGHT_DATA_GPIO,
        &siren_right_rx_handle);

    if (!ok_left && !ok_right) {
        ESP_LOGE(SIREN_TAG, "Failed to initialize both I2S microphones!");
        return ESP_FAIL;
    }

    BaseType_t result = xTaskCreatePinnedToCore(siren_detection_task, "siren_detection", 10240, nullptr, 4, nullptr, 1);
    if (result != pdPASS) {
        ESP_LOGE(SIREN_TAG, "Could not start dual siren detection task.");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
