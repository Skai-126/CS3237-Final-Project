"""JSON endpoints for the React dashboard.

    GET /api/live?source=db|sim&scope=latest|today|all
    GET /api/forecast?source=db|sim&model=poisson|xgb|baseline
"""
import logging

from django.db import DatabaseError
from django.http import JsonResponse

from . import data

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
