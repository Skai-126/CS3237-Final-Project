import { useEffect, useState } from "react";

// Chart colours per colour scheme. CSS has the same values as tokens (styles.css);
// Recharts needs plain strings, so they live here too.
const PALETTES = {
  light: {
    A: "#2a78d6", B: "#eb6834", MANUAL: "#1baf7a",
    forecast: "#2a78d6", usual: "#a3a29b",
    surface: "#ffffff", grid: "#ecebe7", axis: "#8a8a84", text: "#1d1d1b",
  },
  dark: {
    A: "#3987e5", B: "#d95926", MANUAL: "#199e70",
    forecast: "#3987e5", usual: "#6b6a64",
    surface: "#1a1a19", grid: "#2b2b29", axis: "#8f8e86", text: "#f0efec",
  },
};

const query = "(prefers-color-scheme: dark)";

export function usePalette() {
  const [dark, setDark] = useState(() => window.matchMedia(query).matches);
  useEffect(() => {
    const mq = window.matchMedia(query);
    const onChange = (e) => setDark(e.matches);
    mq.addEventListener("change", onChange);
    return () => mq.removeEventListener("change", onChange);
  }, []);
  return PALETTES[dark ? "dark" : "light"];
}
