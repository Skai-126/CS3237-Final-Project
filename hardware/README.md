# Hardware

## Devices
| Device | Role | Sensors / actuators | Power |
|---|---|---|---|
| ESP32 #1 (Node 1) | Inspection | IR break-beam | _TBD_ |
| ESP32 #2 (Node 2) | Conveyor & sorting | N20 motor + DRV8833, servos via PCA9685, IR at each gate, buzzer / LEDs | _TBD_ |

**Power plan (draft):** adapter 1 → servos via PCA9685, adapter 2 → motor driver, common ground, decoupling capacitors. Wiring diagram goes in [`wiring/`](wiring/).

## Parts list & budget (S$60 limit)
Items from the course kit cost nothing; list anything we buy so reimbursement is easy.

| Part | Qty | From kit? | Unit price (S$) | Total (S$) | Bought by | Link / receipt |
|---|---|---|---|---|---|---|
| | | | | | | |
| **Total spent** | | | | **0.00** | | |

## Folders
- [`wiring/`](wiring/): pin maps and wiring diagrams (Fritzing / photos)
- [`enclosure/`](enclosure/): 3D-print models (STL/STEP): conveyor chain links, funnel + pusher, flipper, gate flaps, servo mounts, bins, phone mount, IR brackets
- [`datasheets/`](datasheets/): sensor/actuator datasheets (or links)

## Pin map template
| ESP32 | Pin | Connected to | Notes |
|---|---|---|---|
| #1 | GPIO | | |
