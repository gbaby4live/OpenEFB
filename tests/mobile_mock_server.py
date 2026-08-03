"""Local-only smoke server for exercising the OpenEFB mobile UI without X-Plane."""

from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse
import json
import os

ROOT = Path(__file__).resolve().parents[1] / "resources" / "mobile"
TOKEN = "mock-session-token"
SNAPSHOT = {
    "version": "2.0", "readOnly": False,
    "telemetry": {"available": True, "aircraft": "Cessna 172", "lat": 24.5561, "lon": -81.7565, "altitudeFt": 4500, "groundSpeedKt": 118, "headingDeg": 91, "verticalSpeedFpm": 120},
    "route": {"available": True, "revision": 7, "activeIndex": 1, "legs": [
        {"id": "KEYW", "kind": 0, "lat": 24.5561, "lon": -81.7596, "altitudeFt": 0, "active": False},
        {"id": "DEEDS", "kind": 3, "lat": 24.70, "lon": -81.20, "altitudeFt": 4000, "active": True},
        {"id": "KMIA", "kind": 0, "lat": 25.7959, "lon": -80.2870, "altitudeFt": 0, "active": False},
    ]},
    "progress": {"available": True, "active": "DEEDS", "distanceNm": 22.4, "destination": "KMIA", "destinationDistanceNm": 109.5},
    "weather": {"departure": {"id": "KEYW", "metar": "KEYW 031753Z 09012KT 10SM FEW025 30/24 A2998"}, "destination": {"id": "KMIA", "metar": "KMIA 031753Z 11009KT 10SM SCT035 31/23 A2997"}},
    "traffic": [{"callsign": "AAL421", "lat": 24.8, "lon": -81.4, "altitudeFt": 7000, "headingDeg": 215}],
}
AIRPORT = {"state": "ready", "id": "KMIA", "name": "Miami International", "message": "Installed X-Plane airport and procedure data", "approaches": [
    {"id": "I09", "name": "ILS Runway 09", "runway": "09", "transitions": [{"id": "DEEDS", "legs": [{"id": "DEEDS", "sequence": 10, "altitudeFt": 4000}, {"id": "JARCO", "sequence": 20, "altitudeFt": 3000}]}], "finalLegs": [{"id": "JARCO", "sequence": 30, "altitudeFt": 3000}, {"id": "RW09", "sequence": 40, "altitudeFt": 0}]},
    {"id": "R27", "name": "RNAV Runway 27", "runway": "27", "transitions": [], "finalLegs": [{"id": "RW27", "sequence": 10, "altitudeFt": 0}]},
]}
LIBRARY = {"airport": "KMIA", "message": "DEP/DEST briefs and charts synchronized", "entries": [{"id": 0, "category": "chart", "name": "KMIA ILS RWY 09.pdf"}, {"id": 1, "category": "document", "name": "KEYW-KMIA Flight Briefing.pdf"}]}


class Handler(SimpleHTTPRequestHandler):
    def translate_path(self, path):
        relative = urlparse(path).path.lstrip("/") or "index.html"
        return os.fspath(ROOT / relative)

    def json_response(self, status, value):
        body = json.dumps(value).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def authorized(self, query):
        return query.get("token", [""])[0] == TOKEN

    def do_GET(self):
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)
        if parsed.path == "/api/v2/session":
            return self.json_response(200, {"version": "2.0", "token": TOKEN}) if query.get("code", [""])[0] == "123456" else self.json_response(401, {"error": "pairing code required"})
        if parsed.path.startswith("/api/v2/") and not self.authorized(query):
            return self.json_response(401, {"error": "session expired"})
        if parsed.path == "/api/v2/snapshot": return self.json_response(200, SNAPSHOT)
        if parsed.path == "/api/v2/airport": return self.json_response(200, AIRPORT)
        if parsed.path == "/api/v2/library": return self.json_response(200, LIBRARY)
        if parsed.path == "/api/v2/command": return self.json_response(200, {"done": True, "success": True, "message": "Mock command applied"})
        if parsed.path == "/api/v2/library/file":
            body = b"%PDF-1.4\n1 0 obj<</Type/Catalog/Pages 2 0 R>>endobj\n2 0 obj<</Type/Pages/Count 0/Kids[]>>endobj\ntrailer<</Root 1 0 R>>\n%%EOF"
            self.send_response(200); self.send_header("Content-Type", "application/pdf"); self.send_header("Content-Length", str(len(body))); self.end_headers(); return self.wfile.write(body)
        return super().do_GET()

    def do_POST(self):
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)
        if parsed.path != "/api/v2/commands" or not self.authorized(query): return self.json_response(401, {"error": "session expired"})
        length = int(self.headers.get("Content-Length", "0")); self.rfile.read(length)
        return self.json_response(202, {"commandId": 1})


if __name__ == "__main__":
    ThreadingHTTPServer(("127.0.0.1", 8765), Handler).serve_forever()
