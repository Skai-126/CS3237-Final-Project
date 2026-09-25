# Project ideas

Vote for your top 2 in the group chat (or react on the voting Issue) by **Sat 26 Sep**.

| # | Idea | Proposed by | One-liner |
|---|---|---|---|
| 1 | [Smart Clothing Drying Rack](01-smart-clothing-drying-rack.md) | Wern She | Predicts when clothes are dry and protects them from rain or high humidity. |
| 2 | [Smart Waste Bin with Fill-Level and Odour Prediction](02-smart-waste-bin-with-fill-level-and-odour-prediction.md) | Wern She | A bin that detects its fill level, identifies waste conditions and predicts when it needs to be collected. |
| 3 | [Smart Dormitory Energy-Waste Detector](03-smart-dormitory-energy-waste-detector.md) | Wern She | Detects appliances being left on unnecessarily. |
| 4 | [Smart AI Sorting Bin](04-smart-ai-sorting-bin.md) | Chyn | Identifies the type of waste dropped in using computer vision, then sorts it into the correct compartment using a rotating funnel. |
| 5 | [Smart Fridge System](05-smart-fridge-system.md) | Thomas | Monitors food availability, storage conditions and consumption patterns, and notifies the user through an app when an item is missing or running low. |
| 6 | [Smart Wireless Interference Detection System](06-smart-wireless-interference-detection-system.md) | Ren Jie | A distributed system that detects areas with abnormal Wi-Fi or cellular connectivity and identifies possible interference. |

## Side-by-side comparison

_These are rough judgements to start discussion, not final scores._

| Idea | Novelty | Technical depth | Hardware risk | Extra cost | Data collection | Demo impact |
|---|:-:|:-:|:-:|:-:|:-:|:-:|
| 1. Drying rack | Medium | Medium | Low | Low | Slow | Good |
| 2. Waste bin | Low | Medium | Low | Low | Medium | OK |
| 3. Energy detector | Medium | Medium | Medium | Medium | Medium | OK |
| 4. AI sorting bin | Med–High | High | High | Medium | Medium | Excellent |
| 5. Smart fridge | Low–Med | Medium | Low | Low | Slow | Good |
| 6. Interference detector | High | Medium | Low | None | Easy | OK |

## Possible merge
Ideas **2 + 4** fit together: the AI sorting bin (edge: camera classification + funnel sorting) plus fill-level/odour sensing and multi-bin MQTT routing (cloud: fill forecasting and collection-route suggestions). That covers both the real-time edge action and the long-term cloud ML.

📄 Word version for sharing: [CS3237_Group3_Project_Ideas.docx](CS3237_Group3_Project_Ideas.docx)
