#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <VL53L0X.h>

// ZoneSort hardware test: live ToF + both KY-032s, and manual control of all 3 servos
// Gate A = KY G18 + servo CH11   (pass = 90 deg left, A = swing right)
// Gate B = KY G19 + servo CH15   (pass = 90 deg right, B = swing left)
// Pusher = servo CH0

Adafruit_PWMServoDriver pca(0x40);
VL53L0X tof;

const int KY_A_PIN = 18;
const int KY_B_PIN = 19;

const int SERVO_LEFT_US  = 2450;   // 90 deg left   (swap with RIGHT if directions are reversed)
const int SERVO_RIGHT_US = 550;    // 90 deg right

const int GATE_A_CH = 11;
const int GATE_A_PASS_US   = SERVO_LEFT_US;
const int GATE_A_DIVERT_US = SERVO_RIGHT_US;

const int GATE_B_CH = 15;
const int GATE_B_PASS_US   = SERVO_RIGHT_US;
const int GATE_B_DIVERT_US = SERVO_LEFT_US;

const int PUSHER_CH = 0;
const int PUSHER_HOME_US = 2450;
const int PUSHER_PUSH_US = 550;

bool tofOk = false;
unsigned long lastPrint = 0;

void setGates(char zone) {
  pca.writeMicroseconds(GATE_A_CH, zone == 'A' ? GATE_A_DIVERT_US : GATE_A_PASS_US);
  pca.writeMicroseconds(GATE_B_CH, zone == 'B' ? GATE_B_DIVERT_US : GATE_B_PASS_US);
  if (zone == 'A')      Serial.println(">> Zone A: Gate A swung RIGHT (divert), Gate B pass");
  else if (zone == 'B') Serial.println(">> Zone B: Gate A pass, Gate B swung LEFT (divert)");
  else                  Serial.println(">> Manual: both gates in PASS");
}

void push() {
  Serial.println(">> Pusher: push");
  pca.writeMicroseconds(PUSHER_CH, PUSHER_PUSH_US); delay(500);
  pca.writeMicroseconds(PUSHER_CH, PUSHER_HOME_US); delay(300);
}

void printHelp() {
  Serial.println("Commands: A = zone A | B = zone B | M = manual (both pass) | P = push | T = test all servos");
}

void setup() {
  Serial.begin(115200);
  Serial.setTimeout(50);
  delay(500);
  Wire.begin(21, 22);
  pinMode(KY_A_PIN, INPUT_PULLUP);
  pinMode(KY_B_PIN, INPUT_PULLUP);

  Wire.beginTransmission(0x40);
  if (Wire.endTransmission() != 0) {
    Serial.println("PCA9685 not found - check SDA/SCL/VCC/GND");
    while (1) delay(1000);
  }
  pca.begin();
  pca.setOscillatorFrequency(27000000);
  pca.setPWMFreq(50);
  delay(10);
  pca.writeMicroseconds(PUSHER_CH, PUSHER_HOME_US); delay(300);
  setGates('M');

  tof.setTimeout(500);
  if (tof.init()) { tof.startContinuous(); tofOk = true; }
  else Serial.println("ToF not found - check its 4 wires");

  printHelp();
}

void loop() {
  if (Serial.available()) {
    String c = Serial.readStringUntil('\n');
    c.trim(); c.toUpperCase();
    if (c == "A" || c == "B" || c == "M") setGates(c[0]);
    else if (c == "P") push();
    else if (c == "T") {
      push();
      setGates('A'); delay(1000);
      setGates('B'); delay(1000);
      setGates('M');
    }
    else printHelp();
  }

  if (millis() - lastPrint >= 300) {
    lastPrint = millis();
    int d = -1;
    if (tofOk) {
      d = tof.readRangeContinuousMillimeters();
      if (tof.timeoutOccurred()) d = -1;
    }
    Serial.printf("ToF: %4d mm | KY A (G18): %-8s | KY B (G19): %s\n",
                  d,
                  digitalRead(KY_A_PIN) == LOW ? "PARCEL" : "clear",
                  digitalRead(KY_B_PIN) == LOW ? "PARCEL" : "clear");
  }
  delay(5);
}
