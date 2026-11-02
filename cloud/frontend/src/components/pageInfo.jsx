import { Modal } from "./ui.jsx";

/** "About this page" pop-up: explains the page currently open in the nav bar. */
export function PageInfoModal({ page, open, onClose }) {
  return page === "settings" ? (
    <Modal open={open} onClose={onClose} title="About Settings">
      <SettingsInfo />
    </Modal>
  ) : (
    <Modal open={open} onClose={onClose} title="About the dashboard">
      <DashboardInfo />
    </Modal>
  );
}

function Terms({ items }) {
  return (
    <dl className="clues">
      {items.map(([t, d]) => (
        <div key={t}><dt>{t}</dt><dd>{d}</dd></div>
      ))}
    </dl>
  );
}

function DashboardInfo() {
  return (
    <>
      <p>
        ZoneSort reads each parcel's QR label and routes it to <strong>Zone A</strong> (floors 1–5),{" "}
        <strong>Zone B</strong> (floors 6–10) or <strong>Manual</strong>. This page shows what the sorter has
        done and how many parcels to expect tomorrow.
      </p>

      <h3>Choosing what to show</h3>
      <Terms items={[
        ["Live database", "Real parcels from MySQL, refreshed every 5 seconds."],
        ["Simulated", "A year of simulated parcels, for trying the dashboard without hardware."],
        ["Latest session", "Since Node 1 last came online. A reboot starts a new session."],
        ["Today / All time", "Every session from today, or everything recorded."],
      ]} />
      <p className="muted small">
        The dot next to ZoneSort is green while the page is updating and red if the server can't be reached.
      </p>

      <h3>Live counts</h3>
      <Terms items={[
        ["Zone A / B / Manual", "Where parcels actually went: the gate Node 2 fired."],
        ["Unreadable / unknown", "No QR found, or the block or floor isn't one we deliver to."],
        ["Timeouts", "No scan reached Node 1 in time, so the parcel went to Manual."],
        ["Missed at bin", "The bin's IR sensor never saw the parcel arrive."],
        ["Faults", "Jams, missed sensors, repeated scan failures. Outlined red when there are any."],
      ]} />
      <p>
        <strong>Parcels by block</strong> stacks each block's parcels by the gate that fired.{" "}
        <strong>Recent parcels</strong> lists the latest 50; a gate followed by “…” means the parcel was
        scanned but hasn't reached a bin yet.
      </p>

      <h3>Forecast</h3>
      <p>
        Tomorrow's parcels per block, from the model picked in the Model menu, next to the usual count for
        that weekday. The courier count divides the total by the parcels per courier set on the Settings
        page. <strong>How the models work</strong> explains the three models and how accurate each one is.
      </p>
    </>
  );
}

function SettingsInfo() {
  return (
    <>
      <p>
        This page tunes the forecast models and retrains them. Nothing here affects sorting: the belt and
        gates run on the ESP32s and never wait for the cloud.
      </p>

      <h3>Save or retrain</h3>
      <Terms items={[
        ["Save", "Stores the settings. Parcels per courier and test runs apply straight away."],
        ["Retrain", "Trains all three models with the saved settings. Takes a few seconds."],
        ["Reset to defaults", "Puts that section's settings back. Test runs stay ticked."],
      ]} />
      <p className="muted small">
        The dashboard keeps using the old models until all the new ones have trained, so a failed run
        changes nothing. Anyone who can open the dashboard can retrain.
      </p>

      <h3>What the settings do</h3>
      <Terms items={[
        ["Training data", "Simulated history, real runs from MySQL, or both. Real runs replace the simulated counts on the days they happened."],
        ["Real runs count as", "A demo run is a few dozen parcels. Scaled up, it keeps its real split between blocks at a full day's volume."],
        ["Test period", "The most recent days are kept out of training and used to score the models."],
        ["Poisson / XGBoost", "Each setting's help text says what raising or lowering it does."],
      ]} />

      <h3>Pretend data</h3>
      <p>
        The simulated history that fills every day the sorter didn't run: how busy each block is, the weekday
        pattern, sale-day rushes, growth, how much days vary and how often labels go wrong. Change the
        numbers, then <strong>Regenerate and retrain</strong> remakes the whole history and retrains on it.
        Boxes that differ from the current pretend data are highlighted until you do.
      </p>
      <p className="muted small">
        Useful for matching the pretend data to what your real runs show, or for testing the models, e.g.
        set every sale box to 1 and see how much worse they get on sale days.
      </p>

      <h3>Real runs</h3>
      <p>
        Every session recorded in MySQL. Tick the ones that were tests, such as <code>fake_events.py</code>,
        the <code>mqtt_test</code> sketch or bring-up runs, so they don't teach the model wrong volumes.
        They stay on the dashboard either way.
      </p>

      <h3>Reading the results</h3>
      <p>
        After a run, each model shows its score before → after. Lower MAE and RMSE are better, and green
        means the new run did better. If the data changed between runs (new days, test runs ticked), the
        page says so; the same-weekday average has no settings, so its change is how much the data alone
        moved the scores.
      </p>
      <p className="muted small">
        For a fair comparison, retrain once without changes, then change one setting at a time.
      </p>
    </>
  );
}
