# Gateway: QR scanning and zone lookup

**Owner:** Wern She

The laptop gateway reads the QR label, validates its destination, chooses a zone,
and publishes the scan result for Node 1 and the cloud subscriber. The gateway
does not control the belt directly. Unknown and unreadable labels always go to
`MANUAL`.

## QR label format

QR codes contain a JSON object:

```json
{"parcel_id":"P0042","block":"123A","floor":7,"unit":"07-123"}
```

Use test labels for floors 1 and 5 (Zone A), floors 6 and 10 (Zone B), an
unknown block, and an unreadable/no-QR case. The prototype's known blocks are
listed in `KNOWN_BLOCKS` in `scanner.py`; agree the real test blocks with the
team and update that set.

## Zone and scan rules

| Condition | `scan` | `zone` | Label fields |
|---|---|---|---|
| QR decoded; block known; floor 1–5 | `ok` | `A` | included |
| QR decoded; block known; floor 6–10 | `ok` | `B` | included |
| QR decoded but destination unknown/unsupported | `unknown` | `MANUAL` | `null` |
| QR missing or unreadable after allowed attempts | `unreadable` | `MANUAL` | `null` |

`seq` comes from Node 1's arrival event and identifies the parcel. The gateway
must copy it into the scan event. `flips` is the number of flip commands issued
before the final scan. The proposed maximum attempts and scan timeout still need
agreement with the firmware owners.

## MQTT contract (proposed; confirm with firmware and cloud owners)

- Subscribe to `zonesort/node1/arrival` to receive `{"seq":17}`.
- If another scan attempt is needed, publish `{"seq":17}` to
  `zonesort/gateway/flip`.
- Publish the final scan event to `zonesort/gateway/scan`, for example:

```json
{"seq":17,"parcel_id":"P0042","block":"123A","floor":7,"unit":"07-123","scan":"ok","zone":"B","flips":1}
```

For unreadable/unknown labels, label fields are `null`, `scan` is respectively
`unreadable` or `unknown`, and `zone` is `MANUAL`. Parcel and fault messages use
QoS 1. Do not change topic names or fields without agreement because Node 1 and
the cloud subscriber depend on them.

## Setup and local test

From the repository root in Windows Terminal / PowerShell:

```powershell
py -m venv .venv
.venv\Scripts\Activate.ps1
py -m pip install -r gateway/requirements.txt
py -m unittest discover -s gateway -v
```

To test by pasting decoded QR JSON text:

```powershell
py gateway/scanner.py --seq 17 --flips 0
```

To decode a QR code saved as an image:

```powershell
py gateway/scanner.py --image path\to\label.png --seq 17 --flips 0
```

The script currently prints the scan event. MQTT connection and live camera
capture are the next implementation steps after the team confirms the message
contract and the printed-label tests pass.

## Configuration and security

Copy `.env.example` to `.env` for local broker settings. Never commit `.env`,
Wi-Fi passwords, MQTT passwords, or bot tokens. QR labels should use synthetic
test details, not real residents' personal information.
