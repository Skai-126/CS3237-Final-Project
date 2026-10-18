from django.http import FileResponse, HttpResponse
from django.urls import path, re_path
from django.views.static import serve

from api import views

from .settings import FRONTEND_DIST


def index(request):
    """Serve the built React app (cloud/frontend/dist), so one server runs the demo."""
    page = FRONTEND_DIST / "index.html"
    if not page.exists():
        return HttpResponse("Frontend not built: run `npm run build` in cloud/frontend, "
                            "or use the Vite dev server (`npm run dev`).", status=404,
                            content_type="text/plain")
    return FileResponse(page.open("rb"), content_type="text/html")


urlpatterns = [
    path("api/live", views.live),
    path("api/forecast", views.forecast),
    re_path(r"^assets/(?P<path>.*)$", serve, {"document_root": FRONTEND_DIST / "assets"}),
    path("", index),
]
