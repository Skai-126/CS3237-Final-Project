"""Tunable training settings, shared by train.py and the dashboard's Settings page.

Defaults live here; saved changes go to ml/config.json (only the keys someone changed are
needed, the rest fall back to the defaults). Every value is checked against its range, so
the dashboard can't save a learning rate of 5.

    from train_config import load_config, save_config, PARAMS
    cfg = load_config()                       # defaults + saved overrides
    save_config({"xgb_max_depth": 6})         # validates, then writes config.json
"""
import json
from pathlib import Path

CONFIG_PATH = Path(__file__).resolve().parent / "config.json"

# key -> group, label, help, default, and either choices or type + min/max(/step for the UI)
PARAMS = {
    "source": dict(group="Data", label="Training data", default="both",
                   choices={"both": "Simulated + real", "sim": "Simulated only", "db": "Real only"},
                   help="Real parcels from MySQL on the days the prototype ran; "
                        "simulated traffic fills every other day."),
    "real_scale": dict(group="Data", label="Real runs count as", default="day",
                       choices={"day": "A full day, scaled up", "none": "Recorded counts"},
                       help="A demo run is a few dozen parcels, not a day of deliveries. Scaled "
                            "up, it keeps its real split between blocks at a full day's volume."),
    "exclude_sessions": dict(section="runs", group="Real runs", label="Test runs", type=list,
                             default=[],
                             help="Sessions left out of training, e.g. fake_events.py or "
                                  "mqtt_test runs."),
    "test_days": dict(group="Data", label="Test period (days)", type=int, default=42,
                      min=7, max=120, step=1,
                      help="The most recent days are held out to measure accuracy."),
    "poisson_alpha": dict(group="Poisson regression", label="Regularisation (alpha)", type=float,
                          default=1e-3, min=0.0, max=10.0, step=0.001,
                          help="Higher shrinks the weights: smoother, less likely to overfit."),
    "poisson_max_iter": dict(group="Poisson regression", label="Max iterations", type=int,
                             default=2000, min=100, max=10000, step=100,
                             help="Raise it if training warns that it did not converge."),
    "xgb_n_estimators": dict(group="XGBoost", label="Number of trees", type=int, default=400,
                             min=10, max=2000, step=10,
                             help="More trees learn more detail but train slower."),
    "xgb_max_depth": dict(group="XGBoost", label="Max tree depth", type=int, default=4,
                          min=1, max=12, step=1,
                          help="Deeper trees capture more interactions but overfit sooner."),
    "xgb_learning_rate": dict(group="XGBoost", label="Learning rate", type=float, default=0.03,
                              min=0.001, max=1.0, step=0.005,
                              help="How much each tree corrects the previous ones. "
                                   "Lower needs more trees."),
    "xgb_subsample": dict(group="XGBoost", label="Row sample", type=float, default=0.8,
                          min=0.1, max=1.0, step=0.05,
                          help="Share of training rows each tree sees."),
    "xgb_colsample_bytree": dict(group="XGBoost", label="Column sample", type=float, default=0.8,
                                 min=0.1, max=1.0, step=0.05,
                                 help="Share of features each tree sees."),
    "parcels_per_courier": dict(group="Dashboard", label="Parcels per courier", type=int,
                                default=40, min=1, max=500, step=1,
                                help="Round size for one courier, used for the courier count. "
                                     "Takes effect without retraining."),
}

# Pretend data (simulate.py). Defaults are the simulator's original numbers, so the data
# only changes once someone saves new values and regenerates it. Blocks are fixed: they
# must match the printed labels and the gateway's zone map.
SIM_BLOCKS = {"123A": 18, "123B": 12, "124": 25, "125": 9, "126A": 15, "126B": 6}
SIM_WEEKDAYS = {"mon": 1.35, "tue": 1.10, "wed": 1.00, "thu": 1.00, "fri": 1.05, "sat": 0.80,
                "sun": 0.45}
SIM_RANDOMNESS = {"low": 50, "medium": 12, "high": 4}  # gamma shape: lower = burstier days
SIM = dict(section="sim", compact=True)
for _block, _rate in SIM_BLOCKS.items():
    PARAMS[f"sim_rate_{_block}"] = dict(**SIM, group="Block volume", label=_block, type=int,
                                        default=_rate, min=0, max=500, step=1,
                                        help=f"Average parcels a day for block {_block}.")
for _day, _mult in SIM_WEEKDAYS.items():
    PARAMS[f"sim_weekday_{_day}"] = dict(**SIM, group="Weekday pattern", label=_day.title(),
                                         type=float, default=_mult, min=0.0, max=5.0, step=0.05,
                                         help=f"{_day.title()} volume as a multiple of a normal day.")
PARAMS.update({
    "sim_sale_day": dict(**SIM, group="Sales, growth and history", label="Sale day ×", type=float,
                         default=2.2, min=0.5, max=5.0, step=0.1,
                         help="Volume on 9.9, 10.10, 11.11 and 12.12."),
    "sim_sale_day1": dict(**SIM, group="Sales, growth and history", label="Day after ×",
                          type=float, default=1.6, min=0.5, max=5.0, step=0.1,
                          help="Volume the day after a sale."),
    "sim_sale_day2": dict(**SIM, group="Sales, growth and history", label="2 days after ×",
                          type=float, default=1.3, min=0.5, max=5.0, step=0.1,
                          help="Volume two days after a sale."),
    "sim_growth_pct": dict(**SIM, group="Sales, growth and history", label="Growth per year (%)",
                           type=int, default=15, min=-50, max=200, step=1,
                           help="How much volume rises over a year."),
    "sim_randomness": dict(**SIM, group="Sales, growth and history", label="Day-to-day randomness",
                           default="medium",
                           choices={"low": "Low", "medium": "Medium", "high": "High"},
                           help="How far a day can stray from what's expected."),
    "sim_days": dict(**SIM, group="Sales, growth and history", label="History (days)", type=int,
                     default=365, min=120, max=1095, step=1,
                     help="How many days of pretend history to make, ending today."),
    "sim_unreadable_pct": dict(**SIM, group="Label problems", label="Unreadable (%)", type=int,
                               default=6, min=0, max=50, step=1,
                               help="No QR found. These have no block, so they lower the counts."),
    "sim_flip_pct": dict(**SIM, group="Label problems", label="Need a flip (%)", type=int,
                         default=15, min=0, max=50, step=1, help="Label facing down at first."),
    "sim_timeout_pct": dict(**SIM, group="Label problems", label="Time out (%)", type=int,
                            default=3, min=0, max=50, step=1,
                            help="No scan reaches Node 1 in time."),
    "sim_missed_pct": dict(**SIM, group="Label problems", label="Missed at bin (%)", type=int,
                           default=2, min=0, max=50, step=1,
                           help="The bin IR never sees the parcel."),
})
SIM_KEYS = [k for k, p in PARAMS.items() if p.get("section") == "sim"]

# one line under each group's heading on the Settings page
GROUPS = {
    "Block volume": "Average parcels a day per block, before the weekday, sale and growth effects.",
    "Weekday pattern": "Each day of the week as a multiple of a normal day (1.00).",
    "Sales, growth and history": "The rush after the 9.9, 10.10, 11.11 and 12.12 sales, growth "
                                 "over the year, how much days vary, and how much history to make.",
    "Label problems": "Share of pretend parcels with each problem. Only unreadable labels change "
                      "what the forecast learns from.",
}

DEFAULTS = {k: p["default"] for k, p in PARAMS.items()}
SCALE_REAL = {"day": True, "none": False}  # real_scale -> features.merge_history(scale=)


def validate(values: dict) -> dict:
    """Return the values coerced to their types; raise ValueError naming the first bad one."""
    clean = {}
    for key, value in values.items():
        p = PARAMS.get(key)
        if p is None:
            raise ValueError(f"unknown setting {key!r}")
        if "choices" in p:
            if value not in p["choices"]:
                raise ValueError(f"{p['label']}: must be one of {list(p['choices'])}")
            clean[key] = value
            continue
        if p["type"] is list:  # session names
            if not isinstance(value, list) or not all(isinstance(v, str) and 0 < len(v) <= 32
                                                      for v in value):
                raise ValueError(f"{p['label']}: must be a list of session names")
            clean[key] = sorted(set(value))
            continue
        if isinstance(value, bool) or not isinstance(value, (int, float, str)):
            raise ValueError(f"{p['label']}: must be a number")
        try:
            num = float(value)
        except ValueError:
            raise ValueError(f"{p['label']}: must be a number") from None
        if p["type"] is int and num != int(num):
            raise ValueError(f"{p['label']}: must be a whole number")
        if not p["min"] <= num <= p["max"]:
            raise ValueError(f"{p['label']}: must be between {p['min']} and {p['max']}")
        clean[key] = p["type"](num)
    return clean


def load_config() -> dict:
    """Defaults, overridden by whatever was saved (unknown or invalid keys are ignored)."""
    cfg = dict(DEFAULTS)
    if CONFIG_PATH.exists():
        saved = json.loads(CONFIG_PATH.read_text())
        for key, value in saved.items():
            try:
                cfg.update(validate({key: value}))
            except ValueError:
                pass
    return cfg


def save_config(changes: dict) -> dict:
    """Validate and merge `changes` into the saved settings; return the full config."""
    cfg = {**load_config(), **validate(changes)}
    if cfg["sim_days"] < cfg["test_days"] + 60:
        raise ValueError(f"History (days): must be at least the test period + 60 "
                         f"({cfg['test_days'] + 60}), or there's too little left to train on")
    CONFIG_PATH.write_text(json.dumps(cfg, indent=2))
    return cfg


def schema() -> list[dict]:
    """PARAMS as a JSON-safe list, in display order, for the Settings form."""
    return [{"key": k, "section": "model",
             **{f: (v.__name__ if f == "type" else v) for f, v in p.items()}}
            for k, p in PARAMS.items()]
