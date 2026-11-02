"""Shared MQTT connection for the subscriber, integration checker and fake-event generator.

Settings come from cloud/.env:
    MQTT_HOST, MQTT_PORT          broker address (8883 for the team broker, 1883 for a local one)
    MQTT_USERNAME, MQTT_PASSWORD  login, if the broker needs one
    MQTT_CA_CERT                  the broker's certificate; setting it turns on TLS

Usage:
    from mqtt_conn import connect
    connect(client)
"""
import os
from pathlib import Path

import paho.mqtt.client as mqtt
from dotenv import load_dotenv

load_dotenv()


def connect(client: mqtt.Client) -> None:
    """Apply login and TLS from .env, then connect (keeps paho's auto-reconnect)."""
    if os.getenv("MQTT_USERNAME"):
        client.username_pw_set(os.environ["MQTT_USERNAME"], os.getenv("MQTT_PASSWORD"))
    ca = os.getenv("MQTT_CA_CERT")
    if ca:
        path = Path(ca) if Path(ca).is_absolute() else Path(__file__).parent / ca
        client.tls_set(ca_certs=str(path))  # also checks the broker's IP/name against the cert
    client.connect(os.getenv("MQTT_HOST", "127.0.0.1"), int(os.getenv("MQTT_PORT", "1883")))
