
#include <SoftwareWire.h>
#include <math.h>

// ======================================
// PIN DEFINITIONS
// ======================================
#define SDA_PIN 12
#define SCL_PIN 11
#define MPU_ADDR 0x68

#define RED_PIN   9
#define GREEN_PIN 8
#define BLUE_PIN  7

#define ARM_PIN   5
#define RESET_PIN 6
#define ALARM_OUT 4

#define COMMON_ANODE false

// ======================================
// SETTINGS
// ======================================
#define TRIGGER_THRESHOLD 0.40
#define AVERAGE_THRESHOLD 0.20

#define WAIT_TIME 5000
#define MEASURE_TIME 5000
#define SAMPLE_INTERVAL 50

#define FLASH_INTERVAL 200

SoftwareWire myWire(SDA_PIN, SCL_PIN);

// ======================================
// STATES
// ======================================
enum State {
  PASSIVE,
  MONITORING,
  WAITING,
  MEASURING,
  ALARM
};

State state = PASSIVE;

// ======================================
// GLOBAL VARIABLES
// ======================================
unsigned long stateStart = 0;
unsigned long lastSample = 0;
unsigned long lastFlash = 0;

float prevX = 0;
float prevY = 0;
float prevZ = 0;

bool firstReading = true;
bool previousResetState = LOW;

float movementSum = 0;
unsigned int sampleCount = 0;

uint8_t colorIndex = 0;


// ======================================
// WRITE MPU REGISTER
// ======================================
void writeRegister(uint8_t reg, uint8_t value) {

  myWire.beginTransmission(MPU_ADDR);
  myWire.write(reg);
  myWire.write(value);
  myWire.endTransmission();
}


// ======================================
// READ ACCELEROMETER
// ======================================
bool readAcceleration(float &ax, float &ay, float &az) {

  myWire.beginTransmission(MPU_ADDR);
  myWire.write(0x3B);

  if (myWire.endTransmission(false) != 0)
    return false;

  if (myWire.requestFrom(MPU_ADDR, (uint8_t)6) != 6)
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


// ======================================
// RGB LED CONTROL
// ======================================
void setRGB(bool red, bool green, bool blue) {

  digitalWrite(RED_PIN, COMMON_ANODE ? !red : red);
  digitalWrite(GREEN_PIN, COMMON_ANODE ? !green : green);
  digitalWrite(BLUE_PIN, COMMON_ANODE ? !blue : blue);
}


// ======================================
// NON-BLOCKING RGB FLASHING
// ======================================
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


// ======================================
// RESET MEASUREMENTS
// ======================================
void resetMeasurements() {

  movementSum = 0;
  sampleCount = 0;

  firstReading = true;
  lastSample = millis();
}


// ======================================
// ENTER PASSIVE MODE
// ======================================
void enterPassive() {

  state = PASSIVE;

  digitalWrite(ALARM_OUT, LOW);
  setRGB(0, 0, 0);

  resetMeasurements();

  Serial.println("System PASSIVE");
}


// ======================================
// ENTER MONITORING MODE
// ======================================
void enterMonitoring() {

  state = MONITORING;

  digitalWrite(ALARM_OUT, LOW);
  setRGB(0, 0, 0);

  resetMeasurements();

  Serial.println("System ARMED - Monitoring");
}


// ======================================
// ACTIVATE ALARM
// ======================================
void activateAlarm() {

  state = ALARM;

  // Alarm output HIGH
  digitalWrite(ALARM_OUT, HIGH);

  // Immediately turn LED red
  setRGB(1, 0, 0);

  // Prepare flashing
  colorIndex = 1;
  lastFlash = millis();

  // Capture reset pin state
  previousResetState = digitalRead(RESET_PIN);

  Serial.println("WARNING: POSSIBLE BIKE THEFT!");
  Serial.println("ALARM ACTIVATED!");
}


// ======================================
// SETUP
// ======================================
void setup() {

  // Configure outputs
  digitalWrite(ALARM_OUT, LOW);
  pinMode(ALARM_OUT, OUTPUT);

  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  // Configure inputs
  pinMode(ARM_PIN, INPUT);
  pinMode(RESET_PIN, INPUT);

  setRGB(0, 0, 0);

  Serial.begin(9600);
  myWire.begin();

  delay(1000);

  // Wake up MPU
  writeRegister(0x6B, 0x00);
  delay(100);

  // Accelerometer +/- 2g
  writeRegister(0x1C, 0x00);

  state = PASSIVE;

  previousResetState = digitalRead(RESET_PIN);

  Serial.println("Bike theft system initialized");
  Serial.println("System PASSIVE");
}


// ======================================
// MAIN LOOP
// ======================================
void loop() {

  unsigned long now = millis();

  bool armed = digitalRead(ARM_PIN);
  bool resetInput = digitalRead(RESET_PIN);

  bool resetRisingEdge =
    (resetInput == HIGH &&
     previousResetState == LOW);

  previousResetState = resetInput;


  // ====================================
  // D5 LOW -> PASSIVE
  // ====================================
  if (!armed) {

    if (state != PASSIVE) {
      enterPassive();
    }

    return;
  }


  // ====================================
  // D5 HIGH -> MONITORING
  // ====================================
  if (state == PASSIVE) {

    enterMonitoring();
    return;
  }


  // ====================================
  // ALARM STATE - LATCHED
  // ====================================
  if (state == ALARM) {

    // Keep alarm output HIGH
    digitalWrite(ALARM_OUT, HIGH);

    // Only reset on D6 rising edge
    if (resetRisingEdge) {

      Serial.println("Alarm reset by D6");

      enterMonitoring();
      return;
    }

    // Continue flashing LED
    flashRGB();

    return;
  }


  // ====================================
  // SAMPLE ACCELEROMETER
  // ====================================
  if (now - lastSample < SAMPLE_INTERVAL)
    return;

  lastSample = now;

  float ax, ay, az;

  if (!readAcceleration(ax, ay, az))
    return;


  // ====================================
  // FIRST READING
  // ====================================
  if (firstReading) {

    prevX = ax;
    prevY = ay;
    prevZ = az;

    firstReading = false;
    return;
  }


  // ====================================
  // CALCULATE MOVEMENT
  // ====================================
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


  // ====================================
  // STATE 1: MONITORING
  // ====================================
  if (state == MONITORING) {

    if (movement > TRIGGER_THRESHOLD) {

      Serial.println("Movement detected!");
      Serial.println("Waiting 5 seconds...");

      state = WAITING;
      stateStart = now;
    }
  }


  // ====================================
  // STATE 2: WAITING
  // ====================================
  else if (state == WAITING) {

    if (now - stateStart >= WAIT_TIME) {

      Serial.println("Measuring for 5 seconds...");

      movementSum = 0;
      sampleCount = 0;

      state = MEASURING;
      stateStart = now;
    }
  }


  // ====================================
  // STATE 3: MEASURING
  // ====================================
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
