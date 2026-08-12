#!/usr/bin/env python3
"""Serial capture that survives a DFU flash.

Why this exists: after `pio run -t upload` the RAK4631's USB CDC device disappears and
takes up to two minutes to come back — the nRF52 bootloader activates the new image, the
board resets, and only then does the host re-enumerate. A capture helper that gives up
after thirty seconds sees nothing and reports silence, which reads exactly like a dead
board. That false negative cost several observations on 2026-08-12 and produced a wrong
entry in docs/EVIDENCE.md before the network side contradicted it.

It also has to survive the *other* disconnect: the field image sleeps with system-off, so
the CDC device drops on every sleep and returns on every wake. A capture spanning more
than one cycle must reattach rather than exit.

Usage:
    scripts/capture.py --duration 120                 # capture 120 s, auto-detect port
    scripts/capture.py --duration 3600 --log soak.log # append to a file
    scripts/capture.py --duration 60 --port /dev/cu.usbmodem31101

Sentinels printed on stdout, stable enough to grep or feed to notify_on_output:
    === CAPTURE WAITING <port> ===      looking for the device
    === CAPTURE ATTACHED <port> ===     device open, lines follow
    === CAPTURE DETACHED ===            device went away, will look again
    === CAPTURE DONE lines=<n> ===      duration elapsed

Exit status is 0 when at least one line was captured, 3 when the device never appeared.
"""

import argparse
import glob
import os
import select
import subprocess
import sys
import time

PORT_GLOB = "/dev/cu.usbmodem*"

# Two minutes is the observed re-enumeration time after a DFU flash on this board; three
# is the working number so a slow activation is not mistaken for a dead board.
DEFAULT_ATTACH_TIMEOUT = 180.0


def find_port(explicit):
    if explicit:
        return explicit if os.path.exists(explicit) else None
    ports = sorted(glob.glob(PORT_GLOB))
    return ports[0] if ports else None


def other_reader(port):
    """Return a description of another process holding the port, or None.

    A second reader does not fail cleanly — both readers get a fraction of the bytes and
    the capture looks corrupt rather than contended, which is a genuinely confusing way
    to lose an hour.
    """
    try:
        out = subprocess.run(
            ["lsof", "-t", port], capture_output=True, text=True, timeout=5
        ).stdout.split()
    except (OSError, subprocess.SubprocessError):
        return None
    pids = [p for p in out if p.strip() and p.strip() != str(os.getpid())]
    if not pids:
        return None
    names = []
    for pid in pids:
        try:
            names.append(
                "%s(%s)"
                % (
                    subprocess.run(
                        ["ps", "-o", "comm=", "-p", pid],
                        capture_output=True,
                        text=True,
                        timeout=5,
                    ).stdout.strip(),
                    pid,
                )
            )
        except (OSError, subprocess.SubprocessError):
            names.append("pid %s" % pid)
    return ", ".join(names)


def configure(port, baud):
    subprocess.run(
        ["stty", "-f", port, str(baud), "raw", "-echo"], stderr=subprocess.DEVNULL
    )


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--duration", type=float, default=120.0, help="seconds to capture")
    ap.add_argument("--port", default=None, help="device path (default: first usbmodem)")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument(
        "--attach-timeout",
        type=float,
        default=DEFAULT_ATTACH_TIMEOUT,
        help="seconds to wait for the device to appear (default %d)"
        % DEFAULT_ATTACH_TIMEOUT,
    )
    ap.add_argument("--log", default=None, help="append captured lines to this file")
    args = ap.parse_args()

    sink = open(args.log, "a", buffering=1) if args.log else None

    def emit(text):
        stamped = "%s %s" % (time.strftime("%Y-%m-%d %H:%M:%S"), text)
        print(stamped, flush=True)
        if sink:
            sink.write(stamped + "\n")

    start = time.time()
    deadline = start + args.duration
    lines = 0
    attached_once = False
    announced_wait = False

    while time.time() < deadline:
        port = find_port(args.port)
        if port is None:
            if not announced_wait:
                emit("=== CAPTURE WAITING %s ===" % (args.port or PORT_GLOB))
                announced_wait = True
            # Only enforce the attach timeout before the first attach. Once the board has
            # been seen, a disappearing port is a sleep cycle, not a missing board, and
            # the capture should keep waiting until its own duration runs out.
            if not attached_once and time.time() > start + args.attach_timeout:
                emit(
                    "=== CAPTURE GAVE UP after %.0fs, device never appeared ==="
                    % args.attach_timeout
                )
                return 3
            time.sleep(0.2)
            continue

        held_by = other_reader(port)
        if held_by:
            emit("=== CAPTURE REFUSED %s held by %s ===" % (port, held_by))
            return 4

        try:
            configure(port, args.baud)
            fd = os.open(port, os.O_RDONLY | os.O_NONBLOCK)
        except OSError:
            time.sleep(0.2)
            continue

        emit("=== CAPTURE ATTACHED %s ===" % port)
        attached_once = True
        announced_wait = False
        buf = b""

        while time.time() < deadline:
            try:
                ready, _, _ = select.select([fd], [], [], 0.5)
            except OSError:
                break
            if not ready:
                continue
            try:
                chunk = os.read(fd, 512)
            except OSError:
                break
            if not chunk:
                break  # device went away
            for byte in chunk:
                if byte in (10, 13):
                    if buf.strip():
                        emit(buf.decode("utf-8", "replace").rstrip())
                        lines += 1
                    buf = b""
                else:
                    buf += bytes([byte])

        try:
            os.close(fd)
        except OSError:
            pass
        if time.time() < deadline:
            emit("=== CAPTURE DETACHED ===")

    emit("=== CAPTURE DONE lines=%d ===" % lines)
    if sink:
        sink.close()
    return 0 if lines else 3


if __name__ == "__main__":
    sys.exit(main())
