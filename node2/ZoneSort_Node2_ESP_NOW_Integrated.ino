/*
  ZoneSort — Node 2 (Conveyor + Sorting)
  -------------------------------------
  FreeRTOS

  Intended mechanical arrangement:
    Zones -> A -> B -> Manual
    IR at each destination bin (A, B, M) to confirm parcel arrival.
    A single upstream entry IR for diagnostics

  Control policy (event-driven, NOT travel-time-based gate switching):
    1. Node 1 sends a parcel assignment: id + destination.
    2. If no parcel is active, pre-position the appropriate diverter immediately.
    3. Additional assignments wait in a FreeRTOS FIFO, preserving order.
    4. Keep diverters unchanged until the active parcel's CORRECT bin IR fires.
    5. On confirmation, position the next queued parcel and set servos for its zone.
    6. PAUSE/RESUME stops/starts belt WITHOUT clearing active parcel or queue.

  IMPORTANT PHYSICAL ASSUMPTIONS:
    - Node 1 sends assignments in the same order parcels travel on the belt.
    - The funnel maintains enough separation that the next parcel does NOT reach
      the FIRST diverter until the preceding parcel has entered its bin AND
      the gates have finished moving for the next parcel.
    - Node 1 requests PAUSE early enough for the belt to stop before an
      unclassified parcel reaches a diverter. PAUSE must also coordinate with
      the independently driven feeder if that feeder continues dispensing.

  Hardware simulation (Serial Monitor 115200 baud, Newline):
    START, STOP, PAUSE, RESUME, STATUS, RESET, JAM, ENTRY
    A, B, M             -> simulate zone message with generated ID
    RX 101 A           -> simulate Node 1 message with explicit parcel ID
    BIN A, BIN B, BIN M -> simulate bin sensor events

  Configuration to calibrate after receiving hardware:
    SERVO_SETTLE_MS, MAX_BELT_TRAVEL_MS, assigned zone positions, GPIOs,
    ESP-NOW ACK/retry handling, MQTT, physical bin sensor interrupts.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_arduino_version.h>
#include <cstring>
#include <atomic>
#include <ESP32Servo.h>

// ------------------- Project settings -------------------
constexpr uint8_t ASSIGNMENT_QUEUE_CAPACITY = 12;
constexpr uint8_t EVENT_QUEUE_CAPACITY = 24;
constexpr uint32_t SERVO_SETTLE_MS = 450;       // Placeholder; measure real servos
constexpr uint32_t MAX_BELT_TRAVEL_MS = 120000000; // Fault timeout counts only belt-running time
constexpr uint32_t CONTROL_POLL_MS = 20;

// ===== SERVO CONFIGURATION =====
Servo servoA;
Servo servoB;
constexpr int SERVO_A_PIN = 13;
constexpr int SERVO_B_PIN = 18;
constexpr int SERVO_PASS = 180;
constexpr int SERVO_DIVERT = 0;

struct Parcel {
  uint32_t id;
  char zone; // A=Zone A, B=Zone B, M=Manual (straight through)
};

enum class EventType : uint8_t {
  Assignment, BinArrival, Pause, Resume, Start, Stop,
  Jam, Reset, Status, Entry
};

struct Event {
  EventType type;
  Parcel parcel;
  char bin;
};

// These handles refer to queues on ONE ESP32, not wireless ESP-NOW channels.
QueueHandle_t eventQueue = nullptr;
QueueHandle_t assignmentQueue = nullptr;

// ---------- ESP-NOW wire protocol (MUST match Node 1) ----------
constexpr uint8_t ESPNOW_CHANNEL = 6; // Both boards must use same Wi-Fi channel
constexpr uint8_t WIRE_ASSIGNMENT = 1;
constexpr uint8_t WIRE_PAUSE = 2;
constexpr uint8_t WIRE_RESUME = 3;
constexpr uint8_t WIRE_STOP = 4;

struct WirePacket {
  uint32_t parcelId;
  uint8_t type;
  char zone;
  uint8_t reserved[2];
};
static_assert(sizeof(WirePacket) == 8, "Node 1 and Node 2 must use an 8-byte packet");

// ESPNow configuration
constexpr bool CHECK_NODE1_MAC = true;
const uint8_t NODE1_MAC[6] = {
    0x58, 0x2A, 0xBD,
    0xD7, 0x3F, 0x58
};

// A dropped wireless event cannot be silently ignored on a real conveyor.
// The control task checks this flag and latches a fault.
std::atomic<bool> espNowEventOverflow{false};


// Owned by the control task ONLY (no direct writes from the comms task).
bool armed = false;
bool paused = false;
bool faulted = false;
bool motorRunning = false;
bool activeValid = false;
Parcel active = {0, '-'};
bool servoSettling = false;
uint32_t servoMoveStartedMs = 0;
uint32_t beltMovingMs = 0;       // Accumulated time for which motor was RUNNING
uint32_t lastUpdateMs = 0;      
uint32_t activeStartBeltMs = 0; // Running-time clock at beginning of active parcel
uint32_t sortedCount = 0;
uint32_t faultCount = 0;
uint32_t lastCompletedId = 0;

constexpr uint8_t RECENT_IDS_CAPACITY = 32;
uint32_t recentIds[RECENT_IDS_CAPACITY] = {0};
uint8_t recentIdPosition = 0;

bool validZone(char z) {
  return z == 'A' || z == 'B' || z == 'M';
}

bool seenId(uint32_t id) {
  if (activeValid && active.id == id) return true;
  for (uint8_t i = 0; i < RECENT_IDS_CAPACITY; ++i) {
    if (recentIds[i] == id) return true;
  }
  return false;
}

void rememberId(uint32_t id) {
  recentIds[recentIdPosition] = id;
  recentIdPosition = (recentIdPosition + 1) % RECENT_IDS_CAPACITY;
}

// ----------- Hardware abstraction: replace later -----------
void setMotor(bool run) {
  if (motorRunning == run) return;
  motorRunning = run;
  Serial.printf("[MOTOR SIM] %s\n", run ? "RUNNING" : "STOPPED");
  // Real hardware: command Node 2's motor driver here.
}

void positionGates(char zone) {

  // Servo position for zone A
  if (zone == 'A') {
    servoA.write(SERVO_DIVERT);
  } else {
    servoA.write(SERVO_PASS);
  }

  // Servo B remains simulated for now
  if (zone == 'B') {
    servoB.write(SERVO_DIVERT);
  } else {
    servoB.write(SERVO_PASS);
  }
}

void buzzerAlarm() {
  // Serial.println("[BUZZER SIM] ALARM ON");
  // Real hardware: switch on buzzer/LED and latch hardware fault if needed.
}

void reportEvent(const char *kind, const Parcel &p) {
  Serial.printf("[MQTT SIM] zonesort/%s parcel=%lu zone=%c\n",
                kind, (unsigned long)p.id, p.zone);
  // Later: send a short event to an MQTT publishing task.
  // Do NOT perform blocking network publishes inside the control task.
}

void updateBeltClock() {
  uint32_t now = millis();
  uint32_t delta = now - lastUpdateMs; // Wraparound-safe unsigned subtraction
  if (motorRunning) beltMovingMs += delta;
  lastUpdateMs = now;
}

void refreshMotor() {
  // Pause and fault always override START. Belt stays running in idle when armed
  // because the upstream feeder may supply the next parcel continuously.
  // WARNING: Node 1 must prevent unclassified parcels reaching a diverter.
  setMotor(armed && !paused && !faulted);
}

void raiseFault(const char *message) {
  if (faulted) return;
  updateBeltClock();
  ++faultCount;
  faulted = true;
  armed = false;
  setMotor(false);
  buzzerAlarm();
  Serial.printf("[FAULT] %s. Operator inspection + RESET required.\n", message);
  if (activeValid) reportEvent("fault", active);
}

void showStatus() {
  Serial.printf("[STATUS] motor=%s armed=%s paused=%s fault=%s\n",
                motorRunning ? "RUN" : "STOP",
                armed ? "YES" : "NO",
                paused ? "YES" : "NO",
                faulted ? "YES" : "NO");
  Serial.printf("[STATUS] active=%s id=%lu zone=%c waiting=%u settling=%s\n",
                activeValid ? "YES" : "NO",
                (unsigned long)(activeValid ? active.id : 0),
                activeValid ? active.zone : '-',
                (unsigned)uxQueueMessagesWaiting(assignmentQueue),
                servoSettling ? "YES" : "NO");
  Serial.printf("[STATUS] sorted=%lu faults=%lu beltClock=%lu ms\n",
                (unsigned long)sortedCount, (unsigned long)faultCount,
                (unsigned long)beltMovingMs);
}

void startNextIfIdle() {
  if (faulted || activeValid) return;

  Parcel next;
  if (xQueueReceive(assignmentQueue, &next, 0) != pdTRUE) return;

  active = next;
  activeValid = true;
  activeStartBeltMs = beltMovingMs;
  servoSettling = true;
  servoMoveStartedMs = millis();
  Serial.printf("[ACTIVE] Parcel %lu -> Zone %c\n",
                (unsigned long)active.id, active.zone);
  positionGates(active.zone);
  // No intentional motor stop here. The guaranteed parcel spacing must allow
  // enough servo settling time BEFORE the next parcel reaches the first gate.
}

void resetController() {
  updateBeltClock();
  setMotor(false);
  armed = false;
  paused = false;
  faulted = false;
  activeValid = false;
  active = {0, '-'};
  servoSettling = false;
  beltMovingMs = 0;
  activeStartBeltMs = 0;
  sortedCount = 0;
  lastCompletedId = 0;
  for (uint8_t i = 0; i < RECENT_IDS_CAPACITY; ++i) recentIds[i] = 0;
  recentIdPosition = 0;
  xQueueReset(assignmentQueue);
  positionGates('M'); // Safe pass-through default for simulator only
  Serial.println("[RESET] Tracking cleared. Confirm conveyor is physically clear, then START.");
  // faultCount intentionally not cleared (session diagnostics).
}

// ------------------- Core 1: control task -------------------
void controlTask(void *parameter) {
  (void)parameter;
  Event ev;
  lastUpdateMs = millis();
  positionGates('M');

  while (true) {
    if (xQueueReceive(eventQueue, &ev,
                      pdMS_TO_TICKS(CONTROL_POLL_MS)) == pdTRUE) {
      updateBeltClock();

      if (ev.type == EventType::Status) {
        showStatus();
      } else if (ev.type == EventType::Reset) {
        resetController();
      } else if (ev.type == EventType::Jam) {
        raiseFault("JAM request received");
      } else if (faulted) {
        Serial.println("[REJECTED] Fault latched; RESET required.");
      } else {
        switch (ev.type) {
          case EventType::Assignment: {
            const Parcel p = ev.parcel;
            if (p.id == 0 || !validZone(p.zone)) {
              Serial.println("[ERROR] Invalid parcel assignment");
              break;
            }
            if (seenId(p.id)) {
              Serial.printf("[DUPLICATE] Parcel %lu ignored\n", (unsigned long)p.id);
              break;
            }
            if (xQueueSend(assignmentQueue, &p, 0) != pdTRUE) {
              raiseFault("Assignment FIFO overflow");
              break;
            }
            rememberId(p.id);
            Serial.printf("[QUEUED] Parcel %lu -> %c; pending=%u\n",
                          (unsigned long)p.id, p.zone,
                          (unsigned)uxQueueMessagesWaiting(assignmentQueue));
            startNextIfIdle();
            break;
          }
          case EventType::BinArrival:
            if (!activeValid) {
              raiseFault("Bin IR fired without active parcel");
            } else if (ev.bin != active.zone) {
              raiseFault("Unexpected bin: parcel assigned to a different zone");
            } else {
              Serial.printf("[SORTED] Parcel %lu confirmed in bin %c\n",
                            (unsigned long)active.id, ev.bin);
              reportEvent("sorted", active);
              ++sortedCount;
              lastCompletedId = active.id;
              activeValid = false;
              servoSettling = false;
              // Correct bin confirmation is the ONLY normal trigger to
              // switch to the following parcel's diverter arrangement.
              startNextIfIdle();
            }
            break;
          case EventType::Pause:
            paused = true;
            refreshMotor();
            Serial.println("[ACK SIM] PAUSED (inform Node 1 when real ESP-NOW is added)");
            break;
          case EventType::Resume:
            paused = false;
            refreshMotor();
            Serial.println("[ACK SIM] RESUMED");
            break;
          case EventType::Start:
            armed = true;
            refreshMotor();
            Serial.println("[SYSTEM] START accepted");
            break;
          case EventType::Stop:
            armed = false;
            refreshMotor();
            Serial.println("[SYSTEM] STOP accepted; parcel tracking preserved");
            break;
          case EventType::Entry:
            Serial.printf("[ENTRY IR SIM] Parcel passed entry beam; active=%lu\n",
                          (unsigned long)(activeValid ? active.id : 0));
            // Optional diagnostic. Not used to change servo positions.
            break;
          default:
            break;
        }
      }
    }

    // A queue overflow in the Wi-Fi callback requests a latched control fault.
    if (espNowEventOverflow.exchange(false)) {
      raiseFault("ESP-NOW event queue overflow");
    }

    updateBeltClock();
    if (!faulted && activeValid) {
      if (servoSettling && millis() - servoMoveStartedMs >= SERVO_SETTLE_MS) {
        servoSettling = false;
        Serial.printf("[SERVO SIM] Gate position ready for parcel %lu\n",
                      (unsigned long)active.id);
      }
      if ((uint32_t)(beltMovingMs - activeStartBeltMs) > MAX_BELT_TRAVEL_MS) {
        raiseFault("Missing bin confirmation / possible jam");
      }
    }
  }
}

// ------------------- ESP-NOW receiver (Wi-Fi task) -------------------
// IMPORTANT: Callback only validates packet and enqueues it.
// It never touches motor/servo state or performs blocking operations.
void processWirelessPacket(const uint8_t *sender, const uint8_t *bytes, int length) {
  if (!sender || !bytes || length != (int)sizeof(WirePacket) || !eventQueue) return;
  if (CHECK_NODE1_MAC && memcmp(sender, NODE1_MAC, 6) != 0) return;

  WirePacket msg{};
  memcpy(&msg, bytes, sizeof(msg));

  Event ev{};
  ev.bin = '-';
  ev.parcel = {0, '-'};

  switch (msg.type) {
    case WIRE_ASSIGNMENT:
      if (msg.parcelId == 0 || !validZone(msg.zone)) return;
      ev.type = EventType::Assignment;
      ev.parcel = {msg.parcelId, msg.zone};
      break;
    case WIRE_PAUSE: ev.type = EventType::Pause; break;
    case WIRE_RESUME: ev.type = EventType::Resume; break;
    case WIRE_STOP: ev.type = EventType::Stop; break;
    default: return;
  }

  // This callback runs in Wi-Fi task context, NOT a hardware ISR.
  // Use a zero-wait queue send; do not block the Wi-Fi task.
  if (xQueueSend(eventQueue, &ev, 0) != pdTRUE) {
    espNowEventOverflow.store(true);
  }
}

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void onEspNowReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (info) processWirelessPacket(info->src_addr, data, len);
}
#else
void onEspNowReceive(const uint8_t *mac, const uint8_t *data, int len) {
  processWirelessPacket(mac, data, len);
}
#endif

void initializeEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
    Serial.println("[FATAL] Failed to set ESP-NOW channel");
    while (true) delay(1000);
  }
  Serial.print("[ESP-NOW] Node 2 STA MAC: ");
  Serial.println(WiFi.macAddress());
  if (esp_now_init() != ESP_OK) {
    Serial.println("[FATAL] ESP-NOW init failed");
    while (true) delay(1000);
  }
  if (esp_now_register_recv_cb(onEspNowReceive) != ESP_OK) {
    Serial.println("[FATAL] ESP-NOW receive callback registration failed");
    while (true) delay(1000);
  }
  Serial.printf("[ESP-NOW] Listening on channel %u\n", ESPNOW_CHANNEL);
  if (!CHECK_NODE1_MAC) {
    Serial.println("[WARNING] Sender MAC filtering OFF; development mode only");
  }
  // Receiving unencrypted unicast doesn't require a registered peer.
  // A peer and protocol acknowledgement will be needed when Node 2 sends replies.
}

// ------------------- Core 0: communication task -------------------
// This section is a Serial Monitor replacement for future ESP-NOW callbacks.
uint32_t nextGeneratedId = 1;

void sendToControl(const Event &event) {
  if (xQueueSend(eventQueue, &event, pdMS_TO_TICKS(20)) != pdTRUE) {
    Serial.println("[CRITICAL] Event queue overflow. Production firmware must fail safe.");
  }
}

void parseCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd.length() == 0) return;

  Event ev = {};
  ev.parcel = {0, '-'};
  ev.bin = '-';

  if (cmd == "START") ev.type = EventType::Start;
  else if (cmd == "STOP") ev.type = EventType::Stop;
  else if (cmd == "PAUSE") ev.type = EventType::Pause;
  else if (cmd == "RESUME") ev.type = EventType::Resume;
  else if (cmd == "JAM") ev.type = EventType::Jam;
  else if (cmd == "RESET") ev.type = EventType::Reset;
  else if (cmd == "STATUS") ev.type = EventType::Status;
  else if (cmd == "ENTRY") ev.type = EventType::Entry;
  else if (cmd.length() == 1 && validZone(cmd[0])) {
    ev.type = EventType::Assignment;
    ev.parcel = {nextGeneratedId++, cmd[0]};
  } else if (cmd.startsWith("BIN ") && cmd.length() == 5 && validZone(cmd[4])) {
    ev.type = EventType::BinArrival;
    ev.bin = cmd[4];
  } else if (cmd.startsWith("RX ")) {
    unsigned long id = 0;
    char z = '-';
    char trailing = 0;
    int n = sscanf(cmd.c_str(), "RX %lu %c %c", &id, &z, &trailing);
    if (n != 2 || id == 0 || id > UINT32_MAX || !validZone(z)) {
      Serial.println("[ERROR] Use RX <positive-parcel-id> <A/B/M>");
      return;
    }
    ev.type = EventType::Assignment;
    ev.parcel = {(uint32_t)id, z};
  } else {
    Serial.println("[ERROR] Commands: START STOP PAUSE RESUME STATUS JAM RESET ENTRY");
    Serial.println("[ERROR] A B M | RX 101 A | BIN A/B/M");
    return;
  }

  sendToControl(ev);
}

void communicationTask(void *parameter) {
  (void)parameter;
  String line;
  line.reserve(64);
  while (true) {
    while (Serial.available() > 0) {
      char c = (char)Serial.read();
      if (c == '\n' || c == '\r') {
        if (line.length() > 0) {
          parseCommand(line);
          line = "";
        }
      } else if (line.length() < 63) {
        line += c;
      } else {
        line = "";
        Serial.println("[ERROR] Command too long");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);

  // Initialize Servos and set initial position
  servoA.setPeriodHertz(50);
  servoB.setPeriodHertz(50);
  servoA.attach(SERVO_A_PIN, 1000, 2000);
  servoB.attach(SERVO_B_PIN, 1000, 2000);
  servoA.write(SERVO_PASS);
  servoB.write(SERVO_PASS);
  Serial.println("[SERVO] Servo initialized");

  eventQueue = xQueueCreate(EVENT_QUEUE_CAPACITY, sizeof(Event));
  assignmentQueue = xQueueCreate(ASSIGNMENT_QUEUE_CAPACITY, sizeof(Parcel));
  if (!eventQueue || !assignmentQueue) {
    Serial.println("[FATAL] FreeRTOS queue creation failed");
    while (true) delay(1000);
  }

  BaseType_t controlResult = xTaskCreatePinnedToCore(
      controlTask, "ZoneSortControl", 6144, nullptr, 2, nullptr, 1);
  BaseType_t commResult = xTaskCreatePinnedToCore(
      communicationTask, "ZoneSortComm", 6144, nullptr, 1, nullptr, 0);
  if (controlResult != pdPASS || commResult != pdPASS) {
    Serial.println("[FATAL] FreeRTOS task creation failed");
    while (true) delay(1000);
  }

  initializeEspNow();

  Serial.println("ZoneSort Node 2 — Event-driven multi-parcel simulator ready!");
  Serial.println("No gate-position timing required for SWITCHING: switches on bin IR.");
  Serial.println("Commands: START STOP PAUSE RESUME STATUS JAM RESET ENTRY");
  Serial.println("Assignment: A B M (auto id), or RX 101 A (explicit id)");
  Serial.println("Confirmation: BIN A, BIN B, BIN M");
}

void loop() {
  delay(1000);  // Arduino's loop task is not used for sorting logic.
}
