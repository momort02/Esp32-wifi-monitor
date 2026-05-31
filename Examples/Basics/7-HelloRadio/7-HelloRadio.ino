/*******************************************************************
  A simple radio player for the ESP32 Cheap Yellow Display.
  This example only works with ESP32 Board package v2.0.17 (not 3.x.x!)
*******************************************************************/

#include <Arduino.h>

#if ESP_IDF_VERSION_MAJOR == 5
#warning This example only works with ESP32 Board package v2.0.17 (not 3.x.x!)
void setup() {}
void loop() {}

#else

#include <WiFi.h>
#include <TFT_eSPI.h>
#include "Audio.h"

const char* ssid     = "Freebox-AFD2EF";
const char* password = "accusavero7-corporatur&-torris-odiosa7";

TFT_eSPI tft = TFT_eSPI();
Audio audio(true, I2S_DAC_CHANNEL_LEFT_EN);

void setup() {
  Serial.begin(115200);

  tft.init();
  tft.setRotation(1);
  tft.setTextWrap(true, true);
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Title", 0, 10, 1);

  WiFi.begin(ssid, password);
  Serial.print("Connecting to ");
  Serial.println(ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.println(WiFi.localIP());

  audio.forceMono(true);
  audio.setVolume(10);

  bool succeeded;
  do {
    succeeded = audio.connecttohost("http://sc8.1.fm:8100/;");
    delay(500);
    Serial.println("Retrying...");
  } while (!succeeded);
}

void loop() {
  audio.loop();
}

void printTitle(const char* info) {
  tft.fillRect(0, 20, 320, 200, TFT_BLACK);
  tft.setCursor(0, 20, 4);
  tft.setTextColor(TFT_SKYBLUE);
  tft.println(info);
}

void printInfo(const char* info) {
  tft.fillRect(0, 230, 320, 10, TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.drawString(info, 0, 230, 1);
}

void audio_info(const char* info)          { Serial.printf("info        %s\n", info); printInfo(info); }
void audio_id3data(const char* info)       { Serial.printf("id3data     %s\n", info); }
void audio_eof_mp3(const char* info)       { Serial.printf("eof_mp3     %s\n", info); }
void audio_showstation(const char* info)   { Serial.printf("station     %s\n", info); }
void audio_showstreamtitle(const char* info) { Serial.printf("streamtitle %s\n", info); printTitle(info); }
void audio_bitrate(const char* info)       { Serial.printf("bitrate     %s\n", info); }
void audio_commercial(const char* info)    { Serial.printf("commercial  %s\n", info); }
void audio_icyurl(const char* info)        { Serial.printf("icyurl      %s\n", info); }
void audio_lasthost(const char* info)      { Serial.printf("lasthost    %s\n", info); }
void audio_eof_speech(const char* info)    { Serial.printf("eof_speech  %s\n", info); }

#endif // ESP_IDF_VERSION_MAJOR
