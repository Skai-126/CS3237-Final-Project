"""JSON endpoints for the React dashboard.

    GET /api/live?source=db|sim&scope=latest|today|all
    GET /api/forecast?source=db|sim&model=poisson|xgb|baseline
    GET/POST /api/ml/config     training settings (POST a JSON object of changes)
    GET/POST /api/ml/train      retraining status / start a run ({"regenerate": true} remakes
                                the pretend data first)
    GET /api/ml/sessions        recorded sessions, for marking test runs
"""
import json
import logging

from django.db import DatabaseError
from django.http import JsonResponse
from django.views.decorators.http import require_http_methods

from simulate import read_meta
from train_config import DEFAULTS, GROUPS, load_config, save_config, schema

from . import data, training

log = logging.getLogger(__name__)
SOURCES, SCOPES, MODELS = {"db", "sim"}, {"latest", "today", "all"}, {"poisson", "xgb", "baseline"}


def _arg(request, name: str, allowed: set, default: str) -> str:
    value = request.GET.get(name, default)
    if value not in allowed:
        raise ValueError(f"{name} must be one of {sorted(allowed)}")
    return value


def _call(fn, *args) -> JsonResponse:
    try:
        return JsonResponse(fn(*args))
    except DatabaseError as e:
        # not str(e): a bad DB_URL can echo part of the password
        log.warning("database error: %s", type(e).__name__)
        return JsonResponse({"error": f"Database unavailable ({type(e).__name__}). "
                                      "Check DB_URL in cloud/.env."}, status=503)
    except FileNotFoundError as e:  # simulated data or trained models missing
        return JsonResponse({"error": str(e)}, status=503)


def live(request):
    try:
        args = _arg(request, "source", SOURCES, "db"), _arg(request, "scope", SCOPES, "latest")
    except ValueError as e:
        return JsonResponse({"error": str(e)}, status=400)
    return _call(data.live, *args)


def forecast(request):
    try:
        args = _arg(request, "source", SOURCES, "db"), _arg(request, "model", MODELS, "poisson")
    except ValueError as e:
        return JsonResponse({"error": str(e)}, status=400)
    return _call(data.next_day, *args)


@require_http_methods(["GET", "POST"])
def ml_config(request):
    if request.method == "POST":
        try:
            changes = json.loads(request.body)
            if not isinstance(changes, dict):
                raise ValueError("send a JSON object of settings")
            save_config(changes)
        except ValueError as e:  # bad JSON or an out-of-range value
            return JsonResponse({"error": str(e)}, status=400)
        data.clear_forecast_cache()  # test runs and scaling change the forecast's input
    metrics = training.saved_metrics()
    return JsonResponse({"values": load_config(), "defaults": DEFAULTS, "params": schema(),
                         "groups": GROUPS, "sim_made_with": read_meta()["settings"],
                         "trained_with": metrics.get("config") if metrics else None})


@require_http_methods(["GET", "POST"])
def ml_train(request):
    if request.method == "POST":
        try:
            regen = bool(json.loads(request.body or b"{}").get("regenerate"))
        except (ValueError, AttributeError):
            return JsonResponse({"error": 'send {"regenerate": true} or nothing'}, status=400)
        if not training.start(regen):
            return JsonResponse({"error": "Training is already running."}, status=409)
    return JsonResponse(training.status())


def ml_sessions(request):
    return _call(lambda: {"sessions": data.sessions()})
