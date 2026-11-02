"""Retraining from the dashboard: one background run at a time, with a status to poll.
A run can first remake the pretend data from the saved sim_* settings.

The dashboard keeps serving forecasts from the old models while a run is going;
train.run() only swaps the files in once every model has trained.
"""
import json
import logging
import threading
from datetime import datetime

from simulate import regenerate
from train import MODEL_DIR, run
from train_config import load_config

from . import data

log = logging.getLogger(__name__)
_lock = threading.Lock()
_status = {"state": "idle", "started_at": None, "finished_at": None, "log": [],
           "error": None, "previous": None, "metrics": None, "regenerated": False}


def _now() -> str:
    return datetime.now().isoformat(timespec="seconds")


def saved_metrics() -> dict | None:
    path = MODEL_DIR / "metrics.json"
    return json.loads(path.read_text()) if path.exists() else None


def status() -> dict:
    with _lock:
        return {**_status, "log": list(_status["log"])}


def start(regen: bool = False) -> bool:
    """Start a run with the saved settings (first remaking the pretend data if `regen`);
    False if one is already running."""
    with _lock:
        if _status["state"] == "running":
            return False
        _status.update(state="running", started_at=_now(), finished_at=None, log=[],
                       error=None, previous=saved_metrics(), metrics=None, regenerated=regen)
    threading.Thread(target=_work, args=(load_config(), regen), daemon=True).start()
    return True


def _work(cfg: dict, regen: bool) -> None:
    def note(line: str) -> None:
        with _lock:
            _status["log"].append(line)

    try:
        if regen:
            note(f"making {cfg['sim_days']} days of pretend data")
            regenerate(cfg)
            data.clear_caches()  # the Simulated view and the forecast read the new file
        metrics = run(cfg, log=note)
    except Exception as e:  # report it on the page instead of losing it in the thread
        log.exception("training failed")
        with _lock:
            _status.update(state="error", finished_at=_now(), error=f"{type(e).__name__}: {e}")
        return
    data.clear_caches()  # next forecast request uses the new models
    with _lock:
        _status.update(state="done", finished_at=_now(), metrics=metrics)
