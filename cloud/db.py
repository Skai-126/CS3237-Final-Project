"""Shared MySQL connection for the subscriber, ML and dashboard.

Usage:
    from db import get_engine
    engine = get_engine()
"""
import os

from dotenv import load_dotenv
from sqlalchemy import create_engine
from sqlalchemy.engine import Engine

load_dotenv()

_engine: Engine | None = None


def get_engine() -> Engine:
    global _engine
    if _engine is None:
        _engine = create_engine(os.environ["DB_URL"])
    return _engine


if __name__ == "__main__":
    from sqlalchemy import text

    with get_engine().connect() as conn:
        print(conn.execute(text("SELECT COUNT(*) FROM parcels")).scalar())
