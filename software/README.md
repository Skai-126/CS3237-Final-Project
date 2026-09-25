# Software

| Folder | Component | Tech (suggested) |
|---|---|---|
| [`firmware/`](firmware/) | ESP32 code: sensor reading, edge decisions, actuators, MQTT client | Arduino / PlatformIO, FreeRTOS |
| [`gateway/`](gateway/) | Optional smartphone app / gateway | _TBD_ |
| [`backend/`](backend/) | MQTT broker, REST API, database | Mosquitto, Python (FastAPI/Flask), SQLite/InfluxDB |
| [`ml/`](ml/) | Data cleaning, training, evaluation, model serving | Python, scikit-learn, TensorFlow/Keras, Jupyter |
| [`dashboard/`](dashboard/) | Visualisation of long-term analytics | Grafana / Streamlit / web |

## Conventions
- Put Wi-Fi credentials, broker passwords and keys in `secrets.h` (firmware) or `.env` (backend); both are ignored by git. Commit a `secrets.example.h` / `.env.example` instead.
- MQTT topic format: `cs3237/g3/<device-id>/<sensor>` (e.g. `cs3237/g3/esp32-1/temperature`).
- Each folder has its own README explaining how to build/run it.
