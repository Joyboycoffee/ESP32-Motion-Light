#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "esp_wifi.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ==========================================
// PIN DEFINITIONS FOR ESP32-C3 SUPER MINI
// ==========================================
#define PIR_PIN     4   // GPIO 4 connected to PIR Sensor OUT (HW-416B / HC-SR501 / AM312)
#define LDR_PIN     2   // GPIO 2 connected to LDR divider or LM393 Module AO/DO (ADC1_CH2)
#define RELAY_PIN   3   // GPIO 3 connected to Relay Module IN (or MOSFET Gate driver)
#define LED_PIN     8   // GPIO 8 = Built-in Blue LED (Active LOW on Super Mini)

// ==========================================
// NETWORK CONFIGURATION
// ==========================================
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// Static IP Configuration for AC Light
IPAddress local_IP = DEFAULT_LOCAL_IP;
IPAddress gateway = DEFAULT_GATEWAY;
IPAddress subnet = DEFAULT_SUBNET;
IPAddress primaryDNS = DEFAULT_DNS1;
IPAddress secondaryDNS = DEFAULT_DNS2;

// Fallback SoftAP if Wi-Fi router is offline
const char* ap_ssid = "AC-Motion-Light";
const char* ap_password = ""; // Open network

WebServer server(80);
Preferences prefs;

// ==========================================
// RUNTIME STATE & CONFIGURATION
// ==========================================
// Operating Modes: 0 = AUTO (Motion + Dusk/Dawn), 1 = FORCE_ON, 2 = FORCE_OFF
int operationMode = 0; 

// Sensor & Logic Variables
bool currentMotion = false;
unsigned long motionStartTime = 0;
unsigned long lastMotionEndedTime = 0;
float motionDuration = 0.0;
int triggerCount = 0;
int ldrValue = 0;
bool isNight = true;
bool relayState = false;

// Saved Settings in NVS Flash
int duskThreshold = 1500;       // LDR threshold (0-4095)
int holdTimeSec = 10;           // Extra hold time after PIR drops (0-300s)
bool relayActiveLow = false;    // false = Active-HIGH (3.3V activates relay), true = Active-LOW (0V activates relay)

// Web Dashboard HTML
const char HTML_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>AC Light Motion Controller</title>
  <style>
    :root {
      --bg: #0b1120;
      --card: #1e293b;
      --border: #334155;
      --accent: #38bdf8;
      --green: #10b981;
      --amber: #f59e0b;
      --red: #ef4444;
      --text: #f8fafc;
      --sub: #94a3b8;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      background: var(--bg);
      color: var(--text);
      text-align: center;
      padding: 16px;
      min-height: 100vh;
    }
    .card {
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 18px;
      padding: 20px;
      max-width: 500px;
      margin: 0 auto 16px;
      box-shadow: 0 10px 25px rgba(0,0,0,0.5);
    }
    .badge-bar {
      display: inline-block;
      background: rgba(56, 189, 248, 0.15);
      color: var(--accent);
      font-size: 11px;
      font-weight: 700;
      padding: 3px 12px;
      border-radius: 99px;
      border: 1px solid rgba(56, 189, 248, 0.3);
      margin-bottom: 8px;
    }
    h1 { font-size: 20px; color: var(--accent); margin-bottom: 12px; }
    
    /* Status Hero Box */
    .status-box {
      padding: 20px 16px;
      border-radius: 14px;
      font-size: 20px;
      font-weight: 800;
      margin-bottom: 16px;
      transition: all 0.3s ease;
      letter-spacing: 0.5px;
    }
    .status-light-on {
      background: #78350f;
      color: #fef08a;
      border: 2px solid #eab308;
      box-shadow: 0 0 25px rgba(234, 179, 8, 0.5);
      animation: glow 1.2s infinite alternate;
    }
    @keyframes glow {
      0% { box-shadow: 0 0 10px rgba(234, 179, 8, 0.4); }
      100% { box-shadow: 0 0 25px rgba(234, 179, 8, 0.85); }
    }
    .status-standby {
      background: #064e3b;
      color: #34d399;
      border: 2px solid #059669;
    }
    .status-daylight {
      background: #1e3a8a;
      color: #93c5fd;
      border: 2px solid #3b82f6;
    }
    .status-override {
      background: #4c1d95;
      color: #d8b4fe;
      border: 2px solid #a855f7;
    }

    /* Mode Selection Pill Buttons */
    .mode-container {
      display: grid;
      grid-template-columns: 1fr 1fr 1fr;
      gap: 6px;
      background: #0f172a;
      padding: 6px;
      border-radius: 12px;
      margin-bottom: 16px;
      border: 1px solid var(--border);
    }
    .mode-btn {
      background: transparent;
      color: var(--sub);
      border: none;
      padding: 9px 6px;
      border-radius: 8px;
      font-weight: 700;
      font-size: 13px;
      cursor: pointer;
      transition: 0.2s;
    }
    .mode-btn.active {
      background: var(--accent);
      color: #0f172a;
      box-shadow: 0 2px 8px rgba(56, 189, 248, 0.4);
    }

    /* Metrics Grid */
    .metric {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 10px 14px;
      background: #0f172a;
      border-radius: 10px;
      margin-bottom: 8px;
      font-size: 14.5px;
      border: 1px solid #1e293b;
    }
    .metric span:first-child { color: var(--sub); font-weight: 500; }
    .metric span:last-child { font-weight: 700; color: var(--accent); font-size: 15px; }

    /* Controls & Sliders */
    .ctrl-group {
      background: #0f172a;
      border-radius: 10px;
      padding: 12px;
      margin-bottom: 10px;
      text-align: left;
      border: 1px solid #1e293b;
    }
    .ctrl-header {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-bottom: 8px;
      font-size: 13px;
      font-weight: 600;
      color: #cbd5e1;
    }
    input[type=range] {
      width: 100%;
      height: 6px;
      border-radius: 4px;
      background: #334155;
      outline: none;
      -webkit-appearance: none;
    }
    input[type=range]::-webkit-slider-thumb {
      -webkit-appearance: none;
      appearance: none;
      width: 18px;
      height: 18px;
      border-radius: 50%;
      background: var(--accent);
      cursor: pointer;
    }

    .btn-row { display: flex; gap: 8px; margin-top: 10px; }
    button.action-btn {
      flex: 1;
      background: #334155;
      color: var(--text);
      border: 1px solid #475569;
      padding: 9px 14px;
      border-radius: 8px;
      font-weight: 700;
      font-size: 13px;
      cursor: pointer;
      transition: 0.2s;
    }
    button.action-btn:hover { background: #475569; color: var(--accent); }
    button.btn-primary { background: var(--accent); color: #0f172a; border: none; }
    button.btn-primary:hover { background: #7dd3fc; }

    /* Wiring Diagram & Guides */
    .guide-box {
      background: #0f172a;
      border: 1px dashed #475569;
      border-radius: 12px;
      padding: 12px;
      text-align: left;
      font-size: 12.5px;
      line-height: 1.5;
      color: #cbd5e1;
    }
    .diagram {
      background: #090d16;
      border-radius: 8px;
      padding: 10px;
      font-family: monospace;
      font-size: 11.5px;
      line-height: 1.4;
      color: #94a3b8;
      margin: 8px 0;
      white-space: pre;
      overflow-x: auto;
    }
  </style>
</head>
<body>
  <div class="card">
    <div class="badge-bar">ESP32-C3 SUPER MINI • 230V AC RELAY SWITCH</div>
    <h1>💡 AC Motion Light Controller</h1>

    <!-- Hero State Banner -->
    <div id="statusBox" class="status-box status-standby">
      🌙 STANDBY (DARK / IDLE)
    </div>

    <!-- Mode Selector -->
    <div class="mode-container">
      <button id="modeAuto" class="mode-btn active" onclick="setMode(0)">🤖 AUTO</button>
      <button id="modeOn" class="mode-btn" onclick="setMode(1)">💡 FORCE ON</button>
      <button id="modeOff" class="mode-btn" onclick="setMode(2)">🔒 FORCE OFF</button>
    </div>

    <!-- Live Telemetry -->
    <div class="metric">
      <span>AC Relay State:</span>
      <span id="relayText" style="color: #4ade80;">OFF (0V AC)</span>
    </div>

    <div class="metric">
      <span>PIR Motion Sensor:</span>
      <span id="motionText">🟢 Idle</span>
    </div>

    <div class="metric">
      <span>Motion Active Timer:</span>
      <span id="activeTimer">0.0s</span>
    </div>

    <div class="metric">
      <span>Software Hold-Off Timer:</span>
      <span id="holdTimer">0s</span>
    </div>

    <div class="metric">
      <span>Total Motion Triggers:</span>
      <span id="triggerCount">0</span>
    </div>

    <div class="metric">
      <span>Ambient Light Sensor (0-4095):</span>
      <span id="ldrText">0</span>
    </div>

    <div class="metric">
      <span>Day / Night State:</span>
      <span id="nightBadge" style="color: #38bdf8;">🌙 Nighttime</span>
    </div>

    <div class="metric">
      <span>Wi-Fi Signal Strength:</span>
      <span id="rssiText">- dBm</span>
    </div>

    <div class="btn-row">
      <button class="action-btn" onclick="resetStats()">Reset Triggers</button>
      <button class="action-btn" onclick="toggleRelayPolarity()" id="btnPolarity">Polarity: HIGH</button>
    </div>
  </div>

  <!-- Calibration & Threshold Tuning -->
  <div class="card">
    <h2 style="font-size: 16px; color: var(--accent); margin-bottom: 12px;">⚙️ Threshold & Timing Settings</h2>

    <!-- Dusk Threshold Slider -->
    <div class="ctrl-group">
      <div class="ctrl-header">
        <span>Dusk Threshold (Light vs Dark)</span>
        <span id="duskValLabel" style="color: var(--accent); font-weight:700;">1500</span>
      </div>
      <input type="range" id="duskSlider" min="100" max="4000" step="50" value="1500" oninput="updateDuskLabel(this.value)" onchange="saveThreshold(this.value)">
      <div style="display:flex; justify-content:space-between; font-size:11px; color:#64748b; margin-top:4px;">
        <span>Dark (Turn ON sooner)</span>
        <button class="action-btn" style="padding: 2px 8px; font-size: 11px;" onclick="setDuskToCurrent()">Capture Current LDR</button>
        <span>Bright (Only pitch black)</span>
      </div>
    </div>

    <!-- Software Hold Time Slider -->
    <div class="ctrl-group">
      <div class="ctrl-header">
        <span>Software Hold Time (After Motion Ends)</span>
        <span id="holdValLabel" style="color: var(--accent); font-weight:700;">10s</span>
      </div>
      <input type="range" id="holdSlider" min="0" max="180" step="5" value="10" oninput="updateHoldLabel(this.value)" onchange="saveHoldTime(this.value)">
      <div style="font-size:11px; color:#64748b; margin-top:4px;">
        Keeps AC bulb illuminated for extra seconds after PIR drops LOW. (0 = relies purely on PIR hardware dial).
      </div>
    </div>
  </div>

  <!-- Wiring & Hardware Reference -->
  <div class="card">
    <h2 style="font-size: 16px; color: #f59e0b; margin-bottom: 8px;">⚡ AC 230V Wiring & Pinout Guide</h2>
    <div class="guide-box">
      <strong>Pin Connections on ESP32-C3 Super Mini:</strong><br>
      • <strong>GPIO 4:</strong> PIR Sensor OUT (3.3V logic)<br>
      • <strong>GPIO 2:</strong> Light Sensor / LDR Divider (or LM393 DO/AO)<br>
      • <strong>GPIO 3:</strong> Relay Module IN pin<br>
      • <strong>GPIO 8:</strong> Built-in Blue LED (Active-LOW)<br>
      • <strong>5V / VIN:</strong> 5V DC from Phone Charger<br>
      • <strong>GND:</strong> Common DC Ground<br>

      <div class="diagram">
[ AC 230V Mains Live/Hot ] ────► Relay COM
                                 Relay NO  ────► [ AC Light Bulb ]
[ AC 230V Mains Neutral ]  ────────────────────► [ AC Light Bulb ]

Low-Voltage DC Side:
ESP32 GPIO 3  ────► Relay Module IN
ESP32 5V      ────► Relay Module VCC &amp; PIR VCC
ESP32 GND     ────► Relay Module GND &amp; PIR GND
ESP32 GPIO 4  ◄─── PIR OUT (HW-416B)
ESP32 GPIO 2  ◄─── LDR Divider / LM393 AO</div>

      ⚠️ <strong>SAFETY FIRST:</strong> Keep the 230V AC wires completely insulated inside a plastic box. Never touch the relay contacts when the AC plug is connected!
    </div>
  </div>

  <script>
    let currentDusk = 1500;
    let currentHold = 10;
    let activeMode = 0;

    function updateDuskLabel(v) { document.getElementById('duskValLabel').innerText = v; }
    function updateHoldLabel(v) { document.getElementById('holdValLabel').innerText = v + 's'; }

    function updateStatus() {
      fetch('/data')
        .then(r => r.json())
        .then(data => {
          const box = document.getElementById('statusBox');
          const relay = document.getElementById('relayText');
          const motion = document.getElementById('motionText');
          const timer = document.getElementById('activeTimer');
          const hold = document.getElementById('holdTimer');
          const count = document.getElementById('triggerCount');
          const ldr = document.getElementById('ldrText');
          const night = document.getElementById('nightBadge');
          const rssi = document.getElementById('rssiText');

          // Relay State UI
          if (data.relay) {
            relay.innerText = '⚡ ON (230V AC Powered)';
            relay.style.color = '#facc15';
          } else {
            relay.innerText = 'OFF (0V AC Isolated)';
            relay.style.color = '#94a3b8';
          }

          // Hero Banner
          if (data.mode === 1) {
            box.className = 'status-box status-light-on';
            box.innerHTML = '💡 MANUAL FORCE ON (LIGHT ACTIVE)';
          } else if (data.mode === 2) {
            box.className = 'status-box status-override';
            box.innerHTML = '🔒 MANUAL FORCE OFF';
          } else if (!data.night) {
            box.className = 'status-box status-daylight';
            box.innerHTML = '☀️ DAYLIGHT LOCKOUT (AC LIGHT OFF)';
          } else if (data.relay) {
            box.className = 'status-box status-light-on';
            box.innerHTML = '🚨 MOTION DETECTED • AC LIGHT ON';
          } else {
            box.className = 'status-box status-standby';
            box.innerHTML = '🌙 STANDBY (DARK / ARMED)';
          }

          // Motion state
          if (data.motion) {
            motion.innerHTML = '<span style="color:#ef4444; font-weight:800;">🚨 MOTION DETECTED!</span>';
          } else {
            motion.innerHTML = '<span style="color:#10b981;">🟢 Idle (No Motion)</span>';
          }

          timer.innerText = data.duration.toFixed(1) + 's';
          hold.innerText = data.holdRemaining.toFixed(0) + 's';
          count.innerText = data.count;
          ldr.innerText = data.ldr;

          if (data.night) {
            night.innerHTML = '<span style="color:#38bdf8;">🌙 Nighttime</span>';
          } else {
            night.innerHTML = '<span style="color:#f59e0b;">☀️ Daylight</span>';
          }

          rssi.innerText = data.rssi + ' dBm';

          // Sync Mode buttons
          document.getElementById('modeAuto').className = 'mode-btn ' + (data.mode === 0 ? 'active' : '');
          document.getElementById('modeOn').className = 'mode-btn ' + (data.mode === 1 ? 'active' : '');
          document.getElementById('modeOff').className = 'mode-btn ' + (data.mode === 2 ? 'active' : '');

          document.getElementById('btnPolarity').innerText = 'Polarity: ' + (data.activeLow ? 'LOW' : 'HIGH');
        })
        .catch(err => console.log('Poll err:', err));
    }

    function setMode(m) {
      fetch('/set_mode?m=' + m)
        .then(() => updateStatus());
    }

    function saveThreshold(val) {
      fetch('/set_threshold?v=' + val);
    }

    function setDuskToCurrent() {
      const cur = document.getElementById('ldrText').innerText;
      document.getElementById('duskSlider').value = cur;
      updateDuskLabel(cur);
      saveThreshold(cur);
    }

    function saveHoldTime(val) {
      fetch('/set_hold?v=' + val);
    }

    function toggleRelayPolarity() {
      fetch('/toggle_polarity')
        .then(() => updateStatus());
    }

    function resetStats() {
      fetch('/reset');
    }

    setInterval(updateStatus, 350);
    updateStatus();
  </script>
</body>
</html>
)rawliteral";

// Helper to set physical relay pin state based on active polarity
void setPhysicalRelay(bool turnOn) {
  relayState = turnOn;
  if (relayActiveLow) {
    digitalWrite(RELAY_PIN, turnOn ? LOW : HIGH);
  } else {
    digitalWrite(RELAY_PIN, turnOn ? HIGH : LOW);
  }
  // Built-in Blue LED mirrors relay (active LOW on Super Mini)
  digitalWrite(LED_PIN, turnOn ? LOW : HIGH);
}

// HTTP Route Handlers
void handleRoot() {
  server.send(200, "text/html; charset=utf-8", HTML_PAGE);
}

void handleData() {
  unsigned long now = millis();
  float holdRemaining = 0.0;
  if (!currentMotion && (now - lastMotionEndedTime < (unsigned long)holdTimeSec * 1000) && lastMotionEndedTime > 0) {
    holdRemaining = (holdTimeSec * 1000 - (now - lastMotionEndedTime)) / 1000.0;
    if (holdRemaining < 0) holdRemaining = 0;
  }

  String json = "{";
  json += "\"relay\":" + String(relayState ? "true" : "false") + ",";
  json += "\"motion\":" + String(currentMotion ? "true" : "false") + ",";
  json += "\"duration\":" + String(motionDuration) + ",";
  json += "\"holdRemaining\":" + String(holdRemaining, 1) + ",";
  json += "\"count\":" + String(triggerCount) + ",";
  json += "\"ldr\":" + String(ldrValue) + ",";
  json += "\"night\":" + String(isNight ? "true" : "false") + ",";
  json += "\"mode\":" + String(operationMode) + ",";
  json += "\"activeLow\":" + String(relayActiveLow ? "true" : "false") + ",";
  json += "\"rssi\":" + String(WiFi.RSSI());
  json += "}";
  server.send(200, "application/json; charset=utf-8", json);
}

void handleSetMode() {
  if (server.hasArg("m")) {
    operationMode = server.arg("m").toInt();
    if (operationMode < 0 || operationMode > 2) operationMode = 0;
    prefs.putInt("mode", operationMode);
  }
  server.send(200, "text/plain", "OK");
}

void handleSetThreshold() {
  if (server.hasArg("v")) {
    duskThreshold = server.arg("v").toInt();
    prefs.putInt("threshold", duskThreshold);
    Serial.printf("[SETTINGS] Dusk Threshold updated: %d\n", duskThreshold);
  }
  server.send(200, "text/plain", "OK");
}

void handleSetHold() {
  if (server.hasArg("v")) {
    holdTimeSec = server.arg("v").toInt();
    prefs.putInt("hold", holdTimeSec);
    Serial.printf("[SETTINGS] Hold Time updated: %ds\n", holdTimeSec);
  }
  server.send(200, "text/plain", "OK");
}

void handleTogglePolarity() {
  relayActiveLow = !relayActiveLow;
  prefs.putBool("active_low", relayActiveLow);
  setPhysicalRelay(relayState);
  Serial.printf("[SETTINGS] Relay Active-LOW set to: %s\n", relayActiveLow ? "true" : "false");
  server.send(200, "text/plain", "OK");
}

void handleReset() {
  triggerCount = 0;
  server.send(200, "text/plain", "OK");
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("\n=========================================\n");
      Serial.printf(">>> SUCCESS: WI-FI CONNECTED! <<<\n");
      Serial.printf(">>> SSID: %s <<<\n", WiFi.SSID().c_str());
      Serial.printf(">>> IP Address: %s <<<\n", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
      Serial.printf(">>> Portal: http://%s or http://ac-light.local <<<\n", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
      Serial.printf("=========================================\n\n");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.println("[WIFI EVENT] Disconnected from AP.");
      break;
    default:
      break;
  }
}

void setup() {
  // 1. Disable RTC Brownout detector for ESP32-C3 RF surge stability
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // 2. Hardware pin modes
  pinMode(PIR_PIN, INPUT_PULLDOWN);
  pinMode(LDR_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  // 3. Initialize Preferences (NVS Storage)
  prefs.begin("ac_light", false);
  duskThreshold  = prefs.getInt("threshold", 1500);
  holdTimeSec    = prefs.getInt("hold", 10);
  relayActiveLow = prefs.getBool("active_low", false); // Default active-HIGH
  operationMode  = prefs.getInt("mode", 0);            // Default AUTO

  // Initial relay state = OFF
  setPhysicalRelay(false);

  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=========================================");
  Serial.println("  ESP32-C3 Super Mini - AC Motion Light  ");
  Serial.println("=========================================");

  // 4. Initialize Wi-Fi Station Mode
  WiFi.onEvent(onWiFiEvent);
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.mode(WIFI_STA);

  wifi_country_t country = {
    .cc = "IN",
    .schan = 1,
    .nchan = 13,
    .max_tx_power = 20,
    .policy = WIFI_COUNTRY_POLICY_MANUAL
  };
  esp_wifi_set_country(&country);

  // 8.5 dBm workaround to avoid power rail collapse on Super Mini
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.setSleep(false);
  WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);

  Serial.printf("[WIFI] Connecting to '%s'...\n", default_ssid);
  WiFi.begin(default_ssid, default_password);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(300);
    Serial.print(".");
    attempts++;
  }

  // Fallback to AP Mode if router isn't found
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WIFI] Router not reachable. Starting SoftAP mode...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password);
    Serial.printf("[WIFI] SoftAP Active! Connect to '%s' -> http://192.168.4.1\n", ap_ssid);
  }

  // 5. Start mDNS Responder
  if (MDNS.begin("ac-light")) {
    Serial.println("[mDNS] Responder started at: http://ac-light.local");
  }

  // 6. Register Web Endpoints
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/set_mode", HTTP_GET, handleSetMode);
  server.on("/set_threshold", HTTP_GET, handleSetThreshold);
  server.on("/set_hold", HTTP_GET, handleSetHold);
  server.on("/toggle_polarity", HTTP_GET, handleTogglePolarity);
  server.on("/reset", HTTP_GET, handleReset);
  server.begin();
  Serial.println("[HTTP] Web Server started on port 80");
}

void loop() {
  server.handleClient();

  int pirState = digitalRead(PIR_PIN);
  ldrValue = analogRead(LDR_PIN);

  // Dusk detection logic: Dark when LDR reading < threshold or high pull
  isNight = (ldrValue < duskThreshold || ldrValue > 4000);

  unsigned long now = millis();

  // Track motion state transitions
  if (pirState == HIGH) {
    if (!currentMotion) {
      currentMotion = true;
      triggerCount++;
      motionStartTime = now;
      Serial.printf("[MOTION] Trigger #%d | LDR: %d | Night: %s\n", triggerCount, ldrValue, isNight ? "YES" : "NO");
    }
    motionDuration = (now - motionStartTime) / 1000.0;
  } else {
    if (currentMotion) {
      currentMotion = false;
      lastMotionEndedTime = now;
      Serial.printf("[IDLE] Motion ended. Active duration: %.1fs\n", motionDuration);
    }
    motionDuration = 0.0;
  }

  // Determine AC Relay Output based on Mode
  if (operationMode == 1) {
    // FORCE ON
    setPhysicalRelay(true);
  } else if (operationMode == 2) {
    // FORCE OFF
    setPhysicalRelay(false);
  } else {
    // AUTO MODE (Motion + Dusk-to-Dawn Lockout)
    if (!isNight) {
      // Daytime lockout: Keep AC Light OFF even if motion occurs
      setPhysicalRelay(false);
    } else {
      // Nighttime: Turn ON during motion OR during the software hold-off window
      bool holdActive = (lastMotionEndedTime > 0) && (now - lastMotionEndedTime < (unsigned long)holdTimeSec * 1000);
      if (currentMotion || holdActive) {
        setPhysicalRelay(true);
      } else {
        setPhysicalRelay(false);
      }
    }
  }

  delay(20);
}
