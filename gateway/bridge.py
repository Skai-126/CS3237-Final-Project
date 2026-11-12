"""Continuously scan parcel labels and publish events over MQTT."""

from __future__ import annotations

import argparse
import json
import os
import ssl
import threading
import time

import cv2
import paho.mqtt.client as mqtt
from dotenv import load_dotenv

from scanner import decode_barcode_from_frame, make_scan_event


SCAN_TOPIC = "zonesort/gateway/scan"
STATUS_TOPIC = "zonesort/gateway/status"

SCRIPT_DIR = Path(__file__).resolve().parent
load_dotenv(SCRIPT_DIR / ".env")


def utc_timestamp() -> str:
    """Return the current UTC time in ISO 8601 format."""
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace(
        "+00:00", "Z"
    )


class Camera:
    """Keep a live camera stream open and decode labels from its newest frames."""

    def __init__(self, source: str):
        camera_source: int | str = int(source) if source.isdigit() else source
        self.capture = cv2.VideoCapture(camera_source)

        if not self.capture.isOpened():
            self.capture.release()
            raise RuntimeError(
                f"Could not open camera source {source!r}. "
                "Check the stream URL and that the phone is streaming."
            )

        self.frame = None
        self.lock = threading.Lock()
        self.running = True

        self.reader_thread = threading.Thread(
            target=self._read_frames,
            daemon=True,
        )
        self.reader_thread.start()

    def _read_frames(self) -> None:
        """Continuously read frames, keeping only the newest one."""
        while self.running:
            ok, frame = self.capture.read()

            if ok:
                with self.lock:
                    self.frame = frame
            else:
                time.sleep(0.05)

    def read_label(self) -> str | None:
        """Try to decode a barcode or QR code from the newest frame."""
        with self.lock:
            frame = self.frame

        if frame is None:
            return None

        return decode_barcode_from_frame(frame)

    def close(self) -> None:
        """Stop reading frames and release the camera."""
        self.running = False
        self.reader_thread.join(timeout=1)
        self.capture.release()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Continuously scan parcel labels and publish MQTT events."
    )
    parser.add_argument(
        "--camera",
        required=True,
        help="Phone stream URL or camera index, such as 0",
    )
    parser.add_argument(
        "--seq",
        type=int,
        default=1,
        help="Sequence number to use for the first scan (default: 1)",
    )
    parser.add_argument(
        "--reset-after",
        type=float,
        default=1.0,
        help="Seconds a label must be out of view before it can scan again",
    )
    args = parser.parse_args()

    if args.seq < 1:
        parser.error("--seq must be at least 1")
    if args.reset_after <= 0:
        parser.error("--reset-after must be greater than 0")

    broker_host = os.getenv("MQTT_HOST", "119.74.242.100")
    broker_port = int(os.getenv("MQTT_PORT", "8883"))
    username = os.getenv("MQTT_USERNAME")
    password = os.getenv("MQTT_PASSWORD")
    ca_cert_value = os.getenv("MQTT_CA_CERT", "server.crt")

    if not username or not password:
        raise RuntimeError(
            "Set MQTT_USERNAME and MQTT_PASSWORD in the gateway .env file."
        )

    ca_cert_path = Path(ca_cert_value)
    if not ca_cert_path.is_absolute():
        ca_cert_path = SCRIPT_DIR / ca_cert_path

    if not ca_cert_path.is_file():
        raise FileNotFoundError(
            f"Could not find broker certificate: {ca_cert_path}"
        )

    camera = Camera(args.camera)

    client = mqtt.Client(
        mqtt.CallbackAPIVersion.VERSION2,
        client_id="zonesort-gateway",
    )
    client.username_pw_set(username, password)
    client.tls_set(
        ca_certs=str(ca_cert_path),
        cert_reqs=ssl.CERT_REQUIRED,
    )
    client.will_set(STATUS_TOPIC, "offline", qos=1, retain=True)

    def on_connect(client, userdata, flags, reason_code, properties):
        print(f"Connected to MQTT broker (result: {reason_code})")

        if reason_code == 0:
            client.publish(STATUS_TOPIC, "online", qos=1, retain=True)
            print(f"Publishing scan events to {SCAN_TOPIC}")

    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(os.getenv("MQTT_HOST", "127.0.0.1"), int(os.getenv("MQTT_PORT", "1883")))
    client.loop_start()

    try:
        while True:
            label_text = camera.read_label()
            now = time.monotonic()

            if label_text:
                missing_since = None

                # Publish this label only once while it remains in view.
                if label_text != active_label:
                    # Without a Node 1 arrival sensor, this is when the
                    # camera first detects the label, not physical arrival.
                    arrival_time = utc_timestamp()

                    event = make_scan_event(seq, label_text)
                    event["arrival_time"] = arrival_time
                    event["scan_time"] = utc_timestamp()

                    info = client.publish(
                        SCAN_TOPIC,
                        json.dumps(event),
                        qos=1,
                    )
                    info.wait_for_publish(timeout=10)

                    print(f"\nBarcode/QR text read: {label_text}")
                    print(f"Published to {SCAN_TOPIC}:")
                    print(json.dumps(event, indent=2))

                    if not info.is_published():
                        print(
                            "Warning: the broker has not confirmed "
                            "the publish yet."
                        )

                    seq += 1
                    active_label = label_text

            elif active_label is not None:
                if missing_since is None:
                    missing_since = now
                elif now - missing_since >= args.reset_after:
                    active_label = None
                    missing_since = None

            time.sleep(0.05)

    except KeyboardInterrupt:
        print("\nStopping bridge.")
    finally:
        try:
            if client.is_connected():
                info = client.publish(
                    STATUS_TOPIC,
                    "offline",
                    qos=1,
                    retain=True,
                )
                info.wait_for_publish(timeout=5)
                client.disconnect()
        finally:
            client.loop_stop()
            camera.close()


if __name__ == "__main__":
    main()