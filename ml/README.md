# ml: next-day parcel forecasting

**Owner:** Shu Kai

Forecasts how many parcels each block will get on the next delivery day.

## Scope
- Simulated traffic generator (blocks × days, weekday effect, noise) in the same format as real events
- Daily count per block, including days with zero parcels
- Features: block ID, day of week, previous day's count, same weekday last week, 7- and 28-day rolling averages (only data available before the forecast is issued)
- Models: same-weekday average (baseline), Poisson regression, XGBoost
- Evaluation: train on earlier days, test on later days; MAE (parcels per block per day) and busiest-block accuracy
- Live inference on real prototype events for the demo

## Build / run
_To be added (Python)._
