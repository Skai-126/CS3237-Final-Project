# Idea 1: Smart Clothing Drying Rack

> **Proposed by:** Wern She  
> Predicts when clothes are dry and protects them from rain or high humidity.

| Component | Details |
|---|---|
| **ESP32 #1** | Humidity and temperature sensors |
| **ESP32 #2** | Light, moisture and rain sensors |
| **Edge action (real-time)** | Retracts or covers the clothes as soon as rain is detected |
| **Actuator** | Servo motor moves a cover or the drying rack |
| **ML** | Predicts drying time from humidity, temperature, sunlight and fabric weight |
| **Cloud (long-term)** | Analyses drying performance across different weather conditions |
| **Demo** | Wet cloth samples; simulate rain with a spray bottle |

## Notes against the brief

- Clear split between the edge (rain → cover) and the cloud (drying-time prediction).
- Fabric weight means adding a load cell; that also gives a direct "is it dry yet" signal (weight stops dropping).
- Collecting enough real drying runs to train on takes time, so data collection needs to start early.

## Discussion

_Add comments, pros/cons and votes here (or in a GitHub Issue)._

---
[← Back to all ideas](README.md)
