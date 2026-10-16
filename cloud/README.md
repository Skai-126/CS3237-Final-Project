# cloud: event store & dashboard

**Owner:** Shu Kai

Stores every parcel event and shows live counts, faults and forecasts.

## Scope
- Parcel event schema, shared by `node1/`, `node2/` and `gateway/`
- Event storage (platform to be decided)
- Dashboard: counts by block, unreadable count, faults, forecast panel

## Database

MySQL 8.4 (Community Server), running on Shu Kai's laptop. Schema: [`schema.sql`](schema.sql)
(`parcels` + `faults` tables — one row per parcel, joined on `(session, seq)` via
`INSERT ... ON DUPLICATE KEY UPDATE` as scan and sort events arrive).

Teammates connect with HeidiSQL to `<tailscale-ip>:3306` using the read-only `viewer`
account (ask Shu Kai for the password). Don't port-forward 3306 on a home router.

## Event schema
_Draft coming. Starting point from the proposal: timestamp, block, floor, scan outcome, sort outcome._

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
