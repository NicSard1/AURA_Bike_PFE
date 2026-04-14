#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <HardwareSerial.h>

// =========================
// WiFi
// =========================
const char* WIFI_SSID = "ESP32_SCREEN_AP";
const char* WIFI_PASS = "12345678";

// =========================
// UDP vidéo
// =========================
WiFiUDP udpVideo;
IPAddress receiverIP;
const uint16_t VIDEO_PORT = 5000;

static const int CHUNK = 1400;
static uint16_t frame_id = 0;

// =========================
// UDP lidar distance
// =========================
WiFiUDP udpLidar;
const uint16_t LIDAR_PORT = 4444;

// =========================
// LED blanche (flash LED)
// GPIO2 = LED ON
// Pilotage PWM pour cette carte
// =========================
#define FLASH_LED_PIN      2

bool ledState = false;
uint32_t lastBlink = 0;
const uint32_t BLINK_PERIOD = 500; // ms

// =========================
// TF-Luna UART
// Recâblage recommandé :
// TF-Luna TX -> GPIO41 (RX ESP)
// TF-Luna RX -> GPIO42 (TX ESP)
// =========================
HardwareSerial LUNA(1);
#define LUNA_RX_PIN  41   // ESP RX  <- TF-Luna TX
#define LUNA_TX_PIN  42   // ESP TX  -> TF-Luna RX

volatile int g_distance_cm = -1;
static uint32_t lastLidarSendMs = 0;

// =========================
// PINS CAMERA
// =========================
#define CAM_PIN_SIOD   4
#define CAM_PIN_SIOC   5
#define CAM_PIN_VSYNC  6
#define CAM_PIN_HREF   7
#define CAM_PIN_PCLK   13
#define CAM_PIN_XCLK   15
#define CAM_PIN_Y2     11
#define CAM_PIN_Y3     9
#define CAM_PIN_Y4     8
#define CAM_PIN_Y5     10
#define CAM_PIN_Y6     12
#define CAM_PIN_Y7     18
#define CAM_PIN_Y8     17
#define CAM_PIN_Y9     16
#define CAM_PIN_PWDN   -1
#define CAM_PIN_RESET  -1

// =========================
// LED blink
// =========================
void initFlashLed() {
  ledcAttach(FLASH_LED_PIN, 5000, 8);
  ledcWrite(FLASH_LED_PIN, 0);
}


void blinkLed() {
  uint32_t now = millis();
  if (now - lastBlink >= BLINK_PERIOD) {
    lastBlink = now;
    ledState = !ledState;

    // Test 1 : active HIGH
    ledcWrite(FLASH_LED_PIN, ledState ? 255 : 0);

    // Si ça ne marche pas, essaie à la place :
    // ledcWrite(FLASH_LED_PIN, ledState ? 0 : 255);
  }
}

// =========================
// Camera init
// =========================
void camera_init() {
  camera_config_t c;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;

  c.pin_d0 = CAM_PIN_Y2;
  c.pin_d1 = CAM_PIN_Y3;
  c.pin_d2 = CAM_PIN_Y4;
  c.pin_d3 = CAM_PIN_Y5;
  c.pin_d4 = CAM_PIN_Y6;
  c.pin_d5 = CAM_PIN_Y7;
  c.pin_d6 = CAM_PIN_Y8;
  c.pin_d7 = CAM_PIN_Y9;

  c.pin_xclk = CAM_PIN_XCLK;
  c.pin_pclk = CAM_PIN_PCLK;
  c.pin_vsync = CAM_PIN_VSYNC;
  c.pin_href  = CAM_PIN_HREF;
  c.pin_sccb_sda = CAM_PIN_SIOD;
  c.pin_sccb_scl = CAM_PIN_SIOC;

  c.pin_pwdn  = CAM_PIN_PWDN;
  c.pin_reset = CAM_PIN_RESET;

  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_RGB565;
  c.frame_size   = FRAMESIZE_QVGA;   // 320x240

  c.fb_count     = 2;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("esp_camera_init failed: 0x%x\n", err);
    while (1) {
      blinkLed();
      delay(10);
    }
  }
}

// =========================
// TF-Luna parser
// =========================
void lunaTick() {
  static uint8_t buf[9];
  static int idx = 0;

  while (LUNA.available()) {
    uint8_t b = (uint8_t)LUNA.read();

    if (idx == 0 && b != 0x59) continue;
    buf[idx++] = b;

    if (idx == 2 && buf[1] != 0x59) {
      idx = 0;
      continue;
    }

    if (idx == 9) {
      uint16_t sum = 0;
      for (int i = 0; i < 8; i++) sum += buf[i];

      if ((sum & 0xFF) == buf[8]) {
        int dist = ((int)buf[3] << 8) | buf[2];
        if (dist > 0 && dist < 60000) g_distance_cm = dist;
        else g_distance_cm = -1;
      }
      idx = 0;
    }
  }
}

// =========================
// Envoi distance lidar
// =========================
void sendLidarUdp() {
  uint32_t now = millis();
  if (now - lastLidarSendMs < 100) return;
  lastLidarSendMs = now;

  if (WiFi.status() != WL_CONNECTED) return;

  char msg[16];
  int d = g_distance_cm;
  int n = snprintf(msg, sizeof(msg), "%d\n", d);

  udpLidar.beginPacket(receiverIP, LIDAR_PORT);
  udpLidar.write((uint8_t*)msg, n);
  udpLidar.endPacket();
}

// =========================
// Envoi frame vidéo
// =========================
static inline void send_frame(const uint8_t* buf, size_t len) {
  frame_id++;
  uint16_t total = (len + CHUNK - 1) / CHUNK;

  static uint8_t packet[8 + CHUNK];

  for (uint16_t cid = 0; cid < total; cid++) {
    size_t off = (size_t)cid * CHUNK;
    uint16_t plen = (uint16_t)min((size_t)CHUNK, len - off);

    packet[0] = frame_id & 0xFF;
    packet[1] = frame_id >> 8;
    packet[2] = cid & 0xFF;
    packet[3] = cid >> 8;
    packet[4] = total & 0xFF;
    packet[5] = total >> 8;
    packet[6] = plen & 0xFF;
    packet[7] = plen >> 8;

    memcpy(packet + 8, buf + off, plen);

    udpVideo.beginPacket(receiverIP, VIDEO_PORT);
    udpVideo.write(packet, 8 + plen);
    udpVideo.endPacket();
  }
}

// =========================
// Setup
// =========================
void setup() {
  Serial.begin(115200);
  delay(100);

  initFlashLed();
  lastBlink = millis();

  camera_init();

  // UART LiDAR
  // Si ton TF-Luna ne répond pas, essaie 9600 à la place
  LUNA.begin(115200, SERIAL_8N1, LUNA_RX_PIN, LUNA_TX_PIN);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) {
    blinkLed();
    delay(100);
    Serial.print(".");
  }
  Serial.println();

  Serial.print("CAM IP: ");
  Serial.println(WiFi.localIP());

  receiverIP = WiFi.gatewayIP();
  Serial.print("Receiver IP (GW): ");
  Serial.println(receiverIP);

  udpVideo.begin(4000);
  udpLidar.begin(4445);
}

// =========================
// Loop
// =========================
void loop() {
  // LED blanche clignote tout le temps
  blinkLed();

  // Lidar
  lunaTick();
  sendLidarUdp();

  // Vidéo seulement si WiFi connecté
  if (WiFi.status() != WL_CONNECTED) {
    delay(5);
    return;
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return;

  send_frame(fb->buf, fb->len);
  esp_camera_fb_return(fb);
}