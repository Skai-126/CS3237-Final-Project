import { useEffect, useRef } from "react";

export function Segmented({ label, value, options, onChange }) {
  return (
    <div className="segmented" role="radiogroup" aria-label={label}>
      {options.map(([v, text]) => (
        <button
          key={v}
          role="radio"
          aria-checked={value === v}
          className={value === v ? "on" : ""}
          onClick={() => onChange(v)}
        >
          {text}
        </button>
      ))}
    </div>
  );
}

export function Tile({ label, value, hint, swatch, tone }) {
  return (
    <div className={`tile ${tone || ""}`} title={hint}>
      <div className="tile-label">
        {swatch && <span className="swatch" style={{ background: swatch }} />}
        {label}
      </div>
      <div className="tile-value">{value}</div>
    </div>
  );
}

export function Card({ title, subtitle, children, wide }) {
  return (
    <section className={`card ${wide ? "wide" : ""}`}>
      <header>
        <h2>{title}</h2>
        {subtitle && <span className="muted">{subtitle}</span>}
      </header>
      {children}
    </section>
  );
}

/** Native <dialog>: Esc, focus trapping and the backdrop come from the browser. */
export function Modal({ open, onClose, title, children }) {
  const ref = useRef(null);
  useEffect(() => {
    const d = ref.current;
    if (open && !d.open) d.showModal();
    else if (!open && d.open) d.close();
  }, [open]);
  return (
    <dialog
      ref={ref}
      className="modal"
      aria-labelledby="modal-title"
      onClose={onClose}
      onClick={(e) => e.target === ref.current && onClose()} // click on the backdrop
    >
      <div className="modal-inner">
        <header>
          <h2 id="modal-title">{title}</h2>
          <button className="icon-btn" onClick={onClose} aria-label="Close">✕</button>
        </header>
        <div className="modal-body">{children}</div>
      </div>
    </dialog>
  );
}

export function Empty({ children }) {
  return <div className="empty">{children}</div>;
}

/** columns: [key, header, format?] */
export function Table({ rows, columns, empty = "Nothing yet." }) {
  if (!rows?.length) return <Empty>{empty}</Empty>;
  return (
    <div className="table-wrap">
      <table>
        <thead>
          <tr>{columns.map(([k, h]) => <th key={k}>{h}</th>)}</tr>
        </thead>
        <tbody>
          {rows.map((r, i) => (
            <tr key={i}>
              {columns.map(([k, , fmt]) => (
                <td key={k} className={typeof r[k] === "number" ? "num" : ""}>
                  {fmt ? fmt(r[k], r) : r[k] ?? "—"}
                </td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

export const fmtTime = (iso, withDate) =>
  iso
    ? new Date(iso).toLocaleString("en-SG", {
        ...(withDate ? { day: "numeric", month: "short" } : {}),
        hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false,
      })
    : "—";
