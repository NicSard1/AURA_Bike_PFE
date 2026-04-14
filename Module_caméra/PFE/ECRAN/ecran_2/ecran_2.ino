#include <WiFi.h>
#include <WiFiUdp.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

// UDP vidéo + UDP lidar
WiFiUDP udpVideo;
WiFiUDP udpLidar;

const char* AP_SSID = "ESP32_SCREEN_AP";
const char* AP_PASS = "12345678";

const uint16_t VIDEO_PORT = 5000;
const uint16_t LIDAR_PORT = 4444;

static const int W = 320;
static const int H = 240;

static const int CHUNK = 1400;
static const int FRAME_BYTES = W * H * 2;
static uint8_t frameBuf[FRAME_BYTES];

static const int MAX_CHUNKS = (FRAME_BYTES + CHUNK - 1) / CHUNK;
static uint8_t got[MAX_CHUNKS];
static uint16_t gotCount = 0;

static uint16_t curFid = 0;
static uint16_t expectedChunks = 0;
static uint32_t lastFrameMs = 0;

// Lidar state
volatile int lastDistCm = -1;
static uint32_t lastOverlayMs = 0;

void resetFrame(uint16_t fid, uint16_t total) {
  curFid = fid;
  expectedChunks = total;
  gotCount = 0;
  memset(got, 0, sizeof(got));
  lastFrameMs = millis();
}

void drawDistanceOverlay(int cm) {
  // petit bandeau en haut gauche
  tft.fillRect(0, 0, 140, 20, TFT_BLACK);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);

  if (cm < 0) {
    tft.drawString("LIDAR: --", 4, 2, 2);
  } else {
    char s[24];
    snprintf(s, sizeof(s), "LIDAR: %d cm", cm);
    tft.drawString(s, 4, 2, 2);
  }
}

void pollLidarUdp() {
  int ps = udpLidar.parsePacket();
  if (!ps) return;

  char buf[32];
  int n = udpLidar.read(buf, sizeof(buf) - 1);
  if (n <= 0) return;
  buf[n] = 0;

  lastDistCm = atoi(buf); // "123\n"
}

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  // tu avais validé que swapBytes false donne les bonnes couleurs
  tft.setSwapBytes(false);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  udpVideo.begin(VIDEO_PORT);
  udpLidar.begin(LIDAR_PORT);

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Waiting RAW UDP...", 10, 10, 2);
}

void loop() {
  // 1) lire lidar (non bloquant)
  pollLidarUdp();

  // 2) vidéo UDP
  int ps = udpVideo.parsePacket();
  if (!ps) {
    if (expectedChunks && (millis() - lastFrameMs > 120)) {
      expectedChunks = 0;
      gotCount = 0;
    }
    return;
  }

  static uint8_t pkt[8 + CHUNK];
  int len = udpVideo.read(pkt, sizeof(pkt));
  if (len < 8) return;

  uint16_t fid   = (uint16_t)pkt[0] | ((uint16_t)pkt[1] << 8);
  uint16_t cid   = (uint16_t)pkt[2] | ((uint16_t)pkt[3] << 8);
  uint16_t total = (uint16_t)pkt[4] | ((uint16_t)pkt[5] << 8);
  uint16_t plen  = (uint16_t)pkt[6] | ((uint16_t)pkt[7] << 8);
  if (8 + plen != len) return;

  if (fid != curFid) resetFrame(fid, total);

  size_t off = (size_t)cid * CHUNK;
  if (cid < MAX_CHUNKS && off + plen <= (size_t)FRAME_BYTES) {
    memcpy(frameBuf + off, pkt + 8, plen);
    if (!got[cid]) { got[cid] = 1; gotCount++; }
  }

  if (expectedChunks && gotCount >= expectedChunks) {
    // plein écran ILI9341
    tft.startWrite();
    tft.pushImage(0, 0, W, H, (uint16_t*)frameBuf);
    tft.endWrite();

    expectedChunks = 0;
    gotCount = 0;

    // overlay (limité à ~15 Hz pour ne pas tuer les FPS)
    uint32_t now = millis();
    if (now - lastOverlayMs > 66) {
      lastOverlayMs = now;
      drawDistanceOverlay(lastDistCm);
    }
  }
}