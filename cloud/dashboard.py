"""ZoneSort dashboard: live sorting counts, faults and the next-day forecast per block.

Reads the `parcels` / `faults` tables (filled by subscriber.py) and refreshes itself every
few seconds. "Simulated" mode reads data/sim_parcels.csv instead, for testing without the
database.

Usage:
    python -m streamlit run dashboard.py
"""
import json
import sys
from datetime import date, timedelta
from pathlib import Path

import pandas as pd
import streamlit as st

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "ml"))
from features import daily_counts, load_history, load_sim  # noqa: E402
from forecast import forecast  # noqa: E402
from train import MODEL_DIR  # noqa: E402

ZONES = ["A", "B", "MANUAL"]
ZONE_COLORS = ["#2a78d6", "#eb6834", "#1baf7a"]
MODEL_NAMES = {"poisson": "Poisson regression", "xgb": "XGBoost", "baseline": "Same-weekday average"}
REFRESH_S = 5
PARCELS_PER_COURIER = 40  # assumed round size for one courier; adjust to taste

st.set_page_config(page_title="ZoneSort", page_icon="📦", layout="wide")


@st.cache_data(ttl=REFRESH_S)
def load_live() -> tuple[pd.DataFrame, pd.DataFrame]:
    from db import get_engine

    engine = get_engine()
    parcels = pd.read_sql("SELECT * FROM parcels ORDER BY id", engine,
                          parse_dates=["arrived_at", "scanned_at", "sorted_at"])
    faults = pd.read_sql("SELECT * FROM faults ORDER BY id", engine, parse_dates=["at"])
    return parcels, faults


@st.cache_data
def load_simulated() -> tuple[pd.DataFrame, pd.DataFrame]:
    parcels = load_sim()
    for c in ("scanned_at", "sorted_at"):
        parcels[c] = pd.to_datetime(parcels[c])
    faults = pd.DataFrame(columns=["session", "source", "type", "seq", "detail", "at"])
    return parcels, faults


@st.cache_data(ttl=60)
def next_day_forecast(source: str, model: str) -> pd.DataFrame:
    daily = daily_counts(load_history(source), end=date.today())
    return forecast(daily, model, date.today() + timedelta(days=1))


# ---- sidebar ---------------------------------------------------------------
with st.sidebar:
    st.header("📦 ZoneSort")
    mode = st.radio("Data", ["Live database", "Simulated"],
                    help="Simulated reads data/sim_parcels.csv (run ml/simulate.py)")
    scope = st.radio("Show", ["Latest session", "Today", "All time"])
    model = st.selectbox("Forecast model", list(MODEL_NAMES), format_func=MODEL_NAMES.get)

try:
    parcels, faults = load_live() if mode == "Live database" else load_simulated()
except Exception as e:
    # not the message itself: a bad DB_URL can echo part of the password
    st.error(f"Can't load data ({type(e).__name__}).")
    st.info("Check DB_URL in cloud/.env, or switch to **Simulated** in the sidebar.")
    st.stop()


def in_scope(df: pd.DataFrame, time_col: str) -> pd.DataFrame:
    if df.empty or scope == "All time":
        return df
    if scope == "Latest session":
        return df[df["session"] == parcels["session"].iloc[-1]] if not parcels.empty else df
    return df[df[time_col].dt.date == date.today()]


@st.fragment(run_every=REFRESH_S if mode == "Live database" else None)
def live_panel() -> None:
    p, f = load_live() if mode == "Live database" else (parcels, faults)
    p, f = in_scope(p, "arrived_at"), in_scope(f, "at")
    label = (f"session {p['session'].iloc[-1]}" if scope == "Latest session" and not p.empty
             else scope.lower())
    st.caption(f"{len(p)} parcels · {label} · refreshed {pd.Timestamp.now():%H:%M:%S}")

    routed = p["routed"].fillna(p["zone"])
    cols = st.columns(7)
    cols[0].metric("Parcels", len(p))
    for col, z in zip(cols[1:4], ZONES):
        col.metric(f"Zone {z}" if z != "MANUAL" else "Manual", int((routed == z).sum()))
    cols[4].metric("Unreadable / unknown", int(p["scan"].isin(["unreadable", "unknown"]).sum()))
    cols[5].metric("Timeouts", int((p["reason"] == "timeout").sum()))
    cols[6].metric("Faults", len(f), help="jams, missed IR, repeated scan failures …")

    left, right = st.columns(2)
    with left:
        st.subheader("Parcels by block")
        by_block = (p.assign(routed=routed, block=p["block"].fillna("(no label)"))
                    .pivot_table(index="block", columns="routed", values="seq",
                                 aggfunc="count", fill_value=0)
                    .reindex(columns=ZONES, fill_value=0))
        if by_block.empty:
            st.write("No parcels yet.")
        else:
            st.bar_chart(by_block, color=ZONE_COLORS, x_label="Block", y_label="Parcels")
    with right:
        st.subheader("Arrivals over time")
        if p.empty:
            st.write("No parcels yet.")
        else:
            span = p["arrived_at"].max() - p["arrived_at"].min()
            freq = "1min" if span < pd.Timedelta(hours=2) else "1h" if span < pd.Timedelta(days=2) else "1D"
            series = p.set_index("arrived_at")["seq"].resample(freq).count().rename("Parcels")
            st.line_chart(series, color=ZONE_COLORS[0], x_label="", y_label=f"Parcels per {freq}")

    left, right = st.columns(2)
    with left:
        st.subheader("Faults")
        if f.empty:
            st.success("No faults.")
        else:
            st.dataframe(f.sort_values("at", ascending=False)[["at", "source", "type", "seq", "detail"]],
                         hide_index=True, width="stretch")
    with right:
        st.subheader("Recent parcels")
        st.dataframe(p.tail(50).iloc[::-1][["seq", "parcel_id", "block", "floor", "scan", "zone",
                                            "routed", "reason", "result", "arrived_at"]],
                     hide_index=True, width="stretch")


st.title("ZoneSort")
live_panel()

# ---- forecast ----------------------------------------------------------------
st.divider()
tomorrow = date.today() + timedelta(days=1)
st.subheader(f"Forecast for {tomorrow:%A %d %b}")
try:
    source = "both" if mode == "Live database" else "sim"
    fc = next_day_forecast(source, model)
except Exception as e:
    st.warning(f"No forecast ({type(e).__name__})."
               + (f" {e}" if isinstance(e, FileNotFoundError) else ""))
else:
    total = fc["predicted"].sum()
    busiest = fc.loc[fc["predicted"].idxmax()]
    c1, c2, c3 = st.columns(3)
    c1.metric("Expected parcels", f"{total:.0f}",
              delta=f"{total - fc['dow_avg'].sum():+.0f} vs usual {tomorrow:%A}",
              delta_color="off")
    c2.metric("Busiest block", busiest["block"], f"{busiest['predicted']:.0f} parcels",
              delta_color="off")
    c3.metric(f"Couriers (≈ {PARCELS_PER_COURIER} parcels each)",
              f"{max(1, -(-int(round(total)) // PARCELS_PER_COURIER))}")

    chart = (fc.set_index("block")[["predicted", "dow_avg"]]
             .rename(columns={"predicted": MODEL_NAMES[model], "dow_avg": f"Usual {tomorrow:%A}"}))
    st.bar_chart(chart, color=ZONE_COLORS[:2], stack=False, x_label="Block",
                 y_label="Parcels")
    with st.expander("Forecast table and model accuracy"):
        st.dataframe(fc[["block", "predicted", "dow_avg", "lag1", "lag7"]].round(1)
                     .rename(columns={"dow_avg": "same-weekday avg", "lag1": "today",
                                      "lag7": "same day last week"}),
                     hide_index=True)
        metrics_file = MODEL_DIR / "metrics.json"
        if metrics_file.exists():
            m = json.loads(metrics_file.read_text())
            st.caption(f"Held-out test: last {m['test_days']} days (data: {m['source']}, "
                       f"trained to {m['trained_until']}). MAE = parcels per block per day; "
                       "busiest = share of days the busiest block was picked correctly.")
            st.dataframe(pd.DataFrame(m["models"]).T.rename(index=MODEL_NAMES).round(3))
