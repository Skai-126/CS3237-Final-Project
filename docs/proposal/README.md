# Project proposal

📄 Submitted Mon 28 Sep: [ZoneSort-Proposal-Team03.pdf](ZoneSort-Proposal-Team03.pdf)

## Summary
- **Zones:** A (floors 1–5), B (floors 6–10), Manual (unreadable or unknown label, or the zone lookup timed out).
- **Per parcel:** singulation → label check (flip if no label is seen) → QR identification → zone sent to the conveyor node over ESP-NOW → servo gate fires, bin IR confirms arrival → event published over MQTT and stored in the cloud.
- **ML:** daily parcel count per block (zero days included) to predict the next delivery day. Inputs are block ID, day of week, the previous day's count, the same weekday last week, and 7- and 28-day rolling averages. Poisson regression and XGBoost are compared against a same-weekday average baseline. Train on earlier days, test on later days, and report MAE plus busiest-block accuracy.
- **Data:** records from the real prototype, plus simulated traffic in the same format. Models are evaluated on real prototype runs.

## Changes since submission
- GM65 scanner → phone camera as an IP camera; the laptop decodes QR codes with `pyzbar`.
- Load cells go under the baskets, not under the belt.
- Conveyor motor: N20 + DRV8833 driver.

_The hardware table in the PDF still shows the original parts (Wern She is updating it)._

## Feedback on the proposal
Early feedback from Jingxian Wang:

- ZoneSort has a clear workflow, and the conveyor and sorting gates provide meaningful actuation.
- **Prototype the sorting mechanism early.** Start with a limited parcel size range and reliable scanning and routing before adding automatic feeding and flipping.
- **Demonstrate live ML inference using real sensor data.**
- **Start collecting parcel records early.** Explain how we will get enough data for meaningful next-day forecasting, and evaluate against a simple baseline.
- **Order additional hardware soon**, within the project budget, and validate the scanner and motor setup early.
- To be discussed at the check-in: the prototype.
