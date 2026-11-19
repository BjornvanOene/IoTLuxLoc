
#include <SoftwareWire.h>
#include <SoftwareSerial.h>
#include <rn2xx3.h>
#include <math.h>

// PIN DEFINITIONS

// MPU6050
#define SDA_PIN 12
#define SCL_PIN 11
#define MPU_ADDR 0x68

// RN2483 LoRaWAN
#define LORA_RX_PIN 2
#define LORA_TX_PIN 3
#define LORA_RESET_PIN 6

// RGB LED
#define RED_PIN 9
#define GREEN_PIN 8
#define BLUE_PIN 7

#define COMMON_ANODE false

// ALARM SETTINGS
#define TRIGGER_THRESHOLD 0.40
#define AVERAGE_THRESHOLD 0.20

#define WAIT_TIME 5000UL
#define MEASURE_TIME 5000UL
#define SAMPLE_INTERVAL 50UL
#define FLASH_INTERVAL 200UL

// TTN SETTINGS
// Replace with your own OTAA credentials.
// AppEUI is called JoinEUI in newer TTN consoles.
const char *appEui = "7ECB614D35B14E46";
const char *appKey = "5B404939E7A090EFF3E6D63E340742AD";

// Periodic status uplink every 60 seconds
#define UPLINK_INTERVAL 60000UL

// Retry joining every 60 seconds
#define JOIN_RETRY_INTERVAL 60000UL

// TTN downlink commands
#define CMD_DISARM 0x00
#define CMD_ARM    0x01
#define CMD_RESET  0x02

// TTN uplink message types
#define MSG_STATUS 0x10
#define MSG_ALARM  0x11

// COMMUNICATION OBJECTS
SoftwareWire myWire(SDA_PIN, SCL_PIN);

SoftwareSerial loraSerial(
  LORA_RX_PIN,
  LORA_TX_PIN
);

rn2xx3 myLora(loraSerial);

// SYSTEM STATES
enum State {
  PASSIVE,
  MONITORING,
  WAITING,
  MEASURING,
  ALARM
};

State state = PASSIVE;

// GLOBAL VARIABLES
unsigned long stateStart = 0;
unsigned long lastSample = 0;
unsigned long lastFlash = 0;

unsigned long lastUplink = 0;
unsigned long lastJoinAttempt = 0;

float prevX = 0;
float prevY = 0;
float prevZ = 0;

float movementSum = 0;
unsigned int sampleCount = 0;

bool firstReading = true;

uint8_t colorIndex = 0;

// INTERNAL CONTROL VARIABLES
bool systemArmed = false;
bool resetRequested = false;
bool alarmOut = false;

bool ttnJoined = false;
bool alarmNotificationPending = false;

// MPU6050 FUNCTIONS
void writeRegister(uint8_t reg, uint8_t value) {

  myWire.beginTransmission(MPU_ADDR);
  myWire.write(reg);
  myWire.write(value);

  uint8_t result = myWire.endTransmission();

  if (result != 0) {
    Serial.print("MPU write error: ");
    Serial.println(result);
  }
}

bool readAcceleration(
  float &ax,
  float &ay,
  float &az
) {

  myWire.beginTransmission(MPU_ADDR);
  myWire.write(0x3B);

  if (myWire.endTransmission(false) != 0)
    return false;

  if (myWire.requestFrom(
      MPU_ADDR, (uint8_t)6) != 6)
    return false;

  int16_t axRaw = (int16_t)(
    (myWire.read() << 8) | myWire.read()
  );

  int16_t ayRaw = (int16_t)(
    (myWire.read() << 8) | myWire.read()
  );

  int16_t azRaw = (int16_t)(
    (myWire.read() << 8) | myWire.read()
  );

  ax = axRaw / 16384.0;
  ay = ayRaw / 16384.0;
  az = azRaw / 16384.0;

  return true;
}

// RGB LED CONTROL
void setRGB(
  bool red,
  bool green,
  bool blue
) {

  digitalWrite(
    RED_PIN,
    COMMON_ANODE ? !red : red
  );

  digitalWrite(
    GREEN_PIN,
    COMMON_ANODE ? !green : green
  );

  digitalWrite(
    BLUE_PIN,
    COMMON_ANODE ? !blue : blue
  );
}

void flashRGB() {

  unsigned long now = millis();

  if (now - lastFlash < FLASH_INTERVAL)
    return;

  lastFlash = now;

  switch (colorIndex) {

    case 0:
      setRGB(1, 0, 0);
      break;

    case 1:
      setRGB(0, 1, 0);
      break;

    case 2:
      setRGB(0, 0, 1);
      break;

    case 3:
      setRGB(1, 1, 0);
      break;

    case 4:
      setRGB(1, 0, 1);
      break;

    case 5:
      setRGB(0, 1, 1);
      break;

    case 6:
      setRGB(0, 0, 0);
      break;
  }

  colorIndex = (colorIndex + 1) % 7;
}

// ALARM CONTROL FUNCTIONS
void setArmedFromTTN(bool armed) {

  systemArmed = armed;

  if (armed) {
    Serial.println("TTN command: ARM");
  } else {
    Serial.println("TTN command: DISARM");
    resetRequested = false;
    alarmNotificationPending = false;
  }
}

void startAlarmTTN(bool active) {

  alarmOut = active;

  if (alarmOut) {
    Serial.println("TTN command: Alarm started");
  } else {
    Serial.println("TTN command: No Alarm");
  }
}

void requestResetFromTTN() {

  resetRequested = true;

  Serial.println("TTN command: RESET");
}

void resetMeasurements() {

  movementSum = 0;
  sampleCount = 0;

  firstReading = true;
  lastSample = millis();
}

void enterPassive() {

  state = PASSIVE;

  setRGB(0, 0, 0);

  startAlarmTTN(false);
  resetMeasurements();

  Serial.println("System PASSIVE");
}

void enterMonitoring() {

  state = MONITORING;

  startAlarmTTN(false);
  setRGB(0, 0, 0);

  resetMeasurements();

  Serial.println("System ARMED - Monitoring");
}

void activateAlarm() {

  state = ALARM;

  startAlarmTTN(true);
  // Immediately turn LED red
  setRGB(1, 0, 0);

  // Prepare flashing
  colorIndex = 1;
  lastFlash = millis();

  // Queue alarm notification for TTN
  alarmNotificationPending = true;

  Serial.println("WARNING: POSSIBLE BIKE THEFT!");
  Serial.println("ALARM ACTIVATED!");
  Serial.println("TTN alarm notification pending");
}

// TTN DOWNLINK HANDLING
void handleDownlink(String received) {

  received.trim();
  received.toUpperCase();

  Serial.print("TTN downlink HEX: ");
  Serial.println(received);

  // Commands must be exactly one byte.
  if (received.length() != 2) {
    Serial.println("Invalid command length");
    return;
  }

  if (received == "01") {

    setArmedFromTTN(true);

  } else if (received == "00") {

    setArmedFromTTN(false);

  } else if (received == "02") {

    requestResetFromTTN();

  } else {

    Serial.println("Unknown TTN command");
  }
}

// INITIALIZE RN2483 HARDWARE
void initializeRadioHardware() {

  Serial.println("Initializing RN2483...");

  // Hardware reset (active LOW)
  pinMode(LORA_RESET_PIN, OUTPUT);

  digitalWrite(LORA_RESET_PIN, LOW);
  delay(500);

  digitalWrite(LORA_RESET_PIN, HIGH);
  delay(200);

  // Clear any startup bytes
  while (loraSerial.available()) {
    loraSerial.read();
  }

  // Synchronize UART baud rate
  myLora.autobaud();

  String hweui = myLora.hweui();
  hweui.trim();

  Serial.print("RN2483 HWEUI: ");
  Serial.println(hweui);

  if (hweui.length() != 16) {
    Serial.println("RN2483 communication failed");
    return;
  }

  Serial.println("RN2483 detected");
  Serial.println(myLora.sysver());
}

// JOIN TTN NETWORK
void joinTTN() {

  lastJoinAttempt = millis();

  if (strlen(appEui) != 16 ||
      strlen(appKey) != 32) {

    Serial.println(
      "ERROR: Configure TTN OTAA keys"
    );
    return;
  }

  Serial.println("Attempting TTN join...");

  bool result = myLora.initOTAA(
    appEui,
    appKey
  );

  if (result) {

    ttnJoined = true;

    Serial.println("Successfully joined TTN!");

    // EU868 DR4. Network settings may
    // subsequently change the data rate.
    myLora.setDR(4);

    Serial.println("Data rate set to DR4");

    lastUplink = millis();

  } else {

    ttnJoined = false;

    Serial.println("TTN join failed");
    Serial.println("Will retry later");
  }
}

// SEND UPLINK / RECEIVE DOWNLINK
void sendTTNMessage(uint8_t messageType) {

  if (!ttnJoined)
    return;

  // Two-byte uplink:
  // Byte 0 = message type
  // Byte 1 = current system state
  uint8_t txBuffer[3];

  txBuffer[0] = messageType;
  txBuffer[1] = (uint8_t)state;
  txBuffer[2] = alarmOut ? 1 : 0;

  Serial.print("TTN TX type: 0x");
  Serial.println(messageType, HEX);

  Serial.print("TTN TX state: ");
  Serial.println((int)state);

  // This library call is blocking.
  TX_RETURN_TYPE result = myLora.txBytes(
    txBuffer,
    sizeof(txBuffer)
  );

  lastUplink = millis();

  switch (result) {

    case TX_FAIL:

      Serial.println("TTN TX failed");
      break;

    case TX_SUCCESS:

      Serial.println("TTN TX successful");
      break;

    case TX_WITH_RX:
    {
      Serial.println("TTN TX successful");
      Serial.println("Downlink received!");

      String received = myLora.getRx();

      handleDownlink(received);
      break;
    }

    default:

      Serial.println("Unknown TX response");
      break;
  }

  // The transmission may have blocked the
  // Arduino for several seconds.
  // Restart acceleration comparison so
  // the long sample gap is not interpreted
  // as movement.
  firstReading = true;
  lastSample = millis();

  // Only clear pending alarm notifications
  // when the alarm uplink succeeded.
  if (messageType == MSG_ALARM &&
      result != TX_FAIL) {

    alarmNotificationPending = false;
  }
}

// TTN COMMUNICATION SERVICE
void serviceTTN() {

  unsigned long now = millis();

  // Retry joining without an infinite loop.
  if (!ttnJoined) {

    if (now - lastJoinAttempt >=
        JOIN_RETRY_INTERVAL) {

      joinTTN();
    }

    return;
  }

  // Don't interrupt the 5-second waiting
  // or measuring periods with a radio TX.
  if (state == WAITING ||
      state == MEASURING) {
    return;
  }

  // Alarm event has priority.
  if (alarmNotificationPending) {

    // Prevent repeated rapid TX retries
    // after a transmission failure.
    if (now - lastUplink >=
        UPLINK_INTERVAL) {

      sendTTNMessage(MSG_ALARM);
    }

    return;
  }

  // Normal periodic status uplink
  if (now - lastUplink >=
      UPLINK_INTERVAL) {

    sendTTNMessage(MSG_STATUS);
  }
}

// SETUP
void setup() {

  Serial.begin(9600);
  delay(1000);

  Serial.println("Arduino started!");
  Serial.println("Bike alarm initializing...");

  // RGB LED
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  setRGB(0, 0, 0);

  // MPU6050
  Serial.println("Initializing MPU6050...");

  myWire.begin();

  writeRegister(0x6B, 0x00);
  delay(100);

  writeRegister(0x1C, 0x00);

  Serial.println("MPU6050 initialized");

  // Initial system state
  state = PASSIVE;

  systemArmed = false;
  resetRequested = false;

  alarmNotificationPending = false;

  Serial.println("System PASSIVE");

  // RN2483
  loraSerial.begin(9600);

  initializeRadioHardware();

  joinTTN();

  Serial.println("Initialization completed");
}

// ALARM STATE MACHINE
void updateAlarm() {

  unsigned long now = millis();

  // DISARMED -> PASSIVE
  if (!systemArmed) {

    resetRequested = false;

    if (state != PASSIVE) {
      enterPassive();
    }

    return;
  }

  // ARMED -> MONITORING
  if (state == PASSIVE) {

    enterMonitoring();
    return;
  }

  // ALARM - LATCHED
  if (state == ALARM) {

    if (resetRequested) {

      resetRequested = false;
      alarmNotificationPending = false;

      Serial.println("Alarm reset by TTN");

      enterMonitoring();
      return;
    }

    flashRGB();
    return;
  }

  // Ignore RESET when no alarm is active.
  resetRequested = false;

  // SAMPLE ACCELEROMETER
  if (now - lastSample < SAMPLE_INTERVAL)
    return;

  lastSample = now;

  float ax, ay, az;

  if (!readAcceleration(ax, ay, az))
    return;

  // FIRST READING
  if (firstReading) {

    prevX = ax;
    prevY = ay;
    prevZ = az;

    firstReading = false;
    return;
  }

  // CALCULATE MOVEMENT
  float dx = ax - prevX;
  float dy = ay - prevY;
  float dz = az - prevZ;

  float movement = sqrt(
    dx * dx +
    dy * dy +
    dz * dz
  );

  prevX = ax;
  prevY = ay;
  prevZ = az;

  // STATE 1: MONITORING
  if (state == MONITORING) {

    if (movement > TRIGGER_THRESHOLD) {

      Serial.println("Movement detected!");
      Serial.println("Waiting 5 seconds...");

      state = WAITING;
      stateStart = now;
    }
  }

  // STATE 2: WAITING
  else if (state == WAITING) {

    if (now - stateStart >= WAIT_TIME) {

      Serial.println("Measuring for 5 seconds...");

      movementSum = 0;
      sampleCount = 0;

      state = MEASURING;
      stateStart = now;
    }
  }

  // STATE 3: MEASURING
  else if (state == MEASURING) {

    movementSum += movement;
    sampleCount++;

    if (now - stateStart >= MEASURE_TIME) {

      float averageMovement = 0;

      if (sampleCount > 0) {

        averageMovement =
          movementSum / sampleCount;
      }

      Serial.print("Average movement: ");
      Serial.print(averageMovement, 4);
      Serial.println(" g");

      if (averageMovement > AVERAGE_THRESHOLD) {

        activateAlarm();

      } else {

        Serial.println("No significant movement.");
        enterMonitoring();
      }
    }
  }
}

// MAIN LOOP
void loop() {

  // Run alarm state machine
  updateAlarm();

  // Handle TTN uplinks and downlinks
  serviceTTN();
}
