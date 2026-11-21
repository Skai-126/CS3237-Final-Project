from __future__ import annotations

import argparse
import json
import re
import threading
import time
from typing import Any


# Blocks outside this prototype's map go to MANUAL.
BLOCK_TO_ZONE = {
    "123": "A",
    "124": "A",
    "125": "A",
    "126": "A",
    "127": "A",
    "128": "B",
    "129": "B",
    "130": "B",
    "131": "B",
    "132": "B",
}

KNOWN_BLOCKS = set(BLOCK_TO_ZONE)


def zone_for(block: str) -> str:
    normalized_block = block.strip().upper()
    return BLOCK_TO_ZONE.get(normalized_block, "MANUAL")



# ID:PO042 BLK:123 FL:7 UNIT:07-123 ZN:B SEQ:17
_QR_FIELDS_RE = re.compile(
    r"^ID:(?P<parcel_id>[A-Za-z0-9_-]+)\s+"
    r"BLK:(?P<block>[A-Za-z0-9]+)\s+"
    r"FL:(?P<floor>\d{1,2})\s+"
    r"UNIT:(?P<unit>\d{1,3}-\d{1,4})"
    r"(?:\s+ZN:[A-Za-z0-9_-]+)?"
    r"(?:\s+SEQ:\d+)?$",
    re.IGNORECASE,
)


def _parse_label_text(raw_label: str | None) -> dict[str, Any] | None:
    if not raw_label:
        return None

    raw_label = raw_label.strip()

    # QR payloads may be JSON or plain text.
    try:
        payload = json.loads(raw_label)
    except (json.JSONDecodeError, TypeError):
        payload = None

    if isinstance(payload, dict):
        parcel_id = payload.get("parcel_id", payload.get("id"))
        block = payload.get("block")
        floor = payload.get("floor")
        unit = payload.get("unit")

        if isinstance(floor, str) and floor.isdigit():
            floor = int(floor)

        if (
            isinstance(block, str)
            and isinstance(floor, int)
            and isinstance(unit, str)
        ):
            return {
                "parcel_id": parcel_id,
                "block": block.strip().upper(),
                "floor": floor,
                "unit": unit.strip(),
            }

    # Plain-text QR labels use fields like ID, BLK, FL, and UNIT.
    match = _QR_FIELDS_RE.fullmatch(raw_label)
    if match:
        fields = match.groupdict()
        return {
            "parcel_id": fields["parcel_id"],
            "block": fields["block"].upper(),
            "floor": int(fields["floor"]),
            "unit": fields["unit"],
        }

    return None


def make_scan_event(seq: int, raw_label: str | None) -> dict[str, Any]:
    if not isinstance(seq, int) or isinstance(seq, bool) or seq < 1:
        raise ValueError("seq must be a positive integer")

    payload = _parse_label_text(raw_label)
    scan_status = "unknown" if payload is not None else "unreadable"

    parcel_id = None
    block = None
    floor = None
    unit = None
    zone = "MANUAL"

    if payload is not None:
        candidate_block = payload.get("block")
        candidate_floor = payload.get("floor")
        candidate_unit = payload.get("unit")
        candidate_id = payload.get("parcel_id")

        if isinstance(candidate_block, str):
            candidate_block = candidate_block.strip().upper()

        valid_fields = (
            isinstance(candidate_block, str)
            and isinstance(candidate_floor, int)
            and not isinstance(candidate_floor, bool)
            and isinstance(candidate_unit, str)
            and isinstance(candidate_id, str)
        )

        if valid_fields and candidate_block in KNOWN_BLOCKS:
         
            mapped_zone = zone_for(candidate_block)

            if mapped_zone != "MANUAL":
                parcel_id = candidate_id
                block = candidate_block
                floor = candidate_floor
                unit = candidate_unit
                zone = mapped_zone
                scan_status = "ok"

    return {
        "seq": seq,
        "parcel_id": parcel_id,
        "block": block,
        "floor": floor,
        "unit": unit,
        "scan": scan_status,
        "zone": zone,
    }


def decode_qr_from_frame(frame: Any) -> str | None:
    try:
        import zxingcpp
    except ImportError as exc:
        raise RuntimeError("The zxingcpp QR decoder is unavailable.") from exc

    results = zxingcpp.read_barcodes(frame)

    qr_format = zxingcpp.BarcodeFormat.QRCode

    first_supported_text = None

    for result in results:
        if result.format != qr_format or not result.text:
            continue

        text = result.text.strip()

        # Return a recognized label as soon as one is found.
        if _parse_label_text(text) is not None:
            return text

        if first_supported_text is None:
            first_supported_text = text

    return first_supported_text


class LatestFrameCamera:
    def __init__(self, source: str):
        try:
            import cv2
        except ImportError as exc:
            raise RuntimeError("OpenCV is unavailable.") from exc

        self.capture_module = cv2
        source_value: int | str = int(source) if source.isdigit() else source
        self.capture = cv2.VideoCapture(source_value)

        if not self.capture.isOpened():
            self.capture.release()
            raise RuntimeError(
                f"Could not open camera source {source!r}. "
                "Check the stream URL and network."
            )

     
        self.capture.set(cv2.CAP_PROP_BUFFERSIZE, 1)

        self.lock = threading.Lock()
        self.running = True
        self.frame = None
        self.frame_number = 0
        self.reader = threading.Thread(target=self._read_frames, daemon=True)
        self.reader.start()

    def _read_frames(self) -> None:
        while self.running:
            ok, frame = self.capture.read()
            if ok:
                with self.lock:
                    self.frame = frame
                    self.frame_number += 1
            else:
                time.sleep(0.02)

    def latest_frame(self):
        with self.lock:
            return self.frame, self.frame_number

    def close(self) -> None:
        self.running = False
        self.reader.join(timeout=1.0)
        self.capture.release()


def scan_from_camera(source: str, timeout: float) -> str | None:
    camera = LatestFrameCamera(source)
    deadline = time.monotonic() + timeout
    last_frame_number = 0

    print(f"Scanning for a QR code for up to {timeout:g} seconds...")

    try:
        while time.monotonic() < deadline:
            frame, frame_number = camera.latest_frame()

            if frame is None or frame_number == last_frame_number:
                time.sleep(0.01)
                continue

            # Skip old frames if decoding has fallen behind.
            last_frame_number = frame_number
            label_text = decode_qr_from_frame(frame)

            if label_text:
                print(f"Code read: {label_text}")
                return label_text
    finally:
        camera.close()

    print("No QR code was read before the timeout.")
    return None


def scan_continuously(source: str, start_seq: int) -> None:
    camera = LatestFrameCamera(source)
    seq = start_seq
    active_label: str | None = None
    missing_since: float | None = None
    last_frame_number = 0

    print(f"Continuous scanning started at sequence {seq}. Press Ctrl+C to stop.")

    try:
        while True:
            frame, frame_number = camera.latest_frame()

            if frame is None or frame_number == last_frame_number:
                time.sleep(0.01)
                continue

            last_frame_number = frame_number
            label_text = decode_qr_from_frame(frame)
            now = time.monotonic()

            if label_text:
                missing_since = None

                if label_text != active_label:
                    event = make_scan_event(seq, label_text)
                    print(f"Code read: {label_text}")
                    print(json.dumps(event, indent=2))
                    seq += 1
                    active_label = label_text

            elif active_label is not None:
                if missing_since is None:
                    missing_since = now
                elif now - missing_since >= 1.0:
                    active_label = None
                    missing_since = None
    finally:
        camera.close()


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Scan parcel labels using a live phone camera or webcam"
    )
    parser.add_argument(
        "--camera",
        required=True,
        help="camera index (0) or phone camera stream URL",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=30.0,
        help="seconds to scan before giving up (default: 30)",
    )
    parser.add_argument(
        "--seq",
        type=int,
        default=1,
        help="parcel sequence number (default: 1)",
    )
    parser.add_argument(
        "--continuous",
        action="store_true",
        help="keep scanning and increment seq for each new code; stop with Ctrl+C",
    )
    args = parser.parse_args()

    if args.timeout <= 0:
        parser.error("--timeout must be greater than 0")

    if args.seq < 1:
        parser.error("--seq must be at least 1")

    if args.continuous:
        try:
            scan_continuously(args.camera, args.seq)
        except KeyboardInterrupt:
            print("\nContinuous scanning stopped.")
    else:
        raw_label = scan_from_camera(args.camera, args.timeout)
        event = make_scan_event(args.seq, raw_label)
        print(json.dumps(event, indent=2))


if __name__ == "__main__":
    main()
