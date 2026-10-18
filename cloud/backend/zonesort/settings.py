"""Django settings for the ZoneSort API.

The database comes from DB_URL in cloud/.env, the same one the subscriber uses, e.g.
    DB_URL=mysql+pymysql://zonesort:password@127.0.0.1:3306/zonesort
(sqlite:///path/to/file.db also works, for tests).
"""
import os
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit

from dotenv import load_dotenv

BASE_DIR = Path(__file__).resolve().parent.parent      # cloud/backend
CLOUD_DIR = BASE_DIR.parent                            # cloud
REPO_DIR = CLOUD_DIR.parent
FRONTEND_DIST = CLOUD_DIR / "frontend" / "dist"        # `npm run build` output

load_dotenv(CLOUD_DIR / ".env")
sys.path.insert(0, str(REPO_DIR / "ml"))               # features / forecast / train

# dev server on a LAN / Tailscale: no logins or user data here, so a fixed key is fine
SECRET_KEY = os.getenv("DJANGO_SECRET_KEY", "zonesort-dev-only")
DEBUG = os.getenv("DJANGO_DEBUG", "1") == "1"
ALLOWED_HOSTS = ["*"]

INSTALLED_APPS = ["api"]
MIDDLEWARE = ["django.middleware.common.CommonMiddleware"]
ROOT_URLCONF = "zonesort.urls"
WSGI_APPLICATION = "zonesort.wsgi.application"


def database_from_url(url: str) -> dict:
    u = urlsplit(url)
    scheme = u.scheme.split("+")[0]
    if scheme == "sqlite":
        return {"ENGINE": "django.db.backends.sqlite3", "NAME": url.split(":///", 1)[1]}
    if scheme == "mysql":
        return {"ENGINE": "django.db.backends.mysql", "NAME": u.path.lstrip("/"),
                "USER": unquote(u.username or ""), "PASSWORD": unquote(u.password or ""),
                "HOST": u.hostname or "127.0.0.1", "PORT": str(u.port or 3306),
                "OPTIONS": {"charset": "utf8mb4", "connect_timeout": 3}}
    raise ValueError(f"unsupported DB_URL scheme: {u.scheme}")


DATABASES = {"default": database_from_url(os.environ["DB_URL"])}

# the subscriber stores naive local times (DATETIME(3)), so no timezone conversion
USE_TZ = False
TIME_ZONE = "Asia/Singapore"
DEFAULT_AUTO_FIELD = "django.db.models.AutoField"
