import json
import unittest

from scanner import make_scan_event


class ScannerTests(unittest.TestCase):
    def test_known_block_floor_3_routes_a(self):
        raw = json.dumps({"parcel_id": "P1", "block": "123A", "floor": 3, "unit": "03-123"})
        event = make_scan_event(1, raw)
        self.assertEqual((event["scan"], event["zone"]), ("ok", "A"))

    def test_known_block_floor_7_routes_b(self):
        raw = json.dumps({"parcel_id": "P2", "block": "123A", "floor": 7, "unit": "07-123"})
        event = make_scan_event(2, raw, flips=1)
        self.assertEqual((event["scan"], event["zone"], event["flips"]), ("ok", "B", 1))

    def test_unknown_block_goes_manual_without_exposing_label_fields(self):
        raw = json.dumps({"parcel_id": "P3", "block": "999Z", "floor": 7, "unit": "07-999"})
        event = make_scan_event(3, raw)
        self.assertEqual((event["scan"], event["zone"]), ("unknown", "MANUAL"))
        self.assertIsNone(event["block"])

    def test_missing_qr_is_unreadable_and_manual(self):
        event = make_scan_event(4, None, flips=1)
        self.assertEqual((event["scan"], event["zone"]), ("unreadable", "MANUAL"))
        self.assertIsNone(event["parcel_id"])


if __name__ == "__main__":
    unittest.main()
