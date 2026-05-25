/*
 * ================================================
 *  WiFi Monitor 24h - CYD ESP32 (ESP32-2432S028)
 * ================================================
 * 
 * FONCTIONNEMENT :
 *   - Au démarrage : crée un point d'accès WiFi "CYD-Config"
 *   - Connecte-toi à ce WiFi (mdp: 12345678)
 *   - Ouvre http://192.168.4.1 pour configurer ton réseau
 *   - Une fois sauvegardé, l'ESP32 se connecte et monitore
 * 
 * DÉPENDANCES (à installer via le gestionnaire de bibliothèques) :
 *   - TFT_eSPI           (Bodmer)
 *   - XPT2046_Touchscreen
 *   - ArduinoJson        (v6)
 *   - Preferences        (incluse dans ESP32 core)
 * 
 * CONFIG TFT_eSPI :
 *   Dans le fichier User_Setup.h de la lib TFT_eSPI :
 *   #define ILI9341_DRIVER
 *   #define TFT_MOSI 13
 *   #define TFT_SCLK 14
 *   #define TFT_CS   15
 *   #define TFT_DC    2
 *   #define TFT_RST  -1
 *   #define TFT_BL   21
 *   #define TOUCH_CS  33
 *   #define SPI_FREQUENCY  40000000
 *   #define SPI_TOUCH_FREQUENCY  2500000
 */

#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <SPI.h>

// ─────────────────────────────────────────────
//  CONSTANTES
// ─────────────────────────────────────────────
#define AP_SSID        "CYD-Config"
#define AP_PASSWORD    "12345678"
#define AP_IP          "192.168.4.1"

#define PING_HOST      "8.8.8.8"   // Google DNS — cible du ping
#define PING_INTERVAL  10000        // ms entre chaque mesure
#define HISTORY_SIZE   144          // 144 points × 10s = 24h... 
                                    // (ajuste selon mémoire: 8640 pts pour 1 mesure/10s)
                                    // Pour affichage réel 24h: utilise 1 mesure/min → 1440 pts

#define GRAPH_X        10
#define GRAPH_Y        50
#define GRAPH_W        300
#define GRAPH_H        150
#define GRAPH_MAX_MS   500          // ping max affiché en ms

// ─────────────────────────────────────────────
//  OBJETS GLOBAUX
// ─────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
WebServer server(80);
Preferences prefs;

// ─────────────────────────────────────────────
//  ÉTAT
// ─────────────────────────────────────────────
String savedSSID     = "";
String savedPassword = "";
bool   configured    = false;
bool   connected     = false;

int    pingHistory[HISTORY_SIZE];
int    historyCount  = 0;
int    historyIndex  = 0;   // index circulaire

unsigned long lastPingTime   = 0;
unsigned long lastDisplayTime = 0;

int    currentPing   = -1;  // -1 = timeout
int    totalPings    = 0;
int    failedPings   = 0;
float  avgPing       = 0;
int    minPing       = 9999;
int    maxPing       = 0;

// ─────────────────────────────────────────────
//  PAGE WEB DE CONFIGURATION
// ─────────────────────────────────────────────
const char CONFIG_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>CYD WiFi Monitor - Config</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: 'Segoe UI', sans-serif;
      background: linear-gradient(135deg, #0f2027, #203a43, #2c5364);
      min-height: 100vh;
      display: flex;
      align-items: center;
      justify-content: center;
      color: white;
    }
    .card {
      background: rgba(255,255,255,0.08);
      backdrop-filter: blur(10px);
      border: 1px solid rgba(255,255,255,0.15);
      border-radius: 20px;
      padding: 40px 35px;
      width: 360px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.4);
    }
    .logo { font-size: 42px; text-align: center; margin-bottom: 8px; }
    h1 { text-align: center; font-size: 1.4em; margin-bottom: 4px; }
    .subtitle { text-align: center; color: #aaa; font-size: 0.85em; margin-bottom: 28px; }
    label { display: block; font-size: 0.85em; color: #ccc; margin-bottom: 6px; margin-top: 18px; }
    input {
      width: 100%;
      padding: 12px 16px;
      border-radius: 10px;
      border: 1px solid rgba(255,255,255,0.2);
      background: rgba(255,255,255,0.07);
      color: white;
      font-size: 1em;
      outline: none;
      transition: border 0.2s;
    }
    input:focus { border-color: #4fc3f7; }
    .btn {
      width: 100%;
      margin-top: 28px;
      padding: 14px;
      border-radius: 12px;
      border: none;
      background: linear-gradient(90deg, #4fc3f7, #0288d1);
      color: white;
      font-size: 1.1em;
      font-weight: bold;
      cursor: pointer;
      transition: opacity 0.2s, transform 0.1s;
    }
    .btn:hover { opacity: 0.9; transform: translateY(-1px); }
    .btn:active { transform: translateY(0); }
    .info {
      margin-top: 20px;
      background: rgba(79,195,247,0.12);
      border-left: 3px solid #4fc3f7;
      border-radius: 8px;
      padding: 12px 15px;
      font-size: 0.82em;
      color: #b3e5fc;
    }
    .networks { margin-top: 14px; }
    .net-item {
      padding: 8px 12px;
      background: rgba(255,255,255,0.05);
      border-radius: 8px;
      margin-bottom: 6px;
      cursor: pointer;
      font-size: 0.88em;
      display: flex;
      justify-content: space-between;
      transition: background 0.15s;
    }
    .net-item:hover { background: rgba(79,195,247,0.15); }
    .signal { color: #aaa; font-size: 0.8em; }
    #scan-btn {
      background: transparent;
      border: 1px solid rgba(255,255,255,0.25);
      color: #ccc;
      border-radius: 8px;
      padding: 7px 14px;
      font-size: 0.82em;
      cursor: pointer;
      margin-top: 16px;
      transition: border-color 0.2s;
    }
    #scan-btn:hover { border-color: #4fc3f7; color: #4fc3f7; }
  </style>
</head>
<body>
  <div class="card">
    <div class="logo">📡</div>
    <h1>WiFi Monitor</h1>
    <p class="subtitle">Configuration réseau CYD ESP32</p>

    <button id="scan-btn" onclick="scanNetworks()">🔍 Scanner les réseaux</button>
    <div class="networks" id="networks"></div>

    <form action="/save" method="POST">
      <label>Nom du réseau (SSID)</label>
      <input type="text" name="ssid" id="ssid" placeholder="Mon_WiFi" required>

      <label>Mot de passe</label>
      <input type="password" name="password" id="password" placeholder="••••••••">

      <button type="submit" class="btn">✅ Sauvegarder & Connecter</button>
    </form>

    <div class="info">
      ℹ️ Après la sauvegarde, l'ESP32 redémarre et se connecte à ton réseau.<br>
      Le monitoring WiFi démarre automatiquement.
    </div>
  </div>

  <script>
    function scanNetworks() {
      document.getElementById('networks').innerHTML = '<p style="color:#aaa;font-size:0.85em;margin-top:10px">Scan en cours...</p>';
      fetch('/scan').then(r => r.json()).then(nets => {
        const div = document.getElementById('networks');
        if (!nets.length) { div.innerHTML = '<p style="color:#aaa;font-size:0.85em;margin-top:10px">Aucun réseau trouvé</p>'; return; }
        div.innerHTML = nets.map(n =>
          `<div class="net-item" onclick="selectNet('${n.ssid}')">
            <span>${n.ssid}</span>
            <span class="signal">${n.rssi} dBm ${n.secure ? '🔒' : '🔓'}</span>
          </div>`
        ).join('');
      });
    }
    function selectNet(ssid) {
      document.getElementById('ssid').value = ssid;
      document.getElementById('password').focus();
    }
  </script>
</body>
</html>
)rawliteral";

const char SAVED_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <title>Sauvegardé</title>
  <style>
    body { font-family: sans-serif; background: #0f2027; color: white;
           display:flex; align-items:center; justify-content:center; min-height:100vh; }
    .box { text-align:center; padding:40px; }
    .icon { font-size:60px; }
    h2 { margin-top:16px; }
    p { color:#aaa; margin-top:8px; }
  </style>
</head>
<body>
  <div class="box">
    <div class="icon">✅</div>
    <h2>Configuration sauvegardée !</h2>
    <p>L'ESP32 va redémarrer et se connecter à ton réseau.<br>
       Ferme cette page et reconnecte-toi à ton WiFi habituel.</p>
  </div>
</body>
</html>
)rawliteral";

// ─────────────────────────────────────────────
//  PING SIMPLE (via TCP ou ICMP simulé)
// ─────────────────────────────────────────────
int doPing(const char* host) {
  // Ping ICMP via la bibliothèque ESP32 (disponible dans le core)
  // On utilise un connect TCP sur port 80 comme mesure de latence
  // Pour un vrai ICMP ping, utiliser ESP_Ping ou la lib "ESP32Ping"
  
  unsigned long t = millis();
  WiFiClient client;
  client.setTimeout(2000);
  
  bool ok = client.connect(host, 53); // port DNS
  int ms = (int)(millis() - t);
  client.stop();
  
  if (!ok) return -1;
  return ms;
}

// ─────────────────────────────────────────────
//  ROUTES WEB
// ─────────────────────────────────────────────
void handleRoot() {
  server.send(200, "text/html", CONFIG_PAGE);
}

void handleSave() {
  if (server.hasArg("ssid")) {
    savedSSID     = server.arg("ssid");
    savedPassword = server.arg("password");

    prefs.begin("wifi", false);
    prefs.putString("ssid",     savedSSID);
    prefs.putString("password", savedPassword);
    prefs.end();

    server.send(200, "text/html", SAVED_PAGE);
    delay(2000);
    ESP.restart();
  } else {
    server.send(400, "text/plain", "Paramètres manquants");
  }
}

void handleScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\","
            "\"rssi\":" + WiFi.RSSI(i) + ","
            "\"secure\":" + (WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleStatus() {
  String json = "{";
  json += "\"connected\":" + String(connected ? "true" : "false") + ",";
  json += "\"ssid\":\"" + savedSSID + "\",";
  json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  json += "\"ping\":" + String(currentPing) + ",";
  json += "\"avg\":" + String((int)avgPing) + ",";
  json += "\"min\":" + String(minPing == 9999 ? 0 : minPing) + ",";
  json += "\"max\":" + String(maxPing) + ",";
  json += "\"total\":" + String(totalPings) + ",";
  json += "\"failed\":" + String(failedPings);
  json += "}";
  server.send(200, "application/json", json);
}

// ─────────────────────────────────────────────
//  AFFICHAGE TFT
// ─────────────────────────────────────────────
void drawBackground() {
  tft.fillScreen(TFT_BLACK);
  
  // Titre
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 8);
  tft.print("WiFi Monitor 24h");
  
  // Ligne de séparation
  tft.drawLine(0, 30, 320, 30, TFT_DARKGREY);
  
  // Cadre graphique
  tft.drawRect(GRAPH_X - 1, GRAPH_Y - 1, GRAPH_W + 2, GRAPH_H + 2, TFT_DARKGREY);
  
  // Labels axe Y
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(GRAPH_X + GRAPH_W + 3, GRAPH_Y);
  tft.print("500ms");
  tft.setCursor(GRAPH_X + GRAPH_W + 3, GRAPH_Y + GRAPH_H/2 - 3);
  tft.print("250ms");
  tft.setCursor(GRAPH_X + GRAPH_W + 3, GRAPH_Y + GRAPH_H - 4);
  tft.print("0ms");
  
  // Zone stats (bas de l'écran)
  tft.drawLine(0, 210, 320, 210, TFT_DARKGREY);
}

void drawStats() {
  tft.fillRect(0, 212, 320, 28, TFT_BLACK);
  
  tft.setTextSize(1);
  
  // Ping actuel
  tft.setCursor(10, 215);
  if (currentPing < 0) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.print("Ping: TIMEOUT");
  } else {
    if      (currentPing < 50)  tft.setTextColor(TFT_GREEN,  TFT_BLACK);
    else if (currentPing < 150) tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    else                         tft.setTextColor(TFT_RED,    TFT_BLACK);
    tft.print("Ping: ");
    tft.print(currentPing);
    tft.print("ms");
  }
  
  // Avg / Min / Max
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 226);
  tft.print("Avg:");
  tft.print((int)avgPing);
  tft.print("ms  Min:");
  tft.print(minPing == 9999 ? 0 : minPing);
  tft.print("ms  Max:");
  tft.print(maxPing);
  tft.print("ms");
  
  // Perte de paquets
  tft.setCursor(200, 215);
  int lossPercent = totalPings > 0 ? (failedPings * 100 / totalPings) : 0;
  if (lossPercent > 10) tft.setTextColor(TFT_RED, TFT_BLACK);
  else                  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.print("Perte: ");
  tft.print(lossPercent);
  tft.print("%");
  
  // IP
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setCursor(200, 226);
  tft.print(WiFi.localIP().toString());
}

void drawGraph() {
  // Effacer la zone graphique
  tft.fillRect(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, TFT_BLACK);
  
  // Lignes de grille horizontales
  for (int g = 0; g <= 4; g++) {
    int y = GRAPH_Y + (GRAPH_H * g / 4);
    tft.drawLine(GRAPH_X, y, GRAPH_X + GRAPH_W, y, 0x2104); // gris très sombre
  }
  
  if (historyCount < 2) return;
  
  int count = min(historyCount, HISTORY_SIZE);
  float xStep = (float)GRAPH_W / (HISTORY_SIZE - 1);
  
  int prevX = -1, prevY = -1;
  
  for (int i = 0; i < count; i++) {
    // Index dans le tableau circulaire
    int idx = (historyIndex - count + i + HISTORY_SIZE) % HISTORY_SIZE;
    int ping = pingHistory[idx];
    
    int x = GRAPH_X + (int)(i * (float)GRAPH_W / (HISTORY_SIZE - 1));
    
    if (ping < 0) {
      // Timeout : petit carré rouge
      tft.fillRect(x - 1, GRAPH_Y + GRAPH_H - 4, 3, 4, TFT_RED);
      prevX = prevY = -1;
      continue;
    }
    
    int clampedPing = min(ping, GRAPH_MAX_MS);
    int y = GRAPH_Y + GRAPH_H - 1 - (clampedPing * (GRAPH_H - 1) / GRAPH_MAX_MS);
    
    // Couleur selon latence
    uint16_t color;
    if      (ping < 50)  color = TFT_GREEN;
    else if (ping < 150) color = TFT_YELLOW;
    else                 color = TFT_ORANGE;
    
    if (prevX >= 0) {
      tft.drawLine(prevX, prevY, x, y, color);
    }
    tft.drawPixel(x, y, TFT_WHITE);
    
    prevX = x;
    prevY = y;
  }
}

void showAPScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 15);
  tft.print("Mode Configuration");
  
  tft.drawLine(0, 38, 320, 38, TFT_YELLOW);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  
  tft.setCursor(10, 55);
  tft.print("1. Connecte-toi au WiFi :");
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setCursor(20, 70);
  tft.setTextSize(2);
  tft.print(AP_SSID);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 95);
  tft.print("   Mot de passe : ");
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.print(AP_PASSWORD);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(10, 115);
  tft.print("2. Ouvre ton navigateur :");
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(20, 130);
  tft.print("http://");
  tft.print(AP_IP);
  
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(10, 158);
  tft.print("3. Saisis tes identifiants WiFi");
  tft.setCursor(10, 172);
  tft.print("   et clique sur Sauvegarder.");
  
  // Indicateur clignotant
  tft.fillCircle(300, 20, 8, TFT_GREEN);
  tft.setTextColor(TFT_BLACK, TFT_GREEN);
  tft.setCursor(295, 17);
  tft.print("AP");
}

void showConnectingScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(30, 80);
  tft.print("Connexion a :");
  tft.setCursor(30, 110);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.print(savedSSID);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(30, 145);
  tft.print("Veuillez patienter...");
}

// ─────────────────────────────────────────────
//  MISE À JOUR PING
// ─────────────────────────────────────────────
void updatePing() {
  if (!connected) return;
  if (millis() - lastPingTime < PING_INTERVAL) return;
  lastPingTime = millis();
  
  currentPing = doPing(PING_HOST);
  totalPings++;
  
  if (currentPing < 0) {
    failedPings++;
  } else {
    if (currentPing < minPing) minPing = currentPing;
    if (currentPing > maxPing) maxPing = currentPing;
    // Moyenne mobile
    avgPing = (avgPing * (totalPings - failedPings - 1) + currentPing) / (totalPings - failedPings);
  }
  
  // Stockage dans l'historique circulaire
  pingHistory[historyIndex] = currentPing;
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;
  
  // Mise à jour de l'affichage
  drawGraph();
  drawStats();
}

// ─────────────────────────────────────────────
//  SETUP
// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  
  // Init TFT
  tft.init();
  tft.setRotation(1); // Paysage
  tft.fillScreen(TFT_BLACK);
  
  // Rétroéclairage
  pinMode(21, OUTPUT);
  digitalWrite(21, HIGH);
  
  // Init historique
  for (int i = 0; i < HISTORY_SIZE; i++) pingHistory[i] = -1;
  
  // Lire config sauvegardée
  prefs.begin("wifi", true);
  savedSSID     = prefs.getString("ssid",     "");
  savedPassword = prefs.getString("password", "");
  prefs.end();
  
  if (savedSSID.length() > 0) {
    // Tentative de connexion
    showConnectingScreen();
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
      delay(500);
      attempts++;
      // Petit indicateur de progression
      tft.fillRect(30 + attempts * 8, 170, 6, 6, TFT_CYAN);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      connected   = true;
      configured  = true;
      
      // Route de status (accessible depuis le LAN)
      server.on("/status", handleStatus);
      server.begin();
      
      drawBackground();
      drawStats();
    } else {
      // Echec → mode AP
      savedSSID = "";
    }
  }
  
  if (!connected) {
    // Mode Access Point
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    
    server.on("/",      HTTP_GET,  handleRoot);
    server.on("/save",  HTTP_POST, handleSave);
    server.on("/scan",  HTTP_GET,  handleScan);
    server.begin();
    
    showAPScreen();
  }
}

// ─────────────────────────────────────────────
//  LOOP
// ─────────────────────────────────────────────
void loop() {
  server.handleClient();
  
  if (connected) {
    // Vérifie si le WiFi est toujours connecté
    if (WiFi.status() != WL_CONNECTED) {
      connected = false;
      tft.fillRect(0, 32, 320, 16, TFT_BLACK);
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.setTextSize(1);
      tft.setCursor(10, 34);
      tft.print("⚠ WiFi déconnecté - Reconnexion...");
      
      WiFi.reconnect();
      unsigned long t = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t < 10000) {
        delay(200);
      }
      if (WiFi.status() == WL_CONNECTED) {
        connected = true;
        tft.fillRect(0, 32, 320, 16, TFT_BLACK);
      }
    }
    
    updatePing();
    
    // Afficher le SSID + heure en haut
    if (millis() - lastDisplayTime > 5000) {
      lastDisplayTime = millis();
      tft.fillRect(0, 32, 200, 16, TFT_BLACK);
      tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.setTextSize(1);
      tft.setCursor(10, 34);
      tft.print(savedSSID + "  " + WiFi.localIP().toString());
      
      // Indicateur connexion
      tft.fillCircle(308, 38, 6, connected ? TFT_GREEN : TFT_RED);
    }
  }
}
