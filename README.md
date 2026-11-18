# 🪖 SILENTINAL: Smart Visual Navigation & Offline Edge-AI Siren Detection Helmet

> **An intelligent, assistive two-wheeler helmet providing heads-up peripheral visual navigation and 100% offline acoustic emergency siren alerts for deaf and hard-of-hearing riders.**

[![ESP32-S3](https://img.shields.io/badge/Hardware-ESP32--S3-red?logo=espressif)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework](https://img.shields.io/badge/Framework-ESP--IDF%20v5.x-blue)](https://docs.espressif.com/projects/esp-idf/en/latest/)
[![TinyML](https://img.shields.io/badge/TinyML-ESP--DL%20%7C%20ESP--PPQ-orange)](https://github.com/espressif/esp-dl)
[![Model](https://img.shields.io/badge/Model-W8A8%20Quantized%20CNN-green)](./01_AI_Model_Deployed_ESP32/)
[![Connectivity](https://img.shields.io/badge/Connectivity-BLE%20%7C%20ESP--NOW-brightgreen)]()
[![License](https://img.shields.io/badge/License-MIT-purple.svg)](LICENSE)

---

## 📌 Table of Contents
- [Project Overview](#-project-overview)
- [Key Features](#-key-features)
- [System Architecture](#-system-architecture)
- [Hardware Wiring & Pinout Guide](#-hardware-wiring--pinout-guide)
- [TinyML AI Model Architecture](#-tinyml-ai-model-architecture)
- [Repository Structure](#-repository-structure)
- [Quick Start: Flashing the ESP32-S3](#-quick-start-flashing-the-esp32-s3)
  - [Method 1: Instant 1-Click Flash (Recommended)](#method-1-instant-1-click-flash-recommended)
  - [Method 2: Flash via esptool.py](#method-2-flash-via-esptoolpy)
  - [Method 3: Build & Flash from Source (ESP-IDF)](#method-3-build--flash-from-source-esp-idf)
- [Companion Web & Mobile Apps](#-companion-web--mobile-apps)
- [Project Abstract](#-project-abstract)
- [License](#-license)

---

## 📖 Project Overview

Two-wheeler motorcycling presents unique safety challenges for **deaf and hard-of-hearing (DHH)** riders:
- **Auditory Navigation Is Inaccessible:** Standard voice-guided turn-by-turn prompts (Google Maps, Apple Maps) cannot be heard.
- **Phone Mount Distractions:** Constantly glancing down at handlebar-mounted smartphone screens diverts visual attention away from the road, dramatically increasing crash probability.
- **Emergency Siren Blindness:** Deaf riders cannot hear approaching emergency vehicles (ambulances, police escorts, fire engines), leading to delayed yielding and severe intersection collision risks.
- **Earphone Compression Trauma:** Wearing tight in-ear earbuds inside motorcycle helmets risks chondritis and severe inner-ear injury during crash impacts.

**SILENTINAL** solves this challenge through a hardware-software co-design:
1. **Peripheral Visual HUD:** 9 addressable WS2812B RGB LEDs in the helmet chin-bar provide glance-free turn cues (blue @ 100m, green @ 50m, yellow animated turn indicators).
2. **100% Offline Edge-AI Siren Detector:** Dual digital I2S microphones and an INT8 W8A8 quantized Convolutional Neural Network (`LightSirenCNN`) running locally on an **ESP32-S3** microcontroller detect emergency sirens in real time (~15–20 ms latency) with **zero internet or cloud connectivity**.
3. **Handlebar Visor Remote:** An ESP-NOW wireless transmitter lets the rider actuate the helmet visor via anti-jam micro-servos without taking their hands off the handlebar.

---

## ✨ Key Features

- 🛑 **100% Offline Edge Inference:** The AI siren classification model runs entirely inside the ESP32-S3 memory using the ESP-DL vector acceleration library. No 4G/5G, Wi-Fi, or cloud subscriptions required.
- 🎯 **Region-Specific False Positive Hardening:** Fine-tuned and rigorously evaluated against authentic Indian and Kerala urban environments—discriminating real sirens from loud traffic, autorickshaw engines, heavy bus horns, and crowded classroom chatter.
- 👁️ **Lower Peripheral HUD:** Nine chin-bar LEDs situated below the primary road sightline provide color-coded navigation cues without glare or eye strain.
- 🛰️ **Web BLE Navigation Integration:** Browser-based Progressive Web App (PWA) pairs over Bluetooth Low Energy to beam turn-by-turn vectors directly to the helmet.
- ⚡ **Instant 1-Click Flashing:** Precompiled binaries and automated Windows batch flashing scripts enable deployment to any new ESP32-S3 board in under 15 seconds.

---

## 🏛️ System Architecture

```
                                  +---------------------------------------+
                                  |         SMARTPHONE WEB PWA            |
                                  |   (Turn-by-Turn Navigation Vectors)   |
                                  +---------------------------------------+
                                                     |
                                            (BLE 5.0 GATT)
                                                     v
+------------------------+        +---------------------------------------+        +-------------------------+
|  DUAL I2S MEMS MICS    |        |             ESP32-S3 MCU              |        |   9x WS2812B RGB LEDs   |
| (Left & Right Channels)| -----> |     [Dual-Core 240MHz, 8MB PSRAM]     | -----> |  (Chin-Bar Peripheral)  |
|   INMP441 / SPH0645    |        |                                       |        |  Blue: 100m | Grn: 50m  |
+------------------------+        |  * Continuous Mel Spectrogram DSP     |        |  Yellow: Animated Turn  |
                                  |  * W8A8 INT8 CNN (ESP-DL Vector Ext)  |        |  Red+Blue: Siren Alert  |
+------------------------+        |  * BLE Navigation Service Decoder     |        +-------------------------+
|  HANDLEBAR SWITCH      | -----> |  * Dual PWM Servo Visor Controller    |                     |
|  (ESP32 WROOM ESP-NOW) |        +---------------------------------------+                     v
+------------------------+                           |                             +-------------------------+
                                                     +---------------------------> |   DUAL VISOR SERVOS     |
                                                                                   |  (Anti-Jam PWM Detach)  |
                                                                                   +-------------------------+
```

---

## 🔌 Hardware Wiring & Pinout Guide

### 1. Dual Directional I2S Microphones
Dual digital I2S microphones allow spatial sound capture:
| Signal Line | Left Microphone (Module 3) | Right Microphone (Module 1) |
|---|---|---|
| **I2S Controller** | `I2S_NUM_0` | `I2S_NUM_1` |
| **BCLK (Bit Clock)** | `GPIO 5` | `GPIO 9` |
| **WS / LRCLK (Word Select)** | `GPIO 6` | `GPIO 10` |
| **DATA (Serial Data)** | `GPIO 7` | `GPIO 11` |

### 2. WS2812B Addressable RGB LEDs (RMT Driver)
Nine LEDs arranged in three chin clusters (Left, Center, Right):
| Cluster | GPIO Pin | Function |
|---|---|---|
| **Module 1 (Right Cluster)** | `GPIO 39` | Right turn animations & right siren alert |
| **Module 2 (Center Cluster)**| `GPIO 40` | Straight / Hazard / Distance alerts |
| **Module 3 (Left Cluster)**  | `GPIO 41` | Left turn animations & left siren alert |

### 3. Dual Visor Micro-Servos
Equipped with software PWM detachment to prevent mechanical stall and motor overheating:
| Servo Actuator | GPIO Pin | Angle Range |
|---|---|---|
| **Servo 1 (Left Pivot)** | `GPIO 15` | Closed: 30° $\rightarrow$ Open: 45° (+15° stroke) |
| **Servo 2 (Right Pivot)**| `GPIO 16` | Closed: 150° $\rightarrow$ Open: 135° (-15° stroke) |

### 4. Wireless Handlebar Transmitter
- **MCU:** ESP32-WROOM-32
- **Trigger:** Momentary push button connected between **GPIO 4** and **GND**.
- **Status LED:** **GPIO 2**
- **Protocol:** ESP-NOW broadcast on **Wi-Fi Channel 1** (instant 0 ms pairing latency).

---

## 🧠 TinyML AI Model Architecture

```
Audio Input (16 kHz, 10s Buffer)
       │
       ▼
Mel-Spectrogram Front-End (64 Mel Bins, 1024 FFT, 256 Hop)
       │
       ▼
Input Tensor: [1, 1, 128, 128]
       │
       ▼
LightSirenCNN (Conv2D -> BatchNorm -> ReLU -> MaxPool2D -> Linear)
       │
       ▼
INT8 Quantization (W8A8 Weights & Activations via ESP-PPQ)
       │
       ▼
ESP-DL Hardware Accelerated Vector Inference on ESP32-S3 (PIE)
       │
       ▼
Classification Output: [Siren Detected (P > 0.85) / Normal Road Sound]
```

- **Architecture:** `LightSirenCNN`
- **PyTorch Model Size:** 340 KB (`best.pt`)
- **Exported ONNX Model:** 331 KB (`light_siren_realworld_v2.onnx`)
- **Deployed ESP-DL Quantized Binary:** **109 KB** (`light_siren_realworld_v2_s3_w8a8.espdl`)
- **Inference Latency:** **~15–20 ms** on ESP32-S3 @ 240 MHz.
- **Quantization:** ESP-PPQ 8-bit weight & 8-bit activation (W8A8).

---

## 📁 Repository Structure

```
.
├── ABSTRACT.md                                 <- Formal Academic & Project Abstract
├── README.md                                   <- Main Project Documentation (this file)
├── README_DEPLOYMENT_GUIDE.md                  <- Original comprehensive deployment notes
├── helmet.pptx                                 <- SILENTINAL project presentation slides
│
├── 01_AI_Model_Deployed_ESP32/                 <- Trained & Quantized AI Models
│   ├── best.pt                                 <- PyTorch trained model weights
│   ├── light_siren_cnn.py                      <- PyTorch neural architecture definition
│   ├── light_siren_realworld_v2.onnx           <- Exported ONNX graph
│   ├── light_siren_realworld_v2_s3_w8a8.espdl  <- 8-bit quantized binary deployed on ESP32-S3 (109 KB)
│   ├── calibration_20.npz                      <- Calibration tensor for INT8 quantization
│   ├── light_siren_realworld_v2_s3_w8a8.info   <- Quantizer precision report
│   └── light_siren_realworld_v2_s3_w8a8.json   <- Tensor operator graph metadata
│
├── 02_Model_Training_And_Export_Scripts/       <- Training, Quantization & Test Scripts
│   ├── run_full_pipeline.py                    <- Master automated training-to-deployment script
│   ├── train_light_siren_realworld.py          <- PyTorch trainer with Kerala hard negatives
│   ├── export_onnx.py                          <- Checkpoint to ONNX exporter
│   ├── quantize_s3_w8a8.py                     <- ESP-PPQ ONNX to ESP-DL quantizer
│   ├── test_real_world_sirens.py               <- Siren validation suite
│   └── test_real_world_horns.py                <- Horn false-positive discriminator test
│
├── 03_Instant_ESP32_Flash_Binaries/            <- 1-Click Flashing Package
│   ├── FLASH_ESP32_S3.bat                      <- Automated Windows batch flashing script
│   ├── FLASH_COMMANDS.txt                      <- Flash address offsets & esptool commands
│   ├── template-app.bin                        <- Compiled firmware with embedded AI model
│   ├── bootloader.bin                          <- Second-stage bootloader (0x0)
│   ├── partition-table.bin                     <- Partition table (0x8000)
│   └── srmodels.bin                            <- Espressif Speech & Wake partition (0x510000)
│
├── 04_ESP32_Firmware_Source_Code/              <- ESP-IDF C/C++ Firmware Project
│   ├── CMakeLists.txt                          <- Project build configuration
│   ├── sdkconfig                               <- ESP-IDF hardware & memory settings (16MB Flash, PSRAM)
│   ├── partitions.csv                          <- Custom 16MB flash partition table
│   └── main/
│       ├── main.c                              <- System entry point, RTOS tasks, Visor PWM
│       ├── siren_detector.cpp                  <- ESP-DL inference engine & I2S microphone driver
│       ├── siren_detector.h
│       ├── pixel_led.c                         <- WS2812 RMT driver & animation routines
│       ├── pixel_led.h
│       ├── ble_nav.c                           <- BLE turn-by-turn GATT service
│       ├── ble_nav.h
│       ├── espnow_button.c                     <- ESP-NOW receiver for handlebar trigger
│       ├── espnow_button.h
│       └── models/
│           └── light_siren_realworld_v2_s3_w8a8.espdl
│
├── 05_Handlebar_Button_And_Companion_Apps/     <- Remote Switch Code & Companion Web Apps
│   ├── wroom_handlebar_button.ino              <- Arduino code for handlebar transmitter
│   ├── wroom_button_esp_idf/                   <- ESP-IDF source for handlebar transmitter
│   ├── index.html                              <- Helmet Web BLE HUD & Status Dashboard
│   ├── mobile_app.html                         <- Mobile PWA Navigation Controller
│   ├── helmet_nav_app.html                     <- Turn-by-turn map interface
│   └── servo_calibration.html                  <- Visor servo angle calibrator
│
└── 06_Representative_Real_World_Audio/         <- Real-world Audio Validation Samples
    ├── sirens/                                 <- Kerala 108 Ambulance, Police, Fire sirens
    └── hard_negatives/                         <- Urban traffic, autorickshaws, classrooms, horns
```

---

## ⚡ Quick Start: Flashing the ESP32-S3

### Method 1: Instant 1-Click Flash (Recommended)
No build tools or compiler installations required:
1. Connect your **ESP32-S3** board to your PC via USB.
2. Open the `03_Instant_ESP32_Flash_Binaries` folder.
3. Double-click **`FLASH_ESP32_S3.bat`**.
4. Enter your COM port when prompted (e.g. `COM3`). The firmware and AI model flash in ~15 seconds.

### Method 2: Flash via `esptool.py`
```bash
cd 03_Instant_ESP32_Flash_Binaries
python -m esptool --chip esp32s3 -p COMx -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x0 bootloader.bin 0x8000 partition-table.bin 0x10000 template-app.bin 0x510000 srmodels.bin
```

### Method 3: Build & Flash from Source (ESP-IDF)
For developers wishing to modify firmware source code:
```bash
cd 04_ESP32_Firmware_Source_Code
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

---

## 📱 Companion Web & Mobile Apps

Open the HTML apps directly in Google Chrome or any Chromium browser supporting the Web Bluetooth API:
- **`mobile_app.html`**: Mobile-optimized PWA that turns your phone into a GPS navigation terminal sending real-time directions over BLE.
- **`index.html`**: Live telemetry and LED pattern visualizer.
- **`servo_calibration.html`**: Interactive slider utility to calibrate mechanical visor opening/closing angles.

---

## 📄 Project Abstract

The complete academic and engineering abstract is available in [ABSTRACT.md](./ABSTRACT.md).

> **Summary:** SILENTINAL is an assistive smart helmet that restores road autonomy to deaf motorcyclists through glance-free peripheral visual navigation cues and a 100% offline TinyML Convolutional Neural Network deployed on the ESP32-S3, accurately detecting emergency sirens amidst challenging urban noise with zero cloud dependence.

---

## 📄 License

This project is licensed under the [MIT License](LICENSE).
