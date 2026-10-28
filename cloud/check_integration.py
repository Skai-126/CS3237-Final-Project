"""Live integration checker: watches zonesort/# and flags anything that breaks the event schema.

Run it next to the real hardware during bring-up. Every message is printed, problems are
marked WARN, and Ctrl+C prints a summary with per-parcel latencies (arrival -> scan -> sort)
for the report. Nothing is written to MySQL; run subscriber.py for that.

Usage:
    python check_integration.py
    python check_integration.py --scan-timeout 20 --csv latencies.csv
"""
import argparse
import csv
import json
import os
import statistics
import time
from collections import Counter
from datetime import datetime

import paho.mqtt.client as mqtt
from dotenv import load_dotenv

load_dotenv()

ZONES = {"A", "B", "MANUAL"}
SCANS = {"ok", "unreadable", "unknown"}
RESULTS = {"confirmed", "missed"}
LABEL_FIELDS = ("parcel_id", "block", "floor", "unit")
# blocks the forecast has history for (ml/simulate.py BLOCK_RATES)
FORECAST_BLOCKS = {"123A", "123B", "124", "125", "126A", "126B"}


def zone_for(floor: int) -> str:
    return "A" if 1 <= floor <= 5 else "B" if 6 <= floor <= 10 else "MANUAL"


class Checker:
    def __init__(self, scan_timeout: float, sort_timeout: float):
        self.scan_timeout = scan_timeout
        self.sort_timeout = sort_timeout
        self.session = 0              # bumped on every node1 online, like the subscriber
        self.parcels: dict = {}       # (session, seq) -> {"arrival": t, "scan": t, "sorted": t, ...}
        self.warnings = Counter()
        self.flagged: set = set()     # (key, what) already reported as late

    def warn(self, kind: str, text: str) -> None:
        self.warnings[kind] += 1
        print(f"  WARN [{kind}] {text}")

    def parcel(self, seq: int) -> dict:
        return self.parcels.setdefault((self.session, seq), {"seq": seq, "session": self.session})

    def handle(self, topic: str, payload: bytes, retained: bool) -> None:
        now = time.monotonic()
        stamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        text = payload.decode(errors="replace")
        print(f"{stamp} {topic} {text}{'  (retained)' if retained else ''}")

        if topic.endswith("/status"):
            if text not in ("online", "offline"):
                self.warn("status", f"status must be online/offline, got {text!r}")
            if topic == "zonesort/node1/status" and text == "online" and not retained:
                self.session += 1
                print(f"  new session #{self.session}")
            return

        try:
            msg = json.loads(payload)
        except ValueError:
            self.warn("json", "payload is not valid JSON")
            return
        if not isinstance(msg, dict):
            self.warn("json", "payload must be a JSON object")
            return

        if topic == "zonesort/fault":
            for key in ("source", "type"):
                if not isinstance(msg.get(key), str):
                    self.warn("fault", f"fault needs a string {key!r}")
            return

        seq = msg.get("seq")
        if not isinstance(seq, int) or isinstance(seq, bool) or seq < 1:
            self.warn("seq", f"seq must be a positive integer, got {seq!r}")
            return

        if topic == "zonesort/node1/arrival":
            if self.session == 0:
                self.warn("session", "arrival before any node1/status online: "
                                     "Node 1 needs a Last Will + retained online publish")
            if "arrival" in self.parcel(seq):
                self.warn("duplicate", f"seq {seq} arrived twice in one session "
                                       "(Node 1 rebooted without a new session?)")
                self.session += 1  # keep the earlier parcel's timings intact
            self.parcel(seq)["arrival"] = now
        elif topic == "zonesort/gateway/flip":
            self.parcel(seq)["flips_sent"] = self.parcel(seq).get("flips_sent", 0) + 1
        elif topic == "zonesort/gateway/scan":
            self.check_scan(msg, now)
        elif topic == "zonesort/node2/sorted":
            self.check_sorted(msg, now)
        else:
            self.warn("topic", "not a topic in the schema")

    def check_scan(self, msg: dict, now: float) -> None:
        p = self.parcel(msg["seq"])
        zone, scan = msg.get("zone"), msg.get("scan")
        if zone not in ZONES:
            self.warn("zone", f"zone must be A/B/MANUAL, got {zone!r} (the subscriber drops this)")
        if scan not in SCANS:
            self.warn("scan", f"scan must be ok/unreadable/unknown, got {scan!r}")
        missing = [k for k in (*LABEL_FIELDS, "flips") if k not in msg]
        if missing:
            self.warn("fields", f"missing {missing}")
        if scan == "ok":
            floor = msg.get("floor")
            if not isinstance(floor, int) or zone != zone_for(floor):
                self.warn("zone", f"floor {floor!r} doesn't map to zone {zone!r}")
            if msg.get("block") not in FORECAST_BLOCKS:
                self.warn("block", f"block {msg.get('block')!r} has no forecast history")
        elif scan in ("unreadable", "unknown"):
            if zone != "MANUAL":
                self.warn("zone", f"scan {scan} must route to MANUAL, got {zone!r}")
            filled = [k for k in LABEL_FIELDS if msg.get(k) is not None]
            if filled:
                self.warn("fields", f"scan {scan} must send null for {filled}")
        if "flips" in msg and msg["flips"] != p.get("flips_sent", msg["flips"]):
            self.warn("flips", f"flips={msg['flips']} but saw {p.get('flips_sent', 0)} flip commands")

        if "arrival" not in p:
            self.warn("order", "scan with no arrival for this seq in this session")
        if "scan" in p:
            self.warn("duplicate", f"second scan for seq {msg['seq']}: "
                                   "are Node 1 and the gateway both publishing scan?")
        p["scan"], p["zone"] = now, zone

    def check_sorted(self, msg: dict, now: float) -> None:
        p = self.parcel(msg["seq"])
        routed, reason, result = msg.get("routed"), msg.get("reason"), msg.get("result")
        if routed not in ZONES:
            self.warn("zone", f"routed must be A/B/MANUAL, got {routed!r} (the subscriber drops this)")
        if result not in RESULTS:
            self.warn("result", f"result must be confirmed/missed, got {result!r}")
        if "reason" not in msg:
            self.warn("fields", "missing 'reason' (send null when there is none)")
        if "zone" in p and routed != p["zone"] and reason is None:
            self.warn("reason", f"routed {routed} but the gateway said {p['zone']}, and no reason given")
        if "scan" not in p and reason != "timeout":
            self.warn("reason", "sorted with no scan: expected reason 'timeout'")
        if "arrival" not in p:
            self.warn("order", "sorted with no arrival for this seq in this session")
        p["sorted"] = now

    def check_late(self) -> None:
        """Flag parcels still waiting for their scan or sort after the timeout."""
        now = time.monotonic()
        for key, p in list(self.parcels.items()):  # messages keep arriving on paho's thread
            if "arrival" not in p:
                continue
            if "scan" not in p and "sorted" not in p and now - p["arrival"] > self.scan_timeout \
                    and (key, "scan") not in self.flagged:
                self.flagged.add((key, "scan"))
                self.warn("late", f"seq {p['seq']}: no scan {self.scan_timeout:.0f} s after arrival")
            if "sorted" not in p and now - p["arrival"] > self.sort_timeout \
                    and (key, "sorted") not in self.flagged:
                self.flagged.add((key, "sorted"))
                self.warn("late", f"seq {p['seq']}: not sorted {self.sort_timeout:.0f} s after arrival")

    def summary(self, csv_path: str | None) -> None:
        rows = [p for p in self.parcels.values() if "arrival" in p]
        print(f"\n{len(rows)} parcels in {len({p['session'] for p in rows})} session(s)")

        def stats(name, a, b):
            ms = [1000 * (p[b] - p[a]) for p in rows if a in p and b in p]
            if not ms:
                print(f"  {name:<16} no data")
                return
            q = statistics.quantiles(ms, n=20)[-1] if len(ms) > 1 else ms[0]
            print(f"  {name:<16} n={len(ms):<4} median {statistics.median(ms):7.0f} ms"
                  f"   p95 {q:7.0f} ms   max {max(ms):7.0f} ms")

        stats("arrival -> scan", "arrival", "scan")
        stats("scan -> sorted", "scan", "sorted")
        stats("arrival -> sorted", "arrival", "sorted")
        if self.warnings:
            print("warnings: " + ", ".join(f"{k} {n}" for k, n in self.warnings.most_common()))
        else:
            print("no warnings")

        if csv_path:
            with open(csv_path, "w", newline="") as f:
                w = csv.writer(f)
                w.writerow(["session", "seq", "zone", "arrival_to_scan_ms", "scan_to_sorted_ms"])
                for p in rows:
                    gap = lambda a, b: round(1000 * (p[b] - p[a])) if a in p and b in p else ""
                    w.writerow([p["session"], p["seq"], p.get("zone", ""),
                                gap("arrival", "scan"), gap("scan", "sorted")])
            print(f"latencies saved to {csv_path}")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--scan-timeout", type=float, default=15, help="seconds to wait for a scan")
    ap.add_argument("--sort-timeout", type=float, default=30, help="seconds to wait for a sort")
    ap.add_argument("--csv", help="save per-parcel latencies here on exit")
    args = ap.parse_args()
    checker = Checker(args.scan_timeout, args.sort_timeout)

    def on_connect(client, userdata, flags, reason_code, properties):
        print(f"connected to broker ({reason_code}), watching zonesort/#  (Ctrl+C for the summary)")
        client.subscribe("zonesort/#", qos=1)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="zonesort-checker")
    client.on_connect = on_connect
    client.on_message = lambda c, u, m: checker.handle(m.topic, m.payload, m.retain)
    client.connect(os.getenv("MQTT_HOST", "127.0.0.1"), int(os.getenv("MQTT_PORT", "1883")))
    client.loop_start()
    try:
        while True:
            time.sleep(1)
            checker.check_late()
    except KeyboardInterrupt:
        pass
    finally:
        client.loop_stop()
        client.disconnect()
        checker.summary(args.csv)


if __name__ == "__main__":
    main()
