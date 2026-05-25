/*
 * ================================================
 *  BLE Controller - CYD ESP32 (ESP32-2432S028)
 * ================================================
 * Simule un clavier + souris Bluetooth HID
 * Interface tactile sur écran TFT 320x240
 * 
 * LIBRAIRIES à installer :
 *   - TFT_eSPI        (Bodmer)
 *   - XPT2046_Touchscreen
 *   - ESP32-BLE-Keyboard  (T-vK)
 *     → https://github.com/T-vK/ESP32-BLE-Keyboard
 *     → Dans Arduino IDE : Sketch > Include Library > Add .ZIP Library
 * 
 * CONFIG TFT_eSPI (User_Setup.h) :
 *   #define ILI9341_DRIVER
 *   #define TFT_MOSI 13 / SCLK 14 / CS 15 / DC 2 / BL 21
 *   #define TOUCH_CS 33
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <BleKeyboard.h>

// ─────────────────────────────────────────────
//  TOUCH
// ─────────────────────────────────────────────
#define TOUCH_CS  33
#define TOUCH_IRQ 36
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

// Calibration tactile (ajuste si besoin)
#define TOUCH_X_MIN  200
#define TOUCH_X_MAX  3800
#define TOUCH_Y_MIN  200
#define TOUCH_Y_MAX  3800

TFT_eSPI tft = TFT_eSPI();

// ─────────────────────────────────────────────
//  BLE
// ─────────────────────────────────────────────
BleKeyboard bleKeyboard("CYD Controller", "ESP32", 100);

// ─────────────────────────────────────────────
//  LAYOUT BOUTONS
//  Grille 3 colonnes × 5 lignes sur 320×240
//  Zone titre : 30px, reste : 210px → 42px/ligne
//  Largeur col : 106px
// ─────────────────────────────────────────────
#define COLS       3
#define ROWS       5
#define BTN_W      106
#define BTN_H      40
#define BTN_GAP    2
#define GRID_X     1
#define GRID_Y     32

// Types d'action
#define ACT_KEY        0   // touche simple
#define ACT_COMBO      1   // combinaison de touches
#define ACT_TEXT       2   // taper du texte
#define ACT_CONSUMER   3   // touches media/volume

struct Button {
  const char* label;
  int         action;
  // Pour ACT_KEY / ACT_COMBO : jusqu'à 3 touches
  uint8_t     k1, k2, k3;
  // Pour ACT_TEXT : texte à taper
  const char* text;
  // Couleur du bouton
  uint16_t    color;
};

// ─────────────────────────────────────────────
//  DÉFINITION DES 15 BOUTONS (3×5)
// ─────────────────────────────────────────────
Button buttons[ROWS][COLS] = {
  // Ligne 0 — Media
  {
    {"Vol -",    ACT_CONSUMER, KEY_MEDIA_VOLUME_DOWN, 0, 0, nullptr, 0x3186},
    {"Vol +",    ACT_CONSUMER, KEY_MEDIA_VOLUME_UP,   0, 0, nullptr, 0x3186},
    {"Mute",     ACT_CONSUMER, KEY_MEDIA_MUTE,        0, 0, nullptr, 0x3186},
  },
  // Ligne 1 — Navigation Android
  {
    {"Home",     ACT_KEY,    KEY_HOME,   0,       0, nullptr, 0x0343},
    {"Back",     ACT_KEY,    KEY_ESCAPE, 0,       0, nullptr, 0x0343},
    {"Recents",  ACT_COMBO,  KEY_LEFT_ALT, KEY_TAB, 0, nullptr, 0x0343},
  },
  // Ligne 2 — Texte
  {
    {"Copier",   ACT_COMBO,  KEY_LEFT_CTRL, 'c', 0, nullptr, 0x8800},
    {"Coller",   ACT_COMBO,  KEY_LEFT_CTRL, 'v', 0, nullptr, 0x8800},
    {"Tout sel", ACT_COMBO,  KEY_LEFT_CTRL, 'a', 0, nullptr, 0x8800},
  },
  // Ligne 3 — Raccourcis
  {
    {"Chercher", ACT_KEY,    KEY_F3,     0, 0, nullptr,   0x4208},
    {"Notifs",   ACT_COMBO,  KEY_LEFT_ALT, KEY_F1, 0, nullptr, 0x4208},
    {"Params",   ACT_COMBO,  KEY_LEFT_CTRL, KEY_F1, 0, nullptr, 0x4208},
  },
  // Ligne 4 — Texte rapide
  {
    {"OK",       ACT_TEXT,   0, 0, 0, "OK",        0x2208},
    {"OTW",      ACT_TEXT,   0, 0, 0, "On the way!", 0x2208},
    {"Enter",    ACT_KEY,    KEY_RETURN, 0, 0, nullptr,  0x0228},
  },
};

// ─────────────────────────────────────────────
//  ÉTAT
// ─────────────────────────────────────────────
bool    lastConnected = false;
int     pressedRow    = -1;
int     pressedCol    = -1;
unsigned long pressTime = 0;

// ─────────────────────────────────────────────
//  FONCTIONS AFFICHAGE
// ─────────────────────────────────────────────
void drawStatusBar() {
  tft.fillRect(0, 0, 320, 30, TFT_BLACK);
  tft.drawLine(0, 30, 320, 30, 0x3186);

  tft.setTextSize(2);
  tft.setCursor(8, 7);

  if (bleKeyboard.isConnected()) {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.print("BLE ");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.print("Connecte");
    tft.fillCircle(308, 15, 7, TFT_GREEN);
  } else {
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.print("BLE ");
    tft.setTextColor(0xC618, TFT_BLACK);
    tft.print("En attente...");
    tft.fillCircle(308, 15, 7, TFT_YELLOW);
  }
}

void drawButton(int row, int col, bool pressed) {
  int x = GRID_X + col * (BTN_W + BTN_GAP);
  int y = GRID_Y + row * (BTN_H + BTN_GAP);

  Button& b = buttons[row][col];
  uint16_t bg = pressed ? TFT_WHITE : b.color;
  uint16_t fg = pressed ? TFT_BLACK : TFT_WHITE;

  tft.fillRoundRect(x, y, BTN_W, BTN_H, 6, bg);
  tft.drawRoundRect(x, y, BTN_W, BTN_H, 6, pressed ? TFT_WHITE : 0x7BEF);

  tft.setTextColor(fg, bg);
  tft.setTextSize(1);

  // Centrer le texte
  int tw = strlen(b.label) * 6;
  int tx = x + (BTN_W - tw) / 2;
  int ty = y + (BTN_H - 8) / 2;
  tft.setCursor(tx, ty);
  tft.print(b.label);
}

void drawAllButtons() {
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++)
      drawButton(r, c, false);
}

// ─────────────────────────────────────────────
//  EXÉCUTION D'UNE ACTION BLE
// ─────────────────────────────────────────────
void executeButton(int row, int col) {
  if (!bleKeyboard.isConnected()) return;

  Button& b = buttons[row][col];

  switch (b.action) {

    case ACT_KEY:
      bleKeyboard.press((KeyboardKeycode)b.k1);
      delay(50);
      bleKeyboard.releaseAll();
      break;

    case ACT_COMBO:
      if (b.k3) {
        bleKeyboard.press((KeyboardKeycode)b.k1);
        bleKeyboard.press((KeyboardKeycode)b.k2);
        bleKeyboard.press((KeyboardKeycode)b.k3);
      } else if (b.k2) {
        bleKeyboard.press((KeyboardKeycode)b.k1);
        bleKeyboard.press((KeyboardKeycode)b.k2);
      } else {
        bleKeyboard.press((KeyboardKeycode)b.k1);
      }
      delay(50);
      bleKeyboard.releaseAll();
      break;

    case ACT_TEXT:
      if (b.text) bleKeyboard.print(b.text);
      break;

    case ACT_CONSUMER:
      bleKeyboard.press((KeyboardKeycode)b.k1);
      delay(50);
      bleKeyboard.releaseAll();
      break;
  }
}

// ─────────────────────────────────────────────
//  CONVERSION COORDONNÉES TOUCH → ÉCRAN
// ─────────────────────────────────────────────
void getTouchXY(int& sx, int& sy) {
  TS_Point p = touch.getPoint();
  // Rotation 1 (paysage) : inverser X et mapper Y
  sx = map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 320);
  sy = map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 240);
  sx = constrain(sx, 0, 319);
  sy = constrain(sy, 0, 239);
}

bool getButtonAt(int sx, int sy, int& row, int& col) {
  for (int r = 0; r < ROWS; r++) {
    for (int c = 0; c < COLS; c++) {
      int x = GRID_X + c * (BTN_W + BTN_GAP);
      int y = GRID_Y + r * (BTN_H + BTN_GAP);
      if (sx >= x && sx < x + BTN_W && sy >= y && sy < y + BTN_H) {
        row = r; col = c;
        return true;
      }
    }
  }
  return false;
}

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // TFT
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);

  // Touch
  SPI.begin(14, 12, 13, TOUCH_CS);
  touch.begin();
  touch.setRotation(1);

  // BLE
  bleKeyboard.begin();

  // Affichage initial
  drawStatusBar();
  drawAllButtons();
}

// ─────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────
void loop() {
  // Mise à jour statut connexion
  bool nowConnected = bleKeyboard.isConnected();
  if (nowConnected != lastConnected) {
    lastConnected = nowConnected;
    drawStatusBar();
  }

  // Gestion touch
  if (touch.tirqTouched() && touch.touched()) {
    int sx, sy, row, col;
    getTouchXY(sx, sy);

    if (getButtonAt(sx, sy, row, col)) {
      if (pressedRow != row || pressedCol != col) {
        // Nouveau bouton pressé
        if (pressedRow >= 0) drawButton(pressedRow, pressedCol, false);
        pressedRow = row;
        pressedCol = col;
        pressTime  = millis();
        drawButton(row, col, true);
        executeButton(row, col);
      }
    }
  } else {
    // Relâché
    if (pressedRow >= 0 && millis() - pressTime > 80) {
      drawButton(pressedRow, pressedCol, false);
      pressedRow = -1;
      pressedCol = -1;
    }
  }

  delay(20);
}
