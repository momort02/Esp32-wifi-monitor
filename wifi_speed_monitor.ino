/*
 * ================================================
 *  WiFi Speed Monitor - CYD ESP32 (ESP32-2432S028)
 * ================================================
 * Affiche en gros chiffres :
 *   - Débit Download (mesuré via HTTP)
 *   - Ping (latence DNS)
 * Se rafraîchit en boucle toutes les 30 secondes
 * Config WiFi via AP + page web (192.168.4.1)
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <HTTPClient.h>

// ─────────────────────────────────────────────
//  CONSTANTES
// ─────────────────────────────────────────────
#define AP_SSID      "CYD-Config"
#define AP_PASSWORD  "12345678"

// Fichier de test pour mesurer le débit (~500KB)
#define SPEED_TEST_URL "http://speedtest.tele2.net/512KB.zip"
#define PING_HOST      "8.8.8.8"
#define MEASURE_INTERVAL 30000  // ms entre chaque mesure

// Couleurs
#define COLOR_BG      TFT_BLACK
#define COLOR_TITLE   0x07FF   // Cyan
#define COLOR_VALUE   TFT_WHITE
#define COLOR_UNIT    0xC618   // Gris clair
#define COLOR_GOOD    TFT_GREEN
#define COLOR_WARN    TFT_YELLOW
#define COLOR_BAD     TFT_RED
#define COLOR_DL      0x04FF   // Bleu clair
#define COLOR_PING    0xFD20   // Orange

// ─────────────────────────────────────────────
//  OBJETS
// ─────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
Preferences prefs;

// ─────────────────────────────────────────────
//  ÉTAT
// ─────────────────────────────────────────────
String savedSSID     = "";
String savedPassword = "";
bool   connected     = false;

float  downloadMbps  = 0;
int    pingMs        = -1;
unsigned long lastMeasure = 0;
bool   measuring     = false;

// ─────────────────────────────────────────────
//  PAGE WEB CONFIG
// ─────────────────────────────────────────────
const char CONFIG_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>CYD Speed Monitor</title>
  <style>
    * { box-sizing:border-box; margin:0; padding:0; }
    body { font-family:'Segoe UI',sans-serif; background:linear-gradient(135deg,#0f2027,#203a43,#2c5364);
           min-height:100vh; display:flex; align-items:center; justify-content:center; color:white; }
    .card { background:rgba(255,255,255,0.08); backdrop-filter:blur(10px);
            border:1px solid rgba(255,255,255,0.15); border-radius:20px;
            padding:40px 35px; width:360px; box-shadow:0 20px 60px rgba(0,0,0,0.4); }
    .logo { font-size:48px; text-align:center; margin-bottom:8px; }
    h1 { text-align:center; font-size:1.4em; margin-bottom:4px; }
    .sub { text-align:center; color:#aaa; font-size:.85em; margin-bottom:28px; }
    label { display:block; font-size:.85em; color:#ccc; margin-bottom:6px; margin-top:18px; }
    input { width:100%; padding:12px 16px; border-radius:10px; border:1px solid rgba(255,255,255,0.2);
            background:rgba(255,255,255,0.07); color:white; font-size:1em; outline:none; }
    input:focus { border-color:#4fc3f7; }
    .btn { width:100%; margin-top:28px; padding:14px; border-radius:12px; border:none;
           background:linear-gradient(90deg,#4fc3f7,#0288d1); color:white;
           font-size:1.1em; font-weight:bold; cursor:pointer; }
    #scan-btn { background:transparent; border:1px solid rgba(255,255,255,0.25); color:#ccc;
                border-radius:8px; padding:7px 14px; font-size:.82em; cursor:pointer; margin-top:16px; }
    .net-item { padding:8px 12px; background:rgba(255,255,255,0.05); border-radius:8px;
                margin-bottom:6px; cursor:pointer; font-size:.88em;
                display:flex; justify-content:space-between; }
    .net-item:hover { background:rgba(79,195,247,0.15); }
  </style>
</head>
<body>
  <div class="card">
    <div class="logo">⚡</div>
    <h1>Speed Monitor</h1>
    <p class="sub">Configuration WiFi CYD</p>
    <button id="scan-btn" onclick="scan()">🔍 Scanner</button>
    <div id="nets"></div>
    <form action="/save" method="POST">
      <label>Réseau (SSID)</label>
      <input type="text" name="ssid" id="ssid" required>
      <label>Mot de passe</label>
      <input type="password" name="password" id="pwd">
      <button type="submit" class="btn">✅ Connecter</button>
    </form>
  </div>
  <script>
    function scan(){
      fetch('/scan').then(r=>r.json()).then(nets=>{
        document.getElementById('nets').innerHTML=nets.map(n=>
          `<div class="net-item" onclick="document.getElementById('ssid').value='${n.ssid}'">
            <span>${n.ssid}</span><span style="color:#aaa">${n.rssi}dBm</span>
          </div>`).join('');
      });
    }
  </script>
</body>
</html>
)rawliteral";

// ─────────────────────────────────────────────
//  MESURES
// ─────────────────────────────────────────────
int measurePing() {
  unsigned long t = millis();
  WiFiClient client;
  client.setTimeout(2000);
  bool ok = client.connect(PING_HOST, 53);
  int ms = (int)(millis() - t);
  client.stop();
  return ok ? ms : -1;
}

float measureDownload() {
  HTTPClient http;
  http.begin(SPEED_TEST_URL);
  http.setTimeout(15000);
  
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return -1;
  }
  
  int total = http.getSize();
  WiFiClient* stream = http.getStreamPtr();
  
  uint8_t buf[512];
  int received = 0;
  unsigned long t = millis();
  
  while (http.connected() && received < total) {
    int avail = stream->available();
    if (avail > 0) {
      int toRead = min(avail, (int)sizeof(buf));
      received += stream->readBytes(buf, toRead);
    }
    if (millis() - t > 12000) break;
  }
  
  float elapsed = (millis() - t) / 1000.0;
  http.end();
  
  if (elapsed <= 0 || received <= 0) return -1;
  return (received * 8.0) / (elapsed * 1000000.0); // Mbps
}

// ─────────────────────────────────────────────
//  AFFICHAGE
// ─────────────────────────────────────────────
void drawAPScreen() {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(TFT_YELLOW, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(15, 10);
  tft.print("Configuration");

  tft.setTextColor(TFT_WHITE, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(10, 45);
  tft.print("1. WiFi: ");
  tft.setTextColor(TFT_CYAN, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 58);
  tft.print(AP_SSID);

  tft.setTextColor(TFT_WHITE, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(10, 85);
  tft.print("   mdp: " AP_PASSWORD);

  tft.setCursor(10, 105);
  tft.print("2. Navigateur:");
  tft.setTextColor(TFT_GREEN, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 118);
  tft.print("192.168.4.1");
}

void drawMainScreen() {
  tft.fillScreen(COLOR_BG);

  // ── Titre ──
  tft.setTextColor(COLOR_TITLE, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(70, 5);
  tft.print("SPEED MONITOR");

  tft.drawLine(0, 28, 320, 28, 0x3186);

  // ── Section Download ──
  tft.setTextColor(COLOR_DL, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(10, 38);
  tft.print("DOWNLOAD");

  // Grande valeur débit
  tft.setTextSize(6);
  tft.setCursor(10, 55);
  if (downloadMbps < 0) {
    tft.setTextColor(COLOR_BAD, COLOR_BG);
    tft.print("ERR ");
  } else {
    // Couleur selon débit
    if      (downloadMbps >= 10) tft.setTextColor(COLOR_GOOD,  COLOR_BG);
    else if (downloadMbps >= 2)  tft.setTextColor(COLOR_WARN,  COLOR_BG);
    else                          tft.setTextColor(COLOR_BAD,   COLOR_BG);
    
    if (downloadMbps < 10) {
      tft.print(downloadMbps, 1);
    } else {
      tft.print((int)downloadMbps);
      tft.print("  ");
    }
  }

  // Unité
  tft.setTextColor(COLOR_UNIT, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 125);
  tft.print("Mb/s");

  // ── Séparateur ──
  tft.drawLine(0, 148, 320, 148, 0x3186);

  // ── Section Ping ──
  tft.setTextColor(COLOR_PING, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(10, 156);
  tft.print("PING");

  tft.setTextSize(6);
  tft.setCursor(10, 170);
  if (pingMs < 0) {
    tft.setTextColor(COLOR_BAD, COLOR_BG);
    tft.print("---");
  } else {
    if      (pingMs < 50)  tft.setTextColor(COLOR_GOOD, COLOR_BG);
    else if (pingMs < 150) tft.setTextColor(COLOR_WARN, COLOR_BG);
    else                    tft.setTextColor(COLOR_BAD,  COLOR_BG);
    tft.print(pingMs);
    tft.print("  ");
  }

  tft.setTextColor(COLOR_UNIT, COLOR_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 230);
  tft.print("ms");

  // ── SSID + statut bas ──
  tft.drawLine(0, 212, 320, 212, 0x3186);
  tft.setTextColor(0x7BEF, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(10, 220);
  tft.print(savedSSID);
  tft.setCursor(10, 230);
  tft.print(WiFi.localIP().toString());

  // Indicateur connexion
  tft.fillCircle(308, 220, 7, connected ? TFT_GREEN : TFT_RED);
}

void drawMeasuring() {
  // Efface les zones de valeurs et affiche "..."
  tft.fillRect(10, 55, 290, 85, COLOR_BG);
  tft.setTextColor(COLOR_UNIT, COLOR_BG);
  tft.setTextSize(6);
  tft.setCursor(10, 55);
  tft.print("...");

  tft.fillRect(10, 170, 290, 40, COLOR_BG);
  tft.setCursor(10, 170);
  tft.print("...");
}

void drawCountdown(int seconds) {
  tft.fillRect(200, 218, 110, 12, COLOR_BG);
  tft.setTextColor(0x3186, COLOR_BG);
  tft.setTextSize(1);
  tft.setCursor(200, 220);
  tft.print("Actu dans ");
  tft.print(seconds);
  tft.print("s");
}

// ─────────────────────────────────────────────
//  SERVEUR WEB
// ─────────────────────────────────────────────
void handleRoot()  { server.send(200, "text/html", CONFIG_PAGE); }
void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + WiFi.RSSI(i) + "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}
void handleSave() {
  if (server.hasArg("ssid")) {
    savedSSID     = server.arg("ssid");
    savedPassword = server.arg("password");
    prefs.begin("wifi", false);
    prefs.putString("ssid",     savedSSID);
    prefs.putString("password", savedPassword);
    prefs.end();
    server.send(200, "text/html", "<html><body style='background:#0f2027;color:white;font-family:sans-serif;text-align:center;padding-top:80px'><h2>✅ Sauvegardé !</h2><p>L'ESP32 redémarre...</p></body></html>");
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
  tft.fillScreen(COLOR_BG);
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);

  prefs.begin("wifi", true);
  savedSSID     = prefs.getString("ssid",     "");
  savedPassword = prefs.getString("password", "");
  prefs.end();

  if (savedSSID.length() > 0) {
    tft.setTextColor(TFT_CYAN, COLOR_BG);
    tft.setTextSize(2);
    tft.setCursor(20, 100);
    tft.print("Connexion...");
    tft.setCursor(20, 125);
    tft.setTextColor(TFT_WHITE, COLOR_BG);
    tft.print(savedSSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
      delay(500);
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      server.on("/", handleRoot);
      server.begin();
      drawMainScreen();
      return;
    }
    savedSSID = "";
  }

  // Mode AP
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

  // Vérifie reconnexion
  if (WiFi.status() != WL_CONNECTED) {
    connected = false;
    WiFi.reconnect();
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) delay(200);
    if (WiFi.status() == WL_CONNECTED) connected = true;
    drawMainScreen();
    return;
  }

  unsigned long now = millis();

  // Lancer une mesure
  if (!measuring && (now - lastMeasure >= MEASURE_INTERVAL || lastMeasure == 0)) {
    measuring = true;
    drawMeasuring();

    pingMs       = measurePing();
    downloadMbps = measureDownload();

    lastMeasure = millis();
    measuring   = false;
    drawMainScreen();
  }

  // Countdown
  if (!measuring && lastMeasure > 0) {
    int remaining = (MEASURE_INTERVAL - (int)(millis() - lastMeasure)) / 1000;
    if (remaining >= 0) drawCountdown(remaining);
  }

  delay(1000);
}
