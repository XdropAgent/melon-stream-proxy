/**
 * ESP32-CAM WebSocket Stream Client
 * 
 * Connects to VPS proxy via WebSocket and sends JPEG frames.
 * Supports multi-device viewing through proxy.
 * 
 * Hardware: ESP32-CAM (AI Thinker)
 * 
 * Wiring:
 *   GPIO 0  → GND (for flash, optional)
 *   GPIO 4  → Flash LED
 *   GPIO 12 → HSPI MISO
 *   GPIO 13 → HSPI MOSI
 *   GPIO 14 → HSPI CLK
 *   GPIO 15 → HSPI CS
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WebSocketsClient.h>

// ===================== CONFIG =====================
// WiFi
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// VPS Proxy
const char* WS_HOST       = "43.134.230.63";
const int   WS_PORT       = 8080;
const char* WS_PATH       = "/ws/stream?token=esp32-melon-01";

// Camera pins (AI Thinker ESP32-CAM)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ===================== GLOBALS =====================
WebSocketsClient wsClient;
unsigned long lastFrameTime = 0;
unsigned long frameInterval = 100; // 100ms = ~10 FPS
unsigned long lastReconnect = 0;
bool wsConnected = false;

// ===================== CAMERA INIT =====================
bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode    = CAMERA_GRAB_LATEST;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 12;
  config.fb_count     = 2;
  
  // Resolution based on PSRAM
  if (psramFound()) {
    config.frame_size   = FRAMESIZE_VGA;  // 640x480
    config.jpeg_quality = 10;
  } else {
    config.frame_size   = FRAMESIZE_QVGA; // 320x240
    config.jpeg_quality = 14;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Init failed: 0x%x\n", err);
    return false;
  }

  // Adjust sensor settings
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 1);     // Slightly brighter
    s->set_contrast(s, 1);       // Slightly more contrast
    s->set_saturation(s, 0);
    s->set_whitebal(s, 1);       // Auto white balance
    s->set_awb_gain(s, 1);
    s->set_wb_mode(s, 0);        // Auto
    s->set_exposure_ctrl(s, 1);  // Auto exposure
    s->set_aec2(s, 1);           // Auto exposure DSP
    s->set_gain_ctrl(s, 1);      // Auto gain
    s->set_agc_gain(s, 0);
    s->set_gainceiling(s, (gainceiling_t)6);
    s->set_bpc(s, 1);            // Black pixel correction
    s->set_wpc(s, 1);            // White pixel correction
    s->set_lenc(s, 1);           // Lens correction
  }

  Serial.println("[CAM] Initialized OK");
  return true;
}

// ===================== WEBSOCKET =====================
void wsEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsConnected = true;
      Serial.printf("[WS] Connected to %s:%d%s\n", WS_HOST, WS_PORT, WS_PATH);
      break;
      
    case WStype_DISCONNECTED:
      wsConnected = false;
      Serial.println("[WS] Disconnected");
      break;
      
    case WStype_TEXT:
      Serial.printf("[WS] Text: %s\n", payload);
      break;
      
    case WStype_ERROR:
      Serial.printf("[WS] Error: %s\n", payload);
      wsConnected = false;
      break;
      
    default:
      break;
  }
}

// ===================== SEND FRAME =====================
void sendFrame() {
  if (!wsConnected) return;
  
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[CAM] Frame capture failed");
    return;
  }
  
  // Send JPEG frame via WebSocket
  wsClient.sendBIN(fb->buf, fb->len);
  
  esp_camera_fb_return(fb);
}

// ===================== SETUP =====================
void setup() {
  Serial.begin(115200);
  Serial.println("\n🍈 ESP32-CAM Melon Stream Client");
  
  // Init camera
  if (!initCamera()) {
    Serial.println("[FATAL] Camera init failed! Restarting...");
    delay(3000);
    ESP.restart();
  }
  
  // Connect WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WiFi] Failed! Restarting...");
    delay(3000);
    ESP.restart();
  }
  
  Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
  
  // Connect WebSocket
  wsClient.begin(WS_HOST, WS_PORT, WS_PATH);
  wsClient.onEvent(wsEvent);
  wsClient.setReconnectInterval(5000);
  
  Serial.println("[WS] Connecting to proxy...");
}

// ===================== LOOP =====================
void loop() {
  wsClient.loop();
  
  // Send frame at interval
  if (wsConnected && (millis() - lastFrameTime >= frameInterval)) {
    sendFrame();
    lastFrameTime = millis();
  }
  
  // WiFi reconnect
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Lost connection, reconnecting...");
    WiFi.reconnect();
    delay(5000);
  }
}
