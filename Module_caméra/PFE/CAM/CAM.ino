// ===== ESP32-CAM: MJPEG /stream + LED UDP (3333) + TF-Luna UART + Range UDP (4444) =====
#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiUdp.h>

// ---------- Wi-Fi ----------
const char* WIFI_SSID = "MSI";
const char* WIFI_PASS = "clem2610";

// ---------- MJPEG ----------
WiFiServer server(80);
static const char* BOUNDARY = "frameboundary";

// ---------- LED flash via UDP ----------
#define FLASH_GPIO 4
WiFiUDP udpLed;
const uint16_t FLASH_UDP_PORT = 3333;

// ---------- Range UDP (Q/R) ----------
WiFiUDP udpRange;
const uint16_t RANGE_UDP_PORT = 4444;   // S3 enverra '?' ici, on répond "123\n"

// ---------- TF-Luna UART ----------
#include <HardwareSerial.h>
HardwareSerial LUNA(1);                  // UART1
// Choisis un couple RX/TX LIBRES (pas utilisés par la cam)
// Si tu n'utilises pas la SD: RX=14, TX=15 est simple
#define LUNA_RX 14   // TF-Luna TX -> CAM GPIO12
#define LUNA_TX 15   // TF-Luna RX -> CAM GPIO13

volatile int g_distance_cm = -1;

// --- AI-Thinker pins cam ---
#define PWDN_GPIO_NUM  32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM   0
#define SIOD_GPIO_NUM  26
#define SIOC_GPIO_NUM  27
#define Y9_GPIO_NUM    35
#define Y8_GPIO_NUM    34
#define Y7_GPIO_NUM    39
#define Y6_GPIO_NUM    36
#define Y5_GPIO_NUM    21
#define Y4_GPIO_NUM    19
#define Y3_GPIO_NUM    18
#define Y2_GPIO_NUM     5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM  23
#define PCLK_GPIO_NUM  22

// ---------- Camera ----------
void startCamera() {
  camera_config_t c;
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
  c.pin_d0 = Y2_GPIO_NUM;  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM; c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM; c.pin_href = HREF_GPIO_NUM;
  c.pin_sscb_sda = SIOD_GPIO_NUM; c.pin_sscb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM; c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size   = FRAMESIZE_QVGA;       // 320x240
  c.jpeg_quality = 18;                    // 16–22 = plus fluide
  c.fb_count     = 2;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_LATEST;

  if (esp_camera_init(&c) != ESP_OK) {
    Serial.println("Camera init failed"); while (1) delay(1000);
  }
}

// ---------- TF-Luna UART parser (frame 9 octets : 0x59 0x59 DL DH ... SUM) ----------
void lunaTickUART() {
  static uint8_t buf[9];
  static int idx = 0;

  while (LUNA.available()) {
    uint8_t b = (uint8_t)LUNA.read();

    if (idx == 0 && b != 0x59) continue;   // cherche 1er header
    buf[idx++] = b;

    if (idx == 1 && buf[0] != 0x59) { idx = 0; continue; }
    if (idx == 2 && buf[1] != 0x59) { idx = 0; continue; }

    if (idx == 9) {
      // checksum simple = somme des 9 octets & 0xFF == 0 (ou somme des 8 premiers == 9e)
      uint16_t sum = 0;
      for (int i=0;i<8;i++) sum += buf[i];
      if ( (sum & 0xFF) == buf[8] ) {
        int dist = ((int)buf[3] << 8) | buf[2];   // DH:DL
        if (dist > 0 && dist < 60000) g_distance_cm = dist;
        else g_distance_cm = -1;
      }
      idx = 0;
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(FLASH_GPIO, OUTPUT);
  digitalWrite(FLASH_GPIO, LOW);

  // UART TF-Luna (115200 8N1)
  LUNA.begin(115200, SERIAL_8N1, LUNA_RX, LUNA_TX);
  delay(50);

  startCamera();

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("WiFi connect %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
  Serial.printf("\nIP: %s\n", WiFi.localIP().toString().c_str());

  server.begin();
  udpLed.begin(FLASH_UDP_PORT);
  udpRange.begin(RANGE_UDP_PORT);
  Serial.println("HTTP /stream ; LED UDP:3333 ; Range UDP Q/R:4444 ('?' -> reply 'nnn\\n')");
}

void loop() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("fb NULL");
    delay(200);
    return;
  }
  Serial.printf("OK fb len=%u size=%dx%d format=%d\n",
                (unsigned)fb->len, fb->width, fb->height, fb->format);
  esp_camera_fb_return(fb);
  delay(500);
}
