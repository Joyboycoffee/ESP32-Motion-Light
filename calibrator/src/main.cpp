#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "esp_wifi.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// --- PIN DEFINITIONS FOR ESP32-C3 SUPER MINI ---
#define PIR_PIN    4    // GPIO 4 connected to HW-416B OUT
#define LDR_PIN    2    // GPIO 2 connected to LDR divider (ADC1_CH2)
#define LED_PIN    8    // GPIO 8 = Built-in Blue LED (Active LOW)

// Router Credentials
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// Static IP Configuration
IPAddress local_IP = DEFAULT_LOCAL_IP;
IPAddress gateway = DEFAULT_GATEWAY;
IPAddress subnet = DEFAULT_SUBNET;
IPAddress primaryDNS = DEFAULT_DNS1;
IPAddress secondaryDNS = DEFAULT_DNS2;

WebServer server(80);

// Status variables
bool currentMotion = false;
unsigned long motionStartTime = 0;
float motionDuration = 0.0;
int triggerCount = 0;
int ldrValue = 0;
bool isNight = true;

int duskThreshold = 1500; 

const char HTML_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32-C3 PIR Sensor Calibrator</title>
  <style>
    * { box-sizing: border-box; }
    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; background: #0b1120; color: #f8fafc; text-align: center; padding: 16px; margin: 0; }
    .card { background: #1e293b; border: 1px solid #334155; border-radius: 18px; padding: 20px; max-width: 480px; margin: 0 auto 16px; box-shadow: 0 10px 25px rgba(0,0,0,0.5); }
    h1 { font-size: 21px; margin: 0 0 14px; color: #38bdf8; letter-spacing: 0.5px; }
    .status-box { padding: 22px 16px; border-radius: 14px; font-size: 22px; font-weight: 800; margin-bottom: 16px; transition: all 0.3s ease; letter-spacing: 0.5px; }
    .no-motion { background: #064e3b; color: #34d399; border: 2px solid #059669; }
    .motion { background: #7f1d1d; color: #fca5a5; border: 2px solid #ef4444; animation: pulse 0.8s infinite alternate; }
    @keyframes pulse { 0% { transform: scale(1); box-shadow: 0 0 10px rgba(239,68,68,0.4); } 100% { transform: scale(1.02); box-shadow: 0 0 25px rgba(239,68,68,0.8); } }
    .metric { display: flex; justify-content: space-between; align-items: center; padding: 10px 14px; background: #0f172a; border-radius: 10px; margin-bottom: 8px; font-size: 15px; border: 1px solid #1e293b; }
    .metric span:first-child { color: #94a3b8; font-weight: 500; }
    .metric span:last-child { font-weight: 700; color: #38bdf8; font-size: 16px; }
    button { background: #38bdf8; color: #0f172a; border: none; padding: 10px 20px; border-radius: 8px; font-weight: 700; font-size: 14px; cursor: pointer; margin-top: 6px; transition: 0.2s; }
    button:hover { background: #7dd3fc; }
    
    /* Potentiometer Guide Cards */
    .pot-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; margin-top: 14px; text-align: left; }
    .pot-card { background: #0f172a; border: 1px solid #334155; border-radius: 12px; padding: 12px; }
    .pot-title { font-size: 14px; font-weight: 700; color: #f59e0b; margin-bottom: 6px; display: flex; align-items: center; gap: 4px; }
    .dir-item { font-size: 12.5px; line-height: 1.4; color: #cbd5e1; margin-bottom: 6px; }
    .dir-cw { color: #4ade80; font-weight: 600; }
    .dir-ccw { color: #f87171; font-weight: 600; }
    .board-diagram { background: #0f172a; border: 1px dashed #475569; border-radius: 10px; padding: 12px; font-family: monospace; font-size: 12px; line-height: 1.4; color: #94a3b8; margin-top: 10px; white-space: pre; text-align: center; }
  </style>
</head>
<body>
  <div class="card">
    <h1>📡 ESP32-C3 PIR Sensor Tester</h1>
    
    <div id="statusBox" class="status-box no-motion">
      🟢 NO MOTION / IDLE
    </div>

    <div class="metric">
      <span>Active Motion Timer:</span>
      <span id="activeTimer">0.0s</span>
    </div>

    <div class="metric">
      <span>Total Motion Triggers:</span>
      <span id="triggerCount">0</span>
    </div>

    <div class="metric">
      <span>LDR Raw Value (0-4095):</span>
      <span id="ldrVal">0</span>
    </div>

    <div class="metric">
      <span>Simulated Light Mode:</span>
      <span id="lightState">50% STANDBY GLOW</span>
    </div>

    <button onclick="resetStats()">Reset Counter</button>
  </div>

  <div class="card">
    <div style="font-size: 16px; font-weight: 700; color: #38bdf8; margin-bottom: 8px;">
      🛠️ Potentiometer Adjustment Guide
    </div>
    
    <div style="font-size: 13px; color: #94a3b8; text-align: left; line-height: 1.5;">
      Hold the <strong>HW-416B</strong> board with the <strong>3 connector pins pointing DOWN</strong> towards you:
    </div>

    <div class="board-diagram">
┌───────────────────────────────┐
│     [ SENSITIVITY ]   [ TIME DELAY ]   │
│         (Left)            (Right)      │
│          [ ⚙️ ]            [ ⚙️ ]       │
│                                       │
│          [  PIR DOME SENSOR  ]        │
│                                       │
│          [ VCC ] [ OUT ] [ GND ]      │
└───────────────────────────────┘</div>

    <div class="pot-grid">
      <!-- Left Potentiometer -->
      <div class="pot-card">
        <div class="pot-title">⚙️ LEFT (Sensitivity)</div>
        <div class="dir-item"><span class="dir-cw">↻ Turn Right (CW):</span> Increases distance (up to 7 meters).</div>
        <div class="dir-item"><span class="dir-ccw">↺ Turn Left (CCW):</span> Decreases distance (down to ~3 meters).</div>
        <div style="font-size: 11.5px; color: #94a3b8; margin-top: 4px;">👉 <em>Recommendation: Set to 30-40% so it detects a person but ignores room air currents.</em></div>
      </div>

      <!-- Right Potentiometer -->
      <div class="pot-card">
        <div class="pot-title">⏱️ RIGHT (Time Delay)</div>
        <div class="dir-item"><span class="dir-cw">↻ Turn Right (CW):</span> Increases hold time (up to 300s / 5 mins).</div>
        <div class="dir-item"><span class="dir-ccw">↺ Turn Left (CCW):</span> Decreases hold time (down to ~3-5 seconds).</div>
        <div style="font-size: 11.5px; color: #94a3b8; margin-top: 4px;">👉 <em>Recommendation: Turn CCW first, then turn slightly CW until timer reads ~25-30s.</em></div>
      </div>
    </div>
  </div>

  <script>
    function updateStatus() {
      fetch('/data')
        .then(r => r.json())
        .then(data => {
          const box = document.getElementById('statusBox');
          if (data.motion) {
            box.className = 'status-box motion';
            box.innerHTML = '🚨 MOTION DETECTED!';
          } else {
            box.className = 'status-box no-motion';
            box.innerHTML = '🟢 NO MOTION / IDLE';
          }
          document.getElementById('activeTimer').innerText = data.duration.toFixed(1) + 's';
          document.getElementById('triggerCount').innerText = data.count;
          document.getElementById('ldrVal').innerText = data.ldr;
          document.getElementById('lightState').innerText = data.state;
        })
        .catch(err => console.log('Poll err:', err));
    }

    function resetStats() {
      fetch('/reset');
    }

    setInterval(updateStatus, 300);
  </script>
</body>
</html>
)rawliteral";

void setLedBrightness(int percent) {
  int duty = map(percent, 0, 100, 255, 0); // Active LOW on ESP32-C3 Super Mini GPIO8
  analogWrite(LED_PIN, duty);
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", HTML_PAGE);
}

void handleData() {
  String stateStr;
  if (!isNight) {
    stateStr = "0% (Day Lockout)";
  } else if (currentMotion) {
    stateStr = "100% FULL BRIGHT";
  } else {
    stateStr = "50% STANDBY GLOW";
  }

  String json = "{";
  json += "\"motion\":" + String(currentMotion ? "true" : "false") + ",";
  json += "\"duration\":" + String(motionDuration) + ",";
  json += "\"count\":" + String(triggerCount) + ",";
  json += "\"ldr\":" + String(ldrValue) + ",";
  json += "\"state\":\"" + stateStr + "\"";
  json += "}";
  server.send(200, "application/json; charset=utf-8", json);
}

void handleReset() {
  triggerCount = 0;
  server.send(200, "text/plain; charset=utf-8", "OK");
}

void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_START:
      Serial.println("[WIFI EVENT] Station Started");
      break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[WIFI EVENT] Connected to AP!");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("\n=========================================\n");
      Serial.printf(">>> SUCCESS: WI-FI CONNECTED! <<<\n");
      Serial.printf(">>> SSID: %s <<<\n", WiFi.SSID().c_str());
      Serial.printf(">>> ESP32 IP Address: %s <<<\n", IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str());
      Serial.printf(">>> Signal Strength: %d dBm <<<\n", WiFi.RSSI());
      Serial.printf("=========================================\n\n");
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      Serial.printf("[WIFI EVENT] Disconnected, reason: %d\n", info.wifi_sta_disconnected.reason);
      break;
    default:
      break;
  }
}

void setup() {
  // 1. Disable RTC Brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // 2. Hardware pin modes
  pinMode(PIR_PIN, INPUT_PULLDOWN);
  pinMode(LDR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  setLedBrightness(50);

  Serial.begin(115200);
  delay(1000);

  Serial.println("\n=========================================");
  Serial.println("  ESP32-C3 Super Mini - PIR Calibrator   ");
  Serial.println("=========================================");

  // 3. Initialize Wi-Fi with 8.5dBm TX Power Workaround (Fix for Issue #6767)
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

  // Set 8.5 dBm before and after begin to prevent RF saturation & voltage brownout
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.setSleep(false);

  // Configure Static IP (192.168.1.5)
  WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);

  Serial.printf("[WIFI] Connecting to '%s' with 8.5 dBm TX Power...\n", ssid);
  WiFi.begin(ssid, password);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 35) {
    delay(300);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n>>> ESP32 IS ONLINE AND READY! <<<");
    Serial.print(">>> Dashboard URL: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[INFO] Connecting in background...");
  }

  if (MDNS.begin("pir")) {
    Serial.println("[mDNS] Responder started at http://pir.local");
  }

  // 4. Start HTTP Web Server
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/reset", handleReset);
  server.begin();
  Serial.println("[HTTP] Web Server started on port 80");
}

void loop() {
  server.handleClient();

  int pirState = digitalRead(PIR_PIN);
  ldrValue = analogRead(LDR_PIN);

  isNight = (ldrValue < duskThreshold || ldrValue > 4000); 

  if (pirState == HIGH) {
    if (!currentMotion) {
      currentMotion = true;
      triggerCount++;
      motionStartTime = millis();
      Serial.printf("[MOTION] Trigger #%d | LDR: %d\n", triggerCount, ldrValue);
    }
    motionDuration = (millis() - motionStartTime) / 1000.0;
  } else {
    if (currentMotion) {
      currentMotion = false;
      Serial.printf("[IDLE] Motion ended. Duration: %.1fs\n", motionDuration);
    }
    motionDuration = 0.0;
  }

  if (!isNight) {
    setLedBrightness(0);
  } else if (currentMotion) {
    setLedBrightness(100);
  } else {
    setLedBrightness(50);
  }

  delay(20);
}
