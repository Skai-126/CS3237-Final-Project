# Project brief (summary of the CS3237 spec)

Source: *CS3237-Project-AY2627S1.pdf* and the Assessment Breakdown page on Canvas.

## Objective
Build a complete IoT system: **ESP32 IoT devices**, an **optional gateway (smartphone)**, and the **cloud (laptop or cloud provider)**.

- **Edge:** real-time processing of critical data that requires instant action (e.g. fall detection, heat-ailment detection, motion-based security).
- **Cloud:** long-term analytics (e.g. activity trends).
- Sensible partitioning of the application between device, gateway and cloud is expected.

## Must-haves
- [ ] At least **2 ESP32 devices** and **more than one sensor**, with data used collectively
- [ ] ESP32s powered by a power bank (preferred) or mains/laptop, with **no communication over USB**
- [ ] REST and/or MQTT between devices and gateway/cloud
- [ ] Gateway (if used) connects to the cloud over **Wi-Fi**
- [ ] **ML in the cloud**, trained on **our own sensor data** (web data may only augment it)
- [ ] **Live inference** in the final demo
- [ ] Extra hardware within **S$60 per group**

## ML methods covered in lectures
Self-organising maps / clustering · regression · SVMs · backprop neural networks · autoencoders · LSTMs/RNNs · CNNs

## Power management (report must include)
- [ ] Estimated device lifetime on a LiPo, from multiple readings
- [ ] Experiments with different sampling intervals vs battery life
- [ ] If using an RTOS, MCU sleeps between readings
- [ ] _(Optional)_ Compare sending data to gateway/cloud vs on-device inference

## Bonus points
RTOS features (threading, synchronisation, timing) · bare-metal programming · detailed power analysis · communication · 3D printing · other novelty

## Optional AI-agent extension (not needed for full marks)
An agent (e.g. OpenClaw) takes natural-language requests and coordinates sensors, ML and actuators. Rules:
- Runs in a sandbox (Docker/VM), talks to the system **only via restricted project APIs**
- No personal accounts/files, unrestricted host shell or Docker socket exposed
- Backend validates actuator commands and keeps all real-time/safety-critical logic

## Assessment criteria
Novelty · difficulty/complexity · technical achievement (sensing + ML) · quality of solution · quality of presentation, demo and report.

## Deliverables
| # | Deliverable | Due |
|---|---|---|
| 1 | Proposal (spec says 2 pages; template says 3, **check with Jingxian**) | Mon 28 Sep, 23:59 |
| 2 | Check-in 1 (demo of progress + explanation; slides optional, no report) | Week 9 |
| 3 | Check-in 2 (preliminary assessment incl. demo) | Week 11 |
| 4 | Final demo + presentation | 13 Nov |
| 5 | Final report, ≤ 20 pages: idea, techniques, implementation, evaluation, challenges | 13 Nov, 23:59 |
| 6 | Peer reviews ×2 | Weeks 9 & 13 |

## Past projects (lower novelty if repeated)
Heat injury prevention · elderly health monitor · dementia monitoring · fall detection · heat stroke detection · weather prediction · fridge stock monitoring · fish tank monitoring · smart rubbish bin · driver state detection · smart baby monitor

## Contact
Lecturer: Jingxian Wang, wang@nus.edu.sg, COM2-03-32
