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

**Low-power sketch:** [`ZoneSort_Node1_2ToF_3IR_LowPower/`](ZoneSort_Node1_2ToF_3IR_LowPower/ZoneSort_Node1_2ToF_3IR_LowPower.ino)
is the final 2 ToF + 3 bin IR sketch with light sleep added (changes marked `[POWER]`). After
`IDLE_BEFORE_SLEEP_MS` with nothing to do, it switches the radio off and light-sleeps between sensor
readings. A bin IR beam break, a box under the top ToF or serial input wakes it, and every
`RADIO_CHECK_MS` it reconnects to collect scans the broker queued for it. It prints the time split
(asleep / awake with radio off / radio on) every 10 s. Set `ENABLE_LIGHT_SLEEP 0` for the baseline.
