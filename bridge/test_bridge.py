"""Simulates both games talking to bridge.py. Run: python test_bridge.py"""
import json
import os
import subprocess
import sys
import time
import urllib.request

PORT = 8799
BASE = f"http://127.0.0.1:{PORT}"


def call(path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(BASE + path, data=data,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=2) as r:
        return json.loads(r.read())


here = os.path.dirname(os.path.abspath(__file__))
srv = subprocess.Popen([sys.executable, os.path.join(here, "bridge.py"), "--port", str(PORT)])
try:
    time.sleep(0.8)

    # Fallout reports position -> becomes anchor
    call("/fallout/state", {"x": 1000, "y": 2000, "z": 50, "angle": 1.57})
    # Luanti anchors at 10,5,10 and places a block 2 east, 1 up, 3 north
    call("/luanti/anchor", {"x": 10, "y": 5, "z": 10})
    r = call("/luanti/sync", {"events": [
        {"type": "place", "pos": {"x": 12, "y": 6, "z": 13}, "node": "default:stone"}],
        "player": {"x": 10, "y": 5, "z": 10}})
    fp = r["fallout_player"]
    assert (fp["x"], fp["y"], fp["z"]) == (10, 5, 10), fp  # Fallout player shown at Luanti anchor

    p = call("/fallout/poll?since=0")
    ev = p["events"][0]
    assert ev["fo_pos"] == {"x": 1140.0, "y": 2210.0, "z": 120.0}, ev["fo_pos"]
    assert p["last_seq"] == 1

    call("/luanti/sync", {"events": [{"type": "dig", "pos": {"x": 12, "y": 6, "z": 13}}]})
    p2 = call(f"/fallout/poll?since={p['last_seq']}")
    assert [e["type"] for e in p2["events"]] == ["dig"]

    # Fallout player walks 140 units east -> 2 nodes east in Luanti
    call("/fallout/state", {"x": 1140, "y": 2000, "z": 50, "angle": 0})
    r = call("/luanti/sync", {"events": []})
    assert r["fallout_player"]["x"] == 12

    s = call("/status")
    assert s["fallout_connected"] and s["luanti_connected"]
    print("ALL BRIDGE TESTS PASSED")
finally:
    srv.terminate()
