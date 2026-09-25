# Idea 4: Smart AI Sorting Bin

> **Proposed by:** Chyn  
> Identifies the type of waste dropped in using computer vision, then sorts it into the correct compartment using a rotating funnel.

| Component | Details |
|---|---|
| **ESP32 #1 (base)** | Drives a NEMA17 stepper (rotates the funnel to the right bin) and an MG996R servo (tilts the funnel to drop the item) |
| **ESP32 #2** | Ultrasonic sensor for bin fill level and/or a weight sensor for item weight and capacity |
| **ESP32-CAM** | Mounted at the top; photographs each item for classification |
| **Edge action (real-time)** | After classification, the base rotates the funnel to the matching bin and triggers the servo to drop the item |
| **ML** | Image classifier (recyclable material types vs general waste) running on the ESP32-CAM via Edge Impulse or TFLite Micro |
| **Cloud (long-term)** | Dashboard showing sorting accuracy over time, waste category breakdown and fill trends per compartment |
| **Demo** | Drop different items in front of the camera one at a time; the system classifies each, rotates to the matching bin and drops it in, live |
| **Inspiration** | ameru.ai (design); James Dyson Award 2026 "Smart Recycling Bin" by Malaysian students |

## Possible extensions

- **Extension 1: robot-arm sorting.** A separate unit: an ordinary bin where everything is thrown in mixed. A dedicated ESP32 drives an arm with its own camera looking into the bin, picks out individual items and places each into the correct sorted bin. We could 3D-print an open-source arm and focus only on the CV and inverse kinematics.
- **Extension 2: multi-bin routing** (also works with idea 2). Each bin's ESP32 publishes its fill level over MQTT to a server, which runs a simple nearest-neighbour greedy route over bins above a fill threshold to suggest a collection order.

## Notes against the brief

- The most impressive demo, and it scores on technical difficulty and possible bonus points (3D printing, RTOS timing for the motors).
- The ML here runs on the edge. The spec requires ML in the cloud as well, so we'd add something like fill-level forecasting or model retraining from misclassified images.
- Has the most mechanical risk (funnel, stepper driver, jams). NEMA17 + driver + MG996R + ESP32-CAM need to fit the S$60 budget, and the stepper needs its own power supply.
- The robot arm (extension 1) is a big jump in scope; probably only worth it if the base system is working by Check-in 2.

## Discussion

_Add comments, pros/cons and votes here (or in a GitHub Issue)._

---
[← Back to all ideas](README.md)
