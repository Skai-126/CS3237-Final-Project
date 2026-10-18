"""Live next-day forecast per block, from the models saved by train.py.

    from forecast import forecast
    fc = forecast(daily)          # daily = features.daily_counts(...); predicts the next day

Usage:
    python forecast.py                        # tomorrow, XGBoost, sim history + real events
    python forecast.py --model poisson --source sim
"""
import argparse
from datetime import date, timedelta

import joblib
import pandas as pd

from features import FEATURES, daily_counts, load_history, make_features
from train import MODEL_DIR, SameWeekdayBaseline


def load_model(name: str = "xgb"):
    if name == "baseline":
        return SameWeekdayBaseline()  # nothing to fit
    path = MODEL_DIR / f"{name}.joblib"
    if not path.exists():
        raise FileNotFoundError(f"{path} not found: run `python ml/train.py` first")
    return joblib.load(path)


def forecast(daily: pd.DataFrame, model: str = "xgb", target: date | None = None) -> pd.DataFrame:
    """Predicted parcels per block for `target` (default: the day after the last count)."""
    target = target or (daily["date"].max() + pd.Timedelta(days=1)).date()
    X = make_features(daily, target=target)
    X["predicted"] = load_model(model).predict(X[FEATURES]).clip(min=0)
    return X[["date", "block", "predicted", "dow_avg", "lag1", "lag7"]]


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", choices=["baseline", "poisson", "xgb"], default="xgb")
    ap.add_argument("--source", choices=["sim", "db", "both"], default="both")
    args = ap.parse_args()

    tomorrow = date.today() + timedelta(days=1)
    try:
        parcels = load_history(args.source)
    except Exception as e:
        if args.source != "both":
            raise
        print(f"database unavailable ({type(e).__name__}), using simulated data only")
        parcels = load_history("sim")
    daily = daily_counts(parcels, end=date.today())
    fc = forecast(daily, args.model, tomorrow)
    print(f"forecast for {tomorrow:%a %d %b %Y} ({args.model})")
    print(fc[["block", "predicted", "dow_avg"]].round(1).to_string(index=False))
    print(f"total: {fc['predicted'].sum():.0f} parcels")


if __name__ == "__main__":
    main()
