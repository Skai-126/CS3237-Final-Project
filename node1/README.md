# node1: inspection node (ESP32 #1)

**Owner:** Thomas (with Daniel)

Detects each parcel arriving and passes its zone on to Node 2.

## Scope
- IR arrival trigger → arrival events
- ESP-NOW link to Node 2, message `{parcel_id, zone}`
- MQTT client: publish events, receive zone commands

## Build / run
_To be added (Arduino / PlatformIO). Wi-Fi and broker credentials go in `secrets.h`; commit a `secrets.example.h`._

**MQTT test sketch:** [`mqtt_test/`](mqtt_test/mqtt_test.ino) publishes fake parcels (arrival + scan + sorted)
in the cloud's event format, so the broker → subscriber → MySQL → forecast chain can be tested
before the real firmware exists. Copy `secrets.example.h` to `secrets.h`, install PubSubClient,
flash, then press BOOT (or type in the Serial Monitor) to send a parcel.
