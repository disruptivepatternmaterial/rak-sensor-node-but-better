#!/usr/bin/env python3
# region agent log
"""Debug-session serial capture (session 4378ff).

Replaces the session-5039a2 bridge, which produced garbage: three processes held
/dev/cu.usbmodem111401 at once and each read() stole bytes from the others, so
"=== rak-sensor-node ===" arrived as "=kno-oe==". This version refuses to read a
port that another process already holds, and waits for it to come free instead.

Two outputs, because the agent sandbox denies shell writes into .cursor/:
  - stdout: one human-readable line per console line, plus sentinels
  - /tmp/debug-4378ff.ndjson: one NDJSON record per line, tagged by hypothesis

Temporary debug instrumentation. Delete with the rest of the session's logging.
"""

import json
import os
import re
import select
import subprocess
import sys
import time
from glob import glob

LOG_PATH = "/tmp/debug-4378ff.ndjson"
SESSION = "4378ff"
RUN_ID = sys.argv[1] if len(sys.argv) > 1 else "run1"
DEADLINE = time.time() + (float(sys.argv[2]) if len(sys.argv) > 2 else 360)

# Which hypothesis each console line bears on.
#   H1 pack delivering no power, so neither sensor can answer
#   H2 one-wire conductor not landing on WB_IO1
#   H3 RS-485 A/B swapped, or no 12 V at the RK900
#   H4 cycle hangs, or never reaches sleep
#   H5 persisted brownout gate suppressing the battery ladder
#   E  running image is not the one we think it is
RULES = [
    (re.compile(r"RK900\s*:\s*(raw|wind)", re.I), "H3-REJECT"),
    (re.compile(r"RK900\s*:\s*no data|modbus attempt", re.I), "H1,H3"),
    (re.compile(r"battery\s*:.*\d+\.\d+ V|pack latched pid|raw v=", re.I), "H1-REJECT,H2-REJECT"),
    (re.compile(r"battery\s*:\s*no data|no announcement|silent cycle", re.I), "H1,H2"),
    (re.compile(r"brownout engaged|uplink\s*:\s*held", re.I), "H5"),
    (re.compile(r"^\[cycle", re.I), "H4"),
    (re.compile(r"sleep\s*:|wait\s*:", re.I), "H4-REJECT"),
    (re.compile(r"came from the watchdog", re.I), "H4"),
    (re.compile(r"firmware\s*:|features\s*:|interval\s*:|built\s*:|deveui|region\s*:", re.I), "E"),
]


def hypotheses_for(line):
    hit = []
    for pattern, ids in RULES:
        if pattern.search(line):
            hit.extend(ids.split(","))
    return ",".join(sorted(set(hit))) if hit else "unclassified"


def emit(message, data, hypothesis, echo=True):
    record = {
        "sessionId": SESSION,
        "runId": RUN_ID,
        "hypothesisId": hypothesis,
        "location": "serial:usbmodem",
        "message": message,
        "data": data,
        "timestamp": int(time.time() * 1000),
    }
    with open(LOG_PATH, "a") as handle:
        handle.write(json.dumps(record) + "\n")
    if echo:
        stamp = time.strftime("%H:%M:%S")
        print(f"[{stamp}] [{hypothesis}] {message}", flush=True)


def port_now():
    """Newest usbmodem node. Replugging can rename it, so never cache the name."""
    found = glob("/dev/cu.usbmodem*")
    if not found:
        return None
    return max(found, key=lambda p: os.stat(p).st_mtime)


def other_readers(port):
    """PIDs holding the port, excluding us. This is the check the old bridge lacked."""
    try:
        out = subprocess.run(["lsof", "-t", port], capture_output=True, text=True,
                             timeout=10).stdout.split()
    except Exception:
        return []
    return [p for p in out if p.strip() and p.strip() != str(os.getpid())]


emit("capture started", {"deadline_s": int(DEADLINE - time.time())}, "E")

attached = 0
lines_seen = 0
blocked_notified = False

while time.time() < DEADLINE:
    port = port_now()
    if not port:
        time.sleep(0.25)
        continue

    # A second reader silently corrupts every line, so wait it out rather than join it.
    busy = other_readers(port)
    if busy:
        if not blocked_notified:
            emit("port held by another process — waiting for it to close",
                 {"port": port, "pids": busy}, "E")
            print("=== CAPTURE BLOCKED: unplug and replug the USB cable ===", flush=True)
            blocked_notified = True
        time.sleep(2)
        continue
    blocked_notified = False

    attached += 1
    emit("usb attached", {"port": port, "attach_count": attached}, "E")
    print("=== CAPTURE ATTACHED ===", flush=True)
    subprocess.run(["stty", "-f", port, "115200", "raw", "-echo"],
                   stderr=subprocess.DEVNULL)
    try:
        fd = os.open(port, os.O_RDONLY | os.O_NONBLOCK)
    except OSError as exc:
        emit("open failed", {"errno": getattr(exc, "errno", None)}, "E")
        time.sleep(1)
        continue

    buffer = b""
    try:
        while time.time() < DEADLINE:
            ready, _, _ = select.select([fd], [], [], 1.0)
            if not ready:
                continue
            chunk = os.read(fd, 256)
            if not chunk:
                break
            for byte in chunk:
                if byte in (10, 13):
                    if buffer.strip():
                        text = buffer.decode("utf-8", "replace").strip()
                        lines_seen += 1
                        emit(text, {"seq": lines_seen}, hypotheses_for(text))
                    buffer = b""
                else:
                    buffer += bytes([byte])
                    if len(buffer) > 400:
                        buffer = buffer[-400:]
    except OSError as exc:
        emit("usb detached", {"errno": getattr(exc, "errno", None),
                              "lines_so_far": lines_seen}, "E")
    finally:
        os.close(fd)
    time.sleep(0.25)

emit("capture finished", {"attaches": attached, "lines": lines_seen}, "E")
print(f"=== CAPTURE DONE: {lines_seen} line(s), {attached} attach(es) ===", flush=True)
# endregion
