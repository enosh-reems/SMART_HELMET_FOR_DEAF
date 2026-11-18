# Project Abstract

## SILENTINAL: Smart Assistive Visual Navigation & Offline Edge-AI Acoustic Threat Detection Helmet for Deaf and Hard-of-Hearing Motorcyclists

---

### **Abstract**

Two-wheeler motorcycling presents severe personal safety and navigational challenges for deaf and hard-of-hearing (DHH) individuals. Conventional navigational solutions rely almost exclusively on auditory turn-by-turn prompts transmitted via earphones or visual maps mounted on handlebars. While voice guidance is entirely inaccessible to deaf riders, handlebar-mounted phone mounts induce dangerous glance-off-road distractions, expose costly personal devices to harsh vibration and weather damage, and fail to provide any situational awareness regarding approaching emergency vehicles. Furthermore, wearing in-ear earbuds inside a motorcycle helmet introduces significant acoustic occlusion, prevents detection of vital ambient sounds, and creates severe secondary trauma hazards in crashes.

To bridge this critical accessibility gap, this work presents **SILENTINAL**, an intelligent, self-contained assistive smart helmet designed specifically for deaf and hard-of-hearing riders. The system integrates two coordinated subsystems: **(1) a peripheral visual navigation HUD**, and **(2) an ultra-low-power, 100% offline TinyML acoustic siren detection engine** powered by the Espressif ESP32-S3 dual-core microcontroller.

The visual interface utilizes an array of nine WS2812B individually addressable RGB LEDs positioned inside the helmet chin-bar, strategically situated in the rider's lower peripheral field of view to eliminate focal distraction. Driven via a Web Bluetooth Low Energy (Web BLE) companion progressive web app, the navigation system delivers color-coded spatial cues: blue warnings at 100 meters, green alerts at 50 meters, and animated yellow directional arrows for turns, lane changes, and U-turns. 

To ensure threat awareness, the helmet incorporates a dual-channel digital I2S MEMS microphone array coupled to an on-device Edge AI Convolutional Neural Network (**`LightSirenCNN`**). The TinyML pipeline continuously computes 64-bin Mel spectrograms across 16 kHz ambient road acoustics. To prevent false alarms prevalent in dense Indian traffic, the network was trained with authentic regional hard negatives—including Kerala 108 ambulance sirens, police and fire rescue sirens, heavy traffic roar, autorickshaw engines, and vehicle horns. The model is quantized to 8-bit weights and 8-bit activations (**INT8 W8A8**) using ESP-PPQ and deployed natively via the **ESP-DL** library, fully utilizing the ESP32-S3 Vector Extension (PIE) instructions. The quantized model occupies merely **109 KB**, executes complete on-device inference in **~15–20 ms**, requires **zero internet connection or cloud reliance**, and triggers high-visibility alternating red-and-blue emergency alerts across the chin HUD upon siren detection.

Complementing the sensory pipeline, an ergonomic **ESP-NOW wireless transmitter** mounted on the handlebar enables the rider to actuate the motorized helmet visor via dual anti-jam PWM servos without removing their hands from the steering grips. Real-world validation confirms high detection accuracy (>95%), robust rejection of traffic horns and background urban noise, and extended battery endurance. SILENTINAL demonstrates a viable, low-cost, fully offline assistive edge-computing solution that restores road autonomy, situational safety, and confidence to deaf motorcyclists.

---

### **Keywords**
`Assistive Technology` • `TinyML` • `Edge AI` • `ESP32-S3` • `ESP-DL` • `Acoustic Siren Detection` • `Deaf Accessibility` • `Peripheral Visual HUD` • `Smart Helmet` • `IoT Navigation` • `Embedded Systems`

---

### **Key Technical Specifications**

| Component / Subsystem | Implementation Details |
|---|---|
| **Core Microcontroller** | Espressif ESP32-S3 (Xtensa® 32-bit LX7 dual-core @ 240 MHz, 8 MB PSRAM, 16 MB Flash) |
| **Acoustic Front-End** | Dual digital I2S MEMS Microphones (INMP441 / SPH0645) @ 16 kHz sampling |
| **AI Neural Architecture** | `LightSirenCNN` (Deep 2D Convolutional Neural Network with Mel Spectrogram front-end) |
| **Quantization & Acceleration** | 8-bit Weights & 8-bit Activations (W8A8 INT8) using ESP-PPQ & ESP-DL Vector Extension |
| **Model Footprint & Latency** | **109 KB** deployed binary size; **~15–20 ms** inference time per classification cycle |
| **Offline Independence** | **100% Offline** (No Wi-Fi, No 4G/5G, No Cloud API or subscription required) |
| **Visual HUD Display** | 9× WS2812B RGB Smart LEDs grouped in Chin-Bar lower peripheral field of view (FOV) |
| **Navigation Protocol** | Bluetooth Low Energy (BLE 5.0) GATT service connected to Web PWA Navigation App |
| **Remote Control Protocol** | ESP-NOW 2.4 GHz low-latency connection from handlebar wireless switch |
| **Visor Actuation** | Dual PWM micro-servos with anti-jam angle limits (Left: 30°–45°, Right: 150°–135°) |

---

### **Problem Statement & Motivation**

1. **Inaccessibility of Audio Navigation:** Millions of deaf and hard-of-hearing motorists cannot utilize audio-based turn-by-turn instructions provided by Google Maps, Apple Maps, or voice assistants.
2. **Visual Hazard of Handlebar Mounts:** Looking down at a phone screen mounted on a vibrating handlebar takes the rider's eyes away from traffic for 1.5 to 2.5 seconds—a known leading cause of two-wheeler collisions.
3. **Emergency Siren Blindness:** Deaf riders are unable to hear high-priority sirens from ambulances, fire tenders, and police escorts approaching from behind, creating critical delay in yielding right-of-way and elevating collision hazards.
4. **Physical Risk of Earphones:** Using in-ear earphones under a tight-fitting motorcycle helmet can cause chondritis, canal ulcerations, and severe ear cartilage injury during high-impact helmet compression.

---

### **Proposed System Architecture**

```
+---------------------------------------------------------------------------------+
|                                 SILENTINAL HELMET                               |
|                                                                                 |
|  +---------------------+        +--------------------+        +---------------+ |
|  |   Dual Digital      |        |     ESP32-S3       |        |   9x WS2812B  | |
|  |   I2S Microphones   | -----> |  TinyML Classifier | -----> |   Chin-Bar    | |
|  |   (Road Acoustics)  |        | (W8A8 INT8 Model)  |        |   RGB LEDs    | |
|  +---------------------+        +--------------------+        +---------------+ |
|                                           ^                           ^         |
|                                           |                           |         |
|  +---------------------+                  |                           |         |
|  |  Smartphone Web App | -----------------+ (BLE Turn-by-Turn Nav)    |         |
|  |  (Offline Map PWA)  |                                              |         |
|  +---------------------+                                              |         |
|                                                                       |         |
|  +---------------------+        +--------------------+                |         |
|  | Handlebar Button    | -----> | ESP-NOW Controller | ---------------+         |
|  | (Wireless ESP32)    |        | (Visor Servo PWM)  |                          |
|  +---------------------+        +--------------------+                          |
+---------------------------------------------------------------------------------+
```

---

### **Academic & Research Contributions**

1. **First End-to-End Completely Offline Siren Alert Helmet for DHH Riders:** Eliminates latency and cellular connectivity dropouts by running all DSP and neural inference strictly on the edge.
2. **Hard-Negative Fine-Tuned Audio Model:** Solves high false-positive rates of general acoustic models by training against authentic Indian and Kerala urban traffic sounds (autorickshaws, air horns, bus engines, and classroom crowds).
3. **Non-Intrusive Peripheral Optical Navigation:** Provides intuitive turn and distance indicators within the lower visual periphery without occluding the primary line of sight.
4. **Ergonomic Hands-On-Bar Visor Control:** Enhances rider comfort during sudden rain or fog without sacrificing physical handlebar stability.
