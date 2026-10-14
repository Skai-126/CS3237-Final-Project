# node2: conveyor & sorting node (ESP32 #2)

**Owner:** Daniel (with Thomas)

Runs the belt and fires the right gate for each parcel.

## Scope
- FreeRTOS: control task on core 1, comms task on core 0, parcel FIFO queue
- Belt motor (N20 + DRV8833): PWM start / stop / reverse
- Gate logic: IR before a gate → pop the queue → fire the matching servo (via PCA9685)
- Fail-safes: Manual on timeout; stop the belt and buzz if a parcel misses the next IR within X s
- MQTT client: publish sort / fault events

## Build / run
_To be added (Arduino / PlatformIO). Wi-Fi and broker credentials go in `secrets.h`; commit a `secrets.example.h`._
