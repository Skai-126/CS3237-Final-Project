# cloud: event store & dashboard

**Owner:** Shu Kai

Stores every parcel event and shows live counts, faults and forecasts.

## Scope
- Parcel event schema, shared by `node1/`, `node2/` and `gateway/`
- Event storage (platform to be decided)
- Dashboard: counts by block, unreadable count, faults, forecast panel

## Database

MySQL 8.4 (Community Server), running on Shu Kai's laptop. Schema: [`schema.sql`](schema.sql)
— see [Event schema](#event-schema) below for the table layout.

Teammates connect with HeidiSQL to `<tailscale-ip>:3306` using the read-only `viewer`
account (ask Shu Kai for the password). Don't port-forward 3306 on a home router.

## Event schema

**Status: proposed, waiting for the gateway and firmware owners to agree.** Built from
[`schema.sql`](schema.sql). Comment on the GitHub issue before changing a topic or
field. Changing them later breaks everyone.

### Ground rules
- **`seq` is the parcel key.** Node 1 numbers each arrival 1, 2, 3, … (restarting at 1
  on boot). Every message about a parcel carries `seq`, including the **ESP-NOW message
  from Node 1 to Node 2**. `parcel_id` can't be the key because unreadable parcels
  don't have one.
- **Sessions are set by the cloud subscriber, not the nodes.** Each time Node 1 comes
  online (`zonesort/node1/status` = `online`), the subscriber starts a new session
  (`YYYYMMDD-HHMMSS`). This means a reboot never reuses a `seq` in the same session.
- **The nodes send no timestamps.** The ESP32s have no real clock. The subscriber stamps
  every event when it arrives, which is accurate to a few ms on the LAN.
- Payloads are JSON with lower-case keys. Zones are always `A`, `B` or `MANUAL`.
- QoS 1 for parcel and fault events. Status topics are retained and set as the MQTT
  Last Will, so a node that drops off shows `offline`.

### MQTT topics

| Topic | Publisher → subscriber(s) | Payload | Writes |
|---|---|---|---|
| `zonesort/node1/arrival` | Node 1 → gateway, cloud | `{"seq": 17}` | `arrived_at` |
| `zonesort/gateway/flip` | gateway → Node 1 | `{"seq": 17}` | nothing (count goes in `scan.flips`) |
| `zonesort/gateway/scan` | gateway → Node 1, cloud | see below | `parcel_id` … `zone`, `scanned_at` |
| `zonesort/node2/sorted` | Node 2 → cloud | `{"seq": 17, "routed": "B", "reason": null, "result": "confirmed"}` | `routed`, `reason`, `result`, `sorted_at` |
| `zonesort/fault` | any → cloud, Telegram | `{"source": "node2", "type": "jam", "seq": 17, "detail": "no IR at gate B after 3 s"}` | `faults` row |
| `zonesort/<node>/status` | each node (retained, LWT) | `online` / `offline` | node1 `online` starts a new session |

`zonesort/gateway/scan` is both the zone command for Node 1 and the scan record:

```json
{"seq": 17, "parcel_id": "P0042", "block": "123A", "floor": 7, "unit": "07-123",
 "scan": "ok", "zone": "B", "flips": 1}
```

- `scan`: `ok` (label decoded and mapped), `unreadable` (no QR decoded, even after a
  flip), or `unknown` (QR decoded, but the block or floor isn't in the map). If `scan`
  isn't `ok`, `zone` is `MANUAL` and the label fields are `null`.
- `zone`: floors 1–5 → `A`, 6–10 → `B`, anything else → `MANUAL`.

**The flow for one parcel:** arrival → (optional flip → rescan) → scan → Node 1 sends
`{seq, zone, reason}` to Node 2 over ESP-NOW → sorted. If no scan reaches Node 1
before its timeout, Node 1 sends `MANUAL` with `reason: "timeout"`. Node 2 copies
`reason` into its `sorted` event.

### Tables

**`parcels`**: one row per parcel. Each event upserts the same row on
`UNIQUE (session, seq)` (`INSERT ... ON DUPLICATE KEY UPDATE`, touching only that
event's columns), so events can arrive in any order and no join is needed.

| Column | Type | Meaning | Set by |
|---|---|---|---|
| `id` | INT, auto-increment | internal row id | |
| `session` | VARCHAR(32) | run the parcel belongs to | subscriber |
| `seq` | INT | Node 1's arrival number | every event |
| `parcel_id` | VARCHAR(32) | ID from the label, NULL if unreadable | scan |
| `block` | VARCHAR(8) | destination block | scan |
| `floor` | TINYINT | destination floor | scan |
| `unit` | VARCHAR(8) | destination unit | scan |
| `scan` | VARCHAR(16) | `ok` / `unreadable` / `unknown` | scan |
| `flips` | TINYINT | flips before the final scan | scan |
| `zone` | VARCHAR(8) | the gateway's decision | scan |
| `routed` | VARCHAR(8) | the gate Node 2 actually fired | sorted |
| `reason` | VARCHAR(16) | why `routed` ≠ `zone`, e.g. `timeout` | sorted |
| `result` | VARCHAR(16) | `confirmed` (bin IR saw it) / `missed` | sorted |
| `arrived_at` | DATETIME(3) | Node 1 detected it | arrival |
| `scanned_at` | DATETIME(3) | gateway decided its zone | scan |
| `sorted_at` | DATETIME(3) | Node 2 reported the result | sorted |

`zone` and `routed` are separate, so a scan that arrives late can't overwrite the gate
the parcel actually went through. A mismatch between them shows up as a timeout.

**`faults`**: one row per fault event.

| Column | Type | Meaning |
|---|---|---|
| `id` | INT, auto-increment | internal row id |
| `session` | VARCHAR(32) | run it happened in |
| `source` | VARCHAR(16) | `node1` / `node2` / `gateway` |
| `type` | VARCHAR(32) | `jam` / `missed_ir` / `scan_fail_repeat` / … |
| `seq` | INT | which parcel, if applicable |
| `detail` | VARCHAR(255) | free-text detail |
| `at` | DATETIME(3) | defaults to now |

### Open questions for the team
- Do the load cells under the baskets report a weight? If so, add `weight_g` to `sorted`.
- How long should Node 1 wait for a scan before it sends `MANUAL` with `reason: "timeout"`?
- Is the flip mechanism driven by Node 1? (This schema assumes it is.)

## Build / run

```bash
pip install sqlalchemy pymysql pandas python-dotenv
cp .env.example .env   # fill in your real password
python db.py           # should print 0 once schema.sql has been run
```

Credentials go in `.env` (gitignored); [`.env.example`](.env.example) shows the shape.
[`db.py`](db.py) is the shared connection helper for the subscriber, ML and dashboard.
