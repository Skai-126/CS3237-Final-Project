import {
  Bar, BarChart, CartesianGrid, Legend, Line, LineChart, ResponsiveContainer, Tooltip, XAxis, YAxis,
} from "recharts";

const ZONE_LABELS = { A: "Zone A", B: "Zone B", MANUAL: "Manual" };

function axisProps(pal) {
  return {
    stroke: pal.grid,
    tick: { fill: pal.axis, fontSize: 12 },
    tickLine: false,
  };
}

function tooltipProps(pal) {
  return {
    contentStyle: {
      background: pal.surface, border: `1px solid ${pal.grid}`, borderRadius: 8,
      color: pal.text, fontSize: 13,
    },
    labelStyle: { color: pal.text, fontWeight: 600 },
    itemStyle: { color: pal.text },
    cursor: { fill: pal.grid, opacity: 0.5 },
  };
}

const legendProps = { iconType: "square", iconSize: 10, wrapperStyle: { fontSize: 13 } };

/** Parcels per block, stacked by the gate that actually fired. */
export function ByBlockChart({ rows, pal }) {
  const zones = ["A", "B", "MANUAL"];
  return (
    <ResponsiveContainer width="100%" height={260}>
      <BarChart data={rows} margin={{ top: 8, right: 8, bottom: 0, left: -16 }} barCategoryGap="28%">
        <CartesianGrid vertical={false} stroke={pal.grid} />
        <XAxis dataKey="block" {...axisProps(pal)} />
        <YAxis allowDecimals={false} axisLine={false} {...axisProps(pal)} />
        <Tooltip {...tooltipProps(pal)} />
        <Legend {...legendProps} formatter={(v) => <span style={{ color: pal.text }}>{v}</span>} />
        {zones.map((z, i) => (
          <Bar
            key={z}
            dataKey={z}
            name={ZONE_LABELS[z]}
            stackId="zone"
            fill={pal[z]}
            stroke={pal.surface}
            strokeWidth={2}
            radius={i === zones.length - 1 ? [4, 4, 0, 0] : 0}
            isAnimationActive={false}
          />
        ))}
      </BarChart>
    </ResponsiveContainer>
  );
}

const TICK_FORMAT = {
  minute: { hour: "2-digit", minute: "2-digit", hour12: false },
  hour: { hour: "2-digit", minute: "2-digit", hour12: false },
  day: { day: "numeric", month: "short" },
};

/** Arrivals per minute / hour / day, depending on how long the data spans. */
export function ArrivalsChart({ points, freq, pal }) {
  const fmt = (t) => new Date(t).toLocaleString("en-SG", TICK_FORMAT[freq] || TICK_FORMAT.hour);
  return (
    <ResponsiveContainer width="100%" height={260}>
      <LineChart data={points} margin={{ top: 8, right: 16, bottom: 0, left: -16 }}>
        <CartesianGrid vertical={false} stroke={pal.grid} />
        <XAxis dataKey="t" tickFormatter={fmt} minTickGap={40} {...axisProps(pal)} />
        <YAxis allowDecimals={false} axisLine={false} {...axisProps(pal)} />
        <Tooltip
          {...tooltipProps(pal)}
          cursor={{ stroke: pal.axis, strokeDasharray: "3 3" }}
          labelFormatter={fmt}
          formatter={(v) => [v, `Parcels per ${freq}`]}
        />
        <Line
          type="monotone"
          dataKey="count"
          stroke={pal.A}
          strokeWidth={2}
          dot={points.length <= 40 ? { r: 3, fill: pal.A, stroke: pal.surface, strokeWidth: 2 } : false}
          activeDot={{ r: 5, stroke: pal.surface, strokeWidth: 2 }}
          isAnimationActive={false}
        />
      </LineChart>
    </ResponsiveContainer>
  );
}

/** Tomorrow's forecast per block next to the usual count for that weekday. */
export function ForecastChart({ rows, modelName, weekday, pal }) {
  const data = rows.map((r) => ({
    block: r.block,
    forecast: Math.round(r.predicted * 10) / 10,
    usual: Math.round(r.dow_avg * 10) / 10,
  }));
  return (
    <ResponsiveContainer width="100%" height={280}>
      <BarChart data={data} margin={{ top: 8, right: 8, bottom: 0, left: -16 }} barGap={2} barCategoryGap="24%">
        <CartesianGrid vertical={false} stroke={pal.grid} />
        <XAxis dataKey="block" {...axisProps(pal)} />
        <YAxis allowDecimals={false} axisLine={false} {...axisProps(pal)} />
        <Tooltip {...tooltipProps(pal)} />
        <Legend {...legendProps} formatter={(v) => <span style={{ color: pal.text }}>{v}</span>} />
        <Bar dataKey="forecast" name={modelName} fill={pal.forecast} radius={[4, 4, 0, 0]} isAnimationActive={false} />
        <Bar dataKey="usual" name={`Usual ${weekday}`} fill={pal.usual} radius={[4, 4, 0, 0]} isAnimationActive={false} />
      </BarChart>
    </ResponsiveContainer>
  );
}
