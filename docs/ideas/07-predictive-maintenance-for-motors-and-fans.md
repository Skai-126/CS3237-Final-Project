# Idea 7: Predictive Maintenance for Motors and Fans

> **Proposed by:** Claude (suggested via Shu Kai)  
> Monitors a motor or fan for abnormal vibration, heat and current draw, shuts it down instantly when something dangerous happens, and learns its wear pattern over time. This is an Industrial IoT use case.

| Component | Details |
|---|---|
| **ESP32 #1** | IMU / accelerometer (vibration) + temperature sensor clipped onto the fan or motor housing |
| **ESP32 #2** | Current sensor on the fan's supply + relay to cut power; optional microphone for acoustic signature |
| **Edge action (real-time)** | Vibration or temperature crosses a danger threshold → relay cuts power within milliseconds, buzzer/LED alert |
| **Actuator** | Relay (power cut-off); optional servo-driven fan-speed control |
| **ML** | Autoencoder trained on "healthy" vibration/current data → anomaly score; FFT features + SVM/NN to classify fault type (imbalance, obstruction, loose mount) |
| **Cloud (long-term)** | Anomaly score trend, wear/degradation over time, estimated time until maintenance is needed, fault log |
| **Demo** | Run a small USB/desk fan normally, then tape a coin to one blade (imbalance) or lightly obstruct it; the system detects and cuts power live, and the dashboard shows the anomaly |

## Why it fits the brief
- Industrial IoT is a syllabus topic, and autoencoders/SVMs are covered in lectures.
- Clear edge/cloud split: instant shutdown at the edge, anomaly/wear analytics in the cloud.
- Data is fast and easy to collect: hours of labelled vibration data in a day.
- Not on the past-projects list.

## Risks / notes
- Use a **low-voltage (USB/12 V) fan**, not a mains appliance, for the relay.
- High-rate IMU sampling means the FFT should run on the ESP32 so it only sends features, not raw samples (this is also good power-analysis material).
- Bonus-point opportunities: FreeRTOS tasks for sampling vs networking, on-device FFT, 3D-printed sensor mount.

## Discussion

_Add comments, pros/cons and votes here (or in a GitHub Issue)._

---
[← Back to all ideas](README.md)
