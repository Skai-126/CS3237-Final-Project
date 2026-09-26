# Project ideas

Vote for your top 2 in the group chat (or react on the voting Issue). **We need to decide by Sun 27 Sep** to write the proposal (due Mon 28 Sep, 23:59).

| # | Idea | Proposed by | One-liner |
|---|---|---|---|
| 1 | [Smart Clothing Drying Rack](01-smart-clothing-drying-rack.md) | Wern She | Predicts when clothes are dry and protects them from rain or high humidity. |
| 2 | [Smart Waste Bin with Fill-Level and Odour Prediction](02-smart-waste-bin-with-fill-level-and-odour-prediction.md) | Wern She | Detects fill level and waste conditions, predicts when collection is needed. |
| 3 | [Smart Dormitory Energy-Waste Detector](03-smart-dormitory-energy-waste-detector.md) | Wern She | Detects appliances being left on unnecessarily. |
| 4 | [Smart AI Sorting Bin](04-smart-ai-sorting-bin.md) | Chyn | Classifies waste with a camera and sorts it into the right compartment. |
| 5 | [Smart Fridge System](05-smart-fridge-system.md) | Thomas | Tracks food stock, storage conditions and consumption; notifies when items run low. |
| 6 | [Smart Wireless Interference Detection System](06-smart-wireless-interference-detection-system.md) | Ren Jie | Detects areas with abnormal Wi-Fi connectivity or interference. |
| 7 | [Predictive Maintenance for Motors and Fans](07-predictive-maintenance-for-motors-and-fans.md) | 🆕 Claude | Detects abnormal vibration/heat, shuts the motor off instantly, tracks wear in the cloud. |
| 8 | [Dengue Breeding-Site Monitor](08-dengue-breeding-site-monitor.md) | 🆕 Claude | Detects stagnant water and drains/flags containers before mosquitoes breed. |
| 9 | [Library / Study-Spot Seat Monitor](09-library-study-spot-seat-monitor.md) | 🆕 Claude | Real seat availability, "seat hogging" detection and occupancy forecasts. |

## Side-by-side comparison

_Rough judgements to start discussion, not final scores._

| Idea | Novelty | Technical depth | Hardware risk | Extra cost | Data collection | Demo impact |
|---|:-:|:-:|:-:|:-:|:-:|:-:|
| 1. Drying rack | Medium | Medium | Low | Low | Slow | Good |
| 2. Waste bin | Low | Medium | Low | Low | Medium | OK |
| 3. Energy detector | Medium | Medium | Medium | Medium | Medium | OK |
| 4. AI sorting bin | Med–High | High | High | Medium | Medium | Excellent |
| 5. Smart fridge | Low–Med | Medium | Low | Low | Slow | Good |
| 6. Interference detector | High | Medium | Low | None | Easy | OK |
| 7. Predictive maintenance | Med–High | High | Low | Low | Easy | Good |
| 8. Dengue monitor | High | Medium | Medium | Low | Slow | Good |
| 9. Seat monitor | Medium | Medium | Low | Low | Medium | Good |

## Recommendation (for discussion)

- **Highest ceiling: Idea 4 + Idea 2 merged.** Camera classification and sorting at the edge, with fill-level / waste-mix forecasting across bins in the cloud (this also fixes idea 4's missing cloud ML). To reduce mechanical risk, use a simple servo trapdoor into 2–3 compartments instead of the stepper funnel, and keep the robot arm as a stretch goal.
- **Best balance of risk and marks: Idea 7 (predictive maintenance).** Cheap, easy to collect lots of data, maps directly to lecture content (autoencoders, Industrial IoT), and the coin-on-fan-blade demo is simple and reliable.
- **Safe option: Idea 1 with a Singapore angle.** Sudden afternoon storms and HDB laundry poles; the cloud model can add NEA weather data to our own sensor data.

## Possible merge
Ideas **2 + 4** fit together: the AI sorting bin (edge: camera classification + funnel sorting) plus fill-level/odour sensing and multi-bin MQTT routing (cloud: fill forecasting and collection-route suggestions). That covers both the real-time edge action and the long-term cloud ML.

📄 Word version for sharing (ideas 1–6): [CS3237_Group3_Project_Ideas.docx](CS3237_Group3_Project_Ideas.docx)
