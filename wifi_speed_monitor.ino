/*
 * ================================================
 *  WiFi Speed Monitor - CYD ESP32 (ESP32-2432S028)
 * ================================================
 * Ecran 320x240 paysage
 * Affiche débit download + ping en gros chiffres
 * Config WiFi via AP + page web (192.168.4.1)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <HTTPClient.h>

#define AP_SSID      "CYD-Config"
#define AP_PASSWORD  "12345678"
#define SPEED_TEST_URL "http://ipv4.download.thinkbroadband.com/512KB.zip"
#define PING_HOST      "8.8.8.8"
#define MEASURE_INTERVAL 30000

TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
Preferences prefs;

String savedSSID     = "";
String savedPassword = "";
bool   connected     = false;
float  downloadMbps  = -2;  // -2 = pas encore mesuré
int    pingMs        = -2;
unsigned long lastMeasure = 0;

// ─────────────────────────────────────────────
//  PAGE WEB
// ─────────────────────────────────────────────
const char CONFIG_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="fr"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Speed Monitor Config</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:sans-serif;background:#0f2027;min-height:100vh;display:flex;align-items:center;justify-content:center;color:white}
.card{background:rgba(255,255,255,0.08);border:1px solid rgba(255,255,255,0.15);border-radius:20px;padding:35px;width:340px}
.logo{font-size:44px;text-align:center;margin-bottom:8px}
h1{text-align:center;margin-bottom:4px}
.sub{text-align:center;color:#aaa;font-size:.85em;margin-bottom:24px}
label{display:block;font-size:.85em;color:#ccc;margin-bottom:5px;margin-top:16px}
input{width:100%;padding:11px 14px;border-radius:8px;border:1px solid rgba(255,255,255,0.2);background:rgba(255,255,255,0.07);color:white;font-size:1em;outline:none}
.btn{width:100%;margin-top:24px;padding:13px;border-radius:10px;border:none;background:linear-gradient(90deg,#4fc3f7,#0288d1);color:white;font-size:1em;font-weight:bold;cursor:pointer}
#sb{background:transparent;border:1px solid rgba(255,255,255,0.2);color:#ccc;border-radius:7px;padding:6px 12px;font-size:.82em;cursor:pointer;margin-top:14px}
.ni{padding:7px 11px;background:rgba(255,255,255,0.05);border-radius:7px;margin-bottom:5px;cursor:pointer;display:flex;justify-content:space-between;font-size:.86em}
.ni:hover{background:rgba(79,195,247,0.15)}
</style></head><body><div class="card">
<div class="logo">⚡</div><h1>Speed Monitor</h1><p class="sub">Config WiFi CYD</p>
<button id="sb" onclick="scan()">🔍 Scanner les réseaux</button>
<div id="nets"></div>
<form action="/save" method="POST">
<label>Réseau (SSID)</label><input type="text" name="ssid" id="ssid" required>
<label>Mot de passe</label><input type="password" name="password">
<button type="submit" class="btn">✅ Connecter</button>
</form></div>
<script>
function scan(){fetch('/scan').then(r=>r.json()).then(n=>{
document.getElementById('nets').innerHTML=n.map(x=>
`<div class="ni" onclick="document.getElementById('ssid').value='${x.ssid}'"><span>${x.ssid}</span><span style="color:#aaa">${x.rssi}dBm</span></div>`).join('')})}
</script></body></html>
)rawliteral";

// ─────────────────────────────────────────────
//  MESURES
// ─────────────────────────────────────────────
int measurePing() {
  unsigned long t = millis();
  WiFiClient c;
  c.setTimeout(2000);
  bool ok = c.connect(PING_HOST, 53);
  int ms = (int)(millis() - t);
  c.stop();
  return ok ? ms : -1;
}

float measureDownload() {
  HTTPClient http;
  http.begin(SPEED_TEST_URL);
  http.setTimeout(8000);
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return -1; }
  int total = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[512];
  int received = 0;
  unsigned long t = millis();
  while (http.connected() && received < total) {
    int av = stream->available();
    if (av > 0) received += stream->readBytes(buf, min(av, (int)sizeof(buf)));
    if (millis() - t > 6000) break;
  }
  float elapsed = (millis() - t) / 1000.0;
  http.end();
  if (elapsed <= 0 || received <= 0) return -1;
  return (received * 8.0) / (elapsed * 1000000.0);
}

// ─────────────────────────────────────────────
//  AFFICHAGE  (320x240, rotation 1)
// ─────────────────────────────────────────────
//
//  Y=0  ┌─────────────────────────────────┐
//       │       SPEED MONITOR             │  taille 2 → 16px haut
//  Y=20 ├─────────────────────────────────┤
//       │ DL                              │  taille 1
//  Y=32 │  XX.X  Mb/s                     │  taille 4 → 32px/car → valeur+unité ~40px
//  Y=75 ├─────────────────────────────────┤
//       │ PING                            │
//  Y=90 │  XXX  ms                        │
// Y=132 ├─────────────────────────────────┤
//       │ ssid              IP  ●         │  taille 1
// Y=240 └─────────────────────────────────┘

void drawValue(int x, int y, String val, String unit, uint16_t col) {
  // Valeur taille 5 (40px haut), unité taille 2 (16px)
  tft.setTextSize(5);
  tft.setTextColor(col, TFT_BLACK);
  tft.setCursor(x, y);
  // Pad pour effacer les anciens chiffres
  String padded = val;
  while (padded.length() < 6) padded += " ";
  tft.print(padded);

  tft.setTextSize(2);
  tft.setTextColor(0xC618, TFT_BLACK);
  tft.setCursor(x, y + 44);
  tft.print(unit + "      ");
}

void drawScreen(bool measuring) {
  tft.fillScreen(TFT_BLACK);

  // ── Titre ──
  tft.setTextSize(2);
  tft.setTextColor(0x07FF, TFT_BLACK);
  tft.setCursor(55, 4);
  tft.print("SPEED MONITOR");
  tft.drawLine(0, 22, 320, 22, 0x3186);

  // ── Download ──
  tft.setTextSize(1);
  tft.setTextColor(0x04FF, TFT_BLACK);
  tft.setCursor(10, 28);
  tft.print("DOWNLOAD");

  if (measuring) {
    drawValue(10, 38, "...", "Mb/s", 0xC618);
  } else if (downloadMbps <= -2) {
    drawValue(10, 38, "---", "Mb/s", 0xC618);
  } else if (downloadMbps < 0) {
    drawValue(10, 38, "ERR", "Mb/s", TFT_RED);
  } else {
    uint16_t col = downloadMbps >= 10 ? TFT_GREEN : (downloadMbps >= 2 ? TFT_YELLOW : TFT_RED);
    String v = downloadMbps < 10 ? String(downloadMbps, 1) : String((int)downloadMbps);
    drawValue(10, 38, v, "Mb/s", col);
  }

  tft.drawLine(0, 118, 320, 118, 0x3186);

  // ── Ping ──
  tft.setTextSize(1);
  tft.setTextColor(0xFD20, TFT_BLACK);
  tft.setCursor(10, 124);
  tft.print("PING");

  if (measuring) {
    drawValue(10, 134, "...", "ms", 0xC618);
  } else if (pingMs <= -2) {
    drawValue(10, 134, "---", "ms", 0xC618);
  } else if (pingMs < 0) {
    drawValue(10, 134, "ERR", "ms", TFT_RED);
  } else {
    uint16_t col = pingMs < 50 ? TFT_GREEN : (pingMs < 150 ? TFT_YELLOW : TFT_RED);
    drawValue(10, 134, String(pingMs), "ms", col);
  }

  // ── Barre status bas ──
  tft.drawLine(0, 214, 320, 214, 0x3186);
  tft.setTextSize(1);
  tft.setTextColor(0x7BEF, TFT_BLACK);
  tft.setCursor(10, 220);
  tft.print(savedSSID);
  tft.setCursor(10, 230);
  tft.print(WiFi.localIP().toString());
  tft.fillCircle(308, 224, 7, connected ? TFT_GREEN : TFT_RED);
}

void drawAPScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(15, 8);
  tft.print("Configuration WiFi");
  tft.drawLine(0, 28, 320, 28, TFT_YELLOW);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 40);
  tft.print("1. Connecte-toi au WiFi :");
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(15, 54);
  tft.print(AP_SSID);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(15, 75);
  tft.print("Mot de passe: " AP_PASSWORD);

  tft.setCursor(10, 95);
  tft.print("2. Ouvre dans Chrome :");
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(15, 109);
  tft.print("192.168.4.1");

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 140);
  tft.print("3. Entre ton SSID + mot de passe");
  tft.setCursor(10, 152);
  tft.print("   et clique Connecter.");

  tft.fillCircle(305, 15, 8, TFT_GREEN);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.setCursor(300, 12);
  tft.print("AP");
}

// ─────────────────────────────────────────────
//  SERVEUR WEB
// ─────────────────────────────────────────────
void handleRoot() { server.send(200, "text/html", CONFIG_PAGE); }
void handleScan() {
  int n = WiFi.scanNetworks();
  String j = "[";
  for (int i = 0; i < n; i++) {
    if (i) j += ",";
    j += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + WiFi.RSSI(i) + "}";
  }
  j += "]";
  server.send(200, "application/json", j);
}
void handleSave() {
  if (server.hasArg("ssid")) {
    savedSSID     = server.arg("ssid");
    savedPassword = server.arg("password");
    prefs.begin("wifi", false);
    prefs.putString("ssid",     savedSSID);
    prefs.putString("password", savedPassword);
    prefs.end();
    server.send(200, "text/html",
      "<html><body style='background:#0f2027;color:white;font-family:sans-serif;"
      "text-align:center;padding-top:80px'><h2>✅ Sauvegardé !</h2>"
      "<p>Redémarrage en cours...</p></body></html>");
    delay(1500);
    ESP.restart();
  }
}

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);

  prefs.begin("wifi", true);
  savedSSID     = prefs.getString("ssid",     "");
  savedPassword = prefs.getString("password", "");
  prefs.end();

  if (savedSSID.length() > 0) {
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(60, 105);
    tft.print("Connexion...");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(60, 130);
    tft.print(savedSSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    int att = 0;
    while (WiFi.status() != WL_CONNECTED && att < 30) { delay(500); att++; }

    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      server.on("/", handleRoot);
      server.begin();
      drawScreen(false);
      return;
    }
    savedSSID = "";
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  server.on("/",     HTTP_GET,  handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/scan", HTTP_GET,  handleScan);
  server.begin();
  drawAPScreen();
}

// ─────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────
void loop() {
  server.handleClient();
  if (!connected) return;

  if (WiFi.status() != WL_CONNECTED) {
    connected = false;
    WiFi.reconnect();
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) delay(200);
    if (WiFi.status() == WL_CONNECTED) connected = true;
    drawScreen(false);
    return;
  }

  unsigned long now = millis();
  if (now - lastMeasure >= MEASURE_INTERVAL || lastMeasure == 0) {
    drawScreen(true);  // affiche "..."
    pingMs       = measurePing();
    downloadMbps = measureDownload();
    lastMeasure  = millis();
    drawScreen(false); // affiche les vraies valeurs
  }

  delay(500);
}
