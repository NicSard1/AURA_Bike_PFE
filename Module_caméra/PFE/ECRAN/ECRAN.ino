// ===== ESP32-S3: MJPEG Viewer + Bouton LED (UDP 3333) + LiDAR en UART (local) =====
#include <WiFi.h>
#include <WiFiUdp.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <HardwareSerial.h>
#include <vector>

// ------- Wi-Fi & CAM -------
const char* WIFI_SSID = "MSI";
const char* WIFI_PASS = "clem2610";
const char* CAM_HOST  = "192.168.137.122";   // IP ESP32-CAM
const uint16_t CAM_PORT = 80;
const char* CAM_PATH = "/stream";

// ------- UDP (LED CAM) -------
WiFiUDP udpLed;
const uint16_t FLASH_UDP_PORT = 3333;        // LED on CAM

// ------- Bouton (GPIO17 -> GND) -------
const int BUTTON_PIN = 17;
bool flashOn = false;
bool lastBtn  = true;
unsigned long lastDebounceMs = 0;
const unsigned long DEBOUNCE_MS = 40;

// ------- LiDAR TF-Luna sur UART (local au S3) -------
HardwareSerial LUNA(1);          // UART1 (choix libre des pins)
#define LUNA_RX 8                // <-- TF-Luna TX -> S3 RX
#define LUNA_TX 7                // <-- Optionnel (non utilisé ici)
volatile int range_cm = -1;

// ------- TFT -------
TFT_eSPI tft;

// ------- JPEG callback -------
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (x >= tft.width() || y >= tft.height()) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

// ------- HTTP helpers -------
static String readLine(WiFiClient &c) {
  String s; s.reserve(64);
  while (c.connected()) {
    int ch = c.read();
    if (ch < 0) { delay(1); continue; }
    s += (char)ch;
    if (s.endsWith("\n")) break;
  }
  return s;
}
static String extractBoundary(const String& ctype) {
  int idx = ctype.indexOf("boundary="); if (idx < 0) return "";
  String b = ctype.substring(idx + 9); b.trim(); b.replace("\r",""); b.replace("\n","");
  return b;
}
static bool readNextJpeg(WiFiClient &c, const String& boundary, std::vector<uint8_t>& out) {
  out.clear(); String line; int contentLength = -1;
  while (c.connected()) {
    line = readLine(c);
    if (line.length()==0 || line=="\r\n" || line=="\n") break;
    if (line.startsWith("Content-Length:")) contentLength = line.substring(15).toInt();
  }
  if (contentLength > 0) {
    out.resize(contentLength);
    int readTotal = 0;
    while (readTotal < contentLength && c.connected()) {
      int n = c.read(&out[readTotal], contentLength - readTotal);
      if (n > 0) readTotal += n; else delay(1);
    }
    while (c.connected()) { line = readLine(c); if (line.indexOf(boundary) >= 0) break; }
    return true;
  }
  String marker = "--" + boundary; const size_t MAX_JPEG = 120*1024;
  while (c.connected()) {
    if (!c.available()) { delay(1); continue; }
    if (c.peek() == '-') {
      String l = readLine(c);
      if (l.indexOf(marker) >= 0) return !out.empty();
      for (char ch : l) { if (out.size()<MAX_JPEG) out.push_back((uint8_t)ch); else return true; }
    } else {
      int b = c.read(); if (b < 0) { delay(1); continue; }
      if (out.size()<MAX_JPEG) out.push_back((uint8_t)b); else return true;
    }
  }
  return !out.empty();
}

// ------- Commande LED CAM -------
void sendFlash(bool on) {
  char b = on ? '1' : '0';
  udpLed.beginPacket(CAM_HOST, FLASH_UDP_PORT);
  udpLed.write((uint8_t*)&b, 1);
  udpLed.endPacket();
}

// ------- Overlay LiDAR (anti-clignotement) -------
void drawRangeOverlayThrottled(int value) {
  static int lastVal = -9999;
  static uint32_t lastDraw = 0;
  uint32_t now = millis();
  if ((value == lastVal) && (now - lastDraw < 300)) return;  // throttle + no-change

  lastVal = value; lastDraw = now;

  // Efface petite zone seulement
  tft.fillRect(0, 0, 140, 20, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(4, 4);
  if (value >= 0) { tft.print("LiDAR: "); tft.print(value); tft.print(" cm"); }
  else             { tft.print("LiDAR: --"); }
}

// ------- Parser TF-Luna (trame 9 octets 0x59 0x59 DL DH ... SUM) -------
void lunaTickUART() {
  static uint8_t buf[9];
  static int idx = 0;

  while (LUNA.available()) {
    uint8_t b = (uint8_t)LUNA.read();

    if (idx == 0 && b != 0x59) continue;  // cherche header 1
    buf[idx++] = b;

    if (idx == 1 && buf[0] != 0x59) { idx = 0; continue; } // header 1
    if (idx == 2 && buf[1] != 0x59) { idx = 0; continue; } // header 2

    if (idx == 9) {
      uint16_t sum = 0;
      for (int i=0;i<8;i++) sum += buf[i];
      if ( (sum & 0xFF) == buf[8] ) {
        int dist = ((int)buf[3] << 8) | buf[2]; // DH:DL
        if (dist > 0 && dist < 60000) range_cm = dist;
        else range_cm = -1;
      }
      idx = 0;
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // TFT
  tft.init(); tft.setRotation(1);
  tft.fillScreen(TFT_BLACK); tft.setTextDatum(TC_DATUM);
  tft.drawString("Boot...", tft.width()/2, tft.height()/2);

  // JPEG decoder
  TJpgDec.setSwapBytes(true); TJpgDec.setJpgScale(1); TJpgDec.setCallback(tft_output);

  // Wi-Fi
  WiFi.mode(WIFI_STA); WiFi.persistent(false); WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(200); Serial.print("."); }

  // LED UDP
  udpLed.begin(0);   // port local auto

  // UART TF-Luna (115200 8N1)
  // NOTE: on n'envoie aucune commande au module: seule la broche RX nous intéresse pour lire les trames.
  LUNA.begin(115200, SERIAL_8N1, LUNA_RX, LUNA_TX);
  delay(50);

  tft.fillScreen(TFT_BLACK);
}

void loop() {
  // --- Bouton (toggle LED CAM) ---
  bool btn = digitalRead(BUTTON_PIN);
  unsigned long now = millis();
  if (btn != lastBtn && (now - lastDebounceMs) > DEBOUNCE_MS) {
    lastDebounceMs = now;
    if (lastBtn == true && btn == false) { flashOn = !flashOn; sendFlash(flashOn); Serial.printf("Flash -> %s\n", flashOn ? "ON" : "OFF"); }
    lastBtn = btn;
  }

  // --- Stream MJPEG ---
  WiFiClient client;
  if (!client.connect(CAM_HOST, CAM_PORT)) {
    tft.fillScreen(TFT_BLACK);
    tft.drawString("CAM introuvable", tft.width()/2, tft.height()/2);
    delay(800);
    return;
  }
  client.setNoDelay(true); client.setTimeout(250);
  client.printf("GET %s HTTP/1.1\r\nHost: %s\r\nConnection: keep-alive\r\n\r\n", CAM_PATH, CAM_HOST);

  String boundary;
  while (client.connected()) {
    String line = readLine(client);
    if (line.startsWith("Content-Type:")) boundary = extractBoundary(line);
    if (line.length()==0 || line=="\r\n" || line=="\n") break;
  }
  if (boundary.isEmpty()) boundary = "frameboundary";
  tft.fillScreen(TFT_BLACK);

  std::vector<uint8_t> jpeg;

  while (client.connected()) {
    // 1) Lire et afficher une frame
    if (!readNextJpeg(client, boundary, jpeg)) break;
    tft.startWrite();
    TJpgDec.drawJpg(0, 0, jpeg.data(), jpeg.size());
    tft.endWrite();

    // 2) LiDAR UART tick + overlay (throttle)
    lunaTickUART();
    drawRangeOverlayThrottled(range_cm);

    // 3) Bouton réactif pendant le stream
    bool btnNow = digitalRead(BUTTON_PIN);
    if (btnNow != lastBtn && (millis() - lastDebounceMs) > DEBOUNCE_MS) {
      lastDebounceMs = millis();
      if (lastBtn == true && btnNow == false) { flashOn = !flashOn; sendFlash(flashOn); }
      lastBtn = btnNow;
    }
  }

  client.stop();
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Flux termine / reconnect", tft.width()/2, tft.height()/2);
  delay(300);
}
