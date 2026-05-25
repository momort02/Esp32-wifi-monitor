#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <BleKeyboard.h>

// --- Configuration Hardware CYD ---
#define TOUCH_CS  33
#define TOUCH_IRQ 36
#define TFT_BL    21

SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);
TFT_eSPI tft = TFT_eSPI();
BleKeyboard bleKeyboard("CYD Remote", "ESP32", 100);

// Calibration tactile (ajustée pour le CYD)
#define TOUCH_X_MIN  200
#define TOUCH_X_MAX  3700
#define TOUCH_Y_MIN  200
#define TOUCH_Y_MAX  3700

// Structure des boutons
#define COLS 3
#define ROWS 5
#define BTN_W 104
#define BTN_H 38
#define GRID_Y 35

enum ActionType { ACT_KEY, ACT_COMBO, ACT_TEXT, ACT_MEDIA };
struct Button {
  const char* label;
  ActionType type;
  uint16_t k1; // Code touche ou index media
  uint16_t color;
};

// Grille de boutons optimisée Android
Button btns[ROWS][COLS] = {
  {{"VOL -", ACT_MEDIA, 1, 0x3186}, {"VOL +", ACT_MEDIA, 2, 0x3186}, {"MUTE", ACT_MEDIA, 3, 0x3186}},
  {{"HOME", ACT_KEY, KEY_HOME, 0x0343}, {"RETOUR", ACT_KEY, KEY_ESC, 0x0343}, {"RECENT", ACT_COMBO, 0, 0x0343}},
  {{"COPIER", ACT_COMBO, 'c', 0x8800}, {"COLLER", ACT_COMBO, 'v', 0x8800}, {"TOUT", ACT_COMBO, 'a', 0x8800}},
  {{"PLAY", ACT_MEDIA, 4, 0x4208}, {"NEXT", ACT_MEDIA, 5, 0x4208}, {"RECHERCH", ACT_KEY, KEY_F3, 0x4208}},
  {{"ENTRER", ACT_KEY, KEY_RETURN, 0x0228}, {"ESPACE", ACT_KEY, ' ', 0x2208}, {"OK", ACT_TEXT, 0, 0x2208}}
};

int pRow = -1, pCol = -1;

void drawBtn(int r, int c, bool p) {
  int x = 2 + c * (BTN_W + 2);
  int y = GRID_Y + r * (BTN_H + 2);
  tft.fillRoundRect(x, y, BTN_W, BTN_H, 4, p ? TFT_WHITE : btns[r][c].color);
  tft.setTextColor(p ? TFT_BLACK : TFT_WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(btns[r][c].label, x + (BTN_W/2), y + (BTN_H/2));
}

void execute(int r, int c) {
  if (!bleKeyboard.isConnected()) return;
  Button& b = btns[r][c];
  
  switch(b.type) {
    case ACT_KEY: 
      bleKeyboard.write(b.k1); 
      break;
    case ACT_TEXT: 
      bleKeyboard.print("OK"); 
      break;
    case ACT_MEDIA:
      if(b.k1 == 1) bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
      else if(b.k1 == 2) bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
      else if(b.k1 == 3) bleKeyboard.write(KEY_MEDIA_MUTE);
      else if(b.k1 == 4) bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
      else if(b.k1 == 5) bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
      break;
    case ACT_COMBO:
      if(r == 1) { // RECENT APPS (Alt+Tab)
        bleKeyboard.press(KEY_LEFT_ALT); bleKeyboard.press(KEY_TAB);
      } else { // CTRL + touche
        bleKeyboard.press(KEY_LEFT_CTRL); bleKeyboard.press(b.k1);
      }
      delay(50); bleKeyboard.releaseAll();
      break;
  }
}

void setup() {
  pinMode(TFT_BL, OUTPUT); digitalWrite(TFT_BL, HIGH);
  tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);
  touchSPI.begin(14, 12, 13, TOUCH_CS);
  touch.begin(touchSPI); touch.setRotation(1);
  bleKeyboard.begin();
  
  tft.setCursor(10, 8); tft.setTextSize(2); tft.setTextColor(TFT_YELLOW);
  tft.print("PRET POUR ANDROID");
  for(int r=0; r<ROWS; r++) for(int c=0; c<COLS; c++) drawBtn(r, c, false);
}

void loop() {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();
    int sx = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 320);
    int sy = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 240);

    for (int r = 0; r < ROWS; r++) {
      for (int c = 0; c < COLS; c++) {
        int bx = 2 + c * (BTN_W + 2);
        int by = GRID_Y + r * (BTN_H + 2);
        if (sx >= bx && sx < bx + BTN_W && sy >= by && sy < by + BTN_H) {
          if (pRow != r || pCol != c) {
            drawBtn(r, c, true);
            execute(r, c);
            pRow = r; pCol = c;
          }
        }
      }
    }
  } else if (pRow != -1) {
    drawBtn(pRow, pCol, false);
    pRow = -1; pCol = -1;
  }
}
