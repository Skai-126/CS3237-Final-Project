"""MQTT -> MySQL subscriber: writes every ZoneSort event into the parcels / faults tables.

Topics and payloads are defined in cloud/README.md (Event schema).

Usage:
    python subscriber.py
"""
import json
import logging
from datetime import datetime

import paho.mqtt.client as mqtt
from dotenv import load_dotenv
from sqlalchemy import text

from db import get_engine
from mqtt_conn import connect

load_dotenv()
log = logging.getLogger("subscriber")

ZONES = {"A", "B", "MANUAL"}
RESUME_MINUTES = 10

# columns each parcel topic is allowed to write, plus the timestamp column it stamps
PARCEL_TOPICS = {
    "zonesort/node1/arrival": ([], "arrived_at"),
    "zonesort/gateway/scan": (
        ["parcel_id", "block", "floor", "unit", "scan", "flips", "zone"], "scanned_at"),
    "zonesort/node2/sorted": (["routed", "reason", "result"], "sorted_at"),
}


def new_session() -> str:
    return datetime.now().strftime("%Y%m%d-%H%M%S")


def recent_session(conn) -> str | None:
    """Latest session, if it saw a parcel in the last RESUME_MINUTES.

    Older sessions aren't resumed: Node 1 may have rebooted (seq back to 1) while
    we were down, and resuming would overwrite that session's rows.
    """
    return conn.execute(text(
        "SELECT session FROM parcels "
        "WHERE arrived_at > NOW() - INTERVAL :mins MINUTE "
        "ORDER BY id DESC LIMIT 1"), {"mins": RESUME_MINUTES}).scalar()


def already_scanned(conn, session: str, seq: int) -> bool:
    return conn.execute(text(
        "SELECT 1 FROM parcels WHERE session = :session AND seq = :seq "
        "AND scanned_at IS NOT NULL"), {"session": session, "seq": seq}).first() is not None


def upsert_parcel(conn, session: str, seq: int, fields: dict, keep=()) -> None:
    """Insert or update one parcel row, touching only the given columns.
    Columns in `keep` are only filled in if the row doesn't have them yet."""
    cols = ["session", "seq", *fields]
    updates = ", ".join(f"{c} = COALESCE(parcels.{c}, new.{c})" if c in keep else f"{c} = new.{c}"
                        for c in fields) or "seq = new.seq"
    conn.execute(
        text(f"INSERT INTO parcels ({', '.join(cols)}) "
             f"VALUES ({', '.join(':' + c for c in cols)}) AS new "
             f"ON DUPLICATE KEY UPDATE {updates}"),
        {"session": session, "seq": seq, **fields},
    )


def insert_fault(conn, session: str | None, msg: dict, at: datetime) -> None:
    conn.execute(
        text("INSERT INTO faults (session, source, type, seq, detail, at) "
             "VALUES (:session, :source, :type, :seq, :detail, :at)"),
        {"session": session, "source": msg["source"], "type": msg["type"],
         "seq": msg.get("seq"), "detail": msg.get("detail"), "at": at},
    )


def insert_event(conn, session: str | None, topic: str, payload: bytes, retained: bool,
                 at: datetime) -> None:
    conn.execute(
        text("INSERT INTO events (session, topic, payload, retained, at) "
             "VALUES (:session, :topic, :payload, :retained, :at)"),
        {"session": session, "topic": topic, "payload": payload.decode(errors="replace"),
         "retained": retained, "at": at},
    )


class Subscriber:
    def __init__(self, engine):
        self.engine = engine
        self.session: str | None = None

    def handle(self, topic: str, payload: bytes, retained: bool = False) -> None:
        now = datetime.now()
        try:
            self._handle(topic, payload, retained, now)
        finally:
            # raw copy in its own transaction, kept even if the message was rejected or
            # its parcels / faults write failed; logged after so it gets any new session
            with self.engine.begin() as conn:
                insert_event(conn, self.session, topic, payload, retained, now)

    def _handle(self, topic: str, payload: bytes, retained: bool, now: datetime) -> None:
        if topic.endswith("/status"):
            state = payload.decode(errors="replace").strip()
            log.info("%s is %s", topic.split("/")[1], state)
            if topic == "zonesort/node1/status" and state == "online":
                if retained:
                    # we (re)started while Node 1 was already up: carry on its session
                    with self.engine.connect() as conn:
                        self.session = recent_session(conn) or new_session()
                else:
                    self.session = new_session()
                log.info("session %s", self.session)
            return

        if topic == "zonesort/gateway/flip":
            return  # flip count arrives in the scan event

        try:
            msg = json.loads(payload)
        except ValueError:
            log.warning("bad JSON on %s: %r", topic, payload)
            return

        if self.session is None:
            self.session = new_session()
            log.info("no node1 status seen, starting session %s", self.session)

        with self.engine.begin() as conn:
            if topic in PARCEL_TOPICS:
                allowed, ts_col = PARCEL_TOPICS[topic]
                if not isinstance(msg.get("seq"), int):
                    log.warning("missing seq on %s: %s", topic, msg)
                    return
                for key in ("zone", "routed"):
                    if key in msg and msg[key] not in ZONES:
                        log.warning("bad %s on %s: %s", key, topic, msg)
                        return
                fields = {k: msg.get(k) for k in allowed}
                fields[ts_col] = now
                keep = ()
                if topic == "zonesort/gateway/scan":
                    # Node 1 is on ESP-NOW only, so no node1/arrival or node1/status reaches
                    # us: a repeated seq means the gateway restarted counting, and the scan
                    # time (seconds after arrival) stands in for the arrival time
                    if already_scanned(conn, self.session, msg["seq"]):
                        self.session = new_session()
                        log.info("seq %s seen again, starting session %s", msg["seq"], self.session)
                    fields["arrived_at"], keep = now, ("arrived_at",)
                upsert_parcel(conn, self.session, msg["seq"], fields, keep)
            elif topic == "zonesort/fault":
                if "source" not in msg or "type" not in msg:
                    log.warning("fault missing source/type: %s", msg)
                    return
                insert_fault(conn, self.session, msg, now)
            else:
                return
        log.info("%s %s", topic, msg)


def main() -> None:
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")
    sub = Subscriber(get_engine())

    def on_connect(client, userdata, flags, reason_code, properties):
        log.info("connected to broker (%s)", reason_code)
        client.subscribe("zonesort/#", qos=1)

    def on_message(client, userdata, m):
        try:
            sub.handle(m.topic, m.payload, m.retain)
        except Exception:
            log.exception("failed to store %s %r", m.topic, m.payload)

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="zonesort-cloud")
    client.on_connect = on_connect
    client.on_message = on_message
    connect(client)
    client.loop_forever()


if __name__ == "__main__":
    main()
