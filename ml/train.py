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
Settings (data source, test period, model hyperparameters) come from train_config.py and
the dashboard's Settings page; command-line flags override them for one run.

Usage:
    python train.py                    # saved settings (default: simulated + real events)
    python train.py --source sim --test-days 56
"""
import argparse
import json
import os
from pathlib import Path

import joblib
import numpy as np
import pandas as pd
from sklearn.compose import ColumnTransformer
from sklearn.linear_model import PoissonRegressor
from sklearn.pipeline import Pipeline, make_pipeline
from sklearn.preprocessing import FunctionTransformer, OneHotEncoder, StandardScaler
from xgboost import XGBRegressor

from features import CATEGORICAL, FEATURES, NUMERIC, load_history, make_features
from train_config import DEFAULTS, SCALE_REAL, load_config

MODEL_DIR = Path(__file__).resolve().parent / "models"
MODELS = ["baseline", "poisson", "xgb"]


class SameWeekdayBaseline:
    """Predicts the mean of the same weekday over the previous 4 weeks."""

    def fit(self, X, y):
        return self

    def predict(self, X):
        return X["dow_avg"].to_numpy(dtype=float)


def build(name: str, cfg: dict = DEFAULTS):
    onehot = OneHotEncoder(handle_unknown="ignore")
    if name == "baseline":
        return SameWeekdayBaseline()
    if name == "poisson":
        prep = ColumnTransformer([
            ("cat", onehot, CATEGORICAL),
            ("num", make_pipeline(FunctionTransformer(np.log1p), StandardScaler()), NUMERIC),
        ])
        return Pipeline([("prep", prep), ("model", PoissonRegressor(
            alpha=cfg["poisson_alpha"], max_iter=cfg["poisson_max_iter"]))])
    if name == "xgb":
        prep = ColumnTransformer([("cat", onehot, CATEGORICAL)], remainder="passthrough")
        return Pipeline([("prep", prep), ("model", XGBRegressor(
            objective="count:poisson", n_estimators=cfg["xgb_n_estimators"],
            max_depth=cfg["xgb_max_depth"], learning_rate=cfg["xgb_learning_rate"],
            subsample=cfg["xgb_subsample"], colsample_bytree=cfg["xgb_colsample_bytree"],
            random_state=0))])
    raise ValueError(name)


def busiest_accuracy(test: pd.DataFrame, pred: np.ndarray) -> float:
    t = test.assign(pred=pred)
    hits = t.groupby("date").apply(
        lambda g: g.loc[g["count"].idxmax(), "block"] == g.loc[g["pred"].idxmax(), "block"],
        include_groups=False)
    return float(hits.mean())


def _replace(path: Path, write) -> None:
    """Write to a temp file, then swap it in, so a reader never sees half a file."""
    tmp = path.with_name(path.name + ".tmp")
    write(tmp)
    os.replace(tmp, path)


def run(cfg: dict, log=print) -> dict:
    """Evaluate every model, refit on all days, save; return the metrics.json contents.

    Nothing is saved until every model has trained, so a failed run keeps the old models.
    """
    source = cfg["source"]
    try:
        daily = load_history(source, cfg["exclude_sessions"], SCALE_REAL[cfg["real_scale"]])
    except Exception as e:
        if source != "both":
            raise
        log(f"database unavailable ({type(e).__name__}), training on simulated data only")
        source, daily = "sim", load_history("sim")
    feats = make_features(daily)
    cutoff = feats["date"].max() - pd.Timedelta(days=cfg["test_days"])
    train, test = feats[feats["date"] <= cutoff], feats[feats["date"] > cutoff]
    log(f"{feats['date'].nunique()} days x {feats['block'].nunique()} blocks; "
        f"train to {cutoff:%Y-%m-%d}, test {test['date'].nunique()} days")

    metrics, preds, final = {}, test[["date", "block", "count"]].copy(), {}
    for name in MODELS:
        log(f"training {name}")
        pred = build(name, cfg).fit(train[FEATURES], train["count"]).predict(test[FEATURES])
        preds[name] = pred
        err = pred - test["count"].to_numpy()
        metrics[name] = {"mae": float(np.abs(err).mean()),
                         "rmse": float(np.sqrt((err ** 2).mean())),
                         "busiest": busiest_accuracy(test, pred)}
        if name != "baseline":
            final[name] = build(name, cfg).fit(feats[FEATURES], feats["count"])

    result = {"source": source, "test_days": cfg["test_days"],
              "trained_until": f"{feats['date'].max():%Y-%m-%d}",
              "trained_at": pd.Timestamp.now().isoformat(timespec="seconds"),
              "config": cfg, "models": metrics}
    MODEL_DIR.mkdir(exist_ok=True)
    for name, model in final.items():
        _replace(MODEL_DIR / f"{name}.joblib", lambda p, m=model: joblib.dump(m, p))
    _replace(MODEL_DIR / "test_predictions.csv", lambda p: preds.to_csv(p, index=False))
    _replace(MODEL_DIR / "metrics.json", lambda p: p.write_text(json.dumps(result, indent=2)))
    return result


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", choices=["sim", "db", "both"], help="override the saved setting")
    ap.add_argument("--test-days", type=int, help="override the saved setting")
    args = ap.parse_args()

    cfg = load_config()
    if args.source:
        cfg["source"] = args.source
    if args.test_days:
        cfg["test_days"] = args.test_days
    result = run(cfg)
    print("\n" + pd.DataFrame(result["models"]).T.to_string(float_format=lambda x: f"{x:.3f}"))
    print(f"\nmodels, metrics.json and test_predictions.csv saved to {MODEL_DIR}")


if __name__ == "__main__":
    main()
