# Idea 8: Dengue Breeding-Site Monitor

> **Proposed by:** Claude (suggested via Shu Kai)  
> Detects stagnant water in containers around the home (plant saucers, pails, gully traps) and drains or flags them before mosquitoes can breed. Tailored to Singapore's dengue problem.

| Component | Details |
|---|---|
| **ESP32 #1** | Water-level sensor + waterproof temperature probe (DS18B20) in a container |
| **ESP32 #2** | Second container / location, plus ambient temperature, humidity and light sensors |
| **Edge action (real-time)** | Water has stood longer than a threshold (or rain just filled the container) → servo tips/opens a drain, LED/buzzer alert |
| **Actuator** | Servo tips the container or opens a small drain valve |
| **ML** | Breeding-risk classifier/regressor from standing-water duration, water temperature, humidity and rainfall; can be augmented with NEA dengue-cluster and weather data |
| **Cloud (long-term)** | Which containers fill up most often, risk trend by week, correlation with rainfall, map of risk per location |
| **Demo** | Pour water into the tray to "rain"; after an accelerated timer the node flags it and the servo drains it; dashboard updates risk |

## Why it fits the brief
- Very relevant to Singapore and not on the past-projects list, so it scores well on novelty.
- Uses cheap kit sensors; web data (NEA) can only *supplement* our own readings, which is allowed.

## Risks / notes
- Stagnant water changes slowly, so the demo and data collection need an accelerated time scale.
- Needs waterproofing for sensors and electronics.
- Actuation mechanism (tipping/draining) needs a simple, reliable design.

## Discussion

_Add comments, pros/cons and votes here (or in a GitHub Issue)._

---
[← Back to all ideas](README.md)
