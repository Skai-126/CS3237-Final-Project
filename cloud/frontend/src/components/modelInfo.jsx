import { Modal } from "./ui.jsx";

const SOURCE_TEXT = { sim: "simulated parcels", db: "real parcels", both: "simulated + real parcels" };

const CLUES = [
  ["Which block", "e.g. 123A vs 124 — some blocks just get more parcels."],
  ["Day of the week", "Mondays can be busier than Sundays."],
  ["Sale days", "9.9, 10.10, 11.11, 12.12 and the 2 days after bring a wave of parcels."],
  ["Recent counts", "Yesterday, the same day last week, and the 7- and 28-day averages."],
];

const EXPLAINERS = [
  {
    key: "baseline",
    name: "Same-weekday average",
    tagline: "What usually happens on this weekday",
    how: "No learning at all. To guess next Monday, it averages the last 4 Mondays.",
    analogy: "“Mondays have been about 30 lately, so I'll say 30.”",
    good: "Simple and easy to explain.",
    bad: "Ignores everything else. It doesn't know tomorrow is 11.11.",
    note: "It's the yardstick: a smart model that can't beat it isn't worth using.",
  },
  {
    key: "poisson",
    name: "Poisson regression",
    tagline: "A weighted checklist",
    how: "Learns from history how much each clue matters and gives it a weight, e.g. “a sale "
      + "day roughly doubles the count”. “Poisson” means it's built for counting things, so "
      + "it never guesses a negative number.",
    analogy: "An experienced manager: “normal Monday is 25, sale day so double it, busy week so add a few.”",
    good: "Uses every clue, still explainable, works well with little data.",
    bad: "Each clue has a fixed effect, so it misses combinations like “sales only matter for 123A”.",
  },
  {
    key: "xgb",
    name: "XGBoost",
    tagline: "Hundreds of small rules of thumb",
    how: "Builds 400 small yes/no flowcharts (“Sale day? → Block 124? → Yesterday above 40?”). "
      + "Each new one fixes the mistakes of the ones before, and their answers are added up.",
    analogy: "A committee of 400, each knowing one small pattern, the later ones focused on "
      + "cases the earlier ones got wrong.",
    good: "The most flexible. Finds combinations of clues the checklist can't.",
    bad: "Hard to explain a single guess, and needs lots of history or it learns noise.",
  },
];

export function ModelInfoModal({ open, onClose, model, setModel, metrics }) {
  const scores = metrics?.models ?? {};
  const best = Object.entries(scores).sort((a, b) => a[1].mae - b[1].mae)[0]?.[0];

  return (
    <Modal open={open} onClose={onClose} title="How the forecast models work">
      <p>
        All three models answer the same question: <strong>how many parcels will each block get
        tomorrow?</strong> The forecast above, and the courier count, come from the one you pick.
      </p>

      <h3>The clues they all get</h3>
      <dl className="clues">
        {CLUES.map(([t, d]) => (
          <div key={t}><dt>{t}</dt><dd>{d}</dd></div>
        ))}
      </dl>
      <p className="muted small">Every clue only uses days up to today, so the forecast can be made the evening before.</p>

      <h3>The three models</h3>
      <div className="model-cards">
        {EXPLAINERS.map((m) => {
          const s = scores[m.key];
          const active = m.key === model;
          return (
            <article key={m.key} className={`model-card ${active ? "active" : ""}`}>
              <header>
                <div>
                  <h4>{m.name}</h4>
                  <span className="muted">{m.tagline}</span>
                </div>
                {active
                  ? <span className="pill">In use</span>
                  : <button className="link-btn" onClick={() => setModel(m.key)}>Use this model</button>}
              </header>
              <p>{m.how}</p>
              <p className="analogy">{m.analogy}</p>
              <ul className="pros-cons">
                <li><span className="good">＋</span>{m.good}</li>
                <li><span className="bad">−</span>{m.bad}</li>
              </ul>
              {m.note && <p className="muted small">{m.note}</p>}
              {s && (
                <p className="score">
                  Off by about <strong>{s.mae.toFixed(1)} parcels</strong> per block per day ·
                  picked the busiest block on <strong>{Math.round(s.busiest * 100)}%</strong> of days
                  {m.key === best && <span className="pill good-pill">Most accurate</span>}
                </p>
              )}
            </article>
          );
        })}
      </div>

      {metrics && (
        <p className="muted small">
          Scores come from the last {metrics.test_days} days of {SOURCE_TEXT[metrics.source] ?? "data"},
          which the models never saw while learning (trained up to {metrics.trained_until}).
          {metrics.source === "sim" && " Re-run python ml/train.py once there's enough real data."}
        </p>
      )}
    </Modal>
  );
}
