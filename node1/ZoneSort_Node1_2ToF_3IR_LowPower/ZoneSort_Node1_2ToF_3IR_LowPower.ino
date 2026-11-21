// ZoneSort Node 1: low-power version of ZoneSort_Node1_2ToF_3IR_Integrated.
// Same pipeline (scan-first MQTT over TLS, 2 ToF, 3 bin IR, ESP-NOW to Node 2).
// Every change is marked [POWER]:
//   - two power states, ACTIVE and STANDBY (see section 1A)
//   - in STANDBY the radio is off and the ESP32 light-sleeps between sensor
//     readings; a bin IR beam break, a box under the top ToF or serial input
//     wakes it
//   - in STANDBY the node reconnects every RADIO_CHECK_MS to collect scans the
//     broker queued for it (persistent MQTT session, QoS 1)
//   - scans that arrive together are queued instead of discarded
//   - the time split (asleep / awake with radio off / radio on) is printed
//     every POWER_REPORT_MS for the power experiment
// The stock Arduino-ESP32 core (3.3.x) is built without CONFIG_PM_ENABLE and
// tickless idle, so FreeRTOS never sleeps the chip by itself and Wi-Fi cannot
// stay connected through a light sleep. The power task calls
// esp_light_sleep_start() itself, and only after switching the radio off.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_idf_version.h>  // ESP-NOW callback compatibility
#include <PubSubClient.h>
#include <Wire.h>
#include <VL53L0X.h>
#include <esp_sleep.h>    // [POWER]
#include <esp_timer.h>    // [POWER]
#include <driver/gpio.h>  // [POWER]
#include <driver/uart.h>  // [POWER]

// =====================================
// 1. SETTINGS
// =====================================

// Node 2 MAC address (receiver)
uint8_t NODE2_MAC[6] = {
  0xB0, 0x3F, 0xD3,
  0x5A, 0xF3, 0x90
};


// Set these to the Wi-Fi network shared with the MQTT broker.
// Set these to the Wi-Fi network shared with the MQTT broker.
const char* WIFI_SSID = "Thomas iphone16 Promax";
const char* WIFI_PASSWORD = "cherrytomato";

// Friend 2's broker address, port and MQTT topic.
const char* MQTT_SERVER = "119.74.242.100"; // IP in the supplied TLS certificate; confirm broker public IP
const uint16_t MQTT_PORT = 8883; // TLS
const char* MQTT_TOPIC_ARRIVAL = "zonesort/node1/arrival";
const char* MQTT_TOPIC_SCAN = "zonesort/gateway/scan";
const char* MQTT_TOPIC_PROCESSED = "zonesort/node1/processed";
// New non-retained telemetry topics (Node 1 publishes)
const char* MQTT_TOPIC_DIMENSIONS = "zonesort/node1/dimensions";
const char* MQTT_TOPIC_BIN_ARRIVAL = "zonesort/node1/bin_arrival";
const char* MQTT_TOPIC_FAULT = "zonesort/fault";  // [OVERSIZE] shared fault topic

// If the broker has authentication, fill these in.
// Leave blank for an anonymous broker.
const char* MQTT_USER = "esp32"; // edit if broker username differs
const char* MQTT_PASSWORD = "esp32thomas"; // enter broker password locally; do not commit credentials

// Both Node 1 and Node 2 must use the Wi-Fi access point's channel.
// Do not force ESP-NOW channel 1 after connecting to Wi-Fi.
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);

// Certificate supplied by the MQTT broker administrator.
static const char MQTT_CA_CERT[] PROGMEM = R"CERT(
-----BEGIN CERTIFICATE-----
MIIDIjCCAgqgAwIBAgIUaQ86M+HRBaNoIYhTJD5B74LRsIcwDQYJKoZIhvcNAQEL
BQAwGDEWMBQGA1UEAwwNWm9uZVNvcnQtTVFUVDAeFw0yNjExMDEwOTE5MzhaFw0y
NzAxMzAwOTE5MzhaMBgxFjAUBgNVBAMMDVpvbmVTb3J0LU1RVFQwggEiMA0GCSqG
SIb3DQEBAQUAA4IBDwAwggEKAoIBAQC7qBY/xdwQJTUl1uSxVwJubprje5gZOnls
HJI7KKROQRooOO5a0fMNgAG40ZV5EMLWOhRyksBMcbywZyazakSJIX/bXOa687Ur
lFZn1UM9RzIaY04CjkyQv4GrkAHqBQrC6CTteyF0cK7EJ0Ly5y414Hlurkq6TUgH
n7uX666PLgs3AnaYCqThuSuBe6BWimiO8kXi2I/B/UHvEiJakaOe99Z2mcPqLCuf
eFuPra2dATTdLXLhrUEGtOuSBIMTZdzczieu5Yfa53c0LCcgeIcshGH3Qk1wvXKp
8BFMhy3fbfoWaf7AZ8nn+KpHeC4URmfTxr4c97S13cqvZmngMhO/AgMBAAGjZDBi
MB0GA1UdDgQWBBRzfqLb6iFOWTrqgdxoDIi5JusR/TAfBgNVHSMEGDAWgBRzfqLb
6iFOWTrqgdxoDIi5JusR/TAPBgNVHRMBAf8EBTADAQH/MA8GA1UdEQQIMAaHBHdK
8mQwDQYJKoZIhvcNAQELBQADggEBACBLE/gC2PhD5WNqFhWCze8cu1PegrMC3Mhg
3uFhDwCAJ2UN597pz3cHpYax3+wJVMGYLpzNWTMVmmCXFpdXyh9/pVW02biTXUPo
ALz7KMmvP6s8r2oBxLDAM4WAhpUWI20lr4syjd3ooa1Ix9qqZAvfe2RvQaIFqilt
CTKAxd+SJPTw/f67hRJ3eWnPH77QfWTY8yfzFcKdnA95MXgtFaxYCgoEgNlKDMFv
OeDaW/9o5xbT01J6qNZ7GHAA2+Q2CKDpZJzfIWx+PBtks5yQPqfGprgZBDE12xfL
5fipCZUYPzxP15iXpvgIbQDzGDfHCGrqz4ER95F1DMCDwYyI/Sc=
-----END CERTIFICATE-----
)CERT";

// Simulation: 1 = ON, 0 = real sensor
// The original simulated arrival task is NOT launched in scan-first mode.
// All three BIN_IR pins below always use real hardware.
#define SIMULATE_IR 1
#define SIMULATE_WEIGHT 1
#define SIMULATE_QR 1

// Hardware pins (original IR arrival sensor remains unused in scan-first mode)
#define IR_PIN 34
// IMPORTANT: moved HX711 off 18/19 to avoid ToF XSHUT conflict.
// If you use a real HX711, wire DOUT->32 and SCK->33.
#define HX_DOUT 32
#define HX_SCK 33

// Two VL53L0X sensors: preserve your verified working wiring.
#define TOF_SDA 21
#define TOF_SCL 22
#define TOF_TOP_XSHUT 18
#define TOF_SIDE_XSHUT 19

// Three real IR-08H sensors, one per bin.
#define BIN_IR_A 25
#define BIN_IR_B 26
#define BIN_IR_M 27

// Change these to actual sensor-to-empty-surface distances (millimetres).
const int TOP_BASELINE_MM = 200;
const int SIDE_BASELINE_MM = 250;
// Measure actual belt speed (mm/s) before trusting box length.
const float CONVEYOR_SPEED_MM_S = 35.0f;
const int BOX_HEIGHT_TRIGGER_MM = 20;
const int BOX_WIDTH_TRIGGER_MM = 15;
const uint8_t BOX_CONFIRM_SAMPLES = 3;
const uint32_t BIN_IR_DEBOUNCE_MS = 60;
// [FLICKER] After a bin IR triggers, ignore that sensor for this long so a
// flickering beam counts as one parcel, not many. Parcels into the same bin
// are several seconds apart at 35 mm/s, so a real parcel is never missed.
const uint32_t BIN_IR_LOCKOUT_MS = 2000;
const uint32_t TOF_PRINT_INTERVAL_MS = 1000;
// [OVERSIZE] Largest box the chute and bins accept (mm). Set these from the real chute.  
const int MAX_LENGTH_MM = 300;
const int MAX_WIDTH_MM = 60;   // 6 cm
const int MAX_HEIGHT_MM = 60;  // 6 cm

VL53L0X topToF;
VL53L0X sideToF;
bool tofReady = false;

#define DETECTION_INTERVAL_MS 5000
#define QR_TIMEOUT_MS 15000

// Example weight threshold
#define HEAVY_THRESHOLD_G 2000.0f

// Calibrate before using actual HX711
#define CALIBRATION_FACTOR 1.0f

// =====================================
// 1A. POWER MANAGEMENT  [POWER]
// =====================================
// ACTIVE:  a scan is being processed, a parcel is on the belt, a box is under
//          the ToF, or something happened in the last IDLE_BEFORE_SLEEP_MS.
//          Radio on, ToF ranging every 60 ms, bin IRs polled every 10 ms:
//          the same behaviour as the integrated sketch.
// STANDBY: idle. Radio off, side ToF stopped, top ToF ranging slowly on its own.
//          The ESP32 light-sleeps between sensor readings: it wakes every
//          SAMPLE_INTERVAL_MS, reads the top ToF over I2C and sleeps again.
//          A bin IR beam break or serial input wakes it at once.
//          Every RADIO_CHECK_MS it reconnects to collect scans the broker
//          queued while it was off. Node 2 only pushes a parcel onto the belt
//          after Node 1's assignment, so a late scan delays the parcel but
//          never mis-sorts it.
#define ENABLE_LIGHT_SLEEP 1            // 0 = never sleep (baseline for the power test)
#define SAMPLE_INTERVAL_MS 200          // standby sensor reading period; try 100, 200, 500, 1000
#define IDLE_BEFORE_SLEEP_MS 30000      // stay ACTIVE this long after the last activity
#define RADIO_CHECK_MS 10000            // standby: collect queued scans this often (worst-case extra scan delay)
#define RADIO_CHECK_LISTEN_MS 800       // stay connected this long for queued scans to arrive
#define RADIO_WAKE_TIMEOUT_MS 8000      // longest wait for Wi-Fi and for MQTT after waking the radio
#define PARCEL_TRAVEL_TIMEOUT_MS 60000  // a parcel never seen at a bin stops keeping the node awake after this
#define ACTIVE_TOF_BUDGET_US 50000
#define ACTIVE_TOF_PERIOD_MS 60
#define STANDBY_TOF_BUDGET_US 20000     // shorter ranging in standby: presence check only
#define POWER_REPORT_MS 10000           // print the time split this often

volatile bool radioWanted = true;    // false: mqttTask disconnects and parks
volatile bool mqttParked = false;
volatile bool mqttReady = false;     // TLS connected and subscribed
volatile bool standbyMode = false;   // true: sensorTask parks, the power task reads the ToF
volatile bool tofParked = false;
volatile bool boxInStation = false;
volatile uint32_t lastActivityMs = 0;
volatile uint32_t lastAssignmentMs = 0;
bool radioOnNow = true;
bool wifiWasConnected = false;
uint8_t savedChannel = 0;
uint8_t savedBssid[6] = {};

// Anything that should keep the node ACTIVE calls this. A macro, not a function:
// Arduino inserts its generated prototypes before the first function in the
// file, and a function here would put them above the struct definitions.
#define markActivity() (lastActivityMs = millis())

// =====================================
// 2. NODE 2 ESP-NOW PROTOCOL
// =====================================

constexpr uint8_t WIRE_ASSIGNMENT = 1;
constexpr uint8_t WIRE_PAUSE = 2;
constexpr uint8_t WIRE_RESUME = 3;
constexpr uint8_t WIRE_STOP = 4;
// NEW: Node 2 must recognise type 5 as a bin-arrival EVENT (application ACK requires return reply).
constexpr uint8_t WIRE_BIN_ARRIVED = 5;

// Must match Node 2 exactly
struct WirePacket {
  uint32_t parcelId;
  uint8_t type;
  char zone;
  uint8_t reserved[2];
};

static_assert(
  sizeof(WirePacket) == 8,
  "WirePacket size mismatch"
);

// =====================================
// 3. FREERTOS STRUCTURES
// =====================================

struct ParcelEvent {
  uint32_t id;
};

// Moved up from section 5A so Arduino's generated prototypes can see it.
struct ToFResult {
  uint16_t raw;
  bool timeout;
  bool valid;
};

struct WeightResult {
  uint32_t id;
  float weight;
  bool valid;
  bool heavy;
};

struct QRResult {
  uint32_t id;
  char parcelID[20];
  char block[20];
  char unit[20];
  int floor;
  bool readable;
};

QueueHandle_t detectionQueue;
QueueHandle_t inspectionQueue;
QueueHandle_t qrQueue;
QueueHandle_t espnowQueue;
QueueHandle_t mqttQueue; // kept for compatibility; scan messages are handled below
QueueHandle_t arrivalQueue;
QueueHandle_t scanQueue; // holds complete camera scan messages

// Per-zone FIFO preserves the scan seq for packages assigned to each bin.
// Assumes FIFO arrival order within each bin and that only one parcel at a time
// blocks the chute-entry IR beam. A blocked beam must clear before the next box.
QueueHandle_t pendingA;
QueueHandle_t pendingB;
QueueHandle_t pendingM;

struct DimensionEvent {
  uint32_t measurementId; // independent counter; do NOT claim this is QR seq
  float lengthMm;
  int widthMm;  // -1 means no valid side measurement
  int heightMm;
  uint32_t durationMs;
};
struct BinArrivalEvent {
  uint32_t seq;  // 0 if no matching assignment exists
  char zone;
  uint8_t sensor;
  bool espnowQueued;
  uint32_t uptimeMs;
};
QueueHandle_t dimensionsQueue;
QueueHandle_t binArrivalQueue;
QueueHandle_t oversizeQueue;  // [OVERSIZE] measured boxes over the limits
QueueHandle_t unmatchedQueue; // [UNMATCHED] bin IR fired with no parcel expected there

struct ScanEvent {
  uint32_t seq;
  char zone;
  bool readable;
};

struct MqttEvent {
  uint32_t seq;
  char parcelID[20];
  char block[20];
  int floor;
  char unit[20];
  char scan[16];
  char zone;
  uint8_t flips;
};

EventGroupHandle_t systemEvents;

#define INSPECTION_ACTIVE BIT0

bool espnowReady = false;

// MAC-layer send result: callback runs in the Wi-Fi task.
volatile uint32_t espnowDelivered = 0;
volatile uint32_t espnowFailed = 0;

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
void onEspNowSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
#else
void onEspNowSent(const uint8_t *mac, esp_now_send_status_t status) {
#endif
  if (status == ESP_NOW_SEND_SUCCESS) ++espnowDelivered;
  else ++espnowFailed;
}


uint32_t parcelCounter = 0;

// =====================================
// 4. CLASSIFICATION
// =====================================

char classifyFloor(int floor) {

  if (floor >= 1 && floor <= 5) {
    return 'A';
  }

  if (floor >= 6 && floor <= 10) {
    return 'B';
  }

  return 'M';
}

// =====================================
// 5. ESP-NOW PACKET CREATION
// =====================================

QueueHandle_t pendingForZone(char zone) {
  switch (zone) {
    case 'A': return pendingA;
    case 'B': return pendingB;
    case 'M': return pendingM;
    default: return nullptr;
  }
}

bool sendCommand(
  uint32_t parcelId,
  uint8_t type,
  char zone,
  const QRResult* qrData = nullptr
) {

  if (!espnowReady) {
    Serial.println("[ESP-NOW] Not ready");
    return false;
  }

  WirePacket packet = {};

  packet.parcelId = parcelId;
  packet.type = type;
  packet.zone = zone;

  if (xQueueSend(
      espnowQueue,
      &packet,
      pdMS_TO_TICKS(100)
    ) == pdTRUE) {

    Serial.printf(
      "[Queue] Parcel=%lu Type=%u Zone=%c\n",
      (unsigned long)packet.parcelId,
      packet.type,
      packet.zone ? packet.zone : '-'
    );

    // Associate the original QR seq with the chosen bin for future IR confirmation.
    // This is FIFO-per-zone, not a guaranteed identity if boxes overtake each other.
    if (type == WIRE_ASSIGNMENT) {
      lastAssignmentMs = millis();  // [POWER] parcel now on its way to a bin
      markActivity();
      QueueHandle_t pending = pendingForZone(zone);
      if (pending && xQueueSend(pending, &parcelId, 0) != pdTRUE) {
        Serial.printf("[Bin] Pending queue full zone=%c seq=%lu\n",
                      zone, (unsigned long)parcelId);
      }
      // Zone M currently has no physical IR bin sensor.
    }
    return true;
  }

  Serial.println("[ESP-NOW] Queue full");
  return false;
}


// =====================================
// 5A. ToF + THREE REAL BIN-IR SENSORS
// =====================================

// Keep the identical XSHUT and ranging sequence that passed the isolated test.
bool setupToFSensors() {
  pinMode(TOF_TOP_XSHUT, OUTPUT);
  pinMode(TOF_SIDE_XSHUT, OUTPUT);
  digitalWrite(TOF_TOP_XSHUT, LOW);
  digitalWrite(TOF_SIDE_XSHUT, LOW);
  delay(100);

  Wire.begin(TOF_SDA, TOF_SCL);
  Wire.setClock(100000);

  pinMode(TOF_TOP_XSHUT, INPUT); // release shutdown
  delay(100);
  topToF.setTimeout(500);
  if (!topToF.init()) {
    Serial.println("[ToF] TOP sensor initialisation FAILED");
    return false;
  }
  topToF.setAddress(0x30);

  pinMode(TOF_SIDE_XSHUT, INPUT);
  delay(100);
  sideToF.setTimeout(500);
  if (!sideToF.init()) {
    Serial.println("[ToF] SIDE sensor initialisation FAILED");
    return false;
  }
  sideToF.setAddress(0x31);

  topToF.setMeasurementTimingBudget(ACTIVE_TOF_BUDGET_US);
  sideToF.setMeasurementTimingBudget(ACTIVE_TOF_BUDGET_US);
  topToF.startContinuous(ACTIVE_TOF_PERIOD_MS);
  sideToF.startContinuous(ACTIVE_TOF_PERIOD_MS);
  delay(200);
  Serial.println("[ToF] TOP 0x30, SIDE 0x31 ready");
  return true;
}

ToFResult readToF(VL53L0X& sensor) {
  ToFResult r;
  r.raw = sensor.readRangeContinuousMillimeters();
  r.timeout = sensor.timeoutOccurred();
  r.valid = !r.timeout && r.raw >= 20 && r.raw <= 1200;
  return r;
}

void reportBinEntry(uint8_t index, char zone) {
  markActivity();  // [POWER]
  uint32_t seq = 0;
  QueueHandle_t pending = pendingForZone(zone);
  const bool matched = pending && xQueuePeek(pending, &seq, 0) == pdTRUE;
  if (!matched) seq = 0;

  // Packet retains the exact original 8-byte WirePacket format.
  // WIRE_BIN_ARRIVED=5 is a NEW application-level message; Node 2 must add it.
  bool queued = sendCommand(seq, WIRE_BIN_ARRIVED, zone);
  if (queued && matched) {
    uint32_t removed = 0;
    xQueueReceive(pending, &removed, 0);
  }

  BinArrivalEvent event = {};
  event.seq = seq;
  event.zone = zone;
  event.sensor = index + 1;
  event.espnowQueued = queued;
  event.uptimeMs = millis();
  if (xQueueSend(binArrivalQueue, &event, 0) != pdTRUE)
    Serial.println("[Bin] MQTT bin arrival queue full");

  // [UNMATCHED] Nothing was sent to this bin: report it as a fault too.
  if (!matched && xQueueSend(unmatchedQueue, &event, 0) != pdTRUE)
    Serial.println("[Bin] MQTT fault queue full");

  Serial.printf("[BIN %c] IR%d triggered; seq=%lu (%s); ESP-NOW queued=%s\n",
                zone, index + 1, (unsigned long)seq,
                matched ? "matched" : "unmatched", queued ? "YES" : "NO");
}

// Dedicated IR task prevents ToF ranging delays from missing quick bin events.
void binIrTask(void* parameter) {
  const uint8_t irPins[3] = { BIN_IR_A, BIN_IR_B, BIN_IR_M };
  const char zones[3] = { 'A', 'B', 'M' };
  uint8_t stable[3], lastRaw[3];
  uint32_t lastChange[3];
  uint32_t lastTrigger[3] = {0, 0, 0};          // [FLICKER]
  bool triggered[3] = {false, false, false};    // [FLICKER] waiting for "beam clear"
  uint32_t ignored[3] = {0, 0, 0};              // [FLICKER] flickers ignored per trigger

  for (int i = 0; i < 3; i++) {
    pinMode(irPins[i], INPUT);
    stable[i] = lastRaw[i] = digitalRead(irPins[i]);
    lastChange[i] = millis();
  }

  for (;;) {
    uint32_t now = millis();
    for (int i = 0; i < 3; i++) {
      uint8_t raw = digitalRead(irPins[i]);
      if (raw != lastRaw[i]) {
        lastRaw[i] = raw;
        lastChange[i] = now;
      }
      if (raw != stable[i] && now - lastChange[i] >= BIN_IR_DEBOUNCE_MS) {
        stable[i] = raw;
        if (raw == LOW) {
          // [FLICKER] Only the first block within the lockout counts as a parcel.
          if (lastTrigger[i] != 0 && now - lastTrigger[i] < BIN_IR_LOCKOUT_MS) {
            ++ignored[i];
          } else {
            if (ignored[i] > 0)
              Serial.printf("[BIN %c] ignored %lu flicker(s) after last trigger\n",
                            zones[i], (unsigned long)ignored[i]);
            ignored[i] = 0;
            lastTrigger[i] = now;
            triggered[i] = true;
            reportBinEntry(i, zones[i]);
          }
        } else if (triggered[i]) {
          triggered[i] = false;
          Serial.printf("[BIN %c] beam clear\n", zones[i]);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ToF measurements run independently, including when MQTT is disconnected.
void sensorTask(void* parameter) {
  uint32_t lastToFPrint = 0, lastValidTop = millis();
  uint32_t measurementCounter = 0;
  bool boxActive = false, sideSeen = false;
  uint8_t enterCount = 0, exitCount = 0;
  uint32_t candidateStart = 0, boxStart = 0, candidateEnd = 0;
  int maxHeight = 0, maxWidth = 0;

  for (;;) {
    // [POWER] In standby the power task owns the ToF sensors: park here.
    if (standbyMode) {
      boxActive = false;
      sideSeen = false;
      enterCount = exitCount = 0;
      boxInStation = false;
      tofParked = true;
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    uint32_t now = millis();

    if (!tofReady) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    ToFResult top = readToF(topToF);
    ToFResult side = readToF(sideToF);
    now = millis();

    if (now - lastToFPrint >= TOF_PRINT_INTERVAL_MS) {
      // Live reading: height = how far the top surface is above the belt,
      // width = how far the box sticks out towards the side sensor.
      char heightText[16], widthText[16];
      if (top.valid)
        snprintf(heightText, sizeof(heightText), "%d mm", max(0, TOP_BASELINE_MM - (int)top.raw));
      else
        snprintf(heightText, sizeof(heightText), "%s", top.timeout ? "TIMEOUT" : "INVALID");
      if (side.valid)
        snprintf(widthText, sizeof(widthText), "%d mm", max(0, SIDE_BASELINE_MM - (int)side.raw));
      else
        snprintf(widthText, sizeof(widthText), "%s", side.timeout ? "TIMEOUT" : "INVALID");

      Serial.printf("[ToF] height=%s width=%s | raw TOP=%u SIDE=%u mm | box=%s\n",
                    heightText, widthText, top.raw, side.raw,
                    boxActive ? "YES" : "NO");
      lastToFPrint = now;
    }

    if (!top.valid) {
      // Do not publish a fake length or trigger an exit on invalid readings.
      enterCount = 0;
      exitCount = 0;
      if (boxActive && now - lastValidTop > 1500) {
        Serial.println("[ToF] Aborted: top measurement invalid for 1.5 s");
        boxActive = false;
        boxInStation = false;  // [POWER]
        sideSeen = false;
      }
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    lastValidTop = now;
    int height = max(0, TOP_BASELINE_MM - (int)top.raw);
    int width = side.valid ? max(0, SIDE_BASELINE_MM - (int)side.raw) : 0;
    bool detected = height > BOX_HEIGHT_TRIGGER_MM;

    if (!boxActive) {
      if (detected) {
        if (enterCount == 0) candidateStart = now;
        if (++enterCount >= BOX_CONFIRM_SAMPLES) {
          boxActive = true;
          boxStart = candidateStart;
          maxHeight = height;
          maxWidth = 0;
          sideSeen = false;
          exitCount = 0;
          Serial.println("[ToF] Box entered measurement station");
          boxInStation = true;  // [POWER]
          markActivity();
        }
      } else {
        enterCount = 0;
      }
    }

    if (boxActive) {
      if (detected) {
        exitCount = 0;
        maxHeight = max(maxHeight, height);
        if (side.valid && width > BOX_WIDTH_TRIGGER_MM) {
          maxWidth = max(maxWidth, width);
          sideSeen = true;
        }
      } else {
        if (exitCount == 0) candidateEnd = now;
        if (++exitCount >= BOX_CONFIRM_SAMPLES) {
          DimensionEvent event = {};
          event.measurementId = ++measurementCounter;
          event.durationMs = candidateEnd - boxStart;
          event.lengthMm = event.durationMs * CONVEYOR_SPEED_MM_S / 1000.0f;
          event.heightMm = maxHeight;
          event.widthMm = sideSeen ? maxWidth : -1;
          if (xQueueSend(dimensionsQueue, &event, 0) != pdTRUE)
            Serial.println("[ToF] MQTT dimensions queue full");
          // [OVERSIZE] widthMm is -1 when the side sensor saw nothing; that never counts.
          bool oversize = event.lengthMm > MAX_LENGTH_MM ||
                          event.widthMm > MAX_WIDTH_MM ||
                          event.heightMm > MAX_HEIGHT_MM;
          if (oversize) {
            Serial.printf("[ToF] OVERSIZE box id=%lu\n", (unsigned long)event.measurementId);
            if (xQueueSend(oversizeQueue, &event, 0) != pdTRUE)
              Serial.println("[ToF] MQTT fault queue full");
          }
          Serial.printf("[DIM] id=%lu L=%.1f W=%d H=%d mm\n",
                        (unsigned long)event.measurementId, event.lengthMm,
                        event.widthMm, event.heightMm);
          boxActive = false;
          sideSeen = false;
          enterCount = exitCount = 0;
          boxInStation = false;  // [POWER]
          markActivity();
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// =====================================
// 6. WIFI + ESP-NOW INITIALISATION
// =====================================

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("[Wi-Fi] Connecting");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("[Wi-Fi] Connected, IP: ");
    Serial.println(WiFi.localIP());
    Serial.printf("[Wi-Fi] Channel: %d\n", WiFi.channel());
  } else {
    Serial.println("[Wi-Fi] Connection failed. Check credentials/router.");
    WiFi.disconnect();
    WiFi.setAutoReconnect(false);
  }
}

void setupESPNow() {
  // A single ESP32 radio must use the hotspot channel for MQTT and ESP-NOW.
  // If Wi-Fi is unavailable, keep the original ESP-NOW-only channel 1 fallback.
  if (WiFi.status() != WL_CONNECTED) {
    esp_err_t ch = esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    if (ch != ESP_OK) {
      Serial.printf("[ESP-NOW] Could not set fallback channel 1: %d\n", ch);
      return;
    }
    Serial.println("[ESP-NOW] Wi-Fi offline: direct channel 1 fallback");
  } else {
    Serial.printf("[ESP-NOW] Using hotspot channel %d (Node 2 MUST match)\n", WiFi.channel());
  }

  Serial.print("[ESP-NOW] Node 1 MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Init failed");
    return;
  }

  if (esp_now_register_send_cb(onEspNowSent) != ESP_OK) {
    Serial.println("[ESP-NOW] Send callback registration failed");
    return;
  }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, NODE2_MAC, 6);
  // 0 = use Node 1's CURRENT Wi-Fi channel.
  peer.channel = 0;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;

  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("[ESP-NOW] Peer setup failed");
    return;
  }

  espnowReady = true;
  Serial.printf("[ESP-NOW] Ready on channel %d\n", WiFi.channel());
  Serial.printf("[ESP-NOW] Node 2 peer MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                NODE2_MAC[0], NODE2_MAC[1], NODE2_MAC[2],
                NODE2_MAC[3], NODE2_MAC[4], NODE2_MAC[5]);
  Serial.println("[ESP-NOW] Set Node 2 ESPNOW_CHANNEL to the channel printed above.");
}

// Read simple JSON fields from the bridge's scan event without a new dependency.
// The bridge produces flat JSON using json.dumps().
bool readJsonString(const String& json, const char* key, String& out) {
  String needle = String("\"") + key + "\"";
  int pos = json.indexOf(needle);
  if (pos < 0) return false;
  pos = json.indexOf(':', pos + needle.length());
  if (pos < 0) return false;
  pos++;
  while (pos < json.length() && json[pos] == ' ') pos++;
  if (pos >= json.length() || json[pos] != '"') return false;
  int finish = json.indexOf('"', pos + 1);
  if (finish < 0) return false;
  out = json.substring(pos + 1, finish);
  return true;
}

bool readJsonSequence(const String& json, uint32_t& seq) {
  int pos = json.indexOf("\"seq\"");
  if (pos < 0) return false;
  pos = json.indexOf(':', pos + 5);
  if (pos < 0) return false;
  pos++;
  while (pos < json.length() && json[pos] == ' ') pos++;
  if (pos >= json.length() || json[pos] < '0' || json[pos] > '9') return false;
  uint64_t value = 0;
  while (pos < json.length() && json[pos] >= '0' && json[pos] <= '9') {
    value = value * 10 + (json[pos++] - '0');
    if (value > UINT32_MAX) return false;
  }
  if (value == 0) return false;
  seq = (uint32_t)value;
  return true;
}

// Extract integer field (null or missing returns false).
bool readJsonInt(const String& json, const char* key, int& out) {
  String needle = String("\"") + key + "\"";
  int pos = json.indexOf(needle);
  if (pos < 0) return false;
  pos = json.indexOf(':', pos + needle.length());
  if (pos < 0) return false;
  pos++;
  while (pos < json.length() && json[pos] == ' ') pos++;
  int sign = 1;
  if (pos < json.length() && json[pos] == '-') { sign = -1; pos++; }
  if (pos >= json.length() || !isDigit(json[pos])) return false;
  int value = 0;
  while (pos < json.length() && isDigit(json[pos])) {
    if (value > 100000) return false;
    value = value * 10 + json[pos++] - '0';
  }
  out = sign * value;
  return true;
}

bool supportedBlock(const String& block) {
  const char* blocks[] = {"123", "123A", "124", "125", "126", "127", "128", "708"};
  for (const char* known : blocks) if (block == known) return true;
  return false;
}

// Called only by mqttClient.loop() from the MQTT FreeRTOS task.
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, MQTT_TOPIC_SCAN) != 0) return;
  String json;
  json.reserve(length + 1);
  for (unsigned int i = 0; i < length; ++i) json += (char)payload[i];

  MqttEvent event = {};
  String scan, suppliedZone, parcelID, block, unit;
  if (!readJsonSequence(json, event.seq) ||
      !readJsonString(json, "scan", scan) ||
      !readJsonString(json, "zone", suppliedZone)) {
    Serial.println("[MQTT] Invalid camera JSON; ignored");
    return;
  }
  readJsonString(json, "parcel_id", parcelID);
  readJsonString(json, "block", block);
  readJsonString(json, "unit", unit);
  int floor = 0;
  bool hasFloor = readJsonInt(json, "floor", floor);

  // [ZONE] Use the gateway's zone: it maps the block to a zone
  // (123-127 -> A, 128-132 -> B, anything else -> MANUAL), so the rule lives in one place.
  char zone = 'M';
  if (scan == "ok" && suppliedZone == "A") zone = 'A';
  else if (scan == "ok" && suppliedZone == "B") zone = 'B';
  event.zone = zone;
  event.floor = hasFloor ? floor : 0;
  snprintf(event.parcelID, sizeof(event.parcelID), "%s", parcelID.c_str());
  snprintf(event.block, sizeof(event.block), "%s", block.c_str());
  snprintf(event.unit, sizeof(event.unit), "%s", unit.c_str());
  snprintf(event.scan, sizeof(event.scan), "%s", (zone == 'M' ? scan.c_str() : "ok"));
  event.flips = 0;

  // Process one scan at a time. Avoid replay of a duplicate QoS 1 delivery.
  static uint32_t lastSeq = 0;
  if (event.seq <= lastSeq) {
    Serial.printf("[MQTT] Duplicate/old scan seq=%lu ignored\n", (unsigned long)event.seq);
    return;
  }
  // [POWER] After standby the broker delivers queued scans back to back.
  // Queue them in order instead of discarding the ones that arrive while the
  // previous scan is still being inspected. This callback is the only writer
  // of both queues, so checking for space first keeps them in step.
  if (uxQueueSpacesAvailable(scanQueue) == 0 ||
      uxQueueSpacesAvailable(detectionQueue) == 0) {
    Serial.println("[MQTT] Processing queue full; scan discarded");
    return;
  }
  ParcelEvent parcel = { event.seq };
  xEventGroupSetBits(systemEvents, INSPECTION_ACTIVE);
  xQueueSend(scanQueue, &event, 0);
  xQueueSend(detectionQueue, &parcel, 0);
  markActivity();
  lastSeq = event.seq;
  parcelCounter = event.seq;
  Serial.printf("[MQTT] QR received seq=%lu parcel=%s block=%s floor=%d zone=%c\n",
      (unsigned long)event.seq, event.parcelID, event.block, event.floor, event.zone);
}

// MQTT operations are confined to this task (PubSubClient isn't thread-safe).
// CORE 0 - PRIORITY 1
void mqttTask(void* parameter) {
  wifiClient.setCACert(MQTT_CA_CERT);
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setCallback(onMqttMessage);
  mqttClient.setBufferSize(512);
  mqttClient.setKeepAlive(15);
  mqttClient.setSocketTimeout(3);

  MqttEvent processed;
  unsigned long lastAttempt = 0;
  while (true) {
    // [POWER] The power task is about to switch the radio off: disconnect
    // cleanly (the broker keeps the session) and wait here.
    if (!radioWanted) {
      if (mqttClient.connected()) mqttClient.disconnect();
      mqttReady = false;
      lastAttempt = 0;  // reconnect at once when the radio comes back
      mqttParked = true;
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    if (WiFi.status() != WL_CONNECTED) {
      mqttReady = false;
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    // TLS certificate date validation requires correct system time.
    if (time(nullptr) < 1700000000) {
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }

    if (!mqttClient.connected()) {
      mqttReady = false;
      if (millis() - lastAttempt >= 5000 || lastAttempt == 0) {
        lastAttempt = millis();
        String clientId = "ZoneSort-Node1-" + WiFi.macAddress();
        clientId.replace(":", "");
        // [POWER] cleanSession = false: the broker keeps the QoS 1 subscription
        // and queues scans while the radio is off; they arrive on reconnect.
        bool hasUser = strlen(MQTT_USER) > 0;
        bool ok = mqttClient.connect(clientId.c_str(),
                                     hasUser ? MQTT_USER : nullptr,
                                     hasUser ? MQTT_PASSWORD : nullptr,
                                     nullptr, 0, false, nullptr, false);
        if (ok) {
          Serial.println("[MQTT] TLS connected");
          if (mqttClient.subscribe(MQTT_TOPIC_SCAN, 1)) {
            Serial.println("[MQTT] Subscribed to zonesort/gateway/scan");
            mqttReady = true;  // [POWER]
          } else Serial.println("[MQTT] Subscribe FAILED");
        } else {
          Serial.printf("[MQTT] TLS/connect failed state=%d; check IP, certificate, credentials\n", mqttClient.state());
        }
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    mqttClient.loop();
    if (xQueuePeek(mqttQueue, &processed, 0) == pdTRUE) {
      char payload[320];
      snprintf(payload, sizeof(payload),
          "{\"seq\":%lu,\"parcel_id\":\"%s\",\"block\":\"%s\","
          "\"floor\":%d,\"unit\":\"%s\",\"scan\":\"%s\","
          "\"zone\":\"%c\",\"flips\":%u}",
          (unsigned long)processed.seq, processed.parcelID, processed.block,
          processed.floor, processed.unit, processed.scan,
          processed.zone, (unsigned)processed.flips);
      if (mqttClient.publish(MQTT_TOPIC_PROCESSED, payload)) {
        xQueueReceive(mqttQueue, &processed, 0);
        Serial.printf("[MQTT] Processed parcel published: %s\n", payload);
      }
    }
    // New dimension payload. Queue only removed after broker publish succeeds.
    DimensionEvent dim;
    if (xQueuePeek(dimensionsQueue, &dim, 0) == pdTRUE) {
      char widthText[20];
      if (dim.widthMm < 0) snprintf(widthText, sizeof(widthText), "null");
      else snprintf(widthText, sizeof(widthText), "%d", dim.widthMm);
      char payload[220];
      snprintf(payload, sizeof(payload),
        "{\"measurement_id\":%lu,\"length_mm\":%.1f,\"width_mm\":%s,"
        "\"height_mm\":%d,\"duration_ms\":%lu}",
        (unsigned long)dim.measurementId, dim.lengthMm,
        widthText, dim.heightMm, (unsigned long)dim.durationMs);
      if (mqttClient.publish(MQTT_TOPIC_DIMENSIONS, payload)) {
        xQueueReceive(dimensionsQueue, &dim, 0);
        Serial.printf("[MQTT] Dimensions: %s\n", payload);
      }
    }

    BinArrivalEvent bin;
    if (xQueuePeek(binArrivalQueue, &bin, 0) == pdTRUE) {
      char payload[180];
      snprintf(payload, sizeof(payload),
        "{\"seq\":%lu,\"zone\":\"%c\",\"ir_sensor\":%u,"
        "\"espnow_queued\":%s,\"uptime_ms\":%lu}",
        (unsigned long)bin.seq, bin.zone, (unsigned)bin.sensor,
        bin.espnowQueued ? "true" : "false", (unsigned long)bin.uptimeMs);
      if (mqttClient.publish(MQTT_TOPIC_BIN_ARRIVAL, payload)) {
        xQueueReceive(binArrivalQueue, &bin, 0);
        Serial.printf("[MQTT] Bin arrival: %s\n", payload);
      }
    }

    // [OVERSIZE] Fault in the shared schema; removed only after the broker accepts it.
    DimensionEvent big;
    if (xQueuePeek(oversizeQueue, &big, 0) == pdTRUE) {
      char widthText[12];
      if (big.widthMm < 0) snprintf(widthText, sizeof(widthText), "?");
      else snprintf(widthText, sizeof(widthText), "%d", big.widthMm);
      char payload[240];
      snprintf(payload, sizeof(payload),
        "{\"source\":\"node1\",\"type\":\"oversize\",\"seq\":null,"
        "\"detail\":\"box %.0fx%sx%d mm (LxWxH) over limit %dx%dx%d, measurement %lu\"}",
        big.lengthMm, widthText, big.heightMm,
        MAX_LENGTH_MM, MAX_WIDTH_MM, MAX_HEIGHT_MM,
        (unsigned long)big.measurementId);
      if (mqttClient.publish(MQTT_TOPIC_FAULT, payload)) {
        xQueueReceive(oversizeQueue, &big, 0);
        Serial.printf("[MQTT] Fault: %s\n", payload);
      }
    }

    // [UNMATCHED] Bin IR fired but no parcel was assigned to that bin.
    BinArrivalEvent stray;
    if (xQueuePeek(unmatchedQueue, &stray, 0) == pdTRUE) {
      char payload[200];
      snprintf(payload, sizeof(payload),
        "{\"source\":\"node1\",\"type\":\"bin_unmatched\",\"seq\":null,"
        "\"detail\":\"IR%u fired at bin %c but no parcel was sent there\"}",
        (unsigned)stray.sensor, stray.zone);
      if (mqttClient.publish(MQTT_TOPIC_FAULT, payload)) {
        xQueueReceive(unmatchedQueue, &stray, 0);
        Serial.printf("[MQTT] Fault: %s\n", payload);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

// =====================================
// 7. ESP-NOW TASK
// CORE 1 - PRIORITY 2
// =====================================

void espnowTask(void* parameter) {

  WirePacket packet;
  uint32_t reportedDelivered = 0;
  uint32_t reportedFailed = 0;

  while (true) {
    // Print callback results here rather than inside the Wi-Fi callback.
    if (espnowDelivered != reportedDelivered || espnowFailed != reportedFailed) {
      reportedDelivered = espnowDelivered;
      reportedFailed = espnowFailed;
      Serial.printf("[ESP-NOW] MAC delivery: success=%lu failed=%lu\n",
                    (unsigned long)reportedDelivered,
                    (unsigned long)reportedFailed);
    }


    if (xQueueReceive(
        espnowQueue,
        &packet,
        pdMS_TO_TICKS(250)
      ) != pdTRUE) {
      continue;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[ESP-NOW] TX Wi-Fi channel: %d (Node 2 must match)\n", WiFi.channel());
    }

    esp_err_t result = esp_now_send(
      NODE2_MAC,
      (const uint8_t*)&packet,
      sizeof(packet)
    );

    if (result == ESP_OK) {

      Serial.printf(
        "[ESP-NOW] SEND REQUEST QUEUED "
        "ID=%lu Type=%u Zone=%c\n",
        (unsigned long)packet.parcelId,
        packet.type,
        packet.zone ? packet.zone : '-'
      );

    } else {

      Serial.printf(
        "[ESP-NOW] Send error=%d\n",
        result
      );
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// =====================================
// 9. HX711 WEIGHT TASK
// CORE 1 - PRIORITY 2
// =====================================

void weightTask(void* parameter) {

  ParcelEvent event;

  while (true) {

    if (xQueueReceive(
        detectionQueue,
        &event,
        portMAX_DELAY
      ) != pdTRUE) {
      continue;
    }

    WeightResult result = {};

    result.id = event.id;

#if SIMULATE_WEIGHT

    switch (event.id % 4) {

      case 1:
        result.weight = 1050.0f;
        break;

      case 2:
        result.weight = 1750.0f;
        break;

      case 3:
        // [WEIGHT] Was 2450 g, which is over HEAVY_THRESHOLD_G and sent every
        // 4th parcel to Manual during demos. All simulated weights are now under it.
        result.weight = 650.0f;
        break;

      default:
        result.weight = 350.0f;
        break;
    }

    result.valid = true;

#else

    if (scale.wait_ready_timeout(1000)) {

      result.weight = scale.get_units(5);
      result.valid = true;

    } else {

      result.valid = false;
    }

#endif

    result.heavy =
      result.valid &&
      result.weight > HEAVY_THRESHOLD_G;

    Serial.printf(
      "[HX711] ID=%lu Weight=%.1fg Heavy=%s\n",
      (unsigned long)result.id,
      result.weight,
      result.heavy ? "YES" : "NO"
    );

    if (xQueueSend(
        inspectionQueue,
        &result,
        pdMS_TO_TICKS(500)
      ) != pdTRUE) {

      Serial.println("[HX711] Inspection queue full");

      xEventGroupClearBits(
        systemEvents,
        INSPECTION_ACTIVE
      );
    }
  }
}

// =====================================
// 10. QR SIMULATION
// =====================================

QRResult simulateQR(uint32_t id) {

  QRResult qr = {};

  qr.id = id;

  snprintf(
    qr.parcelID,
    sizeof(qr.parcelID),
    "P%04lu",
    (unsigned long)id
  );

  snprintf(
    qr.block,
    sizeof(qr.block),
    "123A"
  );

  snprintf(qr.unit, sizeof(qr.unit), "");

  // Every fourth QR is unreadable
  if (id % 4 == 0) {

    qr.readable = false;
    return qr;
  }

  int floors[] = {3, 6, 9};

  qr.floor = floors[(id - 1) % 3];
  snprintf(qr.unit, sizeof(qr.unit), "%02d-123", qr.floor);
  qr.readable = true;

  return qr;
}

// =====================================
// 11. INSPECTION TASK
// CORE 1 - PRIORITY 2
// =====================================

void inspectionTask(void* parameter) {
  WeightResult parcel;
  while (true) {
    if (xQueueReceive(inspectionQueue, &parcel, portMAX_DELAY) != pdTRUE) continue;
    MqttEvent scan = {};
    if (xQueueReceive(scanQueue, &scan, pdMS_TO_TICKS(1000)) != pdTRUE ||
        scan.seq != parcel.id) {
      Serial.println("[Inspection] Missing or mismatched scan; manual route");
      scan.seq = parcel.id;
      scan.zone = 'M';
      snprintf(scan.scan, sizeof(scan.scan), "unreadable");
    }
    char zone = scan.zone;
    if (!parcel.valid || parcel.heavy) {
      zone = 'M';
      Serial.println("[Weight] Exception -> Manual Bin");
    }
    scan.zone = zone;
    Serial.printf("[Inspection] QR seq=%lu -> Bin %c\n", (unsigned long)scan.seq, zone);
    if (!sendCommand(parcel.id, WIRE_ASSIGNMENT, zone))
      Serial.println("[Inspection] ESP-NOW queue failed");
    if (xQueueSend(mqttQueue, &scan, pdMS_TO_TICKS(200)) != pdTRUE)
      Serial.println("[MQTT] Processed record queue full");
    xEventGroupClearBits(systemEvents, INSPECTION_ACTIVE);
    Serial.println("[Inspection] Finished");
  }
}

// Simulated arrivals or an actual IR beam break. Tune IR polarity if needed.
void detectionTask(void* parameter) {
  bool previousBlocked = false;
  while (true) {
#if SIMULATE_IR
    vTaskDelay(pdMS_TO_TICKS(DETECTION_INTERVAL_MS));
    bool detected = true;
#else
    bool blocked = digitalRead(IR_PIN) == LOW; // common active-LOW IR module
    bool detected = blocked && !previousBlocked;
    previousBlocked = blocked;
    vTaskDelay(pdMS_TO_TICKS(30));
#endif
    if (!detected || (xEventGroupGetBits(systemEvents) & INSPECTION_ACTIVE)) continue;
    ParcelEvent event = { ++parcelCounter };
    xEventGroupSetBits(systemEvents, INSPECTION_ACTIVE);
    if (xQueueSend(detectionQueue, &event, 0) != pdTRUE) {
      xEventGroupClearBits(systemEvents, INSPECTION_ACTIVE);
      Serial.println("[IR] Detection queue full");
    } else Serial.printf("[IR] Arrival ID=%lu\n", (unsigned long)event.id);
  }
}

// =====================================
// 12. SERIAL INPUT TASK
// CORE 0 - PRIORITY 2
// =====================================

void printMenu() {

  Serial.println();
  Serial.println("===== ZONESORT MENU =====");
  Serial.println("A      = Bin A");
  Serial.println("B      = Bin B");
  Serial.println("M      = Manual Bin");
  Serial.println("P      = Pause Node 2");
  Serial.println("R      = Resume Node 2");
  Serial.println("S      = Stop Node 2");
  Serial.println("=========================");
}

void handleInput(String input) {

  markActivity();  // [POWER]
  input.trim();

  String command = input;
  command.toUpperCase();

  if (command == "A" ||
      command == "B" ||
      command == "M") {

    uint32_t id = ++parcelCounter;

    char zone = command.charAt(0);

    if (sendCommand(
        id,
        WIRE_ASSIGNMENT,
        zone
      )) {

      Serial.printf(
        "[Manual] Parcel %lu -> Bin %c\n",
        (unsigned long)id,
        zone
      );
    }

    return;
  }

  if (command == "P") {

    sendCommand(
      parcelCounter,
      WIRE_PAUSE,
      '\0'
    );

    return;
  }

  if (command == "R") {

    sendCommand(
      parcelCounter,
      WIRE_RESUME,
      '\0'
    );

    return;
  }

  if (command == "S") {

    sendCommand(
      parcelCounter,
      WIRE_STOP,
      '\0'
    );

    return;
  }

#if !SIMULATE_QR

  // External Python QR result:
  // RESULT,1,P0001,123A,3
  //
  // Unreadable QR:
  // UNREADABLE,1

  QRResult qr = {};
  unsigned long id = 0;
  int floor = 0;
  char parcelID[20] = {};
  char block[20] = {};

  if (sscanf(
        input.c_str(),
        "RESULT,%lu,%19[^,],%19[^,],%d",
        &id,
        parcelID,
        block,
        &floor
      ) == 4) {

    qr.id = id;
    qr.floor = floor;
    qr.readable = true;
    snprintf(qr.unit, sizeof(qr.unit), "%02d-123", floor);

    snprintf(qr.parcelID, sizeof(qr.parcelID),
             "%s", parcelID);
    snprintf(qr.block, sizeof(qr.block),
             "%s", block);

    xQueueSend(qrQueue, &qr, 0);
    return;
  }

  if (input.startsWith("UNREADABLE,") &&
      sscanf(input.c_str(),
             "UNREADABLE,%lu", &id) == 1) {

    qr.id = id;
    qr.readable = false;

    xQueueSend(qrQueue, &qr, 0);
    return;
  }

#endif

  if (command.length() > 0) {

    Serial.println("[Input] Unknown command");
    printMenu();
  }
}

void serialTask(void* parameter) {

  String buffer = "";

  while (true) {

    while (Serial.available()) {

      char c = Serial.read();

      if (c == '\n' || c == '\r') {

        if (buffer.length() > 0) {

          handleInput(buffer);
          buffer = "";
        }

      } else {

        if (buffer.length() < 100) {

          buffer += c;

        } else {

          buffer = "";
          Serial.println("[Input] Too long");
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

// =====================================
// 13. POWER MANAGEMENT TASK  [POWER]
// CORE 1 - PRIORITY 1
// =====================================

const uint8_t BIN_IR_PINS[3] = { BIN_IR_A, BIN_IR_B, BIN_IR_M };

int64_t sleepUs = 0;        // time in light sleep this report window
int64_t radioOnUs = 0;      // time with the radio on this report window
int64_t radioOnSinceUs = 0;
uint32_t sleepCount = 0;
uint32_t radioChecks = 0;

// Work that must finish before the node may sleep.
bool pipelineBusy() {
  if (xEventGroupGetBits(systemEvents) & INSPECTION_ACTIVE) return true;
  if (uxQueueMessagesWaiting(detectionQueue) > 0 ||
      uxQueueMessagesWaiting(inspectionQueue) > 0 ||
      uxQueueMessagesWaiting(scanQueue) > 0 ||
      uxQueueMessagesWaiting(espnowQueue) > 0) return true;
  if (boxInStation) return true;
  // Parcels assigned but not yet seen at a bin are still on the belt.
  UBaseType_t onBelt = uxQueueMessagesWaiting(pendingA) +
                       uxQueueMessagesWaiting(pendingB) +
                       uxQueueMessagesWaiting(pendingM);
  return onBelt > 0 && millis() - lastAssignmentMs < PARCEL_TRAVEL_TIMEOUT_MS;
}

// MQTT records still going out. They do not keep the node ACTIVE: if the
// broker is unreachable they stay in RAM through the sleep and go out at the
// next radio check.
bool mqttBacklog() {
  return mqttReady &&
         (uxQueueMessagesWaiting(mqttQueue) > 0 ||
          uxQueueMessagesWaiting(dimensionsQueue) > 0 ||
          uxQueueMessagesWaiting(binArrivalQueue) > 0 ||
          uxQueueMessagesWaiting(oversizeQueue) > 0 ||
          uxQueueMessagesWaiting(unmatchedQueue) > 0);
}

void setRadio(bool on) {
  if (on == radioOnNow) return;

  if (on) {
    uint32_t start = millis();
    WiFi.mode(WIFI_STA);
    if (wifiWasConnected) {
      // Same access point and channel as at boot: skips the channel scan.
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD, savedChannel, savedBssid);
      while (WiFi.status() != WL_CONNECTED &&
             millis() - start < RADIO_WAKE_TIMEOUT_MS) {
        vTaskDelay(pdMS_TO_TICKS(50));
      }
    }
    setupESPNow();
    radioOnNow = true;
    radioOnSinceUs = esp_timer_get_time();
    radioWanted = true;  // lets mqttTask reconnect
    Serial.printf("[Power] Radio on in %lu ms (Wi-Fi %s, ESP-NOW %s)\n",
                  (unsigned long)(millis() - start),
                  WiFi.status() == WL_CONNECTED ? "connected" : "offline",
                  espnowReady ? "ready" : "NOT ready");
    if (wifiWasConnected && WiFi.status() != WL_CONNECTED) {
      Serial.println("[Power] Wi-Fi did not reconnect: ESP-NOW fell back to "
                     "channel 1, so Node 2 may not hear this node");
    }
    return;
  }

  // Let mqttTask disconnect first: PubSubClient belongs to that task.
  mqttParked = false;
  radioWanted = false;
  uint32_t start = millis();
  while (!mqttParked && millis() - start < 3000) vTaskDelay(pdMS_TO_TICKS(20));

  if (espnowReady) {
    espnowReady = false;
    esp_now_deinit();
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  radioOnNow = false;
  radioOnUs += esp_timer_get_time() - radioOnSinceUs;
}

void enterStandby() {
  Serial.println("[Power] STANDBY: radio off, light sleep between sensor readings");
  tofParked = false;
  standbyMode = true;
  if (tofReady) {
    uint32_t start = millis();
    while (!tofParked && millis() - start < 1000) vTaskDelay(pdMS_TO_TICKS(5));
    // Side ToF to its own standby (microamps). The top ToF keeps ranging on
    // its own every SAMPLE_INTERVAL_MS with a short budget, and the ESP32
    // only reads the result over I2C after each light sleep.
    sideToF.stopContinuous();
    topToF.stopContinuous();
    topToF.setMeasurementTimingBudget(STANDBY_TOF_BUDGET_US);
    topToF.startContinuous(SAMPLE_INTERVAL_MS);
  }
  setRadio(false);
}

void exitStandby() {
  // ToF first (fast) so a box is measured while the radio reconnects.
  if (tofReady) {
    topToF.stopContinuous();
    topToF.setMeasurementTimingBudget(ACTIVE_TOF_BUDGET_US);
    topToF.startContinuous(ACTIVE_TOF_PERIOD_MS);
    sideToF.startContinuous(ACTIVE_TOF_PERIOD_MS);
  }
  standbyMode = false;
  setRadio(true);
  Serial.println("[Power] ACTIVE: radio on, full-rate sensing");
}

// Non-blocking: reads the top ToF only if it has a new result, so the CPU
// never waits for a ranging to finish.
bool standbyBoxSeen() {
  if (!tofReady) return false;
  if ((topToF.readReg(VL53L0X::RESULT_INTERRUPT_STATUS) & 0x07) == 0) return false;
  uint16_t raw = topToF.readReg16Bit(VL53L0X::RESULT_RANGE_STATUS + 10);
  topToF.writeReg(VL53L0X::SYSTEM_INTERRUPT_CLEAR, 0x01);
  return raw >= 20 && raw <= 1200 &&
         TOP_BASELINE_MM - (int)raw > BOX_HEIGHT_TRIGGER_MM;
}

esp_sleep_wakeup_cause_t lightSleep(uint32_t ms) {
  esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);

  // A beam break (LOW) on any bin IR wakes the chip. A beam that is already
  // blocked is left out, or it would wake the chip at once on every sleep.
  bool armed[3] = { false, false, false };
  bool anyArmed = false;
  for (int i = 0; i < 3; i++) {
    if (digitalRead(BIN_IR_PINS[i]) == HIGH) {
      gpio_wakeup_enable((gpio_num_t)BIN_IR_PINS[i], GPIO_INTR_LOW_LEVEL);
      armed[i] = anyArmed = true;
    }
  }
  if (anyArmed) esp_sleep_enable_gpio_wakeup();

  // Typing in the Serial Monitor wakes the node; the first character is lost.
  uart_set_wakeup_threshold(UART_NUM_0, 3);
  esp_sleep_enable_uart_wakeup(UART_NUM_0);

  Serial.flush();  // finish printing before the UART clock stops

  int64_t before = esp_timer_get_time();
  esp_light_sleep_start();
  sleepUs += esp_timer_get_time() - before;
  ++sleepCount;

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  for (int i = 0; i < 3; i++) {
    if (armed[i]) gpio_wakeup_disable((gpio_num_t)BIN_IR_PINS[i]);
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  return cause;
}

// Standby: bring the radio up just long enough for the broker to hand over
// scans queued while the node was off. A scan keeps the radio on.
void radioCheck() {
  ++radioChecks;
  uint32_t activityBefore = lastActivityMs;

  setRadio(true);
  uint32_t start = millis();
  while (!mqttReady && millis() - start < RADIO_WAKE_TIMEOUT_MS) {
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  if (mqttReady) {
    vTaskDelay(pdMS_TO_TICKS(RADIO_CHECK_LISTEN_MS));
    // Let records saved up during standby go out, but not for ever.
    uint32_t drainStart = millis();
    while (mqttBacklog() && millis() - drainStart < 2000) {
      vTaskDelay(pdMS_TO_TICKS(50));
    }
  } else {
    Serial.println("[Power] Radio check: MQTT did not connect");
  }

  if (lastActivityMs != activityBefore) return;  // a scan came in: stay on
  setRadio(false);
}

// Average current ~= I_sleep * asleep + I_awake * awake + I_radio * radio on.
void powerReport(bool standby, int64_t& windowStart) {
  int64_t now = esp_timer_get_time();
  if (radioOnNow) {
    radioOnUs += now - radioOnSinceUs;
    radioOnSinceUs = now;
  }
  float total = (float)(now - windowStart);
  float asleepPct = 100.0f * (float)sleepUs / total;
  float radioPct = 100.0f * (float)radioOnUs / total;
  float awakePct = max(0.0f, 100.0f - asleepPct - radioPct);

  Serial.printf("[Power] last %.1f s: asleep %.1f%%, awake radio off %.1f%%, "
                "radio on %.1f%% | %lu sleeps, %lu radio checks | %s\n",
                total / 1e6f, asleepPct, awakePct, radioPct,
                (unsigned long)sleepCount, (unsigned long)radioChecks,
                standby ? "STANDBY" : "ACTIVE");

  sleepUs = 0;
  radioOnUs = 0;
  sleepCount = 0;
  radioChecks = 0;
  windowStart = now;
}

void powerTask(void* parameter) {

  bool standby = false;
  uint32_t lastRadioCheck = millis();
  uint32_t lastReport = millis();
  int64_t windowStart = esp_timer_get_time();
  radioOnSinceUs = windowStart;
  markActivity();

  Serial.printf("[Power] Light sleep %s: standby after %d s idle, sensors read "
                "every %d ms, radio check every %d s\n",
                ENABLE_LIGHT_SLEEP ? "ON" : "OFF",
                IDLE_BEFORE_SLEEP_MS / 1000, SAMPLE_INTERVAL_MS,
                RADIO_CHECK_MS / 1000);

  while (true) {

    if (pipelineBusy()) markActivity();
    bool idle = ENABLE_LIGHT_SLEEP &&
                millis() - lastActivityMs >= IDLE_BEFORE_SLEEP_MS;

    if (millis() - lastReport >= POWER_REPORT_MS) {
      powerReport(standby, windowStart);
      lastReport = millis();
    }

    if (!standby) {
      if (idle && !mqttBacklog()) {
        enterStandby();
        standby = true;
        lastRadioCheck = millis();
      } else {
        vTaskDelay(pdMS_TO_TICKS(100));
      }
      continue;
    }

    // STANDBY from here on.

    if (!idle) {  // a scan, a command, a box or a bin IR beam break
      exitStandby();
      standby = false;
      continue;
    }

    if (wifiWasConnected && millis() - lastRadioCheck >= RADIO_CHECK_MS) {
      radioCheck();
      lastRadioCheck = millis();
      continue;
    }

    if (standbyBoxSeen()) {
      Serial.println("[Power] Top ToF sees a box: waking");
      markActivity();
      continue;
    }

    esp_sleep_wakeup_cause_t cause = lightSleep(SAMPLE_INTERVAL_MS);
    if (cause == ESP_SLEEP_WAKEUP_GPIO) {
      Serial.println("[Power] Bin IR beam break: waking");
      markActivity();
    } else if (cause == ESP_SLEEP_WAKEUP_UART) {
      Serial.println("[Power] Serial input: waking (first character lost)");
      markActivity();
    }
  }
}

// =====================================
// 14. SETUP
// =====================================

void setup() {

  Serial.begin(115200);
  delay(1500);

  Serial.println();
  Serial.println("==========================");
  Serial.println("ZONESORT NODE 1 (LOW POWER)");
  Serial.println("ESP-NOW + MQTT + FreeRTOS");
  Serial.println("Manual bin commands A, B and M");
  Serial.println("Direct ESP-NOW + MQTT over hotspot (shared radio channel)");
  Serial.println("==========================");

#if !SIMULATE_IR
  pinMode(IR_PIN, INPUT);
#endif

#if !SIMULATE_WEIGHT

  scale.begin(HX_DOUT, HX_SCK);

  scale.set_scale(CALIBRATION_FACTOR);

  if (scale.wait_ready_timeout(1000)) {

    // Empty platform when taring
    scale.tare(10);

  } else {

    Serial.println("[HX711] Sensor not detected");
  }

#endif

  detectionQueue =
    xQueueCreate(5, sizeof(ParcelEvent));

  inspectionQueue =
    xQueueCreate(5, sizeof(WeightResult));

  qrQueue =
    xQueueCreate(5, sizeof(QRResult));

  espnowQueue =
    xQueueCreate(10, sizeof(WirePacket));

  mqttQueue =
    xQueueCreate(20, sizeof(MqttEvent));

  arrivalQueue = xQueueCreate(5, sizeof(uint32_t));
  scanQueue = xQueueCreate(5, sizeof(MqttEvent));
  dimensionsQueue = xQueueCreate(12, sizeof(DimensionEvent));
  binArrivalQueue = xQueueCreate(12, sizeof(BinArrivalEvent));
  oversizeQueue = xQueueCreate(6, sizeof(DimensionEvent));  // [OVERSIZE]
  unmatchedQueue = xQueueCreate(6, sizeof(BinArrivalEvent));  // [UNMATCHED]
  pendingA = xQueueCreate(12, sizeof(uint32_t));
  pendingB = xQueueCreate(12, sizeof(uint32_t));
  pendingM = xQueueCreate(12, sizeof(uint32_t));


  systemEvents = xEventGroupCreate();

  if (!detectionQueue ||
      !inspectionQueue ||
      !qrQueue ||
      !espnowQueue ||
      !mqttQueue ||
      !arrivalQueue ||
      !scanQueue ||
      !dimensionsQueue ||
      !binArrivalQueue ||
      !oversizeQueue ||
      !unmatchedQueue ||
      !pendingA || !pendingB || !pendingM ||
      !systemEvents) {

    Serial.println("[System] Setup failed");
    return;
  }

  // Real bin sensors are handled by binIrTask, independent of SIMULATE_IR.
  tofReady = setupToFSensors();
  if (!tofReady) Serial.println("[System] ToF OFFLINE; IR bin tracking still available");
  xTaskCreatePinnedToCore(binIrTask, "Bin-IR", 4096, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(sensorTask, "ToF", 6144, NULL, 2, NULL, 1);

  setupWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    // [POWER] Remember the access point for fast reconnects after standby.
    wifiWasConnected = true;
    savedChannel = WiFi.channel();
    memcpy(savedBssid, WiFi.BSSID(), sizeof(savedBssid));
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    Serial.println("[TLS] Synchronising clock for certificate validation");
    unsigned long timeStart = millis();
    while (time(nullptr) < 1700000000 && millis() - timeStart < 15000) delay(250);
    if (time(nullptr) < 1700000000) Serial.println("[TLS] Clock not synchronised; MQTT will wait");
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[System] MQTT unavailable for now; trying ESP-NOW-only fallback");
  }
  setupESPNow();

  if (!espnowReady) {
    Serial.println("[System] ESP-NOW unavailable: bin events will not reach Node 2");
  }

  // Core 0: Serial input
  xTaskCreatePinnedToCore(
    serialTask, "Serial",
    4096, NULL, 2, NULL, 0
  );

  // Core 0: MQTT broker publishing
  xTaskCreatePinnedToCore(
    mqttTask, "MQTT",
    6144, NULL, 1, NULL, 0
  );

  // Scan-first: camera MQTT messages trigger the weight/inspection pipeline.
  // Do not start the simulated IR detection task (it would create fake parcels).

  // Core 1: Weight measurement
  xTaskCreatePinnedToCore(
    weightTask, "Weight",
    4096, NULL, 2, NULL, 1
  );

  // Core 1: Inspection
  xTaskCreatePinnedToCore(
    inspectionTask, "Inspection",
    4096, NULL, 2, NULL, 1
  );

  // Core 1: ESP-NOW
  xTaskCreatePinnedToCore(
    espnowTask, "ESP-NOW",
    3072, NULL, 2, NULL, 1
  );

  // Core 1: Power management  [POWER] bigger stack: it makes the Wi-Fi calls
  xTaskCreatePinnedToCore(
    powerTask, "Power",
    6144, NULL, 1, NULL, 1
  );

  Serial.println("[System] Scan-first mode + 2 ToF + 3 REAL bin IR sensors (A/B/M)");
  Serial.println("[System] Node 2 must add WIRE_BIN_ARRIVED = 5 handling");
  Serial.println("[System] Tasks started");

  printMenu();
}

// =====================================
// 15. LOOP
// =====================================

void loop() {
  delay(1000);
}