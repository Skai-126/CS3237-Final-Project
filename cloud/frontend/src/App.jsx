import { useState } from "react";
import { usePolling } from "./api.js";
import { ArrivalsChart, ByBlockChart, ForecastChart } from "./components/charts.jsx";
import { ModelInfoModal } from "./components/modelInfo.jsx";
import { Card, Empty, Segmented, Table, Tile, fmtTime } from "./components/ui.jsx";
import { usePalette } from "./theme.js";

const LIVE_MS = 5000;
const FORECAST_MS = 60000;
const PARCELS_PER_COURIER = 40; // assumed round size for one courier; adjust to taste

const MODELS = { poisson: "Poisson regression", xgb: "XGBoost", baseline: "Same-weekday average" };
const SCOPES = [["latest", "Latest session"], ["today", "Today"], ["all", "All time"]];
const SOURCES = [["db", "Live database"], ["sim", "Simulated"]];
const SOURCE_TEXT = { sim: "simulated data", db: "real data", both: "simulated + real data" };

export default function App() {
  const [source, setSource] = useState("db");
  const [scope, setScope] = useState("latest");
  const [model, setModel] = useState("poisson");
  const pal = usePalette();

  const live = usePolling("/api/live", { source, scope }, source === "db" ? LIVE_MS : 0);
  const fc = usePolling("/api/forecast", { source, model }, FORECAST_MS);

  return (
    <div className="page">
      <header className="top">
        <div className="brand">
          <h1>ZoneSort</h1>
          <span className={`live-dot ${live.error ? "down" : ""}`} aria-hidden />
          <span className="muted">
            {live.error ? "Not updating" : live.at ? `Updated ${fmtTime(live.at.toISOString())}` : "Loading…"}
          </span>
        </div>
        <div className="controls">
          <Segmented label="Data source" value={source} options={SOURCES} onChange={setSource} />
          <Segmented label="Time range" value={scope} options={SCOPES} onChange={setScope} />
        </div>
      </header>

      {live.error && <div className="banner">⚠ {live.error}</div>}
      {live.data && <LiveSection d={live.data} pal={pal} />}

      <ForecastSection fc={fc} model={model} setModel={setModel} pal={pal} />
    </div>
  );
}

function LiveSection({ d, pal }) {
  const k = d.kpis;
  const scopeText =
    d.scope === "latest" ? (d.session ? `session ${d.session}` : "no sessions yet")
      : d.scope === "today" ? "today" : "all time";
  const withDate = d.scope === "all";
  return (
    <>
      <p className="muted scope-line">{k.parcels} parcels · {scopeText}</p>
      <div className="tiles">
        <Tile label="Parcels" value={k.parcels} />
        <Tile label="Zone A" value={k.A} swatch={pal.A} hint="Floors 1–5" />
        <Tile label="Zone B" value={k.B} swatch={pal.B} hint="Floors 6–10" />
        <Tile label="Manual" value={k.MANUAL} swatch={pal.MANUAL} hint="Unreadable, unknown or timed out" />
        <Tile label="Unreadable / unknown" value={k.unreadable} />
        <Tile label="Timeouts" value={k.timeouts} hint="No scan reached Node 1 in time" />
        <Tile label="Missed at bin" value={k.missed} hint="Bin IR never saw the parcel" />
        <Tile label={k.faults ? "⚠ Faults" : "Faults"} value={k.faults} tone={k.faults ? "critical" : ""}
              hint="Jams, missed IR, repeated scan failures…" />
      </div>

      <div className="grid">
        <Card title="Parcels by block" subtitle="stacked by the gate that fired">
          {d.by_block.length ? <ByBlockChart rows={d.by_block} pal={pal} /> : <Empty>No parcels yet.</Empty>}
        </Card>
        <Card title="Arrivals over time" subtitle={d.arrivals.freq ? `per ${d.arrivals.freq}` : ""}>
          {d.arrivals.points.length
            ? <ArrivalsChart points={d.arrivals.points} freq={d.arrivals.freq} pal={pal} />
            : <Empty>No parcels yet.</Empty>}
        </Card>
        <Card title="Faults" subtitle={d.faults.length ? `latest ${d.faults.length}` : ""}>
          <Table
            rows={d.faults}
            empty="✓ No faults."
            columns={[
              ["at", "Time", (v) => fmtTime(v, withDate)],
              ["source", "Source"], ["type", "Type"], ["seq", "Parcel"], ["detail", "Detail"],
            ]}
          />
        </Card>
        <Card title="Recent parcels" subtitle={d.recent.length ? `latest ${d.recent.length}` : ""}>
          <Table
            rows={d.recent}
            columns={[
              ["seq", "#"], ["parcel_id", "Parcel"], ["block", "Block"], ["floor", "Floor"],
              ["scan", "Scan"],
              ["routed", "Gate", (v, r) => <ZoneTag zone={v ?? r.zone} pal={pal} pending={!v} />],
              ["reason", "Reason"], ["result", "Result"],
              ["arrived_at", "Arrived", (v) => fmtTime(v, withDate)],
            ]}
          />
        </Card>
      </div>
    </>
  );
}

function ZoneTag({ zone, pal, pending }) {
  if (!zone) return "—";
  return (
    <span className="zone-tag" title={pending ? "Scanned, not sorted yet" : undefined}>
      <span className="swatch" style={{ background: pal[zone] }} />
      {zone === "MANUAL" ? "Manual" : zone}
      {pending && <span className="muted"> …</span>}
    </span>
  );
}

function ForecastSection({ fc, model, setModel, pal }) {
  const [infoOpen, setInfoOpen] = useState(false);
  const d = fc.data;
  const date = d ? new Date(d.date) : null;
  const weekday = date?.toLocaleDateString("en-SG", { weekday: "long" });
  const total = d ? d.blocks.reduce((s, b) => s + b.predicted, 0) : 0;
  const usual = d ? d.blocks.reduce((s, b) => s + (b.dow_avg ?? 0), 0) : 0;
  const busiest = d?.blocks.reduce((a, b) => (b.predicted > a.predicted ? b : a), d.blocks[0]);

  return (
    <section className="forecast">
      <header className="forecast-head">
        <h2>
          Forecast for{" "}
          {date ? date.toLocaleDateString("en-SG", { weekday: "long", day: "numeric", month: "short" }) : "tomorrow"}
        </h2>
        <div className="forecast-controls">
          <label className="select">
            Model
            <select value={model} onChange={(e) => setModel(e.target.value)}>
              {Object.entries(MODELS).map(([v, t]) => <option key={v} value={v}>{t}</option>)}
            </select>
          </label>
          <button className="info-btn" onClick={() => setInfoOpen(true)}>ⓘ How the models work</button>
        </div>
      </header>
      <ModelInfoModal open={infoOpen} onClose={() => setInfoOpen(false)}
                      model={model} setModel={setModel} metrics={d?.metrics} />

      {fc.error && <div className="banner">⚠ {fc.error}</div>}
      {d && (
        <>
          <div className="tiles">
            <Tile label="Expected parcels" value={Math.round(total)} />
            <Tile label={`Vs. usual ${weekday}`} value={`${total - usual >= 0 ? "+" : ""}${Math.round(total - usual)}`} />
            <Tile label="Busiest block" value={busiest ? `${busiest.block} · ${Math.round(busiest.predicted)}` : "—"} />
            <Tile label={`Couriers (≈ ${PARCELS_PER_COURIER} parcels each)`}
                  value={Math.max(1, Math.ceil(Math.round(total) / PARCELS_PER_COURIER))} />
          </div>
          <div className="grid">
            <Card title="Parcels per block">
              <ForecastChart rows={d.blocks} modelName={MODELS[model]} weekday={weekday} pal={pal} />
            </Card>
            <Card title="Model accuracy" subtitle={d.metrics ? `last ${d.metrics.test_days} days held out` : ""}>
              {d.metrics ? (
                <>
                  <Table
                    rows={Object.entries(d.metrics.models).map(([name, m]) => ({ name, ...m }))}
                    columns={[
                      ["name", "Model", (v) => <span className={v === model ? "strong" : ""}>{MODELS[v] ?? v}</span>],
                      ["mae", "MAE", (v) => v.toFixed(2)],
                      ["rmse", "RMSE", (v) => v.toFixed(2)],
                      ["busiest", "Busiest block right", (v) => `${Math.round(v * 100)}%`],
                    ]}
                  />
                  <p className="muted small">
                    MAE = average miss in parcels per block per day. Trained on {SOURCE_TEXT[d.metrics.source]} up
                    to {d.metrics.trained_until}.
                  </p>
                </>
              ) : (
                <Empty>Run <code>python ml/train.py</code> to see accuracy.</Empty>
              )}
              <Table
                rows={d.blocks}
                columns={[
                  ["block", "Block"],
                  ["predicted", "Forecast", (v) => v.toFixed(1)],
                  ["dow_avg", `Usual ${weekday}`, (v) => v?.toFixed(1) ?? "—"],
                  ["lag1", "Today", (v) => v ?? "—"],
                  ["lag7", "Last week", (v) => v ?? "—"],
                ]}
              />
            </Card>
          </div>
        </>
      )}
    </section>
  );
}
