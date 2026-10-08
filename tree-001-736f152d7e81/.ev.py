#!/usr/bin/env python3
"""Stream TTN events for puma-concolor-001.

The point of this is to tell two very different failures apart. A `join.request` event means
the gateway heard the device and the problem is somewhere in the keys or the accept; no events
at all means nothing on the network ever heard the transmission, which is a gateway, antenna,
or channel problem instead.
"""
import json
import os
import sys
import time
import urllib.request

KEY = os.environ["TTN_KEY"]
APP = "my-app-tobi"
DEV = "puma-concolor-001"
WINDOW = int(os.environ.get("WINDOW", "150"))

body = json.dumps({
    "identifiers": [{"device_ids": {"application_ids": {"application_id": APP}, "device_id": DEV}}],
    "tail": 20,
}).encode()

req = urllib.request.Request(
    "https://nam1.cloud.thethings.network/api/v3/events",
    data=body,
    headers={"Authorization": f"Bearer {KEY}", "Content-Type": "application/json"},
)

print(f"-- listening {WINDOW}s for events on {DEV} --", flush=True)
seen = 0
start = time.time()
try:
    with urllib.request.urlopen(req, timeout=WINDOW + 10) as r:
        for raw in r:
            if time.time() - start > WINDOW:
                break
            raw = raw.strip()
            if not raw:
                continue
            try:
                ev = json.loads(raw).get("result", {})
            except json.JSONDecodeError:
                continue
            name = ev.get("name", "?")
            seen += 1
            detail = ""
            data = ev.get("data") or {}
            if "join_request" in json.dumps(data)[:2000] or name.startswith("js."):
                detail = json.dumps(data)[:220]
            if name.endswith(".fail") or "error" in name:
                detail = json.dumps(data)[:400]
            print(f"{ev.get('time','')[:19]}  {name}  {detail}", flush=True)
except urllib.error.HTTPError as e:
    print(f"HTTP {e.code}: {e.read().decode()[:400]}")
    sys.exit(1)
except Exception as e:  # noqa: BLE001 - the stream just ends when the window closes
    print(f"stream ended: {type(e).__name__}: {e}")

print(f"-- {seen} event(s) --")
