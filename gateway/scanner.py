"""Decode a parcel QR payload and produce the agreed gateway scan event."""

from __future__ import annotations

import argparse
import json
from typing import Any


# Replace/add real destinations after the team agrees on the prototype blocks.
# Mapping is by block and floor, so an unknown block never gets routed by guess.
KNOWN_BLOCKS = {"123A", "456B"}


def zone_for(block: str, floor: int) -> str:
    if block not in KNOWN_BLOCKS:
        return "MANUAL"
    if 1 <= floor <= 5:
        return "A"
    if 6 <= floor <= 10:
        return "B"
    return "MANUAL"


def make_scan_event(seq: int, raw_qr: str | None, flips: int = 0) -> dict[str, Any]:
    """Convert QR text to the gateway/scan MQTT payload.

    raw_qr=None or invalid JSON means unreadable. Decoded but missing/unknown
    destination fields means unknown. Per the proposed schema, non-ok labels
    have null address fields and always route to MANUAL.
    """
    if not isinstance(seq, int) or seq < 1:
        raise ValueError("seq must be a positive integer")
    if not isinstance(flips, int) or flips < 0:
        raise ValueError("flips must be a non-negative integer")

    payload: dict[str, Any] | None = None
    scan_status = "unreadable"
    if raw_qr:
        try:
            parsed = json.loads(raw_qr)
            if isinstance(parsed, dict):
                payload = parsed
                scan_status = "unknown"
        except (json.JSONDecodeError, TypeError):
            pass

    parcel_id = block = unit = None
    floor = None
    zone = "MANUAL"

    if payload is not None:
        candidate_block = payload.get("block")
        candidate_floor = payload.get("floor")
        candidate_unit = payload.get("unit")
        candidate_id = payload.get("parcel_id")

        # Booleans are ints in Python, so exclude them explicitly.
        valid_fields = (
            isinstance(candidate_block, str)
            and isinstance(candidate_floor, int)
            and not isinstance(candidate_floor, bool)
            and isinstance(candidate_unit, str)
            and isinstance(candidate_id, str)
        )
        if valid_fields and candidate_block in KNOWN_BLOCKS:
            mapped_zone = zone_for(candidate_block, candidate_floor)
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
        "flips": flips,
    }


def decode_qr_from_image(path: str) -> str | None:
    """Decode a QR image using OpenCV's QR detector; return its text or None."""
    try:
        import cv2
    except ImportError as exc:
        raise RuntimeError("Install requirements first: python -m pip install -r gateway/requirements.txt") from exc
    image = cv2.imread(path)
    if image is None:
        raise FileNotFoundError(f"Could not open image: {path}")
    text, _points, _straight = cv2.QRCodeDetector().detectAndDecode(image)
    return text or None


def main() -> None:
    parser = argparse.ArgumentParser(description="Test ZoneSort QR decoding and routing")
    parser.add_argument("--image", help="path to a QR-code image (omit to test JSON from stdin)")
    parser.add_argument("--seq", type=int, default=1, help="Node 1 parcel sequence number")
    parser.add_argument("--flips", type=int, default=0, help="number of flips before final scan")
    args = parser.parse_args()

    if args.image:
        raw_qr = decode_qr_from_image(args.image)
    else:
        print('Paste the QR JSON text, then press Enter (blank = unreadable):')
        raw_qr = input().strip() or None
    print(json.dumps(make_scan_event(args.seq, raw_qr, args.flips), indent=2))


if __name__ == "__main__":
    main()
