#!/usr/bin/env python3
"""A synthetic RAK4631 for testing the soak harness. Driven by `scripts/soak.sh selftest`.

The one behavior that has repeatedly been misread as a failure is that this board's
serial port *disappears* while it sleeps -- `power.cpp` detaches the USB device, so the
`/dev/cu.usbmodem*` node is gone until the next wake. Any capture that treats a missing
port as a dead node will call a perfectly healthy soak a crash.

That path cannot be exercised on real hardware on demand: you cannot ask the node to
vanish at a chosen moment, and a 24 h soak is a poor place to discover the harness got
it wrong. So this stands in -- a pty whose symlink is created on wake and removed on
sleep, printing the same log lines the firmware prints. It fabricates nothing about the
hardware; it is a fixture for the tooling, and no reading it emits may ever be recorded
as evidence.
"""

from __future__ import annotations

import argparse
import os
import pty
import time
from pathlib import Path


def emit(fd: int, text: str) -> None:
    os.write(fd, (text + "\r\n").encode())


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--portdir", required=True)
    p.add_argument("--seconds", type=int, default=120)
    p.add_argument("--awake", type=float, default=6.0, help="seconds of console per cycle")
    p.add_argument("--asleep", type=float, default=8.0, help="seconds with no USB device")
    args = p.parse_args()

    portdir = Path(args.portdir)
    portdir.mkdir(parents=True, exist_ok=True)
    link = portdir / "cu.usbmodemFAKE1"

    deadline = time.time() + args.seconds
    cycle = 0
    boot = 41
    voltage = 1223  # centivolts, walked down slightly each cycle

    while time.time() < deadline:
        master, slave = pty.openpty()
        if link.exists() or link.is_symlink():
            link.unlink()
        link.symlink_to(os.ttyname(slave))

        # Give the monitor a moment to notice the port, the way re-enumeration does.
        time.sleep(1.0)

        if cycle == 0:
            emit(master, "=== rak-sensor-node ===")
            emit(master, "firmware : 0.0.0-selftest")
            emit(master, f"   config  : interval 300 s, boot #{boot}")
        cycle += 1
        emit(master, f"[cycle {cycle}]")
        emit(master, f"   battery : {voltage // 100}.{voltage % 100:02d} V  +0.00 A  98%  23.0 C")
        emit(master, "   radio   : sent 18 bytes on port 2")
        emit(master, "   sleep   : 8 s")
        voltage -= 1

        time.sleep(max(0.0, args.awake - 1.0))

        # Sleep: the USB device goes away entirely.
        link.unlink(missing_ok=True)
        os.close(master)
        os.close(slave)
        time.sleep(args.asleep)

    link.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
