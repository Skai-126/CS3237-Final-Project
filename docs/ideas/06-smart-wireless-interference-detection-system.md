# Idea 6: Smart Wireless Interference Detection System

> **Proposed by:** Ren Jie  
> A distributed system that detects areas with abnormal Wi-Fi or cellular connectivity and identifies possible interference.

| Component | Details |
|---|---|
| **ESP32 #1** | Monitors Wi-Fi RSSI, connection status, packet loss, latency and nearby networks |
| **ESP32 #2, #3, ...** | Same measurements at other locations, to tell whether a problem is local to one spot or affects several |
| **Edge action (real-time)** | The ESP32 immediately detects severe degradation and triggers an LED/buzzer or shows a warning, without waiting for the cloud |
| **ML** | Learns normal conditions at each location; classifies readings as normal, weak coverage, temporary congestion or possible interference |
| **Cloud (long-term)** | Stores signal strength, packet loss, latency, connection failures and interference events; shows which areas repeatedly have poor connectivity and when |
| **Dashboard / app** | Status of each node with a simple heatmap (green = normal, yellow = degraded, red = possible interference) |
| **Demo** | Place two or more ESP32s in different locations and create safe, legitimate signal degradation: move one away from the router, put obstacles around it, or generate heavy Wi-Fi traffic |

## Notes against the brief

- Novel compared to the past-project list and needs no extra hardware, and the multi-node comparison is a real use of "collective" sensing.
- The spec requires more than one sensor. Wi-Fi measurements come from the radio, so we should add at least one physical sensor (e.g. temperature or motion) to be safe.
- The system should only detect interference. Deliberately jamming radio signals is illegal, so the demo must stay with the safe methods listed above.
- Few natural actuators; the edge action is mainly an alert.

## Discussion

_Add comments, pros/cons and votes here (or in a GitHub Issue)._

---
[← Back to all ideas](README.md)
