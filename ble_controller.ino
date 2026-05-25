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

#define TOUCH_X_MIN  200
#define TOUCH_X_MAX  3800
#define TOUCH_Y_MIN  200
#define TOUCH_Y_MAX  3800

TFT_eSPI tft = TFT_eSPI();
BleKeyboard bleKeyboard("CYD Controller", "ESP32", 100);

// --- Structure des Boutons ---
#define COLS       3
#define ROWS       5
#define BTN_W      104
#define BTN_H      38
#define BTN_GAP    2
#define GRID_X     2
#define GRID_Y     35

enum ActionType { ACT_KEY, ACT_COMBO, ACT_TEXT, ACT_CONSUMER };

struct Button {
  const char* label;
  ActionType  action;
  uint16_t    k1, k2, k3; // Changé en uint16_t pour supporter les codes étendus
  const char* text;
  uint16_t    color;
};

// --- Grille de boutons ---
// Note: On force le cast (uint16_t) pour les touches Media afin d'éviter l'erreur de conversion
Button buttons[ROWS][COLS] = {
  {{"Vol -", ACT_CONSUMER, (uint16_t)KEY_MEDIA_VOLUME_DOWN, 0, 0, NULL, 0x3186}, {"Vol +", ACT_CONSUMER, (uint16_t)KEY_MEDIA_VOLUME_UP, 0, 0, NULL, 0x3186}, {"Mute", ACT_CONSUMER, (uint16_t)KEY_MEDIA_MUTE, 0, 0, NULL, 0x3186}},
  {{"Home", ACT_KEY, KEY_HOME, 0, 0, NULL, 0x0343}, {"Back", ACT_KEY, KEY_ESC, 0, 0, NULL, 0x0343}, {"Apps", ACT_COMBO, KEY_LEFT_ALT, KEY_TAB, 0, NULL, 0x0343}},
  {{"Copier", ACT_COMBO, KEY_LEFT_CTRL, 'c', 0, NULL, 0x8800}, {"Coller", ACT_COMBO, KEY_LEFT_CTRL, 'v', 0, NULL, 0x8800}, {"Tout", ACT_COMBO, KEY_LEFT_CTRL, 'a', 0, NULL, 0x8800}},
  {{"Search", ACT_KEY, KEY_F3, 0, 0, NULL, 0x4208}, {"Notifs", ACT_COMBO, KEY_LEFT_ALT, KEY_F1, 0, NULL, 0x4208}, {"Params", ACT_COMBO, KEY_LEFT_CTRL, KEY_F1, 0, NULL, 0x4208}},
  {{"OK", ACT_TEXT, 0, 0, 0, "OK", 0x2208}, {"OTW", ACT_TEXT, 0, 0, 0, "On the way!", 0x2208}, {"Enter", ACT_KEY, KEY_RETURN, 0, 0, NULL, 0x0228}}
};

bool lastConnected = false;
int pressedRow = -1, pressedCol = -1;
unsigned long pressTime = 0;

void drawStatusBar() {
  tft.fillRect(0, 0, 320, 32, TFT_BLACK);
  tft.drawLine(0, 32, 320, 32, 0x7BEF);
  tft.setCursor(10, 8);
  tft.setTextSize(2);
  
  if (bleKeyboard.isConnected()) {
    tft.setTextColor(TFT_GREEN);
    tft.print("ANDROID: OK");
  } else {
    tft.setTextColor(TFT_YELLOW);
    tft.print("BLE: ATTENTE...");
  }
}

void drawButton(int row, int col, bool pressed) {
  int x = GRID_X + col * (BTN_W + BTN_GAP);
  int y = GRID_Y + row * (BTN_H + BTN_GAP);
  Button& b = buttons[row][col];
  
  uint16_t bg = pressed ? TFT_WHITE : b.color;
  uint16_t fg = pressed ? TFT_BLACK : TFT_WHITE;
  
  tft.fillRoundRect(x, y, BTN_W, BTN_H, 4, bg);
  tft.setTextColor(fg);
  tft.setTextSize(1);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(b.label, x + (BTN_W/2), y + (BTN_H/2));
}

void executeButton(int row, int col) {
  if (!bleKeyboard.isConnected()) return;
  Button& b = buttons[row][col];

  switch (b.action) {
    case ACT_KEY:
      bleKeyboard.write((uint8_t)b.k1);
      break;
    case ACT_COMBO:
      bleKeyboard.press((uint8_t)b.k1);
      if (b.k2) bleKeyboard.press((uint8_t)b.k2);
      if (b.k3) bleKeyboard.press((uint8_t)b.k3);
      delay(50);
      bleKeyboard.releaseAll();
      break;
    case ACT_TEXT:
      bleKeyboard.print(b.text);
      break;
    case ACT_CONSUMER:
      // On reconstruit le code média à partir du uint16_t
      const uint8_t mediaKey[2] = {(uint8_t)b.k1, (uint8_t)(b.k1 >> 8)};
      bleKeyboard.write(mediaKey);
      break;
  }
}

void setup() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  touchSPI.begin(14, 12, 13, TOUCH_CS);
  touch.begin(touchSPI);
  touch.setRotation(1);

  bleKeyboard.begin();
  drawStatusBar();
  for(int r=0; r<ROWS; r++) for(int c=0; c<COLS; c++) drawButton(r, c, false);
}

void loop() {
  if (bleKeyboard.isConnected() != lastConnected) {
    lastConnected = bleKeyboard.isConnected();
    drawStatusBar();
  }

  if (touch.tirqTouched() && touch.touched()) {
    TS_Point p = touch.getPoint();
    int sx = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 320);
    int sy = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 240);

    for (int r = 0; r < ROWS; r++) {
      for (int c = 0; c < COLS; c++) {
        int bx = GRID_X + c * (BTN_W + BTN_GAP);
        int by = GRID_Y + r * (BTN_H + BTN_GAP);
        if (sx >= bx && sx < bx + BTN_W && sy >= by && sy < by + BTN_H) {
          if (pressedRow != r || pressedCol != c) {
            drawButton(r, c, true);
            executeButton(r, c);
            pressedRow = r; pressedCol = c;
            pressTime = millis();
          }
        }
      }
    }
  } else if (pressedRow != -1 && millis() - pressTime > 150) {
    drawButton(pressedRow, pressedCol, false);
    pressedRow = -1; pressedCol = -1;
  }
  delay(10);
}
