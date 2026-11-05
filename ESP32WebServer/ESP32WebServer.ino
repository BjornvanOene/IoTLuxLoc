#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

// Configuration
const char* AP_SSID = "LuxLoc";
const char* AP_PASSWORD = "67676767";

IPAddress AP_IP(192, 168, 4, 1);
IPAddress SUBNET(255, 255, 255, 0);
const byte DNS_PORT = 53;

const int ARM_PIN = 25;
const int MOVEMENT_PIN = 33;
const int RESET_PIN = 32;

const unsigned long WIFI_WINDOW_MS = 15000;
const uint64_t SLEEP_TIME_US = 90ULL * 1000000ULL;
const unsigned long RESET_DURATION_MS = 200;

// Global variables
WebServer server(80);
DNSServer dnsServer;

bool isArmed = false;
bool alarmTriggered = false;
bool resetActive = false;

unsigned long resetStartTime = 0;
unsigned long wifiWindowStart = 0;

double bikeLatitude = 0.0;
double bikeLongitude = 0.0;
bool gpsValid = false;

// Values retained during deep sleep
const uint32_t RTC_MAGIC_VALUE = 0x4C584C43;

RTC_DATA_ATTR uint32_t rtcMagic = 0;
RTC_DATA_ATTR bool storedArmed = false;
RTC_DATA_ATTR bool storedAlarm = false;

// Webpage code
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>LuxLoc | Bike Security</title>

<style>
* { box-sizing:border-box; margin:0; padding:0; }

:root {
  --bg:#090e17;
  --panel:#141c29;
  --border:#283447;
  --text:#f3f6fb;
  --muted:#8d9cb0;
  --accent:#41e0b5;
  --danger:#ff647c;
}

body {
  background:var(--bg);
  color:var(--text);
  font-family:system-ui,'Segoe UI',sans-serif;
  min-height:100vh;
  padding:28px 20px 50px;
}

.dashboard {
  max-width:440px;
  margin:auto;
}

header {
  display:flex;
  justify-content:space-between;
  align-items:center;
  margin-bottom:38px;
}

.brand {
  font-size:28px;
  font-weight:850;
  letter-spacing:-1px;
}

.brand span { color:var(--accent); }

.tag {
  font-size:11px;
  color:var(--accent);
  border:1px solid #276757;
  padding:7px 10px;
  border-radius:20px;
}

.section-label {
  text-transform:uppercase;
  letter-spacing:2px;
  font-size:11px;
  color:var(--muted);
}

.lock-area {
  text-align:center;
  padding:15px 0 35px;
}

.lock-circle {
  width:190px;
  height:190px;
  margin:28px auto 25px;
  border-radius:50%;
  border:2px solid var(--accent);
  display:flex;
  flex-direction:column;
  justify-content:center;
  align-items:center;
  background:radial-gradient(circle,#19392f,#101d21 70%);
  box-shadow:0 0 45px #41e0b51a;
}

.lock-circle.armed {
  border-color:var(--danger);
  background:radial-gradient(circle,#3e202b,#1d1720 70%);
}

.lock-circle.triggered {
  border-color:#ff334f;
  animation:pulse 1.5s infinite;
}

@keyframes pulse {
  0%,100% { box-shadow:0 0 20px #ff334f33; }
  50% { box-shadow:0 0 50px #ff334f88; }
}

.lock-icon {
  font-size:48px;
  margin-bottom:10px;
}

.lock-state {
  font-size:17px;
  font-weight:800;
  letter-spacing:2px;
}

.lock-description {
  font-size:13px;
  color:var(--muted);
  margin:8px 0 23px;
}

.main-button, .maps-btn {
  width:100%;
  padding:17px;
  border:none;
  border-radius:13px;
  background:var(--accent);
  color:#061610;
  font-size:16px;
  font-weight:800;
  cursor:pointer;
}

.main-button { max-width:300px; }

.main-button.armed {
  background:var(--danger);
  color:white;
}

button:disabled {
  opacity:.45;
  cursor:not-allowed;
}

.panel {
  background:var(--panel);
  border:1px solid var(--border);
  border-radius:17px;
  padding:21px;
  margin-bottom:16px;
}

.panel-title {
  display:flex;
  justify-content:space-between;
  align-items:center;
  margin-bottom:18px;
}

.panel-title h2 {
  font-size:15px;
  font-weight:650;
}

.small-label {
  color:var(--muted);
  font-size:12px;
}

.movement-row {
  display:flex;
  gap:13px;
  align-items:center;
}

.dot {
  width:11px;
  height:11px;
  border-radius:50%;
  background:var(--accent);
}

.dot.alert {
  background:var(--danger);
  box-shadow:0 0 12px var(--danger);
}

.value {
  font-size:16px;
  font-weight:650;
}

.detail {
  color:var(--muted);
  font-size:12px;
  margin-top:5px;
}

.location-box {
  border:1px dashed #364559;
  border-radius:12px;
  background:#101824;
  min-height:120px;
  display:flex;
  flex-direction:column;
  align-items:center;
  justify-content:center;
  gap:10px;
  text-align:center;
  margin-bottom:17px;
}

.location-icon { font-size:30px; }

.coordinates {
  font-family:monospace;
  font-size:13px;
  color:var(--muted);
  text-align:center;
  line-height:1.8;
}

.maps-btn { margin-top:20px; }

.sleep-note {
  text-align:center;
  color:var(--muted);
  font-size:12px;
  margin-top:15px;
  line-height:1.6;
}

footer {
  text-align:center;
  margin-top:35px;
  color:#586579;
  font-size:11px;
  letter-spacing:1px;
}
</style>
</head>

<body>
<div class="dashboard">

<header>
  <div class="brand">LUX<span>LOC</span></div>
  <div class="tag">● LOCAL SYSTEM</div>
</header>

<section class="lock-area">
  <div class="section-label">Security Control</div>

  <div class="lock-circle" id="lockCircle">
    <div class="lock-icon" id="lockIcon">🔓</div>
    <div class="lock-state" id="lockState">
      CONNECTING
    </div>
  </div>

  <p class="lock-description" id="lockDescription">
    Checking alarm status...
  </p>

  <button class="main-button" id="toggleBtn"
          onclick="toggleAlarm()" disabled>
    Connecting...
  </button>

  <p class="sleep-note" id="sleepInfo">
    Checking power status...
  </p>
</section>

<section class="panel">
  <div class="panel-title">
    <h2>Movement Sensor</h2>
    <span class="small-label">LIVE</span>
  </div>

  <div class="movement-row">
    <div class="dot" id="movementDot"></div>
    <div>
      <div class="value" id="movementValue">
        Waiting for data
      </div>
      <div class="detail" id="movementDetail">
        Monitoring sensor input
      </div>
    </div>
  </div>
</section>

<section class="panel">
  <div class="panel-title">
    <h2>Bike Location</h2>
    <span class="small-label" id="gpsLabel">
      GPS OFFLINE
    </span>
  </div>

  <div class="location-box">
    <div class="location-icon">⌖</div>
    <div class="value" id="gpsStatus">
      Waiting for GPS
    </div>
    <div class="small-label">
      No position received yet
    </div>
  </div>

  <div class="coordinates" id="coordinates">
    Latitude: -- | Longitude: --
  </div>

  <button class="maps-btn" id="mapsBtn"
          onclick="openMaps()" disabled>
    📍 Open in Google Maps
  </button>
</section>

<footer>LUXLOC · BIKE SECURITY SYSTEM</footer>
</div>

<script>
let isArmed = false;
let pending = false;
let statusLoaded = false;
let bikeLatitude = null;
let bikeLongitude = null;

async function fetchStatus() {
  try {
    const res = await fetch('/status', {
      cache:'no-store'
    });

    if (!res.ok) throw new Error('Offline');

    const data = await res.json();

    isArmed = data.armed;
    statusLoaded = true;

    const circle = document.getElementById('lockCircle');
    const icon = document.getElementById('lockIcon');
    const state = document.getElementById('lockState');
    const desc = document.getElementById('lockDescription');
    const btn = document.getElementById('toggleBtn');

    circle.classList.toggle('armed', isArmed);
    circle.classList.toggle('triggered', data.alarm);
    btn.classList.toggle('armed', isArmed);
    btn.disabled = pending;

    if (data.alarm) {
      icon.textContent = '🚨';
      state.textContent = 'ALARM TRIGGERED';
      desc.textContent = 'Movement detected while armed';
      btn.textContent = 'DISARM & RESET ALARM';
    } else if (isArmed) {
      icon.textContent = '🔒';
      state.textContent = 'ARMED';
      desc.textContent = 'Your bike is protected';
      btn.textContent = 'DISARM SYSTEM';
    } else {
      icon.textContent = '🔓';
      state.textContent = 'DISARMED';
      desc.textContent = 'Security inactive';
      btn.textContent = 'ARM SYSTEM';
    }

    const movement = document.getElementById('movementValue');
    const detail = document.getElementById('movementDetail');

    document.getElementById('movementDot')
      .classList.toggle('alert',data.movement);

    movement.textContent = data.movement
      ? 'Movement Detected'
      : 'No Movement';

    detail.textContent = data.alarm
      ? 'Alarm triggered - owner reset required'
      : data.movement
        ? 'Bike is currently moving'
        : 'Bike is stationary';

    const sleepInfo = document.getElementById('sleepInfo');

    if (!isArmed) {
      sleepInfo.textContent =
        'Wi-Fi active · Sleep disabled';
    } else if (data.movement) {
      sleepInfo.textContent =
        'Movement detected · Staying awake';
    } else if (data.clients > 0) {
      sleepInfo.textContent =
        'Phone connected · Staying awake';
    } else {
      sleepInfo.textContent =
        'Sleep in approximately ' + data.sleepIn + 's';
    }

  } catch(e) {
    statusLoaded = false;

    document.getElementById('lockState').textContent =
      'OFFLINE';

    document.getElementById('lockDescription').textContent =
      'ESP32 sleeping or disconnected';

    document.getElementById('sleepInfo').textContent =
      'Wi-Fi will return during the next wake window';

    document.getElementById('toggleBtn').disabled = true;
  }
}

async function toggleAlarm() {
  if (pending || !statusLoaded) return;

  pending = true;
  document.getElementById('toggleBtn').disabled = true;

  try {
    const endpoint = isArmed ? '/disarm' : '/arm';

    const res = await fetch(endpoint, {
      method:'POST'
    });

    if (!res.ok) throw new Error('Command failed');

  } catch(e) {
    console.warn(e);
  } finally {
    pending = false;
    await fetchStatus();
  }
}

async function fetchLocation() {
  try {
    const res = await fetch('/location', {
      cache:'no-store'
    });

    if (!res.ok) throw new Error('No GPS');

    const data = await res.json();

    if (
      typeof data.latitude !== 'number' ||
      typeof data.longitude !== 'number' ||
      !Number.isFinite(data.latitude) ||
      !Number.isFinite(data.longitude) ||
      Math.abs(data.latitude) > 90 ||
      Math.abs(data.longitude) > 180
    ) {
      throw new Error('No valid GPS fix');
    }

    bikeLatitude = data.latitude;
    bikeLongitude = data.longitude;

    document.getElementById('gpsLabel').textContent =
      'GPS AVAILABLE';

    document.getElementById('gpsStatus').textContent =
      'Position Received';

    document.getElementById('coordinates').textContent =
      'Lat: ' + bikeLatitude.toFixed(6) +
      ' | Lon: ' + bikeLongitude.toFixed(6);

    document.getElementById('mapsBtn').disabled = false;

  } catch(e) {
    bikeLatitude = null;
    bikeLongitude = null;

    document.getElementById('gpsLabel').textContent =
      'GPS OFFLINE';

    document.getElementById('gpsStatus').textContent =
      'Waiting for GPS';

    document.getElementById('coordinates').textContent =
      'Latitude: -- | Longitude: --';

    document.getElementById('mapsBtn').disabled = true;
  }
}

function openMaps() {
  if (bikeLatitude === null ||
      bikeLongitude === null) return;

  const coordinates = bikeLatitude + ',' + bikeLongitude;

  window.location.href =
    'https://www.google.com/maps/search/?api=1&query=' +
    encodeURIComponent(coordinates);
}

fetchStatus();
fetchLocation();

setInterval(fetchStatus, 1000);
setInterval(fetchLocation, 10000);
</script>
</body>
</html>
)rawliteral";

// ALARM LOGIC

void checkMovement() {
  bool movement = digitalRead(MOVEMENT_PIN) == HIGH;

  if (isArmed && movement && !alarmTriggered) {
    alarmTriggered = true;
    Serial.println("ALARM TRIGGERED: Bike movement detected");
  }
}

// HTTP HANDLERS

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleCaptive() {
  server.sendHeader("Location",
                    "http://192.168.4.1/", true);
  server.sendHeader("Cache-Control", "no-store");
  server.send(302, "text/plain", "");
}

void handleStatus() {
  checkMovement();

  unsigned long elapsed = millis() - wifiWindowStart;
  unsigned long remaining =
    elapsed < WIFI_WINDOW_MS
    ? (WIFI_WINDOW_MS - elapsed + 999) / 1000
    : 0;

  String json = "{";
  json += "\"armed\":";
  json += (isArmed ? "true" : "false");
  json += ",\"movement\":";
  json += (digitalRead(MOVEMENT_PIN) == HIGH ? "true" : "false");
  json += ",\"alarm\":";
  json += (alarmTriggered ? "true" : "false");
  json += ",\"sleepIn\":";
  json += String(remaining);
  json += ",\"clients\":";
  json += String(WiFi.softAPgetStationNum());
  json += "}";

  server.send(200, "application/json", json);
}

void handleArm() {
  // GPIO 25 HIGH while armed
  pinMode(ARM_PIN, OUTPUT);
  digitalWrite(ARM_PIN, HIGH);

  isArmed = true;
  alarmTriggered = false;

  wifiWindowStart = millis();

  Serial.println("Alarm ARMED");
  Serial.printf("GPIO 25 output register: %d\n",
                digitalRead(ARM_PIN));

  server.send(
    200,
    "application/json",
    "{\"status\":\"ok\",\"armed\":true}"
  );
}

void handleDisarm() {
  digitalWrite(ARM_PIN, LOW);

  isArmed = false;
  alarmTriggered = false;

  // Start the 200 ms reset pulse
  digitalWrite(RESET_PIN, HIGH);
  resetStartTime = millis();
  resetActive = true;

  Serial.println("Alarm DISARMED");
  Serial.println("External reset pulse started");

  server.send(
    200, "application/json",
    "{\"status\":\"ok\",\"armed\":false}"
  );
}

void handleLocation() {
  if (!gpsValid) {
    server.send(
      200, "application/json",
      "{\"latitude\":null,\"longitude\":null}"
    );
    return;
  }

  String json = "{";
  json += "\"latitude\":";
  json += String(bikeLatitude, 6);
  json += ",\"longitude\":";
  json += String(bikeLongitude, 6);
  json += "}";

  server.send(200, "application/json", json);
}

// DEEP SLEEP 

void enterDeepSleep() {
  // Do not sleep while movement input is HIGH
  if (digitalRead(MOVEMENT_PIN) == HIGH) {
    return;
  }

  // Do not interrupt the reset pulse
  if (resetActive) {
    return;
  }

  Serial.println("Entering deep sleep for 90 seconds");

  // Save state in RTC memory
  storedArmed = isArmed;
  storedAlarm = alarmTriggered;
  rtcMagic = RTC_MAGIC_VALUE;

  // GPIO 25 must remain HIGH while sleeping
  digitalWrite(ARM_PIN, HIGH);

  // Latch GPIO 25 output
  if (gpio_hold_en(GPIO_NUM_25) != ESP_OK) {
    Serial.println("GPIO hold failed, cancelling sleep");
    return;
  }

  gpio_deep_sleep_hold_en();

  // Enable movement wake-up
  rtc_gpio_pullup_dis(GPIO_NUM_33);
  rtc_gpio_pulldown_en(GPIO_NUM_33);

  esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 1);

  // Enable 90-second timer wake-up
  esp_sleep_enable_timer_wakeup(SLEEP_TIME_US);

  Serial.flush();
  esp_deep_sleep_start();
}

// SETUP

void setup() {
  Serial.begin(19200);
  delay(100);

  esp_sleep_wakeup_cause_t wakeReason =
    esp_sleep_get_wakeup_cause();

  bool wokeFromSleep =
    wakeReason == ESP_SLEEP_WAKEUP_TIMER ||
    wakeReason == ESP_SLEEP_WAKEUP_EXT0;

  bool restoreArmed =
    wokeFromSleep &&
    rtcMagic == RTC_MAGIC_VALUE &&
    storedArmed;

  isArmed = restoreArmed;
  alarmTriggered = restoreArmed && storedAlarm;

  // Restore GPIO 25 before releasing its hold.
  // Avoid deliberately pulling it LOW on wake.
  gpio_set_direction(GPIO_NUM_25, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_NUM_25, restoreArmed ? 1 : 0);

  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(GPIO_NUM_25);

  digitalWrite(ARM_PIN, isArmed ? HIGH : LOW);

  // GPIO 32 normally LOW
  digitalWrite(RESET_PIN, LOW);
  pinMode(RESET_PIN, OUTPUT);

  // Restore GPIO 33 from RTC use
  rtc_gpio_deinit(GPIO_NUM_33);
  pinMode(MOVEMENT_PIN, INPUT_PULLDOWN);

  if (wakeReason == ESP_SLEEP_WAKEUP_EXT0 &&
      isArmed) {
    alarmTriggered = true;
    Serial.println("WAKE: GPIO 33 movement detected");
  } else if (wakeReason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("WAKE: 90-second timer");
  } else {
    Serial.println("Normal power-on/reset");
  }

  checkMovement();

  // Start Wi-Fi
  WiFi.mode(WIFI_AP);

  WiFi.softAPsetHostname("LuxLoc");
  WiFi.softAPConfig(AP_IP, AP_IP, SUBNET);

  if (WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("LuxLoc Wi-Fi started");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Wi-Fi startup failed");
  }

  dnsServer.start(DNS_PORT, "*", AP_IP);

  // HTTP Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/location", HTTP_GET, handleLocation);
  server.on("/arm", HTTP_POST, handleArm);
  server.on("/disarm", HTTP_POST, handleDisarm);

  // Captive Portal
  server.on("/generate_204", HTTP_GET, handleCaptive);
  server.on("/gen_204", HTTP_GET, handleCaptive);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptive);
  server.on("/ncsi.txt", HTTP_GET, handleCaptive);
  server.on("/connecttest.txt", HTTP_GET, handleCaptive);
  server.on("/redirect", HTTP_GET, handleCaptive);
  server.on("/canonical.html", HTTP_GET, handleCaptive);
  server.on("/success.txt", HTTP_GET, handleCaptive);
  server.on("/library/test/success.html", HTTP_GET, handleCaptive);
  server.onNotFound(handleCaptive);

  server.begin();

  wifiWindowStart = millis();

  Serial.println("LuxLoc server ready");
}

// MAIN LOOP

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  checkMovement();

  // Maintain ARM output while awake
  digitalWrite(ARM_PIN, isArmed ? HIGH : LOW);

  // Finish momentary reset pulse
  if (resetActive &&
      millis() - resetStartTime >= RESET_DURATION_MS) {

    digitalWrite(RESET_PIN, LOW);
    resetActive = false;

    Serial.println("External reset pulse complete");
  }

  // No sleep when disarmed
  if (!isArmed) {
    return;
  }

  // Keep Wi-Fi on while a client is connected
  if (WiFi.softAPgetStationNum() > 0) {
    wifiWindowStart = millis();
    return;
  }

  // Remain awake while movement input is HIGH
  if (digitalRead(MOVEMENT_PIN) == HIGH) {
    wifiWindowStart = millis();
    return;
  }

  // Check the 15-second awake window
  if (millis() - wifiWindowStart >= WIFI_WINDOW_MS) {
    enterDeepSleep();
  }
}
