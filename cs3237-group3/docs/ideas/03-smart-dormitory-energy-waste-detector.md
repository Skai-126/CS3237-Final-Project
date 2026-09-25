# Idea 3: Smart Dormitory Energy-Waste Detector

> **Proposed by:** Wern She  
> Detects appliances being left on unnecessarily.

| Component | Details |
|---|---|
| **ESP32 #1** | Current sensor and temperature sensor |
| **ESP32 #2** | Motion, light and door sensors |
| **Edge action (real-time)** | Turn off a simulated appliance when no one is present |
| **ML** | Learn normal room-usage patterns; tell apart normal use, unusual consumption and possible overheating |
| **Cloud (long-term)** | Energy consumption trends and estimated wasted energy |
| **Demo** | Leave a lamp or fan running while simulating an empty room |

## Notes against the brief

- Strong real-world motivation, and the long-term energy analytics fit the cloud requirement well.
- Switching a real mains appliance is a safety risk. Stick to a low-voltage (USB) fan or lamp driven through a relay.
- A current sensor (e.g. a clamp-on type) will probably need to be bought.

## Discussion

_Add comments, pros/cons and votes here (or in a GitHub Issue)._

---
[← Back to all ideas](README.md)
