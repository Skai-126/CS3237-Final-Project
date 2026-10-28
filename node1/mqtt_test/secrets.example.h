// Copy to secrets.h (gitignored) and fill in.
#pragma once

// Use a phone hotspot: the ESP32 can't join NUS_STU, and campus Wi-Fi blocks
// devices from reaching each other.
#define WIFI_SSID     "your-hotspot-name"
#define WIFI_PASSWORD "your-hotspot-password"

// The laptop running Mosquitto: its Wi-Fi IPv4 address on the hotspot (`ipconfig`).
// It changes when you reconnect, so check it each time.
#define MQTT_HOST "172.20.10.2"
#define MQTT_PORT 1883
