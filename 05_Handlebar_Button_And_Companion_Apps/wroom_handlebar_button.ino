/*
 * ======================================================================================
 * SMART HELMET - BIKE HANDLEBAR WIRELESS VISOR BUTTON (ESP32 WROOM)
 * ======================================================================================
 * Hardware:
 *   - Any standard ESP32 WROOM / DevKit board mounted on the bike handlebar.
 *   - Momentary push button connected between GPIO 4 and GND.
 *   - On-board LED on GPIO 2 flashes when transmit succeeds.
 *
 * How it works:
 *   - When the rider presses the handlebar button, ESP-NOW instantly transmits "TOGGLE"
 *     to the ESP32-S3 in the helmet on Wi-Fi channel 1 (< 2ms latency, zero pairing needed).
 * ======================================================================================
 */

#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>

#define BUTTON_PIN 4      // Momentary push button to GND
#define LED_PIN    2      // Built-in blue LED for visual feedback

// Broadcast MAC address (reaches ESP32-S3 instantly without needing to know its MAC)
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

esp_now_peer_info_t peerInfo;

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  // Turn LED on briefly to confirm button packet sent
  digitalWrite(LED_PIN, HIGH);
  delay(50);
  digitalWrite(LED_PIN, LOW);
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  // Set to channel 1 (matching ESP32-S3)
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_send_cb(onDataSent);

  // Register broadcast peer
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = 1;  
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add peer");
    return;
  }

  Serial.println("Bike Handlebar Button Ready! Press button on GPIO 4 to toggle helmet visor.");
}

void loop() {
  static int lastButtonState = HIGH;
  static unsigned long lastDebounceTime = 0;
  const unsigned long debounceDelay = 250; // ms debounce

  int reading = digitalRead(BUTTON_PIN);

  if (reading == LOW && lastButtonState == HIGH) {
    if ((millis() - lastDebounceTime) > debounceDelay) {
      lastDebounceTime = millis();

      const char *msg = "TOGGLE";
      esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *)msg, strlen(msg));

      if (result == ESP_OK) {
        Serial.println("Sent: TOGGLE visor to Smart Helmet!");
      } else {
        Serial.println("Error sending ESP-NOW message");
      }
    }
  }

  lastButtonState = reading;
  delay(10);
}
