# Idea 9: Library / Study-Spot Seat Monitor

> **Proposed by:** Claude (suggested via Shu Kai)  
> Shows which study seats are really free, detects "seat hogging" (belongings left with nobody there) and forecasts how busy a study area will be.

| Component | Details |
|---|---|
| **ESP32 #1 (per desk)** | PIR or mmWave presence sensor + pressure pad / load cell on the seat |
| **ESP32 #2 (per desk / area)** | Microphone for noise level + light sensor; a second desk to show multi-node data |
| **Edge action (real-time)** | Seat has weight/items but no person detected for > N minutes → LED turns amber ("hogged"); immediate status change when someone sits or leaves |
| **Actuator** | RGB LED / small display showing seat status (free / occupied / hogged) |
| **ML** | Occupancy classifier fusing presence + pressure + noise; LSTM forecasting of area occupancy by time of day |
| **Cloud (long-term)** | Occupancy history, peak-hour trends, hogging statistics, predicted free seats in the next hour |
| **App / gateway** | Phone app or web page showing a live seat map and forecasts |
| **Demo** | Sit down → green to red; leave a bag and walk away → amber after a shortened timer; dashboard shows live map and forecast |

## Why it fits the brief
- A problem every NUS student and marker knows, with a clear, quick demo.
- Cheap hardware, and scales naturally to many nodes (collective sensing).

## Risks / notes
- Real occupancy data takes time to collect; we'd need to log data in an actual study area or simulate usage patterns.
- Keep it privacy-friendly: no cameras, only presence/pressure/noise levels.

## Discussion

_Add comments, pros/cons and votes here (or in a GitHub Issue)._

---
[← Back to all ideas](README.md)
