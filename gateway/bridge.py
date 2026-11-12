"""MQTT bridge: for every Node 1 arrival, read the QR label and publish the scan event.

Flow per parcel (cloud/README.md, Event schema):
    zonesort/node1/arrival {"seq": 17}
      -> look for a QR for --window seconds
      -> none found and flips left: publish zonesort/gateway/flip {"seq": 17}, look again
      -> publish zonesort/gateway/scan (built by scanner.make_scan_event)

Keep window x (max flips + 1) well under Node 1's QR_TIMEOUT_MS, or Node 1 sends the
parcel to MANUAL before the scan arrives.

Usage:
    python bridge.py --camera http://<phone-ip>:8080/video   # phone IP-camera app
    python bridge.py --camera 0                              # laptop webcam
    python bridge.py --manual                                # type the QR JSON per parcel
"""
from __future__ import annotations

import argparse
import json
import os
import queue
import threading
import time
from pathlib import Path

import paho.mqtt.client as mqtt
from dotenv import load_dotenv

from scanner import make_scan_event

load_dotenv()

STATUS_TOPIC = "zonesort/gateway/status"


class Camera:
    """Keeps only the newest frame, so a decode never runs on a stale, buffered one."""

    def __init__(self, source: str):
        import cv2

        self.cv2 = cv2
        self.cap = cv2.VideoCapture(int(source) if source.isdigit() else source)
        if not self.cap.isOpened():
            raise RuntimeError(f"Could not open camera {source!r}")
        self.detector = cv2.QRCodeDetector()
        self.frame = None
        self.lock = threading.Lock()
        threading.Thread(target=self._grab, daemon=True).start()

    def _grab(self) -> None:
        while True:
            ok, frame = self.cap.read()
            if ok:
                with self.lock:
                    self.frame = frame
            else:
                time.sleep(0.05)

    def read_qr(self, window: float) -> str | None:
        """Try to decode a QR from the live feed for up to `window` seconds."""
        deadline = time.monotonic() + window
        while time.monotonic() < deadline:
            with self.lock:
                frame = self.frame
            if frame is not None:
                text, _points, _straight = self.detector.detectAndDecode(frame)
                if text:
                    return text
            time.sleep(0.05)
        return None


class Manual:
    """Stand-in for the camera: type the decoded QR text for each parcel."""

    def read_qr(self, window: float) -> str | None:
        print('  QR JSON for this parcel (blank = not found):')
        try:
            return input("  > ").strip() or None
        except EOFError:  # input piped from a file has run out
            return None


def main() -> None:
    ap = argparse.ArgumentParser(description="ZoneSort gateway: arrival -> QR -> scan event")
    src = ap.add_mutually_exclusive_group(required=True)
    src.add_argument("--camera", help="camera index (0) or IP-camera stream URL")
    src.add_argument("--manual", action="store_true", help="type the QR text instead of using a camera")
    ap.add_argument("--window", type=float, default=3.0, help="seconds to look for a QR per attempt")
    ap.add_argument("--max-flips", type=int, default=1, help="flip commands before giving up")
    args = ap.parse_args()

    reader = Manual() if args.manual else Camera(args.camera)
    arrivals: queue.Queue[int] = queue.Queue()  # handled one at a time, in belt order

    def on_connect(client, userdata, flags, reason_code, properties):
        print(f"connected to broker ({reason_code})")
        client.publish(STATUS_TOPIC, "online", qos=1, retain=True)
        client.subscribe("zonesort/node1/arrival", qos=1)

    def on_message(client, userdata, m):
        try:
            seq = json.loads(m.payload)["seq"]
        except (ValueError, KeyError, TypeError):
            print(f"ignored bad arrival: {m.payload!r}")
            return
        if isinstance(seq, int) and seq >= 1:
            arrivals.put(seq)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="zonesort-gateway")
    client.will_set(STATUS_TOPIC, "offline", qos=1, retain=True)
    client.on_connect = on_connect
    client.on_message = on_message
    if os.getenv("MQTT_USERNAME"):
        client.username_pw_set(os.environ["MQTT_USERNAME"], os.getenv("MQTT_PASSWORD"))
    ca = os.getenv("MQTT_CA_CERT")  # the broker's certificate; setting it turns on TLS
    if ca:
        client.tls_set(ca_certs=str(Path(ca) if Path(ca).is_absolute() else Path(__file__).parent / ca))
    client.connect(os.getenv("MQTT_HOST", "127.0.0.1"), int(os.getenv("MQTT_PORT", "1883")))
    client.loop_start()

    try:
        while True:
            seq = arrivals.get()
            print(f"arrival seq {seq}")
            flips = 0
            raw = reader.read_qr(args.window)
            while raw is None and flips < args.max_flips:
                flips += 1
                client.publish("zonesort/gateway/flip", json.dumps({"seq": seq}), qos=1)
                print(f"  no QR, flip {flips}")
                raw = reader.read_qr(args.window)
            event = make_scan_event(seq, raw, flips)
            client.publish("zonesort/gateway/scan", json.dumps(event), qos=1)
            print(f"  scan {event['scan']} -> zone {event['zone']}")
    except KeyboardInterrupt:
        pass
    finally:
        client.publish(STATUS_TOPIC, "offline", qos=1, retain=True).wait_for_publish()
        client.loop_stop()
        client.disconnect()


if __name__ == "__main__":
    main()
