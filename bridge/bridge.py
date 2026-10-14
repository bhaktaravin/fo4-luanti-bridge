"""
Fallout 4 <-> Luanti bridge.

Runs a tiny local HTTP server that both games talk to:
  * The Luanti mod POSTs block place/dig events and its player position.
  * The Fallout 4 plugin POSTs its player position and polls for block events.

Coordinates:
  Luanti:    1 node = 1 metre, Y is up.
  Fallout 4: ~70 game units = 1 metre, Z is up.
The first Fallout position received becomes the Fallout "anchor"; the Luanti
anchor is set with the /fo_anchor chat command (defaults to 0,0,0). Everything
is placed relative to those two points.

Usage:  python bridge.py            (listens on 127.0.0.1:8765)
        python bridge.py --port 9000
Only uses the Python standard library.
"""

import argparse
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

UNITS_PER_NODE = 70.0  # Fallout units per Luanti node (~1 m)
MAX_EVENTS = 5000      # keep a bounded history


class State:
    def __init__(self):
        self.lock = threading.Lock()
        self.events = []          # list of dicts, each with a "seq"
        self.next_seq = 1
        self.luanti_anchor = {"x": 0.0, "y": 0.0, "z": 0.0}
        self.fallout_anchor = None  # set from first Fallout position
        self.fallout_player = None  # latest Fallout position (Fallout coords)
        self.fallout_seen = 0.0
        self.luanti_player = None
        self.luanti_seen = 0.0

    # --- coordinate mapping -------------------------------------------------
    def luanti_to_fallout(self, p):
        """Luanti node pos -> Fallout world pos (needs a Fallout anchor)."""
        a, f = self.luanti_anchor, self.fallout_anchor
        return {
            "x": f["x"] + (p["x"] - a["x"]) * UNITS_PER_NODE,
            "y": f["y"] + (p["z"] - a["z"]) * UNITS_PER_NODE,  # Luanti Z -> Fallout Y
            "z": f["z"] + (p["y"] - a["y"]) * UNITS_PER_NODE,  # Luanti Y (up) -> Fallout Z (up)
        }

    def fallout_to_luanti(self, p):
        a, f = self.luanti_anchor, self.fallout_anchor
        return {
            "x": a["x"] + (p["x"] - f["x"]) / UNITS_PER_NODE,
            "y": a["y"] + (p["z"] - f["z"]) / UNITS_PER_NODE,
            "z": a["z"] + (p["y"] - f["y"]) / UNITS_PER_NODE,
        }

    def add_event(self, ev):
        ev["seq"] = self.next_seq
        self.next_seq += 1
        self.events.append(ev)
        if len(self.events) > MAX_EVENTS:
            self.events = self.events[-MAX_EVENTS:]


STATE = State()


class Handler(BaseHTTPRequestHandler):
    server_version = "FO4LuantiBridge/0.1"

    # quieter logging
    def log_message(self, fmt, *args):
        if self.server.verbose:
            super().log_message(fmt, *args)

    def _json(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self):
        n = int(self.headers.get("Content-Length", 0) or 0)
        if n == 0:
            return {}
        try:
            return json.loads(self.rfile.read(n))
        except json.JSONDecodeError:
            return None

    # ------------------------------------------------------------------ GET
    def do_GET(self):
        url = urlparse(self.path)
        s = STATE

        if url.path == "/status":
            with s.lock:
                now = time.time()
                return self._json(200, {
                    "events": len(s.events),
                    "next_seq": s.next_seq,
                    "fallout_connected": now - s.fallout_seen < 3,
                    "luanti_connected": now - s.luanti_seen < 3,
                    "fallout_anchor": s.fallout_anchor,
                    "luanti_anchor": s.luanti_anchor,
                })

        if url.path == "/fallout/poll":
            # Fallout plugin asks: "what happened since seq N?"
            q = parse_qs(url.query)
            since = int(q.get("since", ["0"])[0])
            with s.lock:
                s.fallout_seen = time.time()
                if s.fallout_anchor is None:
                    return self._json(200, {"events": [], "last_seq": since,
                                            "note": "send /fallout/state first"})
                out = []
                for ev in s.events:
                    if ev["seq"] > since:
                        e = dict(ev)
                        e["fo_pos"] = s.luanti_to_fallout(ev["pos"])
                        out.append(e)
                last = out[-1]["seq"] if out else since
                return self._json(200, {"events": out, "last_seq": last})

        return self._json(404, {"error": "not found"})

    # ----------------------------------------------------------------- POST
    def do_POST(self):
        url = urlparse(self.path)
        data = self._read_json()
        if data is None:
            return self._json(400, {"error": "bad json"})
        s = STATE

        if url.path == "/fallout/state":
            # {"x":..,"y":..,"z":..,"angle":..}  (Fallout world coords)
            with s.lock:
                pos = {k: float(data.get(k, 0)) for k in ("x", "y", "z")}
                if s.fallout_anchor is None:
                    s.fallout_anchor = dict(pos)
                    print(f"[bridge] Fallout anchor set to {pos}")
                s.fallout_player = {**pos, "angle": float(data.get("angle", 0))}
                s.fallout_seen = time.time()
            return self._json(200, {"ok": True})

        if url.path == "/fallout/reanchor":
            # Re-anchor at the Fallout player's current position.
            with s.lock:
                if s.fallout_player:
                    s.fallout_anchor = {k: s.fallout_player[k] for k in ("x", "y", "z")}
            return self._json(200, {"ok": True, "fallout_anchor": s.fallout_anchor})

        if url.path == "/luanti/anchor":
            with s.lock:
                s.luanti_anchor = {k: float(data.get(k, 0)) for k in ("x", "y", "z")}
                print(f"[bridge] Luanti anchor set to {s.luanti_anchor}")
            return self._json(200, {"ok": True})

        if url.path == "/luanti/sync":
            # {"events":[{"type":"place"|"dig","pos":{x,y,z},"node":"..."}],
            #  "player":{"x":..,"y":..,"z":..}}
            with s.lock:
                s.luanti_seen = time.time()
                for ev in data.get("events", []):
                    if ev.get("type") in ("place", "dig") and "pos" in ev:
                        s.add_event({
                            "type": ev["type"],
                            "pos": {k: float(ev["pos"][k]) for k in ("x", "y", "z")},
                            "node": ev.get("node", ""),
                        })
                if "player" in data:
                    s.luanti_player = data["player"]
                reply = {"ok": True, "fallout_player": None}
                if s.fallout_player and s.fallout_anchor and time.time() - s.fallout_seen < 3:
                    lp = s.fallout_to_luanti(s.fallout_player)
                    lp["angle"] = s.fallout_player["angle"]
                    reply["fallout_player"] = lp
            return self._json(200, reply)

        return self._json(404, {"error": "not found"})


def main():
    ap = argparse.ArgumentParser(description="Fallout 4 <-> Luanti bridge")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    srv = ThreadingHTTPServer((args.host, args.port), Handler)
    srv.verbose = args.verbose
    print(f"[bridge] listening on http://{args.host}:{args.port}  (Ctrl+C to stop)")
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        print("\n[bridge] stopped")


if __name__ == "__main__":
    main()
