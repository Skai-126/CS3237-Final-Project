"""Train and evaluate the next-day forecasters, then save them for live inference.

Models:
  baseline  average of the same weekday over the last 4 weeks (no fitting)
  poisson   Poisson regression (log link) on one-hot block / weekday / sale + log lags
  xgb       gradient-boosted trees with a Poisson objective

Evaluation is time-ordered: train on earlier days, test on the last --test-days days.
  MAE      mean |predicted - actual| parcels per block per day
  RMSE     same, penalising big misses
  busiest  share of test days where the model picks the block that really was busiest

The saved models are refit on every day (train + test) so live forecasts use all data.

Usage:
    python train.py                    # simulated history + real events (default)
    python train.py --source sim --test-days 56
"""
import argparse
import json
from pathlib import Path

import joblib
import numpy as np
import pandas as pd
from sklearn.compose import ColumnTransformer
from sklearn.linear_model import PoissonRegressor
from sklearn.pipeline import Pipeline, make_pipeline
from sklearn.preprocessing import FunctionTransformer, OneHotEncoder, StandardScaler
from xgboost import XGBRegressor

from features import CATEGORICAL, FEATURES, NUMERIC, daily_counts, load_history, make_features

MODEL_DIR = Path(__file__).resolve().parent / "models"
MODELS = ["baseline", "poisson", "xgb"]


class SameWeekdayBaseline:
    """Predicts the mean of the same weekday over the previous 4 weeks."""

    def fit(self, X, y):
        return self

    def predict(self, X):
        return X["dow_avg"].to_numpy(dtype=float)


def build(name: str):
    onehot = OneHotEncoder(handle_unknown="ignore")
    if name == "baseline":
        return SameWeekdayBaseline()
    if name == "poisson":
        prep = ColumnTransformer([
            ("cat", onehot, CATEGORICAL),
            ("num", make_pipeline(FunctionTransformer(np.log1p), StandardScaler()), NUMERIC),
        ])
        return Pipeline([("prep", prep), ("model", PoissonRegressor(alpha=1e-3, max_iter=2000))])
    if name == "xgb":
        prep = ColumnTransformer([("cat", onehot, CATEGORICAL)], remainder="passthrough")
        return Pipeline([("prep", prep), ("model", XGBRegressor(
            objective="count:poisson", n_estimators=400, max_depth=4, learning_rate=0.03,
            subsample=0.8, colsample_bytree=0.8, random_state=0))])
    raise ValueError(name)


def busiest_accuracy(test: pd.DataFrame, pred: np.ndarray) -> float:
    t = test.assign(pred=pred)
    hits = t.groupby("date").apply(
        lambda g: g.loc[g["count"].idxmax(), "block"] == g.loc[g["pred"].idxmax(), "block"],
        include_groups=False)
    return float(hits.mean())


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", choices=["sim", "db", "both"], default="both")
    ap.add_argument("--test-days", type=int, default=42)
    args = ap.parse_args()

    try:
        parcels = load_history(args.source)
    except Exception as e:
        if args.source != "both":
            raise
        print(f"database unavailable ({type(e).__name__}), training on simulated data only")
        parcels = load_history("sim")
    feats = make_features(daily_counts(parcels))
    cutoff = feats["date"].max() - pd.Timedelta(days=args.test_days)
    train, test = feats[feats["date"] <= cutoff], feats[feats["date"] > cutoff]
    print(f"{feats['date'].nunique()} days x {feats['block'].nunique()} blocks; "
          f"train to {cutoff:%Y-%m-%d}, test {test['date'].nunique()} days\n")

    MODEL_DIR.mkdir(exist_ok=True)
    metrics, preds = {}, test[["date", "block", "count"]].copy()
    for name in MODELS:
        pred = build(name).fit(train[FEATURES], train["count"]).predict(test[FEATURES])
        preds[name] = pred
        err = pred - test["count"].to_numpy()
        metrics[name] = {"mae": float(np.abs(err).mean()),
                         "rmse": float(np.sqrt((err ** 2).mean())),
                         "busiest": busiest_accuracy(test, pred)}
        if name != "baseline":
            joblib.dump(build(name).fit(feats[FEATURES], feats["count"]),
                        MODEL_DIR / f"{name}.joblib")

    table = pd.DataFrame(metrics).T
    print(table.to_string(float_format=lambda x: f"{x:.3f}"))
    (MODEL_DIR / "metrics.json").write_text(json.dumps(
        {"source": args.source, "test_days": args.test_days,
         "trained_until": f"{feats['date'].max():%Y-%m-%d}", "models": metrics}, indent=2))
    preds.to_csv(MODEL_DIR / "test_predictions.csv", index=False)
    print(f"\nmodels, metrics.json and test_predictions.csv saved to {MODEL_DIR}")


if __name__ == "__main__":
    main()
