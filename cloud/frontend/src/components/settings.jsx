import { useEffect, useState } from "react";
import { getJson, postJson } from "../api.js";
import { Card, Empty, Table, fmtTime } from "./ui.jsx";

const MODEL_NAMES = { poisson: "Poisson regression", xgb: "XGBoost", baseline: "Same-weekday average" };
const POLL_MS = 1000;

/** Model settings, pretend-data settings, test runs, retrain buttons and before/after accuracy. */
export function SettingsPage({ onSaved, onTrained }) {
  const [cfg, setCfg] = useState(null);     // {values, defaults, params, groups, sim_made_with, trained_with}
  const [form, setForm] = useState({});     // key -> value as typed
  const [error, setError] = useState(null);
  const [busy, setBusy] = useState(false);
  const [notice, setNotice] = useState(null);
  const [train, setTrain] = useState(null); // /api/ml/train status

  const load = (c) => { setCfg(c); setForm(c.values); };
  useEffect(() => {
    getJson("/api/ml/config").then(load).catch((e) => setError(e.message));
    getJson("/api/ml/train").then(setTrain).catch(() => {});
  }, []);

  // poll while a run is going; refresh everything once it ends
  const running = train?.state === "running";
  useEffect(() => {
    if (!running) return;
    const t = setInterval(() => getJson("/api/ml/train").then((s) => {
      setTrain(s);
      if (s.state !== "running") {
        getJson("/api/ml/config").then(load);
        if (s.state === "done") onTrained?.();
      }
    }).catch(() => {}), POLL_MS);
    return () => clearInterval(t);
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [running]);

  if (!cfg) return error ? <div className="banner">⚠ {error}</div> : <Empty>Loading settings…</Empty>;

  const changed = cfg.params.filter((p) => String(form[p.key]) !== String(cfg.values[p.key]));
  const same = (a, b) => String(a) === String(b);
  const section = (s) => cfg.params.filter((p) => p.section === s);
  const tuning = section("model");
  const sim = section("sim");
  // each section resets on its own; the test-run list is never reset
  const atDefaults = (ps) => ps.every((p) => same(form[p.key], cfg.defaults[p.key]));
  const reset = (ps) => () => setForm((f) => ({ ...f, ...Object.fromEntries(ps.map((p) => [p.key, cfg.defaults[p.key]])) }));
  const set = (key) => (v) => setForm((f) => ({ ...f, [key]: v }));
  // pretend-data settings saved, or typed, but not yet used to remake the data
  const simSaved = sim.some((p) => !same(cfg.values[p.key], cfg.sim_made_with[p.key]));
  const simTyped = sim.some((p) => !same(form[p.key], cfg.sim_made_with[p.key]));

  // then: null = only save, "train" = retrain, "regen" = remake the pretend data and retrain
  const save = async (then) => {
    setBusy(true); setError(null); setNotice(null);
    try {
      if (changed.length) {
        const body = Object.fromEntries(changed.map((p) => [
          p.key, p.choices || p.type === "list" ? form[p.key] : Number(form[p.key])]));
        load(await postJson("/api/ml/config", body));
        onSaved?.();
      }
      if (then) setTrain(await postJson("/api/ml/train", { regenerate: then === "regen" }));
      else setNotice(changed.some((p) => p.section === "sim")
        ? "Saved. Click Regenerate and retrain to remake the pretend data."
        : "Saved. Retrain to apply the model settings.");
    } catch (e) {
      setError(e.message);
    } finally {
      setBusy(false);
    }
  };

  const groups = (ps) => [...new Set(ps.map((p) => p.group))];
  return (
    <div className="settings">
      <header className="settings-head">
        <div>
          <h2>Model settings</h2>
          <p className="muted small">
            Model settings take effect when you retrain. Parcels per courier applies as soon as you save.
          </p>
        </div>
        <div className="settings-actions">
          <button className="info-btn" disabled={busy || atDefaults(tuning)}
                  onClick={reset(tuning)}>Reset to defaults</button>
          <button className="info-btn" disabled={busy || !changed.length} onClick={() => save(false)}>
            Save
          </button>
          <button className="primary-btn" disabled={busy || running} onClick={() => save("train")}>
            {running ? "Training…" : changed.length ? "Save and retrain" : "Retrain"}
          </button>
        </div>
      </header>

      {error && <div className="banner">⚠ {error}</div>}
      {notice && <div className="banner ok">{notice}</div>}

      <div className="grid">
        {groups(tuning).map((g) => (
          <Card key={g} title={g}>
            <div className="fields">
              {tuning.filter((p) => p.group === g).map((p) => (
                <Field key={p.key} p={p} value={form[p.key]} trainedWith={cfg.trained_with?.[p.key]}
                       onChange={set(p.key)} />
              ))}
            </div>
          </Card>
        ))}
      </div>

      <section className="settings-section" aria-labelledby="pretend-data">
        <header className="settings-head">
          <div>
            <h2 id="pretend-data">Pretend data</h2>
            <p className="muted small">
              The simulated history that fills every day the sorter didn't run. Changing it remakes the whole
              history, so the scores change too. Hover a box for what it means.
            </p>
          </div>
          <div className="settings-actions">
            <button className="info-btn" disabled={busy || atDefaults(sim)} onClick={reset(sim)}>
              Reset to defaults
            </button>
            <button className="primary-btn" disabled={busy || running} onClick={() => save("regen")}>
              {running ? "Working…" : "Regenerate and retrain"}
            </button>
          </div>
        </header>
        {(simSaved || simTyped) && (
          <div className="banner note">
            Highlighted values differ from the current pretend data. Click <strong>Regenerate and
            retrain</strong> to use them.
          </div>
        )}
        <div className="grid">
          {groups(sim).map((g) => (
            <Card key={g} title={g}>
              <p className="muted small group-help">{cfg.groups[g]}</p>
              <div className="compact-fields">
                {sim.filter((p) => p.group === g).map((p) => (
                  <CompactField key={p.key} p={p} value={form[p.key]} madeWith={cfg.sim_made_with[p.key]}
                                onChange={set(p.key)} />
                ))}
              </div>
            </Card>
          ))}
        </div>
      </section>

      <TestRuns excluded={form.exclude_sessions ?? []} onChange={set("exclude_sessions")} />

      <TrainingResult train={train} />
    </div>
  );
}

function Field({ p, value, trainedWith, onChange }) {
  const id = `set-${p.key}`;
  const show = (v) => (p.choices ? p.choices[v] : v);
  const outOfRange = !p.choices && (value === "" || Number(value) < p.min || Number(value) > p.max);
  const notes = [`Default ${show(p.default)}`];
  if (trainedWith !== undefined && p.group !== "Dashboard" && String(trainedWith) !== String(value))
    notes.push(`current models use ${show(trainedWith)}`);
  return (
    <div className="field">
      <label htmlFor={id}>{p.label}</label>
      {p.choices ? (
        <select id={id} value={value} onChange={(e) => onChange(e.target.value)}>
          {Object.entries(p.choices).map(([v, t]) => <option key={v} value={v}>{t}</option>)}
        </select>
      ) : (
        <input id={id} type="number" value={value} min={p.min} max={p.max} step={p.step}
               aria-invalid={outOfRange} onChange={(e) => onChange(e.target.value)} />
      )}
      <p className="muted field-help">
        {p.help} <span className={notes.length > 1 ? "changed" : ""}>{notes.join(" · ")}</span>
      </p>
    </div>
  );
}

/** Small labelled box for the pretend-data grids; the help text is its tooltip. */
function CompactField({ p, value, madeWith, onChange }) {
  const id = `set-${p.key}`;
  const show = (v) => (p.choices ? p.choices[v] : v);
  const outOfRange = !p.choices && (value === "" || Number(value) < p.min || Number(value) > p.max);
  const differs = String(value) !== String(madeWith);
  const tip = `${p.help} Default ${show(p.default)}.${differs ? ` Current data uses ${show(madeWith)}.` : ""}`;
  return (
    <div className={`compact-field ${differs ? "pending" : ""}`} title={tip}>
      <label htmlFor={id}>{p.label}</label>
      {p.choices ? (
        <select id={id} value={value} onChange={(e) => onChange(e.target.value)}>
          {Object.entries(p.choices).map(([v, t]) => <option key={v} value={v}>{t}</option>)}
        </select>
      ) : (
        <input id={id} type="number" value={value} min={p.min} max={p.max} step={p.step}
               aria-invalid={outOfRange} aria-description={tip} onChange={(e) => onChange(e.target.value)} />
      )}
    </div>
  );
}

/** Recorded sessions, each with a tick box to leave it out of training as a test run. */
function TestRuns({ excluded, onChange }) {
  const [sessions, setSessions] = useState(null);
  const [error, setError] = useState(null);
  useEffect(() => {
    getJson("/api/ml/sessions").then((d) => setSessions(d.sessions)).catch((e) => setError(e.message));
  }, []);
  const toggle = (s) => onChange(excluded.includes(s) ? excluded.filter((x) => x !== s) : [...excluded, s]);
  const used = sessions?.filter((s) => !excluded.includes(s.session)) ?? [];
  return (
    <Card title="Real runs" subtitle={sessions ? `${used.length} of ${sessions.length} used for training` : ""}>
      <p className="muted small">
        Tick the test runs (<code>fake_events.py</code>, the <code>mqtt_test</code> sketch, bring-up). They stay
        on the dashboard but aren't used for training or the forecast. Save, then retrain.
      </p>
      {error ? <div className="banner">⚠ {error}</div> : !sessions ? <Empty>Loading…</Empty> : (
        <Table
          rows={sessions}
          empty="No runs recorded yet."
          columns={[
            ["session", "Test run", (v) => (
              <input type="checkbox" aria-label={`Session ${v} is a test run`}
                     checked={excluded.includes(v)} onChange={() => toggle(v)} />
            )],
            ["first", "Started", (v) => fmtTime(v, true)],
            ["parcels", "Parcels"],
            ["blocks", "Blocks", (v) => v.join(", ") || "—"],
            ["last", "Session", (_, r) => <span className="muted">{r.session}</span>],
          ]}
        />
      )}
    </Card>
  );
}

function TrainingResult({ train }) {
  if (!train || train.state === "idle") return null;
  const { state, previous, metrics } = train;
  const dataChanged = previous && metrics && [
    previous.trained_until !== metrics.trained_until
      && `data up to ${previous.trained_until} → ${metrics.trained_until}`,
    previous.source !== metrics.source && `training data ${previous.source} → ${metrics.source}`,
    previous.test_days !== metrics.test_days && `test period ${previous.test_days} → ${metrics.test_days} days`,
    train.regenerated && "pretend data remade",
  ].filter(Boolean).join(", ");
  const delta = (name, key) => {
    const before = previous?.models?.[name]?.[key];
    const after = metrics?.models?.[name]?.[key];
    if (after === undefined) return "—";
    const pct = key === "busiest";
    const fmt = (v) => (pct ? `${Math.round(v * 100)}%` : v.toFixed(2));
    if (before === undefined) return fmt(after);
    const better = pct ? after > before : after < before;
    const same = Math.abs(after - before) < 0.005;
    return (
      <span>
        {fmt(before)} → <span className={same ? "" : better ? "good-text" : "bad-text"}>{fmt(after)}</span>
      </span>
    );
  };
  return (
    <Card title="Last training run"
          subtitle={state === "running" ? `started ${fmtTime(train.started_at)}`
            : `finished ${fmtTime(train.finished_at)}`}>
      {state === "running" && <p className="small">Training… {train.log.at(-1) ?? ""}</p>}
      {state === "error" && <div className="banner">⚠ Training failed: {train.error}. The previous models are still in use.</div>}
      {state === "done" && (
        <>
          <Table
            rows={Object.keys(metrics.models).map((name) => ({ name }))}
            columns={[
              ["name", "Model", (v) => MODEL_NAMES[v] ?? v],
              ["mae", "MAE", (_, r) => delta(r.name, "mae")],
              ["rmse", "RMSE", (_, r) => delta(r.name, "rmse")],
              ["busiest", "Busiest block right", (_, r) => delta(r.name, "busiest")],
            ]}
          />
          <p className="muted small">
            Previous run → this run, on the last {metrics.test_days} days held out. Lower MAE and RMSE are
            better; green means this run did better.
          </p>
          {dataChanged && (
            <p className="small">
              ⚠ The two runs were scored on different days ({dataChanged}), so part of the change comes
              from the data, not the settings. The same-weekday average has no settings: how much it
              moved is how much the data alone moved the scores.
            </p>
          )}
        </>
      )}
    </Card>
  );
}
