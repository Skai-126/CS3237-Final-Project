# cloud: event store & dashboard

**Owner:** Shu Kai

Stores every parcel event and shows live counts, faults and forecasts.

## Scope
- Parcel event schema, shared by `node1/`, `node2/` and `gateway/`
- Event storage (platform to be decided)
- Dashboard: counts by block, unreadable count, faults, forecast panel

## Event schema
_Draft coming. Starting point from the proposal: timestamp, block, floor, scan outcome, sort outcome._

## MQTT topics
_To be agreed with the gateway and firmware owners._

## Build / run
_To be added. Credentials go in `.env`; commit a `.env.example`._
