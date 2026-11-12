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

```bash
pip install pandas numpy scikit-learn xgboost joblib sqlalchemy pymysql python-dotenv
python simulate.py     # 365 days of traffic up to today -> data/sim_parcels.csv (gitignored)
python train.py        # evaluate + save models to ml/models/ (gitignored)
python forecast.py     # tomorrow's parcels per block
```

| File | What it does |
|---|---|
| [`simulate.py`](simulate.py) | Generates parcels in the `parcels` table format: 6 blocks with their own rates, weekday effect, slow growth, 9.9 / 10.10 / 11.11 / 12.12 sale spikes, noisy days, plus unreadable labels, flips, timeouts and misses. The numbers are the `sim_*` settings in `train_config.py`, editable under *Pretend data* on the dashboard's Settings page (**Regenerate and retrain**); the ones a file was made with go in `data/sim_parcels.meta.json` |
| [`features.py`](features.py) | Loads history (`sim`, `db` or `both`), counts parcels per block per day (zero-filled), builds features from past days only |
| [`train.py`](train.py) | Trains baseline / Poisson / XGBoost, tests on the last 42 days, writes `metrics.json` and `test_predictions.csv` (for report plots), then refits on all days and saves |
| [`train_config.py`](train_config.py) | Training settings: defaults, allowed ranges and help text. Changes saved from the dashboard's Settings page go to `config.json`; `train.py` reads them, and `--source` / `--test-days` override them for one run |
| [`forecast.py`](forecast.py) | Loads a saved model and predicts the next day per block. The dashboard API (`cloud/backend`) calls `forecast()` |

**Data sources.** `--source both` (the default) uses the real MySQL events on days the
prototype ran and simulated traffic on every other day, so real runs feed both training
and the live forecast. If the database is unreachable, `train.py` and `forecast.py` fall
back to simulated data and say so.

- **Simulated history reaches today.** Each simulated day is drawn from its own seed (seed +
  date), so `load_sim()` appends the days since the file was made without changing older ones.
  Days with no data would otherwise count as zero deliveries.
- **Test runs are left out.** Sessions ticked under *Real runs* on the dashboard's Settings page
  (`exclude_sessions` in `config.json`) are skipped by training and the forecast. They stay in
  MySQL and on the live dashboard.
- **Real days are scaled to a full day** (`real_scale`, default on). A demo run is a few dozen
  parcels, so each real day keeps its real split between blocks but is scaled up to that day's
  simulated total. Blocks the simulator doesn't know are dropped.

**Features** for day *d*, all known the evening before: block, weekday, days since a
double-date sale, yesterday's count, same weekday last week, 7- and 28-day means, and the
mean of the same weekday over the last 4 weeks (which is also the baseline's prediction).

**Results on simulated data** (365 days; test = last 42 days, which include 9.9 and 10.10):

| Model | MAE | RMSE | Busiest-block accuracy |
|---|---|---|---|
| Same-weekday average | 5.96 | 8.45 | 57% |
| Poisson regression | **5.06** | **7.06** | **60%** |
| XGBoost (Poisson objective) | 5.31 | 7.53 | 52% |

Poisson regression wins: the simulator is multiplicative (rate × weekday × sale), which a
log-link model fits exactly, and a year of daily data is small for boosted trees.
Busiest-block accuracy is limited because blocks 123A and 124 are often within noise of
each other.
