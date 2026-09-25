# Idea 5: Smart Fridge System

> **Proposed by:** Thomas  
> Monitors food availability, storage conditions and consumption patterns, and notifies the user through an app when an item is missing or running low.

| Component | Details |
|---|---|
| **ESP32 #1** | Temperature and humidity sensors for fridge conditions |
| **ESP32 #2** | Load cells / weight sensors, door sensor, optional gas/VOC sensor |
| **Edge action (real-time)** | Immediately detects a missing or low-stock item and triggers an LED, buzzer or display |
| **Actuator** | Servo could automatically close a model fridge door or control an air vent |
| **ML** | Learns consumption patterns and predicts when commonly used items will run out; estimates spoilage risk from temperature, humidity, storage time and gas readings |
| **App** | Notifications such as "Milk is running low" or "Eggs are no longer detected" |
| **Cloud (long-term)** | Consumption history, fridge conditions and predicted restocking times |
| **Demo** | Put items on the load cells, remove one, and show the app receiving a notification |

## Notes against the brief

- Clean, easy-to-explain demo, and the phone app naturally fills the optional gateway role.
- "Monitoring food stock levels in fridge" is on the list of past projects, so novelty would need to come from the freshness/spoilage side.
- Consumption patterns take weeks to build up for real; we would likely need to simulate usage to train the forecasting model.

## Discussion

_Add comments, pros/cons and votes here (or in a GitHub Issue)._

---
[← Back to all ideas](README.md)
