
#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_idf_version.h>  // ESP-NOW callback compatibility
#include <HX711.h>
#include <PubSubClient.h>

// =====================================
// 1. SETTINGS
// =====================================

// Node 2 MAC address (receiver)
uint8_t NODE2_MAC[6] = {
  0xB0, 0x3F, 0xD3,
  0x5C, 0x4B, 0xAC
};


// Set these to the Wi-Fi network shared with the MQTT broker.
// Set these to the Wi-Fi network shared with the MQTT broker.
const char* WIFI_SSID = "";
const char* WIFI_PASSWORD = "";

// Friend 2's broker address, port and MQTT topic.
const char* MQTT_SERVER = ""; // REPLACE with current IPV4 address
const uint16_t MQTT_PORT = 1883; //disable firework but network settings on the sever's side
const char* MQTT_TOPIC_ARRIVAL = "zonesort/node1/arrival";
const char* MQTT_TOPIC_SCAN = "zonesort/gateway/scan";

// If the broker has authentication, fill these in.
// Leave blank for an anonymous broker.
const char* MQTT_USER = "";
const char* MQTT_PASSWORD = "";

// Both Node 1 and Node 2 must use the Wi-Fi access point's channel.
// Do not force ESP-NOW channel 1 after connecting to Wi-Fi.
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Simulation: 1 = ON, 0 = real sensor
#define SIMULATE_IR 1
#define SIMULATE_WEIGHT 1
#define SIMULATE_QR 1

// Hardware pins
#define IR_PIN 32
#define HX_DOUT 18
#define HX_SCK 19

#define DETECTION_INTERVAL_MS 5000
#define QR_TIMEOUT_MS 15000

// Example weight threshold
#define HEAVY_THRESHOLD_G 2000.0f

// Calibrate before using actual HX711
#define CALIBRATION_FACTOR 1.0f

HX711 scale;

// =====================================
// 2. NODE 2 ESP-NOW PROTOCOL
// =====================================

constexpr uint8_t WIRE_ASSIGNMENT = 1;
constexpr uint8_t WIRE_PAUSE = 2;
constexpr uint8_t WIRE_RESUME = 3;
constexpr uint8_t WIRE_STOP = 4;

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
QueueHandle_t mqttQueue;

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

    // Only publish parcel assignments. P/R/S are control commands.
    if (type == WIRE_ASSIGNMENT) {
      MqttEvent event = {};
      event.seq = parcelId;
      event.zone = zone;
      event.flips = 0; // No physical flipping implemented yet.

      if (qrData != nullptr) {
        snprintf(event.parcelID, sizeof(event.parcelID), "%s", qrData->parcelID);
        snprintf(event.block, sizeof(event.block), "%s", qrData->block);
        event.floor = qrData->floor;
        snprintf(event.unit, sizeof(event.unit), "%s", qrData->unit);
        snprintf(event.scan, sizeof(event.scan), "%s",
                 qrData->readable ? "ok" : "unreadable");
      } else {
        // Manual A/B/M input: generate clearly simulated example data.
        snprintf(event.parcelID, sizeof(event.parcelID), "P%04lu",
                 (unsigned long)parcelId);
        snprintf(event.block, sizeof(event.block), "123A");
        // Matching example floors are for the MQTT test only.
        event.floor = (zone == 'A') ? 3 : (zone == 'B') ? 7 : 0;
        if (event.floor > 0) {
          snprintf(event.unit, sizeof(event.unit), "%02d-123", event.floor);
        } else {
          snprintf(event.unit, sizeof(event.unit), "");
        }
        snprintf(event.scan, sizeof(event.scan), "ok");
      }

      if (xQueueSend(mqttQueue, &event, 0) != pdTRUE) {
        Serial.println("[MQTT] Queue full; data not logged");
      } else {
        Serial.printf("[MQTT] Queued JSON record for parcel %lu\n", (unsigned long)parcelId);
      }
    }
    return true;
  }

  Serial.println("[ESP-NOW] Queue full");
  return false;
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

// MQTT operations are confined to this task (PubSubClient isn't thread-safe).
// CORE 0 - PRIORITY 1
void mqttTask(void* parameter) {
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setBufferSize(384);
  mqttClient.setKeepAlive(15);
  mqttClient.setSocketTimeout(2);

  MqttEvent event;
  unsigned long lastAttempt = 0;
  while (true) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(300));
      continue;
    }

    if (!mqttClient.connected()) {
      if (millis() - lastAttempt >= 5000 || lastAttempt == 0) {
        lastAttempt = millis();
        String clientId = "ZoneSort-Node1-" + WiFi.macAddress();
        clientId.replace(":", "");
        bool ok;
        if (strlen(MQTT_USER) > 0) {
          ok = mqttClient.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD);
        } else {
          ok = mqttClient.connect(clientId.c_str());
        }
        if (ok) Serial.println("[MQTT] Connected to friend's broker");
        else Serial.printf("[MQTT] Connect failed, state=%d\n", mqttClient.state());
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    mqttClient.loop();
    if (xQueueReceive(mqttQueue, &event, 0) == pdTRUE) {
      char payload[256];
      // Eight-field schema requested by the data-collection team.
      snprintf(payload, sizeof(payload),
        "{\"seq\":%lu,\"parcel_id\":\"%s\",\"block\":\"%s\","
        "\"floor\":%d,\"unit\":\"%s\",\"scan\":\"%s\","
        "\"zone\":\"%c\",\"flips\":%u}",
        (unsigned long)event.seq,
        event.parcelID, event.block, event.floor, event.unit,
        event.scan, event.zone, (unsigned int)event.flips);
      // Two MQTT messages for each parcel, using the same sequence number.
      char arrivalPayload[64];
      snprintf(arrivalPayload, sizeof(arrivalPayload),
               "{\"seq\":%lu}", (unsigned long)event.seq);

      bool arrivalOK = mqttClient.publish(MQTT_TOPIC_ARRIVAL, arrivalPayload);
      if (arrivalOK) {
        Serial.printf("[MQTT] Published %s: %s\n", MQTT_TOPIC_ARRIVAL, arrivalPayload);
      } else {
        Serial.printf("[MQTT] Publish failed: %s\n", MQTT_TOPIC_ARRIVAL);
      }

      bool scanOK = mqttClient.publish(MQTT_TOPIC_SCAN, payload);
      if (scanOK) {
        Serial.printf("[MQTT] Published %s: %s\n", MQTT_TOPIC_SCAN, payload);
      } else {
        Serial.printf("[MQTT] Publish failed: %s\n", MQTT_TOPIC_SCAN);
      }

      if (!arrivalOK || !scanOK) {
        // Best-effort retry of the pair; the subscriber should deduplicate by seq.
        if (xQueueSendToFront(mqttQueue, &event, 0) != pdTRUE) {
          Serial.println("[MQTT] Retry queue full; record lost");
        }
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
        result.weight = 2450.0f;
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

    if (xQueueReceive(
        inspectionQueue,
        &parcel,
        portMAX_DELAY
      ) != pdTRUE) {
      continue;
    }

    Serial.printf(
      "[Inspection] Starting ID=%lu\n",
      (unsigned long)parcel.id
    );

    QRResult qr = {};
    bool receivedQR = false;

#if SIMULATE_QR

    vTaskDelay(pdMS_TO_TICKS(500));

    qr = simulateQR(parcel.id);
    receivedQR = true;

#else

    // Python can send results using USB
    Serial.printf(
      "SCAN_REQUEST,%lu\n",
      (unsigned long)parcel.id
    );

    TickType_t start = xTaskGetTickCount();

    while (
      xTaskGetTickCount() - start <
      pdMS_TO_TICKS(QR_TIMEOUT_MS)
    ) {

      QRResult incoming;

      if (xQueueReceive(
          qrQueue,
          &incoming,
          pdMS_TO_TICKS(100)
        ) == pdTRUE) {

        if (incoming.id == parcel.id) {

          qr = incoming;
          receivedQR = true;
          break;
        }
      }
    }

#endif

    char zone = 'M';

    if (!receivedQR) {

      Serial.println(
        "[QR] Timeout -> Manual Bin"
      );

    } else if (!qr.readable) {

      Serial.println(
        "[QR] Unreadable -> Manual Bin"
      );

    } else {

      zone = classifyFloor(qr.floor);

      Serial.printf(
        "[QR] %s | Block=%s | Floor=%d\n",
        qr.parcelID,
        qr.block,
        qr.floor
      );
    }

    // Weight exceptions override QR routing
    if (!parcel.valid || parcel.heavy) {

      zone = 'M';

      Serial.println(
        "[Weight] Exception -> Manual Bin"
      );
    }

    if (sendCommand(
        parcel.id,
        WIRE_ASSIGNMENT,
        zone,
        &qr
      )) {

      Serial.printf(
        "[Inspection] Parcel %lu -> Bin %c\n",
        (unsigned long)parcel.id,
        zone
      );
    } else {

      Serial.println(
        "[Inspection] Assignment failed"
      );
    }

    xEventGroupClearBits(
      systemEvents,
      INSPECTION_ACTIVE
    );

    Serial.println("[Inspection] Finished");
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
// 13. POWER MANAGEMENT TASK
// CORE 1 - PRIORITY 1
// =====================================

void powerTask(void* parameter) {

  bool previous = false;

  while (true) {

    bool active =
      (xEventGroupGetBits(systemEvents)
       & INSPECTION_ACTIVE) != 0;

    if (active != previous) {

      Serial.printf(
        "[Power] %s\n",
        active ? "ACTIVE" : "IDLE"
      );

      previous = active;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
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
  Serial.println("ZONESORT NODE 1");
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

  systemEvents = xEventGroupCreate();

  if (!detectionQueue ||
      !inspectionQueue ||
      !qrQueue ||
      !espnowQueue ||
      !mqttQueue ||
      !systemEvents) {

    Serial.println("[System] Setup failed");
    return;
  }

  setupWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[System] MQTT unavailable for now; trying ESP-NOW-only fallback");
  }
  setupESPNow();

  if (!espnowReady) {
    Serial.println("[System] ESP-NOW unavailable");
    return;
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

  // Core 1: Power management
  xTaskCreatePinnedToCore(
    powerTask, "Power",
    2048, NULL, 1, NULL, 1
  );

  Serial.println("[System] Tasks started");

  printMenu();
}

// =====================================
// 15. LOOP
// =====================================

void loop() {
  delay(1000);
}
