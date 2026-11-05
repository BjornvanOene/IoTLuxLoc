#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

// CONFIG
const char* AP_SSID = "LuxLoc";
const char* AP_PASSWORD = "67676767";

// IP Setup
IPAddress AP_IP(192, 168, 4, 1);
IPAddress SUBNET(255, 255, 255, 0);
const byte DNS_PORT = 53;

// GPIO Pins
const int ARM_PIN = 25;
const int MOVEMENT_PIN = 33;
const int RESET_PIN = 32;

// Global variables
WebServer server(80);
DNSServer dnsServer;

bool isArmed = false;
bool alarmTriggered = false;

bool resetActive = false;
unsigned long resetStartTime = 0;

const unsigned long RESET_DURATION = 200; // 200 milliseconds

// GPS data from future backend
double bikeLatitude = 0.0;
double bikeLongitude = 0.0;
bool gpsValid = false;

// HTML Webpage
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport"
      content="width=device-width, initial-scale=1.0">
<title>LuxLoc | Bike Security</title>

<style>
  * {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
  }

  :root {
    --bg: #090e17;
    --panel: #141c29;
    --border: #283447;
    --text: #f3f6fb;
    --muted: #8d9cb0;
    --accent: #41e0b5;
    --danger: #ff647c;
  }

  body {
    background: var(--bg);
    color: var(--text);
    font-family: system-ui, -apple-system,
                 'Segoe UI', sans-serif;
    min-height: 100vh;
    padding: 28px 20px 50px;
  }

  .dashboard {
    max-width: 440px;
    margin: 0 auto;
  }

  header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 44px;
  }

  .brand {
    font-size: 27px;
    font-weight: 850;
    letter-spacing: -1.5px;
  }

  .brand span {
    color: var(--accent);
  }

  .tag {
    font-size: 11px;
    color: var(--accent);
    border: 1px solid #276757;
    padding: 7px 10px;
    border-radius: 20px;
  }

  .section-label {
    text-transform: uppercase;
    letter-spacing: 2px;
    font-size: 11px;
    color: var(--muted);
  }

  .lock-area {
    text-align: center;
    padding: 15px 0 38px;
  }

  .lock-circle {
    width: 190px;
    height: 190px;
    margin: 28px auto 25px;
    border-radius: 50%;
    border: 2px solid var(--accent);
    display: flex;
    flex-direction: column;
    justify-content: center;
    align-items: center;
    background: radial-gradient(
      circle, #19392f 0%, #101d21 65%, #0e141c 100%
    );
    box-shadow: 0 0 45px #41e0b51a;
    transition: .3s;
  }

  .lock-circle.armed {
    border-color: var(--danger);
    background: radial-gradient(
      circle, #3e202b 0%, #1d1720 65%, #0e141c 100%
    );
    box-shadow: 0 0 45px #ff647c20;
  }

  .lock-circle.triggered {
    border-color: #ff334f;
    background: radial-gradient(
      circle, #581a29 0%, #29141e 65%, #0e141c 100%
    );
    box-shadow: 0 0 35px #ff334f55;
    animation: alarmPulse 1.5s infinite;
  }

  @keyframes alarmPulse {
    0%, 100% {
      box-shadow: 0 0 20px #ff334f33;
    }
    50% {
      box-shadow: 0 0 50px #ff334f88;
    }
  }

  .lock-icon {
    font-size: 48px;
    margin-bottom: 10px;
  }

  .lock-state {
    font-size: 17px;
    font-weight: 800;
    letter-spacing: 2px;
  }

  .lock-description {
    font-size: 13px;
    color: var(--muted);
    margin-top: 8px;
    margin-bottom: 23px;
  }

  .main-button {
    width: 100%;
    max-width: 300px;
    background: var(--accent);
    border: none;
    color: #061610;
    font-size: 16px;
    font-weight: 800;
    padding: 17px;
    border-radius: 13px;
    cursor: pointer;
    transition: .2s;
  }

  .main-button.armed {
    background: var(--danger);
    color: white;
  }

  .main-button:disabled {
    opacity: .5;
    cursor: not-allowed;
  }

  .panel {
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 17px;
    padding: 21px;
    margin-bottom: 16px;
  }

  .panel-title {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 18px;
  }

  .panel-title h2 {
    font-size: 15px;
    font-weight: 650;
  }

  .small-label {
    color: var(--muted);
    font-size: 12px;
  }

  .movement-row {
    display: flex;
    gap: 13px;
    align-items: center;
  }

  .dot {
    width: 11px;
    height: 11px;
    background: var(--accent);
    border-radius: 50%;
    flex-shrink: 0;
  }

  .dot.alert {
    background: var(--danger);
    box-shadow: 0 0 12px var(--danger);
  }

  .value {
    font-size: 16px;
    font-weight: 650;
  }

  .detail {
    color: var(--muted);
    font-size: 12px;
    margin-top: 5px;
  }

  .location-box {
    border: 1px dashed #364559;
    border-radius: 12px;
    min-height: 120px;
    display: flex;
    align-items: center;
    justify-content: center;
    flex-direction: column;
    gap: 10px;
    background: #101824;
    margin-bottom: 17px;
    text-align: center;
  }

  .location-icon {
    font-size: 30px;
  }

  .coordinates {
    font-family: monospace;
    font-size: 13px;
    color: var(--muted);
    text-align: center;
    line-height: 1.8;
  }

  .maps-btn {
    width: 100%;
    margin-top: 20px;
    padding: 15px;
    background: var(--accent);
    color: #061610;
    border: none;
    border-radius: 12px;
    font-weight: 700;
    font-size: 15px;
    cursor: pointer;
  }

  .maps-btn:disabled {
    background: #364559;
    color: var(--muted);
    cursor: not-allowed;
  }

  footer {
    text-align: center;
    margin-top: 35px;
    font-size: 11px;
    letter-spacing: 1px;
    color: #586579;
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
      <div class="small-label" id="gpsDescription">
        No position received yet
      </div>
    </div>

    <div class="coordinates" id="coordinates">
      Latitude: --
      <br>
      Longitude: --
    </div>

    <button class="maps-btn" id="mapsBtn"
            onclick="openMaps()" disabled>
      📍 Open in Google Maps
    </button>
  </section>

  <footer>
    LUXLOC · BIKE SECURITY SYSTEM
  </footer>

</div>

<script>
let isArmed = false;
let requestPending = false;
let statusLoaded = false;

let bikeLatitude = null;
let bikeLongitude = null;

// GET ALARM STATUS
async function fetchStatus() {
  try {
    const res = await fetch('/status', {
      cache: 'no-store'
    });

    if (!res.ok) {
      throw new Error('Status unavailable');
    }

    const data = await res.json();

    isArmed = data.armed;
    statusLoaded = true;

    const circle =
      document.getElementById('lockCircle');
    const icon =
      document.getElementById('lockIcon');
    const state =
      document.getElementById('lockState');
    const description =
      document.getElementById('lockDescription');
    const button =
      document.getElementById('toggleBtn');

    circle.classList.toggle('armed', isArmed);
    circle.classList.toggle('triggered', data.alarm);

    button.classList.toggle('armed', isArmed);
    button.disabled = requestPending;

    // Alarm state handling
    if (data.alarm) {

      icon.textContent = '🚨';
      state.textContent = 'ALARM TRIGGERED';

      description.textContent =
        'Warning: movement detected while armed';

      button.textContent = 'DISARM & RESET ALARM';

    } else if (isArmed) {

      icon.textContent = '🔒';
      state.textContent = 'ARMED';

      description.textContent =
        'Your bike is protected';

      button.textContent = 'DISARM SYSTEM';

    } else {

      icon.textContent = '🔓';
      state.textContent = 'DISARMED';

      description.textContent =
        'Your bike security is inactive';

      button.textContent = 'ARM SYSTEM';
    }

    // Movement sensor display
    const dot =
      document.getElementById('movementDot');
    const movement =
      document.getElementById('movementValue');
    const detail =
      document.getElementById('movementDetail');

    dot.classList.toggle('alert', data.movement);

    movement.textContent = data.movement
      ? 'Movement Detected'
      : 'No Movement';

    if (data.alarm) {
      detail.textContent =
        'Alarm triggered - owner reset required';
    } else if (data.movement) {
      detail.textContent = isArmed
        ? 'Warning: bike moving while armed'
        : 'Bike is currently moving';
    } else {
      detail.textContent = 'Bike is stationary';
    }

  } catch(e) {
    console.warn(e);
    statusLoaded = false;

    document.getElementById('lockState').textContent =
      'OFFLINE';

    document.getElementById('lockDescription').textContent =
      'Cannot reach ESP32';

    document.getElementById('toggleBtn').disabled = true;

    document.getElementById('movementValue').textContent =
      'Sensor unavailable';
  }
}

// ARM / DISARM ALARM
async function toggleAlarm() {
  if (requestPending || !statusLoaded) return;

  requestPending = true;

  document.getElementById('toggleBtn').disabled = true;

  try {
    const endpoint = isArmed ? '/disarm' : '/arm';

    const res = await fetch(endpoint, {
      method: 'POST'
    });

    if (!res.ok) {
      throw new Error('Command failed');
    }

  } catch(e) {
    console.warn(e);

    document.getElementById('lockDescription').textContent =
      'Command failed';

  } finally {
    requestPending = false;
    await fetchStatus();
  }
}

// GET GPS LOCATION
async function fetchLocation() {
  try {
    const res = await fetch('/location', {
      cache: 'no-store'
    });

    if (!res.ok) {
      throw new Error('Location unavailable');
    }

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

    document.getElementById('gpsDescription').textContent =
      'Latest reported GPS position';

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

    document.getElementById('gpsDescription').textContent =
      'No position received yet';

    document.getElementById('coordinates').textContent =
      'Latitude: -- | Longitude: --';

    document.getElementById('mapsBtn').disabled = true;
  }
}

// OPEN GPS LOCATION IN GOOGLE MAPS
function openMaps() {
  if (bikeLatitude === null ||
      bikeLongitude === null) {
    return;
  }

  const coordinates =
    bikeLatitude + ',' + bikeLongitude;

  const url =
    'https://www.google.com/maps/search/?api=1&query=' +
    encodeURIComponent(coordinates);

  window.location.href = url;
}

// Initial updates
fetchStatus();
fetchLocation();

// Automatic updates
setInterval(fetchStatus, 1000);
setInterval(fetchLocation, 10000);

</script>
</body>
</html>
)rawliteral";

// HTTP HANDLERS

void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleCaptive() {
  server.sendHeader(
    "Location",
    "http://192.168.4.1/",
    true
  );

  server.sendHeader("Cache-Control", "no-store");
  server.send(302, "text/plain", "");
}

// GET CURRENT ALARM STATUS
void handleStatus() {
  bool movement = digitalRead(MOVEMENT_PIN) == HIGH;

  String json = "{";

  json += "\"armed\":";
  json += (isArmed ? "true" : "false");

  json += ",\"movement\":";
  json += (movement ? "true" : "false");

  json += ",\"alarm\":";
  json += (alarmTriggered ? "true" : "false");

  json += "}";

  server.send(200, "application/json", json);
}

// ARM ALARM
void handleArm() {
  digitalWrite(ARM_PIN, HIGH);
  isArmed = true;
  alarmTriggered = false;

  Serial.println("Alarm ARMED");

  server.send(
    200,
    "application/json",
    "{\"status\":\"ok\",\"armed\":true,\"alarm\":false}"
  );
}

// DISARM AND RESET ALARM
void handleDisarm() {

  // Disarm external system
  digitalWrite(ARM_PIN, LOW);

  isArmed = false;
  alarmTriggered = false;

  Serial.println("Alarm DISARMED");

  // Start momentary reset pulse
  digitalWrite(RESET_PIN, HIGH);

  resetStartTime = millis();
  resetActive = true;

  Serial.println("Reset pulse started");

  server.send(
    200,
    "application/json",
    "{\"status\":\"ok\",\"armed\":false,\"alarm\":false}"
  );
}

// GET GPS COORDINATES
void handleLocation() {
  if (!gpsValid) {
    server.send(
      200,
      "application/json",
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

// SETUP
void setup() {
  Serial.begin(115200);

  Serial.println("Setting up LuxLoc Bike Alarm");

  // Configure GPIO
  digitalWrite(ARM_PIN, LOW);
  pinMode(ARM_PIN, OUTPUT);

  pinMode(MOVEMENT_PIN, INPUT_PULLDOWN);

  // Reset output
  digitalWrite(RESET_PIN, LOW);
  pinMode(RESET_PIN, OUTPUT);

  // Wi-Fi Access Point
  WiFi.mode(WIFI_AP_STA);

  WiFi.softAPsetHostname("LuxLoc");

  WiFi.softAPConfig(AP_IP, AP_IP, SUBNET);

  if (WiFi.softAP(AP_SSID, AP_PASSWORD)) {
    Serial.println("LuxLoc WiFi started successfully!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("ERROR: WiFi Access Point failed!");
  }

  Serial.printf("AP started - SSID: %s\n", AP_SSID);
  Serial.printf("AP IP: %s\n", AP_IP.toString().c_str());

  // DNS Server
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

  // Start Web Server
  server.begin();

  Serial.println("LuxLoc Web Server Started");
}

void loop() {

  dnsServer.processNextRequest();
  server.handleClient();

  // Keep arm output synchronized with armed state
  digitalWrite(ARM_PIN, isArmed ? HIGH : LOW);

  // Handle momentary reset pulse
  if (resetActive) {

    if (millis() - resetStartTime >= RESET_DURATION) {

      digitalWrite(RESET_PIN, LOW);

      resetActive = false;

      Serial.println("Reset pulse completed");
    }
  }

  // Read movement sensor
  bool movement = digitalRead(MOVEMENT_PIN) == HIGH;

  // Trigger alarm if bike moves while armed
  if (isArmed && movement && !alarmTriggered) {

    alarmTriggered = true;

    Serial.println("WARNING: Bike movement detected!");
    Serial.println("Alarm triggered!");
  }
}
