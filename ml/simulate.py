"""Simulated parcel traffic: months of deliveries in the same format as the `parcels` table.

Real prototype runs only cover a few days, so the forecaster is trained on this history
and the real events are appended on top (see features.load_history).

Each block has its own daily base rate, scaled by a weekday effect (Monday catches the
weekend backlog, Sunday is quiet), a slow upward trend, shopping-festival spikes
(9.9, 10.10, 11.11, 12.12, a few days of after-effect) and day-to-day noise.

Usage:
    python simulate.py                 # 365 days ending yesterday -> data/sim_parcels.csv
    python simulate.py --days 730 --seed 7
"""
import argparse
from datetime import date, datetime, time, timedelta
from pathlib import Path

import numpy as np
import pandas as pd

from features import SALE_WINDOW, sale_lag

OUT = Path(__file__).resolve().parents[1] / "data" / "sim_parcels.csv"

# blocks the gateway's zone map knows, with mean parcels per day
BLOCK_RATES = {"123A": 18, "123B": 12, "124": 25, "125": 9, "126A": 15, "126B": 6}
FLOORS = 12                                     # 11-12 are outside the map -> MANUAL
WEEKDAY = [1.35, 1.10, 1.00, 1.00, 1.05, 0.80, 0.45]  # Mon..Sun
TREND_PER_YEAR = 0.15                           # +15% over a year
NOISE_SHAPE = 12                                # gamma shape: lower = burstier days
UNREADABLE, FLIP, TIMEOUT, MISSED = 0.06, 0.15, 0.03, 0.02
SALE_BOOST = [2.2, 1.6, 1.3]                    # sale day, +1 day, +2 days
assert len(SALE_BOOST) == SALE_WINDOW


def zone_for(floor: int) -> str:
    return "A" if 1 <= floor <= 5 else "B" if 6 <= floor <= 10 else "MANUAL"


def simulate(start: date, days: int, rng: np.random.Generator) -> pd.DataFrame:
    rows = []
    for i in range(days):
        d = start + timedelta(days=i)
        lag = sale_lag(d)
        mult = WEEKDAY[d.weekday()] * (1 + TREND_PER_YEAR * i / 365)
        if lag >= 0:
            mult *= SALE_BOOST[lag]
        session = f"sim-{d:%Y%m%d}"
        seq = 0
        for block, rate in BLOCK_RATES.items():
            lam = rate * mult * rng.gamma(NOISE_SHAPE, 1 / NOISE_SHAPE)
            for _ in range(rng.poisson(lam)):
                seq += 1
                rows.append(parcel(session, seq, d, block, rng))
    df = pd.DataFrame(rows)
    # sessions number parcels in arrival order, like Node 1
    df = df.sort_values(["session", "arrived_at"])
    df["seq"] = df.groupby("session").cumcount() + 1
    return df.reset_index(drop=True)


def parcel(session: str, seq: int, d: date, block: str, rng) -> dict:
    # couriers deliver 09:00-18:00, busiest around late morning
    minute = int(np.clip(rng.normal(12 * 60, 150), 9 * 60, 18 * 60 - 1))
    arrived = datetime.combine(d, time()) + timedelta(minutes=minute,
                                                      seconds=float(rng.uniform(0, 60)))
    flips = int(rng.random() < FLIP)
    floor = int(rng.integers(1, FLOORS + 1))
    if rng.random() < UNREADABLE:
        label = dict(parcel_id=None, block=None, floor=None, unit=None,
                     scan="unreadable", zone="MANUAL", flips=1)
    else:
        zone = zone_for(floor)
        label = dict(parcel_id=f"P{rng.integers(0, 10000):04d}", block=block, floor=floor,
                     unit=f"{floor:02d}-{rng.integers(100, 200)}",
                     scan="ok" if zone != "MANUAL" else "unknown", zone=zone, flips=flips)
    timed_out = rng.random() < TIMEOUT
    scanned = arrived + timedelta(seconds=0.4 + 1.5 * label["flips"])
    return dict(
        session=session, seq=seq, **label,
        routed="MANUAL" if timed_out else label["zone"],
        reason="timeout" if timed_out else None,
        result="missed" if rng.random() < MISSED else "confirmed",
        arrived_at=arrived,
        scanned_at=None if timed_out else scanned,
        sorted_at=scanned + timedelta(seconds=3),
    )


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--days", type=int, default=365)
    ap.add_argument("--end", type=date.fromisoformat, default=date.today() - timedelta(days=1),
                    help="last simulated day (default: yesterday)")
    ap.add_argument("--seed", type=int, default=3237)
    ap.add_argument("-o", "--out", type=Path, default=OUT)
    args = ap.parse_args()

    df = simulate(args.end - timedelta(days=args.days - 1), args.days,
                  np.random.default_rng(args.seed))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    df.to_csv(args.out, index=False)
    print(f"{len(df)} parcels over {args.days} days -> {args.out}")


if __name__ == "__main__":
    main()
