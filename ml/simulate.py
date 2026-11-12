"""Simulated parcel traffic: months of deliveries in the same format as the `parcels` table.

Real prototype runs only cover a few days, so the forecaster is trained on this history
and the real events are merged in (see features.load_history).

Each day is drawn from its own random stream (seed + date), so extend() can add the days
since the file was made, up to today, without changing any day already in it.

Each block has its own daily base rate, scaled by a weekday effect (Monday catches the
weekend backlog, Sunday is quiet), a slow upward trend, shopping-festival spikes
(9.9, 10.10, 11.11, 12.12, a few days of after-effect) and day-to-day noise. The numbers
are the `sim_*` settings in train_config.py (the dashboard's Settings page); the ones a file
was made with are kept next to it (sim_parcels.meta.json), so extend() keeps using them.

Usage:
    python simulate.py                 # saved settings, ending today -> data/sim_parcels.csv
    python simulate.py --days 730 --seed 7
"""
import argparse
import json
from datetime import date, datetime, time, timedelta
from pathlib import Path

import numpy as np
import pandas as pd

from features import SALE_WINDOW, sale_lag
from train_config import DEFAULTS, SIM_BLOCKS, SIM_KEYS, SIM_RANDOMNESS, SIM_WEEKDAYS, load_config

OUT = Path(__file__).resolve().parents[1] / "data" / "sim_parcels.csv"
SEED = 3237
FLOORS = 12                                     # 11-12 are outside the map -> MANUAL


def meta_path(path: Path) -> Path:
    return path.with_name(path.stem + ".meta.json")


def sim_settings(cfg: dict) -> dict:
    """The sim_* settings out of a full config (what a data file records)."""
    return {k: cfg[k] for k in SIM_KEYS}


def params(settings: dict | None = None) -> dict:
    """sim_* settings -> the numbers simulate() uses."""
    s = {**sim_settings(DEFAULTS), **(settings or {})}
    sale = [s["sim_sale_day"], s["sim_sale_day1"], s["sim_sale_day2"]]
    assert len(sale) == SALE_WINDOW
    return dict(
        block_rates={b: s[f"sim_rate_{b}"] for b in SIM_BLOCKS},
        weekday=[s[f"sim_weekday_{d}"] for d in SIM_WEEKDAYS],  # Mon..Sun
        sale_boost=sale,                                        # sale day, +1 day, +2 days
        trend=s["sim_growth_pct"] / 100,                        # per year
        noise_shape=SIM_RANDOMNESS[s["sim_randomness"]],
        unreadable=s["sim_unreadable_pct"] / 100, flip=s["sim_flip_pct"] / 100,
        timeout=s["sim_timeout_pct"] / 100, missed=s["sim_missed_pct"] / 100,
    )


def zone_for(floor: int) -> str:
    return "A" if 1 <= floor <= 5 else "B" if 6 <= floor <= 10 else "MANUAL"


def simulate(start: date, days: int, seed: int = SEED, origin: date | None = None,
             p: dict | None = None) -> pd.DataFrame:
    """`days` days from `start`. The growth trend counts from `origin` (default: `start`)."""
    origin, p = origin or start, p or params()
    rows = []
    for i in range(days):
        d = start + timedelta(days=i)
        rng = np.random.default_rng([seed, d.toordinal()])  # same day, same parcels
        lag = sale_lag(d)
        mult = p["weekday"][d.weekday()] * (1 + p["trend"] * (d - origin).days / 365)
        if lag >= 0:
            mult *= p["sale_boost"][lag]
        session = f"sim-{d:%Y%m%d}"
        seq = 0
        for block, rate in p["block_rates"].items():
            lam = rate * mult * rng.gamma(p["noise_shape"], 1 / p["noise_shape"])
            for _ in range(rng.poisson(lam)):
                seq += 1
                rows.append(parcel(session, seq, d, block, rng, p))
    df = pd.DataFrame(rows)
    # sessions number parcels in arrival order, like Node 1
    df = df.sort_values(["session", "arrived_at"])
    df["seq"] = df.groupby("session").cumcount() + 1
    return df.reset_index(drop=True)


def parcel(session: str, seq: int, d: date, block: str, rng, p: dict) -> dict:
    # couriers deliver 09:00-18:00, busiest around late morning
    minute = int(np.clip(rng.normal(12 * 60, 150), 9 * 60, 18 * 60 - 1))
    arrived = datetime.combine(d, time()) + timedelta(minutes=minute,
                                                      seconds=float(rng.uniform(0, 60)))
    flips = int(rng.random() < p["flip"])
    floor = int(rng.integers(1, FLOORS + 1))
    if rng.random() < p["unreadable"]:
        label = dict(parcel_id=None, block=None, floor=None, unit=None,
                     scan="unreadable", zone="MANUAL", flips=1)
    else:
        zone = zone_for(floor)
        label = dict(parcel_id=f"P{rng.integers(0, 10000):04d}", block=block, floor=floor,
                     unit=f"{floor:02d}-{rng.integers(100, 200)}",
                     scan="ok" if zone != "MANUAL" else "unknown", zone=zone, flips=flips)
    timed_out = rng.random() < p["timeout"]
    scanned = arrived + timedelta(seconds=0.4 + 1.5 * label["flips"])
    return dict(
        session=session, seq=seq, **label,
        routed="MANUAL" if timed_out else label["zone"],
        reason="timeout" if timed_out else None,
        result="missed" if rng.random() < p["missed"] else "confirmed",
        arrived_at=arrived,
        scanned_at=None if timed_out else scanned,
        sorted_at=scanned + timedelta(seconds=3),
    )


def read_meta(path: Path = OUT) -> dict:
    """What a data file was made with. Files from before the meta file used the defaults."""
    m = meta_path(path)
    meta = json.loads(m.read_text()) if m.exists() else {}
    return {"seed": meta.get("seed", SEED),
            "settings": {**sim_settings(DEFAULTS), **meta.get("settings", {})}}


def extend(df: pd.DataFrame, until: date, path: Path = OUT) -> pd.DataFrame:
    """Append simulated days after the last one in `df`, up to and including `until`,
    with the settings the file was made with."""
    days = df["arrived_at"].dt.date
    first, last = days.min(), days.max()
    if last >= until:
        return df
    meta = read_meta(path)
    new = simulate(last + timedelta(days=1), (until - last).days, meta["seed"], origin=first,
                   p=params(meta["settings"]))
    return pd.concat([df, new], ignore_index=True)


def regenerate(cfg: dict, end: date | None = None, seed: int = SEED,
               path: Path = OUT) -> pd.DataFrame:
    """Remake the whole file from the sim_* settings in `cfg`, ending `end` (today)."""
    end, settings = end or date.today(), sim_settings(cfg)
    df = simulate(end - timedelta(days=cfg["sim_days"] - 1), cfg["sim_days"], seed,
                  p=params(settings))
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + ".tmp")
    df.to_csv(tmp, index=False)
    tmp.replace(path)
    meta_path(path).write_text(json.dumps(
        {"seed": seed, "settings": settings,
         "generated_at": datetime.now().isoformat(timespec="seconds")}, indent=2))
    return df


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--days", type=int, help="override the saved setting")
    ap.add_argument("--end", type=date.fromisoformat, default=date.today(),
                    help="last simulated day (default: today)")
    ap.add_argument("--seed", type=int, default=SEED)
    ap.add_argument("-o", "--out", type=Path, default=OUT)
    args = ap.parse_args()

    cfg = load_config()
    if args.days:
        cfg["sim_days"] = args.days
    df = regenerate(cfg, args.end, args.seed, args.out)
    print(f"{len(df)} parcels over {cfg['sim_days']} days -> {args.out}")


if __name__ == "__main__":
    main()
