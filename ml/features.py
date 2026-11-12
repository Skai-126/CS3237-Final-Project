"""Parcel history -> daily counts per block -> forecasting features.

Every feature for day d uses only counts up to d-1, so it is known the evening before
(when the forecast is issued). The calendar feature (sale_lag) is known in advance.

    daily = load_history("both")      # (date, block) counts: simulated + real prototype days
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
    """Simulated parcels, extended (and saved) up to today if the file is older."""
    from simulate import extend  # simulate imports this module

    if not path.exists():
        raise FileNotFoundError(f"{path} not found: run `python ml/simulate.py` first")
    df = pd.read_csv(path, parse_dates=["arrived_at"])
    longer = extend(df, date.today(), path)
    if len(longer) > len(df):
        tmp = path.with_name(path.name + ".tmp")
        longer.to_csv(tmp, index=False)
        tmp.replace(path)
    return longer


def load_db(exclude=()) -> pd.DataFrame:
    """Real parcels from MySQL, minus the sessions in `exclude` (test runs)."""
    sys.path.insert(0, str(ROOT / "cloud"))
    from db import get_engine

    real = pd.read_sql("SELECT session, seq, block, scan, zone, arrived_at FROM parcels "
                       "WHERE arrived_at IS NOT NULL", get_engine(), parse_dates=["arrived_at"])
    return real[~real["session"].isin(list(exclude))]


def load_history(source: str = "both", exclude=(), scale: bool = True,
                 end: date | None = None) -> pd.DataFrame:
    """Daily counts per block. source: 'sim', 'db', or 'both' (real counts on the days the
    prototype ran, simulated on every other day; see merge_history)."""
    if source == "sim":
        return daily_counts(load_sim(), end)
    if source == "db":
        return daily_counts(load_db(exclude), end)
    return merge_history(daily_counts(load_sim(), end), load_db(exclude), scale)


def merge_history(sim_daily: pd.DataFrame, real: pd.DataFrame, scale: bool = True) -> pd.DataFrame:
    """Replace simulated counts with real ones on the days the prototype ran.

    A demo run handles a few dozen parcels, not a block's whole day of deliveries. With
    `scale`, each real day keeps its real split between blocks, but is scaled up so its
    total matches that day's simulated total. Blocks the simulator doesn't know are dropped:
    one day of counts is no history to forecast them from.
    """
    real = real.dropna(subset=["block", "arrived_at"])
    real = real[real["block"].astype(str).isin(sim_daily["block"].unique())]
    if real.empty:
        return sim_daily
    days = real["arrived_at"].dt.normalize()
    counts = real.groupby([days.rename("date"), real["block"].astype(str)]).size()
    grid = pd.MultiIndex.from_product([days.unique(), sorted(sim_daily["block"].unique())],
                                      names=["date", "block"])
    real_daily = counts.reindex(grid, fill_value=0).rename("count").reset_index()
    sim_rest = sim_daily[~sim_daily["date"].isin(real_daily["date"])]
    if scale:
        sim_total = sim_daily.groupby("date")["count"].sum()
        real_total = real_daily.groupby("date")["count"].transform("sum")
        factor = real_daily["date"].map(sim_total) / real_total
        real_daily["count"] = (real_daily["count"] * factor.fillna(1)).round().astype(int)
    return (pd.concat([sim_rest, real_daily], ignore_index=True)
            .sort_values(["date", "block"]).reset_index(drop=True))


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
