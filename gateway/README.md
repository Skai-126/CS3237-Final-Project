# gateway: laptop

**Owner:** Wern She

Reads parcel labels, decides the zone, and runs the MQTT broker that links the ESP32s and the cloud.

## Scope
- Label format `{parcel_id, block, floor, unit}`; QR label generation
- Phone as IP camera → QR decoding with `pyzbar`
- "No QR found" → flip command
- Zone mapping: floors 1–5 → A, 6–10 → B, anything else → Manual
- Mosquitto MQTT broker
- Telegram alerts for jams and repeated scan failures

## Build / run
_To be added. Bot tokens and passwords go in `.env`; commit a `.env.example`._
