/**
 * ESP32-CAM Melon Monitor — Full Version
 * 
 * Fitur:
 *   - MJPEG Stream via WebSocket (multi-device via VPS proxy)
 *   - Auto capture terjadwal (12:00, 16:00, 20:00)
 *   - Manual capture dari dashboard (via Firebase flag)
 *   - Watermark timestamp on-device (bitmap 5x7)
 *   - Upload base64 ke Firebase RTDB
 *   - Auto-retry on failure
 * 
 * Hardware: ESP32-CAM AI Thinker
 * Partition: Huge APP (3MB No OTA/1MB SPIFFS)
 */

#include "esp_camera.h"
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ===================== KONFIGURASI =====================
const char* ssid     = "NAMA_WIFI";
const char* password = "PASSWORD_WIFI";

// VPS Proxy (WebSocket stream via Cloudflare Tunnel)
const char* WS_HOST = "stream.xdrop-agent.my.id";
const int   WS_PORT = 443;
const char* WS_PATH = "/ws/stream?token=esp32-melon-01";

// Firebase
const char* FIREBASE_HOST = "monitoring-tanaman-d2cd2-default-rtdb.firebaseio.com";
const char* FIREBASE_AUTH = "YOUR_FIREBASE_API_KEY";

// Jadwal auto capture (jam WIB)
const int JADWAL_JAM[] = {12, 16, 20};
const int TOTAL_JADWAL = 3;

// ===================== PIN KAMERA (AI Thinker) =====================
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
bool wsConnected = false;
volatile bool isUploading = false;

unsigned long lastFrameTime = 0;
const unsigned long frameInterval = 100; // ~10 FPS

bool sudahFotoHariIni[3] = {false, false, false};
int hariTerakhir = -1;

// Firebase listener for manual capture
WiFiClientSecure fbClient;
bool manualCaptureRequested = false;
unsigned long lastFirebaseCheck = 0;
const unsigned long firebaseCheckInterval = 5000; // Check every 5s

// ===================== FONT BITMAP 5x7 =====================
static const uint8_t font5x7[][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},
  {0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},
  {0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},
  {0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x02},{0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},
  {0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},
  {0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
  {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
  {0x00,0x41,0x41,0x41,0x00},{0x20,0x10,0x08,0x04,0x02},{0x00,0x41,0x41,0x41,0x00},
  {0x04,0x02,0x01,0x02,0x04},{0x08,0x08,0x08,0x08,0x08},{0x00,0x01,0x02,0x04,0x00},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
  {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
  {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
  {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
  {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
  {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
  {0x00,0x08,0x36,0x41,0x00},{0x00,0x00,0x7F,0x00,0x00},{0x00,0x41,0x36,0x08,0x00},
  {0x08,0x04,0x08,0x10,0x08}
};

// ===================== DRAW TEXT =====================
void drawChar(uint8_t* buf, int bw, int bh, int x, int y, char c, uint16_t color) {
  if (!buf) return;
  if (c >= 'a' && c <= 'z') c -= 32;
  if (c < 32 || c > 126) c = ' ';
  int idx = c - 32;
  if (idx < 0 || idx >= 95) return;
  for (int col = 0; col < 5; col++) {
    uint8_t bits = font5x7[idx][col];
    for (int row = 0; row < 7; row++) {
      if (bits & (1 << row)) {
        int px = x + col, py = y + row;
        if (px >= 0 && px < bw && py >= 0 && py < bh) {
          int offset = (py * bw + px) * 2;
          buf[offset]     = (color >> 8) & 0xFF;
          buf[offset + 1] = color & 0xFF;
        }
      }
    }
  }
}

void drawString(uint8_t* buf, int bw, int bh, int x, int y, const char* str, uint16_t color) {
  if (!buf || !str) return;
  while (*str) {
    drawChar(buf, bw, bh, x, y, *str++, color);
    x += 6;
  }
}

// ===================== FOTO DENGAN WATERMARK =====================
camera_fb_t* ambilFotoDenganWatermark(const char* waktuStr) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return NULL;

  s->set_pixformat(s, PIXFORMAT_RGB565);
  delay(200);

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb || !fb->buf) {
    s->set_pixformat(s, PIXFORMAT_JPEG);
    delay(100);
    return NULL;
  }

  int w = fb->width, h = fb->height;
  drawString(fb->buf, w, h, 3, h - 11, waktuStr, 0x0000);
  drawString(fb->buf, w, h, 2, h - 12, waktuStr, 0xFFFF);

  uint8_t* jpg_buf = NULL;
  size_t jpg_len = 0;
  bool ok = frame2jpg(fb, 25, &jpg_buf, &jpg_len);
  esp_camera_fb_return(fb);

  s->set_pixformat(s, PIXFORMAT_JPEG);
  delay(100);

  if (!ok || !jpg_buf || jpg_len < 500) {
    if (jpg_buf) free(jpg_buf);
    return NULL;
  }

  camera_fb_t* result = (camera_fb_t*)malloc(sizeof(camera_fb_t));
  if (!result) { free(jpg_buf); return NULL; }
  result->buf = jpg_buf;
  result->len = jpg_len;
  result->width = w;
  result->height = h;
  result->format = PIXFORMAT_JPEG;
  return result;
}

// ===================== BASE64 =====================
static const char b64chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

String base64Encode(uint8_t* data, size_t len) {
  String result = "";
  result.reserve((len / 3 + 1) * 4 + 4);
  int i = 0;
  uint8_t b3[3], b4[4];
  while (len--) {
    b3[i++] = *data++;
    if (i == 3) {
      b4[0]=(b3[0]&0xfc)>>2; b4[1]=((b3[0]&0x03)<<4)+((b3[1]&0xf0)>>4);
      b4[2]=((b3[1]&0x0f)<<2)+((b3[2]&0xc0)>>6); b4[3]=b3[2]&0x3f;
      for (i=0;i<4;i++) result += b64chars[b4[i]];
      i = 0;
    }
  }
  if (i) {
    for (int j=i;j<3;j++) b3[j]=0;
    b4[0]=(b3[0]&0xfc)>>2; b4[1]=((b3[0]&0x03)<<4)+((b3[1]&0xf0)>>4);
    b4[2]=((b3[1]&0x0f)<<2)+((b3[2]&0xc0)>>6);
    for (int j=0;j<i+1;j++) result += b64chars[b4[j]];
    while (i++<3) result += '=';
  }
  return result;
}

// ===================== NTP =====================
void syncNTP() {
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov", "id.pool.ntp.org");
  Serial.print("Sync NTP");
  struct tm ti;
  unsigned long s = millis();
  while (true) {
    if (millis() - s > 20000) { Serial.println(" timeout"); return; }
    if (getLocalTime(&ti) && ti.tm_year > 120) break;
    delay(500); Serial.print(".");
  }
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
  Serial.println(" OK: " + String(buf));
}

String getTimeString() {
  struct tm ti;
  if (!getLocalTime(&ti) || ti.tm_year < 120) return "n/a";
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &ti);
  return String(buf);
}

unsigned long getUnixTimestamp() {
  struct tm ti;
  if (!getLocalTime(&ti) || ti.tm_year < 120) return 0;
  return mktime(&ti);
}

// ===================== UPLOAD KE FIREBASE =====================
bool uploadToFirebase() {
  struct tm ti;
  if (!getLocalTime(&ti) || ti.tm_year < 120) {
    syncNTP();
    if (!getLocalTime(&ti) || ti.tm_year < 120) return false;
  }

  Serial.println("\n=== Ambil foto ===");
  isUploading = true;

  // Warmup 3x frame
  for (int i = 0; i < 3; i++) {
    camera_fb_t* warmup = esp_camera_fb_get();
    if (warmup) esp_camera_fb_return(warmup);
    delay(100);
  }

  String waktu = getTimeString();
  char waktuChar[25];
  waktu.toCharArray(waktuChar, 25);
  camera_fb_t* fb = ambilFotoDenganWatermark(waktuChar);

  if (!fb || !fb->buf) {
    Serial.println("Foto gagal!");
    isUploading = false;
    return false;
  }

  Serial.printf("Foto OK: %d bytes\n", fb->len);

  String b64 = base64Encode(fb->buf, fb->len);
  free(fb->buf);
  free(fb);
  fb = NULL;

  Serial.printf("Base64: %d chars\n", b64.length());

  unsigned long ts = getUnixTimestamp() * 1000UL;
  String path = "/foto.json?auth=" + String(FIREBASE_AUTH);
  String header = "POST " + path + " HTTP/1.1\r\n"
                  "Host: " + String(FIREBASE_HOST) + "\r\n"
                  "Content-Type: application/json\r\n"
                  "Connection: close\r\n\r\n";
  String bodyPrefix = "{\"image\":\"";
  String bodySuffix = "\",\"timestamp\":" + String(ts) + ",\"waktu\":\"" + waktu + "\"}";

  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.printf("Upload attempt %d/3...\n", attempt);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(60);

    if (!client.connect(FIREBASE_HOST, 443)) {
      Serial.println("Gagal konek Firebase");
      delay(2000);
      continue;
    }

    client.print(header);
    client.print(bodyPrefix);

    int sent = 0;
    while (sent < (int)b64.length()) {
      int end = min(sent + 256, (int)b64.length());
      client.print(b64.substring(sent, end));
      sent = end;
    }

    client.print(bodySuffix);

    unsigned long timeout = millis();
    while (client.available() == 0) {
      if (millis() - timeout > 60000) {
        Serial.println("Response timeout");
        client.stop();
        delay(2000);
        goto retry;
      }
      delay(10);
    }

    {
      String response = client.readStringUntil('\n');
      client.stop();
      Serial.println("Response: " + response);

      if (response.indexOf("200") >= 0 || response.indexOf("201") >= 0) {
        Serial.println("Upload OK!");
        isUploading = false;
        b64 = "";
        return true;
      }
    }

    Serial.println("Upload gagal");
    delay(2000);
    retry:
    continue;
  }

  Serial.println("Upload FAILED after 3 attempts");
  isUploading = false;
  b64 = "";
  return false;
}

// ===================== CEK JADWAL AUTO =====================
void cekJadwalFoto() {
  struct tm ti;
  if (!getLocalTime(&ti) || ti.tm_year < 120) return;
  int jam = ti.tm_hour;
  int hari = ti.tm_yday;

  if (hari != hariTerakhir) {
    for (int i = 0; i < TOTAL_JADWAL; i++) sudahFotoHariIni[i] = false;
    hariTerakhir = hari;
  }

  for (int i = 0; i < TOTAL_JADWAL; i++) {
    if (jam == JADWAL_JAM[i] && !sudahFotoHariIni[i]) {
      Serial.printf("\n>>> Jadwal %02d:00 — ambil foto!\n", JADWAL_JAM[i]);
      bool ok = uploadToFirebase();
      if (ok) {
        sudahFotoHariIni[i] = true;
        Serial.printf("Foto %02d:00 tersimpan!\n", JADWAL_JAM[i]);
      } else {
        Serial.printf("Foto %02d:00 GAGAL — retry nanti\n", JADWAL_JAM[i]);
      }
      break;
    }
  }
}

// ===================== CEK MANUAL CAPTURE DARI FIREBASE =====================
void cekManualCapture() {
  if (millis() - lastFirebaseCheck < firebaseCheckInterval) return;
  lastFirebaseCheck = millis();

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(5);

  String path = "/capture.json?auth=" + String(FIREBASE_AUTH);
  if (!client.connect(FIREBASE_HOST, 443)) return;

  client.print("GET " + path + " HTTP/1.1\r\nHost: " + String(FIREBASE_HOST) + "\r\nConnection: close\r\n\r\n");

  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) { client.stop(); return; }
    delay(10);
  }

  // Skip headers
  while (client.available()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") break;
  }

  String body = client.readString();
  client.stop();

  // Parse response: "true" means capture requested
  body.trim();
  if (body == "true") {
    Serial.println("\n>>> Manual capture dari dashboard!");
    bool ok = uploadToFirebase();
    if (ok) {
      Serial.println("Manual capture OK!");
      // Reset flag di Firebase
      WiFiClientSecure resetClient;
      resetClient.setInsecure();
      resetClient.setTimeout(5);
      if (resetClient.connect(FIREBASE_HOST, 443)) {
        String resetPath = "/capture.json?auth=" + String(FIREBASE_AUTH);
        resetClient.print("PUT " + resetPath + " HTTP/1.1\r\nHost: " + String(FIREBASE_HOST) + "\r\nContent-Type: application/json\r\nConnection: close\r\n\r\nfalse");
        resetClient.stop();
      }
    } else {
      Serial.println("Manual capture GAGAL");
    }
  }
}

// ===================== WEBSOCKET =====================
void wsEvent(WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      wsConnected = true;
      Serial.printf("[WS] Connected to %s:%d\n", WS_HOST, WS_PORT);
      break;
    case WStype_DISCONNECTED:
      wsConnected = false;
      Serial.println("[WS] Disconnected");
      break;
    case WStype_ERROR:
      wsConnected = false;
      Serial.println("[WS] Error");
      break;
    default:
      break;
  }
}

void sendFrame() {
  if (!wsConnected || isUploading) return;

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return;

  wsClient.sendBIN(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

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
  config.jpeg_quality = 20;
  config.fb_count     = 2;

  if (psramFound()) {
    config.frame_size   = FRAMESIZE_QVGA;
    config.jpeg_quality = 18;
  } else {
    config.frame_size   = FRAMESIZE_QQVGA;
    config.jpeg_quality = 14;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("[CAM] Init failed: 0x%x\n", err);
    return false;
  }

  sensor_t* s = esp_camera_sensor_get();
  if (s) {
    s->set_brightness(s, 1);
    s->set_contrast(s, 1);
    s->set_whitebal(s, 1);
    s->set_awb_gain(s, 1);
    s->set_exposure_ctrl(s, 1);
    s->set_aec2(s, 1);
    s->set_gain_ctrl(s, 1);
    s->set_bpc(s, 1);
    s->set_wpc(s, 1);
    s->set_lenc(s, 1);
  }

  Serial.println("[CAM] Initialized OK");
  return true;
}

// ===================== SETUP =====================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n🍈 ESP32-CAM Melon Monitor — Full Version");

  if (psramInit()) Serial.println("PSRAM OK");
  else Serial.println("PSRAM TIDAK AKTIF!");

  if (!initCamera()) {
    Serial.println("[FATAL] Camera init failed! Restarting...");
    delay(3000);
    ESP.restart();
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);
  Serial.printf("[WiFi] Connecting to %s", ssid);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500); Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WiFi] Failed! Restarting...");
    delay(3000);
    ESP.restart();
  }

  Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());

  syncNTP();

  // WebSocket stream
  // SSL WebSocket (port 443 via Cloudflare Tunnel)
  wsClient.beginSSL(WS_HOST, WS_PORT, WS_PATH);
  wsClient.onEvent(wsEvent);
  wsClient.setReconnectInterval(5000);
  Serial.println("[WS] Connecting to proxy via WSS...");

  Serial.println("==============================");
  Serial.println("Fitur: Stream + Auto Capture + Manual Capture + Watermark");
  Serial.printf("Jadwal: ");
  for (int i = 0; i < TOTAL_JADWAL; i++) {
    Serial.printf("%02d:00", JADWAL_JAM[i]);
    if (i < TOTAL_JADWAL - 1) Serial.print(", ");
  }
  Serial.println();
  Serial.println("==============================");
}

// ===================== LOOP =====================
void loop() {
  wsClient.loop();

  // Stream frame
  if (wsConnected && !isUploading && (millis() - lastFrameTime >= frameInterval)) {
    sendFrame();
    lastFrameTime = millis();
  }

  // Auto capture
  cekJadwalFoto();

  // Manual capture dari dashboard
  cekManualCapture();

  // WiFi reconnect
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Lost, reconnecting...");
    WiFi.reconnect();
    delay(5000);
  }
}
