"""Parcel history -> daily counts per block -> forecasting features.

Every feature for day d uses only counts up to d-1, so it is known the evening before
(when the forecast is issued). The calendar feature (sale_lag) is known in advance.

    parcels = load_history("both")    # simulated history + real prototype events
    daily = daily_counts(parcels)     # one row per (date, block), zero-filled
    feats = make_features(daily)      # training rows (first 28 days dropped)
"""
import sys
from datetime import date, timedelta
from pathlib import Path

import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
SIM_CSV = ROOT / "data" / "sim_parcels.csv"

CATEGORICAL = ["block", "dow", "sale_lag"]
NUMERIC = ["lag1", "lag7", "roll7", "roll28", "dow_avg"]
FEATURES = CATEGORICAL + NUMERIC
SALE_WINDOW = 3  # a double-date sale lifts the sale day and the 2 days after it


def sale_lag(d: date) -> int:
    """Days since the latest double-date sale (9.9, 10.10, 11.11, 12.12), or -1."""
    for lag in range(SALE_WINDOW):
        x = d - timedelta(days=lag)
        if x.month == x.day and x.month >= 9:
            return lag
    return -1


def load_sim(path: Path = SIM_CSV) -> pd.DataFrame:
    if not path.exists():
        raise FileNotFoundError(f"{path} not found: run `python ml/simulate.py` first")
    return pd.read_csv(path, parse_dates=["arrived_at"])


def load_db() -> pd.DataFrame:
    sys.path.insert(0, str(ROOT / "cloud"))
    from db import get_engine

    return pd.read_sql("SELECT session, seq, block, scan, zone, arrived_at FROM parcels "
                       "WHERE arrived_at IS NOT NULL", get_engine(), parse_dates=["arrived_at"])


def load_history(source: str = "both") -> pd.DataFrame:
    """source: 'sim', 'db', or 'both' (real events on days the prototype ran, simulated
    traffic on every other day)."""
    if source == "sim":
        return load_sim()
    if source == "db":
        return load_db()
    return merge_history(load_sim(), load_db())


def merge_history(sim: pd.DataFrame, real: pd.DataFrame) -> pd.DataFrame:
    """Real events on the days the prototype ran, simulated traffic on every other day."""
    real_days = real["arrived_at"].dt.normalize().unique()
    return pd.concat([sim[~sim["arrived_at"].dt.normalize().isin(real_days)], real],
                     ignore_index=True)


def daily_counts(parcels: pd.DataFrame, end: date | None = None) -> pd.DataFrame:
    """Parcels per block per day, including days with zero parcels.

    Unreadable / unknown labels have no block, so they aren't counted.
    """
    p = parcels.dropna(subset=["block", "arrived_at"])
    p = p.assign(date=p["arrived_at"].dt.normalize(), block=p["block"].astype(str))
    counts = p.groupby(["date", "block"]).size()
    end = pd.Timestamp(end) if end else counts.index.get_level_values("date").max()
    dates = pd.date_range(counts.index.get_level_values("date").min(), end, freq="D")
    grid = pd.MultiIndex.from_product([dates, sorted(p["block"].unique())],
                                      names=["date", "block"])
    return counts.reindex(grid, fill_value=0).rename("count").reset_index()


def make_features(daily: pd.DataFrame, target: date | None = None) -> pd.DataFrame:
    """Add features to each (date, block). With `target`, also add an unlabelled row per
    block for that day (count = NaN), which is what the live forecast predicts."""
    daily = daily.copy()
    if target is not None:
        future = pd.DataFrame({"date": pd.Timestamp(target),
                               "block": daily["block"].unique(), "count": float("nan")})
        gap = pd.date_range(daily["date"].max() + pd.Timedelta(days=1),
                            pd.Timestamp(target) - pd.Timedelta(days=1), freq="D")
        filler = pd.MultiIndex.from_product([gap, daily["block"].unique()],
                                            names=["date", "block"]).to_frame(index=False)
        daily = pd.concat([daily, filler.assign(count=0), future], ignore_index=True)

    daily = daily.sort_values(["block", "date"]).reset_index(drop=True)
    past = daily.groupby("block")["count"]
    daily["lag1"] = past.shift(1)
    daily["lag7"] = past.shift(7)
    daily["roll7"] = past.transform(lambda s: s.shift(1).rolling(7).mean())
    daily["roll28"] = past.transform(lambda s: s.shift(1).rolling(28).mean())
    daily["dow_avg"] = sum(past.shift(k) for k in (7, 14, 21, 28)) / 4  # baseline
    daily["dow"] = daily["date"].dt.dayofweek
    daily["sale_lag"] = daily["date"].dt.date.map(sale_lag)

    if target is not None:
        return daily[daily["date"] == pd.Timestamp(target)].reset_index(drop=True)
    return daily.dropna(subset=["roll28", "dow_avg"]).reset_index(drop=True)
