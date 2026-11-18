# Smart Helmet AI Model & Complete Deployment Package

This standalone package contains the exact AI model, firmware, compiled binaries, training scripts, and companion tools for the **Smart Helmet Project**.

It preserves everything required to deploy the system onto a new ESP32-S3 board without requiring re-training or keeping the 41+ GB audio datasets.

---

## 📁 Package Directory Structure

```
Smart_Helmet_AI_Model_And_Deployment/
├── 01_AI_Model_Deployed_ESP32/
│   ├── light_siren_realworld_v2_s3_w8a8.espdl    <- Exact 8-bit quantized model deployed on ESP32-S3 (109 KB)
│   ├── light_siren_realworld_v2.onnx             <- Exported ONNX model graph (331 KB)
│   ├── best.pt                                   <- PyTorch trained model checkpoint / weights (340 KB)
│   ├── light_siren_cnn.py                        <- PyTorch neural network architecture definition
│   ├── calibration_20.npz                        <- Calibration dataset used for INT8 W8A8 quantization
│   ├── light_siren_realworld_v2_s3_w8a8.info     <- Quantization layer error & precision report
│   └── light_siren_realworld_v2_s3_w8a8.json     <- Quantization tensor & operator metadata
│
├── 02_Model_Training_And_Export_Scripts/
│   ├── run_full_pipeline.py                      <- Master pipeline (Train -> ONNX -> Quantize -> Deploy)
│   ├── train_light_siren_realworld.py            <- PyTorch training script (with Kerala hard negatives)
│   ├── train_light_siren.py                      <- Base model trainer
│   ├── export_onnx.py                            <- PyTorch checkpoint to ONNX exporter
│   ├── create_calibration.py                     <- Generates representative calibration dataset
│   ├── quantize_s3_w8a8.py                       <- ESP-PPQ ONNX to ESP-DL quantization script
│   ├── eval_model_check.py                       <- Real-world audio validation & probability tester
│   ├── test_light_siren.py                       <- Test script for model inference
│   ├── test_real_world_sirens.py                 <- Siren test suite
│   └── test_real_world_horns.py                  <- Horn / false-positive test suite
│
├── 03_Instant_ESP32_Flash_Binaries/
│   ├── FLASH_ESP32_S3.bat                        <- 1-Click Windows batch script to flash any ESP32-S3
│   ├── FLASH_COMMANDS.txt                        <- Command line & GUI flash addresses
│   ├── template-app.bin                          <- Complete firmware binary (with embedded AI model)
│   ├── bootloader.bin                            <- ESP32-S3 second-stage bootloader (0x0)
│   ├── partition-table.bin                       <- 16MB partition table (0x8000)
│   ├── srmodels.bin                              <- Espressif Speech & Wake Model partition (0x510000)
│   └── flasher_args.json                         <- ESP-IDF flash settings and offsets
│
├── 04_ESP32_Firmware_Source_Code/
│   ├── main/
│   │   ├── main.c                                <- System entry point, tasks, visor servo controls
│   │   ├── siren_detector.cpp                    <- ESP-DL AI inference engine & I2S microphone processor
│   │   ├── siren_detector.h
│   │   ├── pixel_led.c                           <- RMT WS2812 directional indicator lights
│   │   ├── pixel_led.h
│   │   ├── ble_nav.c                             <- Bluetooth Low Energy turn-by-turn navigation service
│   │   ├── ble_nav.h
│   │   ├── espnow_button.c                       <- ESP-NOW wireless receiver for handlebar visor toggle
│   │   ├── espnow_button.h
│   │   ├── CMakeLists.txt                        <- Component build rules with model embedding
│   │   ├── idf_component.yml                     <- ESP-IDF Component Manager dependencies (esp-sr, esp-dl)
│   │   ├── partitions.csv                        <- Custom flash partition table layout
│   │   └── models/
│   │       └── light_siren_realworld_v2_s3_w8a8.espdl
│   ├── CMakeLists.txt                            <- Root CMakeLists
│   ├── CMakePresets.json
│   ├── partitions.csv
│   ├── sdkconfig                                 <- ESP-IDF build configuration (16MB Flash, PSRAM, etc.)
│   ├── dependencies.lock
│   └── manifest.json
│
├── 05_Handlebar_Button_And_Companion_Apps/
│   ├── wroom_handlebar_button.ino                <- Arduino code for ESP32 WROOM handlebar button
│   ├── wroom_button_esp_idf/                     <- ESP-IDF project for handlebar button transmitter
│   ├── index.html                                <- Helmet Web BLE HUD & Status Dashboard
│   ├── mobile_app.html                           <- Mobile PWA Navigation & HUD Controller
│   ├── helmet_nav_app.html                       <- GPS turn-by-turn navigation interface
│   ├── helmet_pc_demo.html                       <- Live PC simulation and visualizer
│   ├── servo_calibration.html                    <- Visor servo angle calibrator
│   ├── smart_helmet_dashboard.html               <- System telemetry dashboard
│   ├── visor_app.html                            <- Visor display interface
│   ├── web_bridge.py                             <- Python serial/BLE bridge
│   └── make_calib.py                             <- Visor calibration script
│
├── 06_Representative_Real_World_Audio/
│   ├── sirens/                                   <- Authentic Kerala & Indian emergency sirens (WAV)
│   └── hard_negatives/                           <- Authentic Kerala traffic, classroom, & autorickshaw sounds
│
└── README_DEPLOYMENT_GUIDE.md                    <- This master documentation
```

---

## ⚡ How to Deploy onto a New ESP32-S3

You have two simple ways to deploy to any new ESP32-S3 board:

### Method 1: Instant 1-Click Flashing (Recommended)
You do **not** need to install ESP-IDF or compile anything:
1. Connect your new **ESP32-S3** board to your PC using a USB cable.
2. Open folder `03_Instant_ESP32_Flash_Binaries`.
3. Double-click `FLASH_ESP32_S3.bat`.
4. Enter the COM port when prompted (e.g., `COM3`).
5. The script flashes the complete system in ~15 seconds!

### Method 2: Manual Flash with `esptool.py`
Open a command prompt in `03_Instant_ESP32_Flash_Binaries` and run:
```bash
python -m esptool --chip esp32s3 -p COMx -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 template-app.bin 0x510000 srmodels.bin
```
*(Replace `COMx` with your actual COM port)*

### Method 3: Flash from Source Code with ESP-IDF
If you want to modify code:
1. Open the **ESP-IDF 5.x CMD** or **PowerShell** prompt.
2. Navigate to `04_ESP32_Firmware_Source_Code`:
   ```bash
   cd "C:\Users\enosh\Desktop\Smart_Helmet_AI_Model_And_Deployment\04_ESP32_Firmware_Source_Code"
   ```
3. Set target and flash:
   ```bash
   idf.py set-target esp32s3
   idf.py build
   idf.py -p COMx flash monitor
   ```

---

## 🔌 Hardware Wiring & Pinout Guide

### 1. Dual Directional I2S Microphones
The helmet uses two digital I2S microphones for left/right siren detection:
| Signal | Left Microphone (Module 3) | Right Microphone (Module 1) |
|---|---|---|
| **I2S Controller** | `I2S_NUM_0` | `I2S_NUM_1` |
| **BCLK (Bit Clock)** | `GPIO 5` | `GPIO 9` |
| **WS / LRCLK (Word Select)** | `GPIO 6` | `GPIO 10` |
| **DATA (Serial Data)** | `GPIO 7` | `GPIO 11` |

### 2. WS2812 Status / Directional Alert LEDs (RMT Driver)
| LED Module | GPIO Pin | Function |
|---|---|---|
| **Module 1 (Right)** | `GPIO 39` | Right siren alert & right turn signal |
| **Module 2 (Center)** | `GPIO 40` | Front hazard / center siren alert |
| **Module 3 (Left)** | `GPIO 41` | Left siren alert & left turn signal |

### 3. Visor Servos (Anti-Jam PWM Detach)
| Servo | GPIO Pin | Travel Angles |
|---|---|---|
| **Servo 1 (Left Pivot)** | `GPIO 15` | Close: 30° -> Open: 45° (+15° stroke) |
| **Servo 2 (Right Pivot)** | `GPIO 16` | Close: 150° -> Open: 135° (-15° stroke) |

### 4. Wireless Handlebar Button (ESP32 WROOM Transmitter)
- Mounted on the bike handlebar.
- Connect a momentary push button between **GPIO 4** and **GND**.
- Status LED on **GPIO 2**.
- Uses broadcast ESP-NOW on **Wi-Fi Channel 1** to toggle the visor instantly with zero pairing.

---

## 🧠 AI Model Architecture & Specs

- **Model Name:** `LightSirenCNN`
- **Trained Formats:**
  - PyTorch Checkpoint: `best.pt` (340 KB)
  - ONNX Model: `light_siren_realworld_v2.onnx` (331 KB)
  - ESP-DL Deployed Binary: `light_siren_realworld_v2_s3_w8a8.espdl` (109 KB)
- **Input Representation:**
  - Sample Rate: 16,000 Hz, 10-second rolling audio buffer.
  - Mel Spectrogram: 64 Mel bins, FFT size 1024, Hop length 256.
  - Resized input tensor: `[1, 1, 128, 128]` (128 frequency bins × 128 time frames).
- **Quantization:**
  - ESP-PPQ INT8 (W8A8): 8-bit quantized weights and 8-bit quantized activations.
  - Fully accelerated on the ESP32-S3 Vector Extension (PIE).
  - Inference Latency: ~15 to 20 ms.
- **Dataset & Fine-Tuning:**
  - AudioSet Emergency Vehicles.
  - Authentic Kerala 108 Ambulance and Kerala Police siren recordings.
  - Hard negative discrimination: Indian traffic noise, classroom chatter, and vehicle horns.

---

## 🗑️ Safe Deletion Guide (Reclaiming 40+ GB of Storage)

Now that this backup package is on your Desktop, you can safely delete the heavy training databases:

1. **Delete the 32 GB AudioSet Database:**
   - Path: `C:\Users\enosh\Downloads\e2panns\e2panns-main\datasets`
   - Reclaims: **~32.3 GB**

2. **Delete Old Heavy Lightning Checkpoints:**
   - Path: `C:\Users\enosh\Downloads\e2panns\e2panns-main\checkpoints\epanns_*`
   - Reclaims: **~2.2 GB**

3. **Delete PyTorch Training Virtual Environments (if no longer training):**
   - Path: `C:\Users\enosh\Downloads\e2panns\e2panns-main\.venv`
   - Reclaims: **~4.9 GB**

4. **Delete ESP-IDF Build Cache (if desired):**
   - Path: `C:\Espressif\projects\s3_mic_test\build`
   - Reclaims: **~360 MB** (Can always be regenerated by running `idf.py build`).

**Everything needed to deploy, run, and understand your Smart Helmet is safely preserved in this Desktop package!**
