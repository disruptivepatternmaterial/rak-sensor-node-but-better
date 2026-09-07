#!/usr/bin/env python3
"""owprobe.py three-channel guards, on synthetic Saleae version-0 exports.

This exists because the three-channel path printed "WITHIN THE GPIO'S INSTANTANEOUS RAIL LIMITS"
for a capture in which data, VDD and ground were all flat 0 V — a tool whose whole job is to keep
the next measurement off a pad, reporting "I saw nothing" as "safe". The first case below is that
capture.

The fifth case is the one that must never regress the other way: an unpowered mate has no rail
and no traffic, so every qualification gate here is unsatisfiable, and it still has to convict on
the −7.84 V excursion rather than being dismissed as inconclusive.

Run: python3 scripts/tests/test_owprobe_guards.py   (from the repo root; no hardware, ~11 s)
"""
import os
import random
import struct
import subprocess
import sys
import tempfile
from array import array

RATE = 1_562_500
DOWNSAMPLE = 1


def write(path, volts, begin=0.0):
    with open(path, "wb") as fh:
        fh.write(b"<SALEAE>")
        fh.write(struct.pack("<ii", 0, 1))
        fh.write(struct.pack("<dQQQ", begin, RATE, DOWNSAMPLE, len(volts)))
        array("f", volts).tofile(fh)


def run(*args):
    p = subprocess.run([sys.executable, "scripts/owprobe.py", *args],
                       capture_output=True, text=True)
    banner = [l for l in p.stdout.splitlines() if l.startswith("===")]
    return p.returncode, (banner[-1] if banner else (p.stderr.strip().splitlines() or [""])[-1])


d = tempfile.mkdtemp()
n = int(RATE * 2.0)  # 2 s
rnd = random.Random(7)

# 1. every channel flat 0 V — the case that used to print the pass banner.
zeros = [0.0] * n
write(f"{d}/z0.bin", zeros)
write(f"{d}/z1.bin", zeros)
write(f"{d}/z2.bin", zeros)

# 2. a healthy powered capture: 3.3 V rail, data toggling between rails.
gnd = [rnd.gauss(0, 0.001) for _ in range(n)]
vdd = [3.3 + rnd.gauss(0, 0.002) for _ in range(n)]
data = [(3.3 if (i // 163) % 2 else 0.05) + rnd.gauss(0, 0.002) for i in range(n)]
write(f"{d}/g_data.bin", data)
write(f"{d}/g_vdd.bin", vdd)
write(f"{d}/g_gnd.bin", gnd)

# 3. powered, but the data line never moves off idle HIGH.
idle = [3.3 + rnd.gauss(0, 0.002) for _ in range(n)]
write(f"{d}/i_data.bin", idle)

# 4. unpowered mate: no rail at all, but a -7.84 V excursion on the data wire.
mate = list(zeros)
for i in range(1000, 3500):
    mate[i] = -7.84
write(f"{d}/m_data.bin", mate)

# 5. too short: 0.5 s of the healthy capture.
short = int(RATE * 0.5)
write(f"{d}/s_data.bin", data[:short])
write(f"{d}/s_vdd.bin", vdd[:short])
write(f"{d}/s_gnd.bin", gnd[:short])

cases = [
    ("all three channels flat 0 V", (f"{d}/z0.bin", f"{d}/z1.bin", f"{d}/z2.bin"), 1),
    ("same file passed three times", (f"{d}/g_data.bin", f"{d}/g_data.bin", f"{d}/g_data.bin"), 1),
    ("healthy powered capture", (f"{d}/g_data.bin", f"{d}/g_vdd.bin", f"{d}/g_gnd.bin"), 0),
    ("powered, line never driven", (f"{d}/i_data.bin", f"{d}/g_vdd.bin", f"{d}/g_gnd.bin"), 1),
    ("unpowered mate, -7.84 V seen", (f"{d}/m_data.bin", f"{d}/z1.bin", f"{d}/z2.bin"), 2),
    ("0.5 s capture", (f"{d}/s_data.bin", f"{d}/s_vdd.bin", f"{d}/s_gnd.bin"), 1),
]

fails = 0
for name, (dp, vp, gp), want in cases:
    code, banner = run(dp, "--vdd-bin", vp, "--gnd-bin", gp)
    ok = code == want
    fails += not ok
    print(f"{'PASS' if ok else 'FAIL'}  {name:<32} exit={code} (want {want})  {banner}")

print("FAILURES:", fails)
sys.exit(1 if fails else 0)
