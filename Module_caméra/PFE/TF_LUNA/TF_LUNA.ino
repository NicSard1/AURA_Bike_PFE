#include <HardwareSerial.h>

// On utilise UART0 car il est câblé physiquement sur GPIO1 (TX0) et GPIO3 (RX0)
HardwareSerial TFSerial(0);

void setup() {
  // On n'utilise pas Serial pour debug car il occupe déjà les pins 1/3
  // On initialise directement l’UART du LiDAR :
  TFSerial.begin(115200, SERIAL_8N1, 3, 1);  
  delay(200);

  // Petite LED pour debug visuel
  pinMode(33, OUTPUT);
}

void loop() {
  if (TFSerial.available() >= 9) {
    uint8_t buf[9];
    TFSerial.readBytes(buf, 9);

    // Format TF-Luna (UART mode)
    // Byte0 = 0x59
    // Byte1 = 0x59
    // Byte2 = Distance LSB
    // Byte3 = Distance MSB
    // Byte4 = Strength LSB
    // Byte5 = Strength MSB
    // Byte6 = Temperature LSB
    // Byte7 = Temperature MSB
    // Byte8 = Checksum

    if (buf[0] == 0x59 && buf[1] == 0x59) {
      int distance = buf[2] + buf[3] * 256;     // en mm
      int strength = buf[4] + buf[5] * 256;
      float temperature = (buf[6] + buf[7] * 256) / 8.0 - 256.0;

      // → Remplace ici par ce que tu veux faire avec la distance
      // (par exemple envoi WiFi, affichage sur écran, servo, etc.)
      
      // Exemple simple : LED ON si obstacle < 1m
      if (distance < 1000) digitalWrite(33, HIGH);
      else                 digitalWrite(33, LOW);
    }
  }
}
