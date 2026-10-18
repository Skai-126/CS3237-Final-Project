import { useEffect, useRef, useState } from "react";

async function getJson(path, params) {
  const res = await fetch(`${path}?${new URLSearchParams(params)}`);
  const body = await res.json().catch(() => ({ error: `HTTP ${res.status}` }));
  if (!res.ok) throw new Error(body.error || `HTTP ${res.status}`);
  return body;
}

/** Fetches `path` now, whenever `params` change, and every `everyMs` (0 = once). */
export function usePolling(path, params, everyMs) {
  const [state, setState] = useState({ data: null, error: null, loading: true, at: null });
  const key = JSON.stringify(params);
  const latest = useRef(key);

  useEffect(() => {
    latest.current = key;
    let stopped = false;
    setState((s) => ({ ...s, loading: true }));
    const load = () =>
      getJson(path, params)
        .then((data) => {
          if (!stopped && latest.current === key)
            setState({ data, error: null, loading: false, at: new Date() });
        })
        .catch((err) => {
          if (!stopped) setState((s) => ({ ...s, error: err.message, loading: false }));
        });
    load();
    const timer = everyMs ? setInterval(load, everyMs) : null;
    return () => {
      stopped = true;
      if (timer) clearInterval(timer);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [path, key, everyMs]);

  return state;
}
