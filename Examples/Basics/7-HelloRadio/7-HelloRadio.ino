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
