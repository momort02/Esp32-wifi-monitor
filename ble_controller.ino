#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <BleKeyboard.h>

#define TOUCH_CS  33
#define TOUCH_IRQ 36
#define TFT_BL    21

SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
TFT_eSPI tft = TFT_eSPI();
BleKeyboard bleKeyboard("CYD Test", "ESP32", 100);

void setup() {
  Serial.begin(115200); // Pour voir les logs sur le PC
  
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  touchSPI.begin(14, 12, 13, TOUCH_CS);
  touch.begin(touchSPI);
  touch.setRotation(1);

  bleKeyboard.begin();
  
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Test de clic...");
}

void loop() {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();
    
    // Affichage sur l'écran pour confirmer la détection
    tft.fillRect(0, 100, 320, 50, TFT_BLACK);
    tft.setCursor(10, 100);
    tft.printf("X:%d Y:%d", p.x, p.y);
    
    // Envoi d'un log au PC via USB
    Serial.printf("Tactile detecte ! X: %d, Y: %d\n", p.x, p.y);

    if (bleKeyboard.isConnected()) {
      // On teste une commande simple (Volume Up)
      bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
      tft.setTextColor(TFT_GREEN);
      tft.setCursor(10, 150);
      tft.println("Ordre BLE envoye!");
      Serial.println("Ordre envoyé au téléphone via Bluetooth");
    } else {
      tft.setTextColor(TFT_RED);
      tft.setCursor(10, 150);
      tft.println("Bluetooth non connecte");
    }
    delay(200); // Anti-rebond
  }
}
