"""Loads parcels / faults (MySQL or the simulated CSV) and shapes them for the frontend."""
import json
import time
from datetime import date, timedelta
from functools import lru_cache

import pandas as pd

from features import daily_counts, load_sim, merge_history
from forecast import forecast
from train import MODEL_DIR

from .models import Fault, Parcel

ZONES = ["A", "B", "MANUAL"]
PARCEL_COLS = [f.name for f in Parcel._meta.fields]
FAULT_COLS = ["id", "session", "source", "type", "seq", "detail", "at"]
RECENT = 50


def records(df: pd.DataFrame) -> list[dict]:
    """DataFrame -> JSON-safe list of dicts (NaN -> null, timestamps -> ISO strings)."""
    return json.loads(df.to_json(orient="records", date_format="iso"))


# ---- loading -----------------------------------------------------------------
def load_db(scope: str) -> tuple[pd.DataFrame, pd.DataFrame, str | None]:
    parcels, faults = Parcel.objects.all(), Fault.objects.all()
    session = None
    if scope == "latest":
        session = Parcel.objects.order_by("-id").values_list("session", flat=True).first()
        parcels, faults = parcels.filter(session=session), faults.filter(session=session)
    elif scope == "today":
        parcels = parcels.filter(arrived_at__date=date.today())
        faults = faults.filter(at__date=date.today())
    p = pd.DataFrame.from_records(parcels.order_by("id").values(*PARCEL_COLS), columns=PARCEL_COLS)
    f = pd.DataFrame.from_records(faults.order_by("id").values(*FAULT_COLS), columns=FAULT_COLS)
    for c in ("arrived_at", "scanned_at", "sorted_at"):
        p[c] = pd.to_datetime(p[c])
    f["at"] = pd.to_datetime(f["at"])
    return p, f, session


@lru_cache(maxsize=1)
def _sim() -> pd.DataFrame:
    p = load_sim()
    for c in ("scanned_at", "sorted_at"):
        p[c] = pd.to_datetime(p[c])
    return p.reset_index(names="id")


def load_simulated(scope: str) -> tuple[pd.DataFrame, pd.DataFrame, str | None]:
    p, session = _sim(), None
    if scope == "latest":
        session = p["session"].iloc[-1]
        p = p[p["session"] == session]
    elif scope == "today":
        p = p[p["arrived_at"].dt.date == date.today()]
    return p, pd.DataFrame(columns=FAULT_COLS), session


# ---- shaping -----------------------------------------------------------------
def live(source: str, scope: str) -> dict:
    p, f, session = load_db(scope) if source == "db" else load_simulated(scope)
    routed = p["routed"].fillna(p["zone"])

    by_block = (p.assign(routed=routed, block=p["block"].fillna("(no label)"))
                .pivot_table(index="block", columns="routed", values="seq",
                             aggfunc="count", fill_value=0)
                .reindex(columns=ZONES, fill_value=0).reset_index())

    arrivals, freq = [], None
    if not p.empty:
        span = p["arrived_at"].max() - p["arrived_at"].min()
        freq = ("minute" if span < pd.Timedelta(hours=2)
                else "hour" if span < pd.Timedelta(days=2) else "day")
        s = p.set_index("arrived_at")["seq"].resample({"minute": "1min", "hour": "1h",
                                                       "day": "1D"}[freq]).count()
        arrivals = records(s.rename("count").rename_axis("t").reset_index())

    return {
        "source": source, "scope": scope, "session": session,
        "kpis": {
            "parcels": len(p),
            **{z: int((routed == z).sum()) for z in ZONES},
            "unreadable": int(p["scan"].isin(["unreadable", "unknown"]).sum()),
            "timeouts": int((p["reason"] == "timeout").sum()),
            "missed": int((p["result"] == "missed").sum()),
            "faults": len(f),
        },
        "by_block": records(by_block),
        "arrivals": {"freq": freq, "points": arrivals},
        "recent": records(p.tail(RECENT).iloc[::-1].astype({"floor": "Int64"})[[
            "seq", "parcel_id", "block", "floor", "scan", "zone", "routed", "reason",
            "result", "arrived_at"]]),
        "faults": records(f.sort_values("at", ascending=False).head(RECENT)[
            ["at", "source", "type", "seq", "detail"]]),
    }


_forecast_cache: dict[tuple, tuple[float, dict]] = {}
FORECAST_TTL_S = 60


def next_day(source: str, model: str) -> dict:
    """Tomorrow's forecast per block, cached for a minute (it re-reads all history)."""
    key = (source, model, date.today())
    hit = _forecast_cache.get(key)
    if hit and time.monotonic() - hit[0] < FORECAST_TTL_S:
        return hit[1]

    tomorrow = date.today() + timedelta(days=1)
    history = load_sim()
    if source == "db":  # through Django, so the API needs only one DB connection setup
        real = Parcel.objects.filter(arrived_at__isnull=False).values(
            "session", "seq", "block", "scan", "zone", "arrived_at")
        real = pd.DataFrame.from_records(real, columns=["session", "seq", "block", "scan",
                                                        "zone", "arrived_at"])
        real["arrived_at"] = pd.to_datetime(real["arrived_at"])
        history = merge_history(history, real)
    fc = forecast(daily_counts(history, end=date.today()), model, tomorrow)
    metrics_file = MODEL_DIR / "metrics.json"
    result = {
        "date": tomorrow.isoformat(), "model": model,
        "blocks": records(fc[["block", "predicted", "dow_avg", "lag1", "lag7"]]),
        "metrics": json.loads(metrics_file.read_text()) if metrics_file.exists() else None,
    }
    _forecast_cache[key] = (time.monotonic(), result)
    return result
