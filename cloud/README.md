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

Draft, built from [`schema.sql`](schema.sql). Will likely shift once the gateway and
firmware owners lock down their actual MQTT payloads.

**`parcels`** — one row per parcel, filled in incrementally as events arrive. A scan
event and its matching sort event write to the *same* row (keyed on `UNIQUE (session,
seq)`, via `INSERT ... ON DUPLICATE KEY UPDATE`) instead of needing a join later.

| Column | Type | Meaning |
|---|---|---|
| `id` | INT, auto-increment | internal row id |
| `session` | VARCHAR(32) | one prototype run = one "delivery day" |
| `seq` | INT | Node 1's arrival number for this parcel |
| `parcel_id` | VARCHAR(32) | the parcel's ID from its label |
| `block` | VARCHAR(8) | destination block |
| `floor` | TINYINT | destination floor |
| `zone` | VARCHAR(8) | `A` / `B` / `MANUAL` |
| `scan` | VARCHAR(16) | `ok` / `no_label` / `unreadable` / `unknown_block` |
| `result` | VARCHAR(16) | `confirmed` / `missed` |
| `reason` | VARCHAR(16) | e.g. `timeout` |
| `scanned_at` | DATETIME(3) | when the gateway scanned the label |
| `sorted_at` | DATETIME(3) | when Node 2 actually sorted it |

**`faults`** — one row per fault event.

| Column | Type | Meaning |
|---|---|---|
| `id` | INT, auto-increment | internal row id |
| `session` | VARCHAR(32) | which run it happened in |
| `type` | VARCHAR(32) | `jam` / `missed_ir` / etc. |
| `seq` | INT | which parcel, if applicable |
| `detail` | VARCHAR(255) | free-text detail |
| `at` | DATETIME(3) | defaults to now |

## MQTT topics
_To be agreed with the gateway and firmware owners._

## Build / run

```bash
pip install sqlalchemy pymysql pandas python-dotenv
cp .env.example .env   # fill in your real password
python db.py           # should print 0 once schema.sql has been run
```

Credentials go in `.env` (gitignored); [`.env.example`](.env.example) shows the shape.
[`db.py`](db.py) is the shared connection helper for the subscriber, ML and dashboard.
