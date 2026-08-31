#!/usr/bin/env python3
"""Instrumented soak engine. Driven by scripts/soak.sh -- see docs/SOAK.md.

A soak that only proves "it did not crash" wastes the hours it takes. This runs the
node for a configurable span and answers the questions that no other test can:

  * does the node come back from every sleep, or does it reset partway through one
  * does the frame counter advance one per cycle, or are cycles going missing
  * after a long run of no-evidence cycles, does the node still transmit once anyway
  * does the pack voltage drift over the span

Two things about this hardware shape the design.

**The serial port disappears on every sleep.** `power.cpp` calls
`TinyUSBDevice.detach()` before sleeping, which removes the USB device from the bus --
so `/dev/cu.usbmodem*` ceases to exist until the node wakes and re-enumerates. A plain
`cat /dev/cu.usbmodem*` therefore dies at the first sleep and looks exactly like a
crashed node. Absence of the port is the *expected* steady state of a healthy sleeping
node and is never treated as a failure here; the monitor polls for it and reattaches.

**Serial alone cannot see a missed uplink.** The console says a frame was handed to the
radio, not that the network received it. TTN's `last_f_cnt_up` is the only witness for
that, and a gap in it is the single most valuable signal a soak produces. It is polled
on an interval and every jump larger than one is recorded.
"""

from __future__ import annotations

import argparse
import glob
import json
import os
import re
import signal
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

try:
    import serial  # pyserial, installed alongside PlatformIO on the build host
except ImportError:  # pragma: no cover - reported by soak.sh before it gets here
    sys.stderr.write("pyserial missing: pip3 install pyserial\n")
    sys.exit(2)


# --------------------------------------------------------------------- log markers
# Every pattern below is anchored on a literal the firmware prints today. A marker that
# silently stops matching would turn this harness into a very expensive way of proving
# nothing, so the source line is named next to each one.
RE_BANNER = re.compile(r"=== rak-sensor-node ===")                # main.cpp boot banner
RE_WATCHDOG = re.compile(r"last reset came from the watchdog")    # main.cpp:134
RE_COMMIT = re.compile(r"commit\s*:\s*([0-9a-f]{7,40})")            # main.cpp print_banner()
RE_BOOTNUM = re.compile(r"config\s*:\s*interval\s+\d+\s*s,\s*boot\s*#(\d+)")  # config.cpp:85
RE_CYCLE = re.compile(r"\[cycle (\d+)\]")                         # main.cpp:183
RE_BATTERY_V = re.compile(r"battery\s*:\s*(\d+)\.(\d{2})\s*V")    # battery.cpp:1444
RE_SENT = re.compile(r"radio\s*:\s*sent (\d+) bytes on port (\d+)")  # radio.cpp:328
RE_JOINED = re.compile(r"radio\s*:\s*joined after")               # radio.cpp:239
RE_JOIN_FAIL = re.compile(r"radio\s*:\s*join failed")             # radio.cpp:258
RE_SLEEP = re.compile(r"sleep\s*:\s*(\d+) s")                     # main.cpp:327
RE_BROWNOUT = re.compile(r"power\s*:\s*.*(holding transmissions|recovered, resuming"
                         r"|brownout hold restored)")             # power.cpp:163/207/229/239
RE_KEEPALIVE = re.compile(r"uplink\s*:\s*(keepalive|proof of life)")  # main.cpp:282/292
RE_QUIET = re.compile(r"uplink\s*:\s*nothing to send \((\d+) quiet")  # main.cpp:276


def utc() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


class Soak:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.outdir = Path(args.outdir)
        self.outdir.mkdir(parents=True, exist_ok=True)
        self.raw = open(self.outdir / "serial.log", "a", buffering=1)
        self.events = open(self.outdir / "events.log", "a", buffering=1)

        self.started = time.time()
        self.deadline = self.started + args.seconds
        self.stop_flag = False

        self.cycles: list[int] = []
        self.voltages: list[float] = []
        self.uplinks_logged = 0
        self.joins = 0
        self.join_failures = 0
        self.sleeps = 0
        self.keepalives = 0
        self.boots: list[int] = []
        self.watchdog_resets = 0
        self.brownouts = 0
        self.reattaches = 0
        self.anomalies: list[dict] = []
        self.fcnt_samples: list[dict] = []
        self.fcnt_gaps: list[dict] = []
        self.last_cycle: int | None = None
        self.cycle_had_battery = False
        self.cycles_without_battery = 0
        self.heartbeats = 0
        # None until the run fails a validity requirement. A soak that never observed the
        # node must not exit 0 -- on 2026-08-12 a run that never attached was recorded as
        # a started soak and three documents inherited the claim (#107, AGENTS.md).
        self.invalid_reason: str | None = None

        signal.signal(signal.SIGTERM, self._signal)
        signal.signal(signal.SIGINT, self._signal)

    def _signal(self, *_: object) -> None:
        self.stop_flag = True

    # ------------------------------------------------------------------ logging
    def event(self, kind: str, msg: str) -> None:
        line = f"{utc()} {kind:<9} {msg}"
        self.events.write(line + "\n")
        print(line, flush=True)

    def anomaly(self, kind: str, msg: str) -> None:
        """An anomaly is recorded, named, and counted -- never left implicit in the raw log.

        A soak whose output is a 24-hour transcript nobody reads has measured nothing.
        """
        self.anomalies.append({"at": utc(), "kind": kind, "detail": msg})
        self.event("ANOMALY", f"{kind}: {msg}")

    # ------------------------------------------------------------------ serial
    def find_port(self) -> str | None:
        ports = sorted(glob.glob(self.args.port_glob))
        return ports[0] if ports else None

    def parse(self, line: str) -> None:
        self.raw.write(f"{utc()} {line}\n")

        if RE_BANNER.search(line):
            # A banner mid-soak means the node restarted. Whether that is benign is
            # decided by the boot counter and the watchdog warning that follow it.
            if self.cycles:
                self.anomaly("reboot", "boot banner after cycles had already started")

        m = RE_COMMIT.search(line)
        if m:
            # The soak is evidence about one image. Without the SHA the log cannot say
            # which, and H8 evidence is worthless unattributed (#73). A banner SHA that
            # disagrees with the tree the run was launched from means the board is not
            # running what the operator believes it is -- that is an anomaly, not a note.
            self.banner_commit = m.group(1)
            if self.args.commit not in ("unknown", "") and \
               not self.banner_commit.startswith(self.args.commit[:7]) and \
               not self.args.commit.startswith(self.banner_commit[:7]):
                self.anomaly("commit-mismatch",
                             f"board banner says {self.banner_commit}, "
                             f"run was launched from {self.args.commit}")

        if RE_WATCHDOG.search(line):
            self.watchdog_resets += 1
            self.anomaly("watchdog-reset", "node reports its last reset came from the WDT")

        m = RE_BOOTNUM.search(line)
        if m:
            n = int(m.group(1))
            if self.boots and n != self.boots[-1] + 1:
                self.anomaly("boot-jump", f"boot #{self.boots[-1]} -> #{n}")
            self.boots.append(n)
            self.event("boot", f"boot #{n}")

        m = RE_CYCLE.search(line)
        if m:
            n = int(m.group(1))
            self._close_out_cycle()
            self.last_cycle = n
            self.cycles.append(n)

        m = RE_BATTERY_V.search(line)
        if m:
            self.cycle_had_battery = True
            self.voltages.append(int(m.group(1)) + int(m.group(2)) / 100.0)

        if RE_SENT.search(line):
            self.uplinks_logged += 1
        if RE_JOINED.search(line):
            self.joins += 1
        if RE_JOIN_FAIL.search(line):
            self.join_failures += 1
            self.anomaly("join-failed", line.strip())
        if RE_SLEEP.search(line):
            self.sleeps += 1
        if RE_BROWNOUT.search(line):
            self.brownouts += 1
            self.anomaly("brownout", line.strip())
        if RE_KEEPALIVE.search(line):
            # Issue #45: bounded silence. The node must break a long quiet run with one
            # transmission rather than going dark indefinitely.
            self.keepalives += 1
            self.event("keepalive", line.strip())
        m = RE_QUIET.search(line)
        if m:
            self.event("quiet", f"{m.group(1)} quiet cycle(s)")

    def _close_out_cycle(self) -> None:
        """Account for the cycle in progress. Called on the next cycle marker AND at end
        of run -- previously the final cycle was never checked for a missing battery
        sample, so a pack that died on the last cycle went uncounted (#107)."""
        if self.last_cycle is not None and not self.cycle_had_battery:
            self.cycles_without_battery += 1
            self.anomaly("no-battery", f"cycle {self.last_cycle} produced no pack voltage")
        self.cycle_had_battery = False

    # ------------------------------------------------------------------ TTN
    def poll_ttn(self) -> None:
        """Record TTN's frame counter. Never fatal -- a network hiccup is not a soak failure."""
        cmd = ["ttn-lw-cli", "end-device", "get", self.args.ttn_app, self.args.ttn_device,
               "--session", "--mac-state"]
        try:
            out = subprocess.run(cmd, capture_output=True, text=True, timeout=60,
                                 env={**os.environ, "NO_COLOR": "1"})
        except (OSError, subprocess.TimeoutExpired) as exc:
            self.event("ttn", f"poll failed: {exc}")
            return
        if out.returncode != 0:
            self.event("ttn", f"poll returned {out.returncode}: {out.stderr.strip()[:200]}")
            return

        fcnt = None
        try:
            doc = json.loads(out.stdout)
            fcnt = doc.get("session", {}).get("last_f_cnt_up")
        except json.JSONDecodeError:
            m = re.search(r'"last_f_cnt_up"\s*:\s*(\d+)', out.stdout)
            if m:
                fcnt = int(m.group(1))
        if fcnt is None:
            self.event("ttn", "no last_f_cnt_up in the reply (device not joined yet?)")
            return

        fcnt = int(fcnt)
        sample = {"at": utc(), "f_cnt_up": fcnt, "cycle": self.last_cycle,
                  "uplinks_logged": self.uplinks_logged}
        if self.fcnt_samples:
            prev = self.fcnt_samples[-1]
            delta = fcnt - prev["f_cnt_up"]
            expected = self.uplinks_logged - prev["uplinks_logged"]
            if delta < 0:
                self.anomaly("fcnt-reset", f"f_cnt_up went {prev['f_cnt_up']} -> {fcnt}")
            elif expected > 0 and delta < expected:
                # The console said it handed N frames to the radio; the network counted
                # fewer. That difference is a missed uplink, and serial cannot see it.
                gap = {"at": utc(), "from": prev["f_cnt_up"], "to": fcnt,
                       "counted": delta, "expected": expected}
                self.fcnt_gaps.append(gap)
                self.anomaly("fcnt-gap",
                             f"{expected} uplink(s) logged on serial, f_cnt_up advanced {delta}")
        self.fcnt_samples.append(sample)
        self.event("ttn", f"last_f_cnt_up={fcnt} (serial uplinks so far: {self.uplinks_logged})")

    # ------------------------------------------------------------------ main loop
    def run(self) -> int:
        a = self.args
        self.event("start", f"soak {a.seconds}s port_glob={a.port_glob} "
                            f"heartbeat={a.heartbeat}s ttn={'off' if a.no_ttn else a.ttn_interval}s")
        (self.outdir / "soak.pid").write_text(f"{os.getpid()}\n")

        port: serial.Serial | None = None
        port_name = ""
        next_beat = self.started + a.heartbeat
        next_ttn = self.started + a.ttn_interval
        buf = b""

        while not self.stop_flag and time.time() < self.deadline:
            now = time.time()

            if now >= next_beat:
                self.heartbeats += 1
                elapsed = int(now - self.started)
                self.event("HEARTBEAT",
                           f"=== SOAK HEARTBEAT {self.heartbeats} === {utc()} "
                           f"elapsed={elapsed}s of {a.seconds}s cycles={len(self.cycles)} "
                           f"uplinks={self.uplinks_logged} anomalies={len(self.anomalies)} "
                           f"port={'attached' if port else 'absent (asleep?)'}")
                next_beat = now + a.heartbeat

            # Validity deadlines (#107). A soak that never saw the node must end as a
            # failure, not run out the clock and look like a pass -- that exact shape
            # produced a false "24 h soak started" evidence entry on 2026-08-12
            # (AGENTS.md, "Record outcomes, never launches"). 0 disables a deadline for
            # a console-less field configuration.
            if a.first_attach_deadline > 0 and self.reattaches == 0 \
                    and now - self.started > a.first_attach_deadline:
                self.invalid_reason = (f"no port attach within {a.first_attach_deadline}s "
                                       f"(glob {a.port_glob})")
                self.anomaly("never-attached", self.invalid_reason)
                break
            if a.first_cycle_deadline > 0 and not self.cycles \
                    and now - self.started > a.first_cycle_deadline:
                self.invalid_reason = f"no cycle observed within {a.first_cycle_deadline}s"
                self.anomaly("no-first-cycle", self.invalid_reason)
                break

            # TTN polling waits for a window where the port is gone whenever it can. The
            # node is asleep then, so nothing is lost while the CLI takes its seconds --
            # unless the poll is badly overdue, at which point an on-time sample matters
            # more than a few buffered console bytes.
            if not a.no_ttn and now >= next_ttn:
                overdue = now >= next_ttn + a.ttn_interval
                if port is None or overdue:
                    self.poll_ttn()
                    next_ttn = time.time() + a.ttn_interval

            if port is None:
                found = self.find_port()
                if found is None:
                    time.sleep(1)
                    continue
                try:
                    candidate = serial.Serial(found, a.baud, timeout=1)
                except (OSError, serial.SerialException) as exc:
                    # Losing the race against re-enumeration is routine, not an anomaly.
                    self.event("port", f"{found} not openable yet: {exc}")
                    time.sleep(1)
                    continue
                try:
                    # Asserted for the same reason _serial_capture.py asserts them: the
                    # CDC console stays quiet otherwise. Kept separate from the open so a
                    # port without modem control lines -- a pty, which is what the
                    # selftest fixture hands over -- still gets read instead of being
                    # dropped mid-configuration and silently read by nobody.
                    candidate.dtr = True
                    candidate.rts = True
                except (OSError, serial.SerialException) as exc:
                    self.event("port", f"{found} has no modem control lines: {exc}")
                port = candidate
                port_name = found
                self.reattaches += 1
                self.event("port", f"attached {found} (attach #{self.reattaches})")
                buf = b""
                continue

            try:
                chunk = port.read(256) or b""
            except (OSError, serial.SerialException):
                chunk = b""
                self.event("port", f"{port_name} vanished -- node is sleeping (expected)")
                try:
                    port.close()
                except Exception:
                    pass
                port = None
                continue

            if chunk:
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode(errors="replace").rstrip("\r")
                    if text:
                        self.parse(text)

        if port is not None:
            try:
                port.close()
            except Exception:
                pass

        # One last TTN sample so the closing frame count is the real one, not the one
        # from up to an interval ago.
        if not a.no_ttn:
            self.poll_ttn()

        # The final cycle is still open here; account for it before the summary (#107).
        self._close_out_cycle()
        if self.invalid_reason is None and not self.cycles:
            self.invalid_reason = "zero cycles observed -- the run measured nothing"
            self.anomaly("zero-observations", self.invalid_reason)

        self.event("stop", f"soak ended after {int(time.time() - self.started)}s")
        self.write_summary()
        return 0 if self.invalid_reason is None else 1

    # ------------------------------------------------------------------ summary
    def write_summary(self) -> None:
        elapsed = int(time.time() - self.started)
        v = self.voltages
        summary = {
            "started_utc": datetime.fromtimestamp(self.started, timezone.utc)
                                   .strftime("%Y-%m-%dT%H:%M:%SZ"),
            "ended_utc": utc(),
            "elapsed_s": elapsed,
            "requested_s": self.args.seconds,
            "completed_full_duration": elapsed >= self.args.seconds - 5,
            # False when the run never observed the node or breached a validity deadline.
            # Duration alone is not a pass -- a reader of this file must not be able to
            # mistake a run that measured nothing for one that measured 24 h (#107).
            "valid_run": self.invalid_reason is None,
            "invalid_reason": self.invalid_reason,
            "label": self.args.label,
            "commit": self.args.commit,
            "banner_commit": getattr(self, "banner_commit", None),
            "host": self.args.host,
            "cycles_seen": len(self.cycles),
            "first_cycle": self.cycles[0] if self.cycles else None,
            "last_cycle": self.cycles[-1] if self.cycles else None,
            "sleeps_entered": self.sleeps,
            "uplinks_logged_on_serial": self.uplinks_logged,
            "joins": self.joins,
            "join_failures": self.join_failures,
            "keepalive_transmissions": self.keepalives,
            "boot_numbers": self.boots,
            "unexpected_reboots": sum(1 for x in self.anomalies if x["kind"] in
                                      ("reboot", "boot-jump")),
            "watchdog_resets": self.watchdog_resets,
            "brownout_events": self.brownouts,
            "cycles_without_battery": self.cycles_without_battery,
            "port_reattaches": self.reattaches,
            "battery_v_min": round(min(v), 2) if v else None,
            "battery_v_max": round(max(v), 2) if v else None,
            "battery_v_mean": round(sum(v) / len(v), 2) if v else None,
            "battery_samples": len(v),
            "ttn_f_cnt_first": self.fcnt_samples[0]["f_cnt_up"] if self.fcnt_samples else None,
            "ttn_f_cnt_last": self.fcnt_samples[-1]["f_cnt_up"] if self.fcnt_samples else None,
            "ttn_f_cnt_gaps": len(self.fcnt_gaps),
            "ttn_f_cnt_gap_detail": self.fcnt_gaps,
            "ttn_samples": len(self.fcnt_samples),
            "anomaly_count": len(self.anomalies),
            "anomalies": self.anomalies,
        }
        (self.outdir / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")

        # Markdown block shaped for docs/EVIDENCE.md. It states what was observed and
        # stops there -- no gate is declared closed by a script.
        f = self.fcnt_samples
        md = [
            f"### Soak — {summary['started_utc']} → {summary['ended_utc']}",
            "",
            f"- **Valid run: {'yes' if summary['valid_run'] else 'NO — ' + str(self.invalid_reason)}**",
            f"- Host: `{self.args.host}` · commit `{self.args.commit}` · label `{self.args.label}`",
            f"- Board banner commit: `{getattr(self, 'banner_commit', None) or 'NOT OBSERVED'}`",
            f"- Duration: {elapsed} s of {self.args.seconds} s requested "
            f"({'full' if summary['completed_full_duration'] else 'CUT SHORT'})",
            f"- Cycles seen: {len(self.cycles)} "
            f"(first {summary['first_cycle']}, last {summary['last_cycle']}) · "
            f"sleeps entered: {self.sleeps} · port reattaches: {self.reattaches}",
            f"- Uplinks logged on serial: {self.uplinks_logged} · joins: {self.joins} · "
            f"join failures: {self.join_failures} · keepalive/proof-of-life TX: {self.keepalives}",
            f"- TTN `last_f_cnt_up`: {summary['ttn_f_cnt_first']} → "
            f"{summary['ttn_f_cnt_last']} over {len(f)} poll(s) · gaps: {len(self.fcnt_gaps)}",
            f"- Watchdog resets: {self.watchdog_resets} · unexpected reboots: "
            f"{summary['unexpected_reboots']} · boot numbers: {self.boots or 'none seen'}",
            f"- Brownout events: {self.brownouts} · cycles with no pack voltage: "
            f"{self.cycles_without_battery}",
            f"- Pack voltage: min {summary['battery_v_min']} V · max {summary['battery_v_max']} V"
            f" · mean {summary['battery_v_mean']} V over {len(v)} sample(s)",
            f"- Anomalies flagged: {len(self.anomalies)}",
        ]
        if self.anomalies:
            md.append("")
            md.append("| when | kind | detail |")
            md.append("|---|---|---|")
            for x in self.anomalies[:50]:
                md.append(f"| {x['at']} | {x['kind']} | {x['detail'][:120]} |")
            if len(self.anomalies) > 50:
                md.append(f"| … | … | {len(self.anomalies) - 50} more in `summary.json` |")
        md.append("")
        md.append("Status: 🚧 NOT YET DEPLOYED. Sleep current is not measurable in software — "
                  "see [SOAK.md](SOAK.md) for the PPK2 procedure.")
        md.append("")
        (self.outdir / "summary.md").write_text("\n".join(md))

        print("\n=== SOAK SUMMARY ===", flush=True)
        print("\n".join(md), flush=True)
        print(f"=== SOAK DONE anomalies={len(self.anomalies)} "
              f"fcnt_gaps={len(self.fcnt_gaps)} ===", flush=True)


def main() -> int:
    p = argparse.ArgumentParser(description="Instrumented soak monitor (see docs/SOAK.md)")
    p.add_argument("--seconds", type=int, default=86400)
    p.add_argument("--outdir", required=True)
    p.add_argument("--port-glob", default="/dev/cu.usbmodem*")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--heartbeat", type=int, default=300)
    # A healthy node mid-sleep keeps the port absent for up to one full uplink interval --
    # 900 s at the field cadence (persisted config; see src/config.h and docs/EVIDENCE.md
    # 2026-08-13) -- so the attach deadline is one interval plus margin, and the first
    # [cycle] marker can be almost two intervals out when the soak starts just after a
    # cycle began. 0 disables a deadline (console-less field runs).
    p.add_argument("--first-attach-deadline", type=int, default=1200,
                   help="fail the run if no port ever attaches within N s (0 = off)")
    p.add_argument("--first-cycle-deadline", type=int, default=2100,
                   help="fail the run if no cycle is observed within N s (0 = off)")
    p.add_argument("--ttn-app", default="my-app-tobi")
    p.add_argument("--ttn-device", default="puma-concolor-001")
    p.add_argument("--ttn-interval", type=int, default=900)
    p.add_argument("--no-ttn", action="store_true")
    p.add_argument("--label", default="bench")
    p.add_argument("--commit", default="unknown")
    p.add_argument("--host", default=os.uname().nodename)
    return Soak(p.parse_args()).run()


if __name__ == "__main__":
    sys.exit(main())
