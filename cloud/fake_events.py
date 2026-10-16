"""Publish a fake ZoneSort run, so the subscriber and dashboard can be tested without hardware.

Usage:
    python fake_events.py            # 10 parcels
    python fake_events.py -n 30
"""
import argparse
import json
import os
import random
import time

import paho.mqtt.client as mqtt
from dotenv import load_dotenv

load_dotenv()

BLOCKS = ["123A", "123B", "124"]


def zone_for(floor: int) -> str:
    return "A" if 1 <= floor <= 5 else "B" if 6 <= floor <= 10 else "MANUAL"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("-n", type=int, default=10, help="number of parcels")
    args = ap.parse_args()

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="zonesort-fake")
    client.connect(os.getenv("MQTT_HOST", "127.0.0.1"), int(os.getenv("MQTT_PORT", "1883")))
    client.loop_start()

    def pub(topic, msg, **kw):
        payload = msg if isinstance(msg, str) else json.dumps(msg)
        client.publish(topic, payload, qos=1, **kw).wait_for_publish()
        print(topic, payload)

    pub("zonesort/node1/status", "online", retain=True)  # starts a new session
    for seq in range(1, args.n + 1):
        pub("zonesort/node1/arrival", {"seq": seq})
        time.sleep(0.2)

        r = random.random()
        flips = 1 if random.random() < 0.2 else 0
        if r < 0.1:
            scan = {"seq": seq, "parcel_id": None, "block": None, "floor": None,
                    "unit": None, "scan": "unreadable", "zone": "MANUAL", "flips": 1}
        else:
            floor = random.randint(1, 12)  # 11-12 are outside the map
            zone = zone_for(floor)
            scan = {"seq": seq, "parcel_id": f"P{random.randint(0, 9999):04d}",
                    "block": random.choice(BLOCKS), "floor": floor,
                    "unit": f"{floor:02d}-{random.randint(100, 199)}",
                    "scan": "ok" if zone != "MANUAL" else "unknown",
                    "zone": zone, "flips": flips}
        timed_out = random.random() < 0.05
        if not timed_out:
            pub("zonesort/gateway/scan", scan)
        time.sleep(0.2)

        missed = random.random() < 0.05
        pub("zonesort/node2/sorted", {
            "seq": seq,
            "routed": "MANUAL" if timed_out else scan["zone"],
            "reason": "timeout" if timed_out else None,
            "result": "missed" if missed else "confirmed",
        })
        if missed:
            pub("zonesort/fault", {"source": "node2", "type": "missed_ir", "seq": seq,
                                   "detail": "no bin IR within 3 s"})
        time.sleep(0.3)

    client.loop_stop()
    client.disconnect()


if __name__ == "__main__":
    main()
