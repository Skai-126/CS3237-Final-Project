/*
  ZoneSort MQTT test: an ESP32 stand-in for Node 1 + the gateway, so the
  ESP32 -> broker -> cloud/subscriber.py -> MySQL -> ml/ forecast chain can be
  tested before the real firmware exists.

  Per parcel it publishes (same seq), in the format of cloud/README.md:
    zonesort/node1/arrival   {"seq":1}
    zonesort/gateway/scan    {"seq":1,"parcel_id":"P0001","block":"124",...}
    zonesort/node2/sorted    {"seq":1,"routed":"A","reason":null,"result":"confirmed"}  (SEND_SORTED)
  and on connect zonesort/node1/status = online (retained, Last Will offline),
  which starts a new session in the subscriber.

  A parcel is sent when you press the BOOT button, type anything in the
  Serial Monitor (115200 baud), or every AUTO_SEND_MS if that isn't 0.

  Setup: copy secrets.example.h to secrets.h and fill it in.
  Library: PubSubClient by Nick O'Leary (Library Manager).
  Board: any ESP32 (Tools > Board > ESP32 Dev Module).
*/
#include <WiFi.h>
#include <PubSubClient.h>

#include "secrets.h"

#define BUTTON_PIN 0          // BOOT button on most ESP32 dev boards
#define AUTO_SEND_MS 0        // e.g. 5000 to send a parcel every 5 s; 0 = off
#define SEND_SORTED 1         // also publish node2/sorted so the dashboard's sort stats fill in

// same blocks as ml/simulate.py, so the forecast has history for each one
const char* BLOCKS[] = {"123A", "123B", "124", "125", "126A", "126B"};
const int N_BLOCKS = sizeof(BLOCKS) / sizeof(BLOCKS[0]);

WiFiClient wifi;
PubSubClient mqtt(wifi);
unsigned long seq = 0;        // counts from 1 after each boot, like the real Node 1
unsigned long lastSend = 0;

const char* zoneFor(int floor) {
  if (floor >= 1 && floor <= 5) return "A";
  if (floor >= 6 && floor <= 10) return "B";
  return "MANUAL";
}

void connectWifi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("Wi-Fi: connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWi-Fi: connected, ESP32 IP %s\n", WiFi.localIP().toString().c_str());
}

void connectMqtt() {
  while (!mqtt.connected()) {
    connectWifi();
    Serial.printf("MQTT: connecting to %s:%d ... ", MQTT_HOST, MQTT_PORT);
    // Last Will: if we drop off, the broker publishes "offline" (retained) for us
    if (mqtt.connect("zonesort-node1-test", nullptr, nullptr,
                     "zonesort/node1/status", 1, true, "offline")) {
      Serial.println("connected");
      mqtt.publish("zonesort/node1/status", "online", true);  // retained; starts a session
    } else {
      // -2 usually means wrong IP, laptop firewall, or not on the same network
      Serial.printf("failed (state %d), retrying in 2 s\n", mqtt.state());
      delay(2000);
    }
  }
}

void publish(const char* topic, const char* payload) {
  bool ok = mqtt.publish(topic, payload);
  Serial.printf("%s %s %s\n", ok ? "sent" : "FAILED", topic, payload);
}

void sendParcel() {
  seq++;
  const char* block = BLOCKS[random(N_BLOCKS)];
  int floor = random(1, 11);  // 1-10, so every test parcel scans ok
  const char* zone = zoneFor(floor);
  char buf[256];

  snprintf(buf, sizeof buf, "{\"seq\":%lu}", seq);
  publish("zonesort/node1/arrival", buf);
  delay(200);

  snprintf(buf, sizeof buf,
           "{\"seq\":%lu,\"parcel_id\":\"P%04lu\",\"block\":\"%s\",\"floor\":%d,"
           "\"unit\":\"%02d-%d\",\"scan\":\"ok\",\"zone\":\"%s\",\"flips\":0}",
           seq, seq, block, floor, floor, (int)random(100, 200), zone);
  publish("zonesort/gateway/scan", buf);

#if SEND_SORTED
  delay(200);
  snprintf(buf, sizeof buf,
           "{\"seq\":%lu,\"routed\":\"%s\",\"reason\":null,\"result\":\"confirmed\"}",
           seq, zone);
  publish("zonesort/node2/sorted", buf);
#endif
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  randomSeed(esp_random());
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
  connectMqtt();
  Serial.println("Ready: press BOOT or type anything in the Serial Monitor to send a parcel");
}

void loop() {
  connectMqtt();  // reconnects if Wi-Fi or the broker dropped
  mqtt.loop();

  bool trigger = false;
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(30);  // debounce
    if (digitalRead(BUTTON_PIN) == LOW) {
      trigger = true;
      while (digitalRead(BUTTON_PIN) == LOW) delay(10);  // one parcel per press
    }
  }
  if (Serial.available()) {
    while (Serial.available()) Serial.read();
    trigger = true;
  }
  if (AUTO_SEND_MS > 0 && millis() - lastSend >= AUTO_SEND_MS) trigger = true;

  if (trigger) {
    sendParcel();
    lastSend = millis();
  }
}
