#!/usr/bin/env python3
"""Turn a Saleae analog export of the RAK9154 data line into a verdict.

Seven GPIO pads have been destroyed across two RAK4631 cores (#102), always the pad carrying
the pack's data line, and that line's voltage has never been measured on any node. This reads
the capture and answers the one question that gates connecting another core: does the pack put
more on that wire than an nRF52840 pad can survive?

The thresholds are not opinions. They come from the parts:

  nRF52840 GPIO, powered      VDD + 0.3 V ~= 3.6 V     [CIT-NRF-GPIO]
  nRF52840 GPIO, unpowered    0.3 V                    [CIT-NRF-BACKPOWER]
  Saleae Logic Pro 8          -25 V .. +25 V abs max,  [CIT-SALEAE-LOGICPRO8]
                              -10 V .. +10 V analog range, 2 MOhm || 10 pF

The analyzer has roughly 7x the pad's tolerance and loads the line with 2 MOhm against the
pack's measured 15 kOhm pull-down, so it reads the answer without spending a core. Its range
saturates at +/-10 V: a reading pinned there means "at least 10 V", which already convicts.

Why a capture and not a meter: every dead pad measures a few ohms to GROUND, which is the
LOWER clamp's failure direction, and no mechanism proposed so far predicts that
[CIT-NRF-GNDLIFT]. A meter averages a negative transient away. This looks for it explicitly.

Usage:
    scripts/owprobe.py /tmp/rak-owprobe/live/analog.csv
    scripts/owprobe.py /tmp/rak-owprobe/live/analog.csv --label "pack live, harness off node"

Exit status:
    0  line is within what a powered pad tolerates
    2  line is out of spec -- do not connect a core
    1  the capture could not be read, or is not evidence of anything
"""

from __future__ import annotations

import argparse
import csv
import statistics
import sys

# CITE(datasheet): [CIT-NRF-GPIO] nRF52840 Product Specification, GPIO -- absolute maximum on
#   any GPIO is VDD + 0.3 V. At VDD = 3.3 V that is 3.6 V, and absolute maximum is a damage
#   threshold, not an operating point.
PAD_ABS_MAX_V = 3.6

# CITE(prior-art): [CIT-NRF-BACKPOWER] Nordic DevZone -- "for an unpowered device, max GPIO
#   voltage is 0.3 V. Any voltage above this level will make the ESD protection diode conduct
#   and you will backpower the device via the GPIO."
PAD_ABS_MAX_UNPOWERED_V = 0.3

# The nominal rail the pack's logic is assumed to reference. Assumed, never measured -- which
# is the entire reason this script exists.
NOMINAL_LOGIC_V = 3.3

# CITE(datasheet): [CIT-SALEAE-LOGICPRO8] Logic Pro 8 data sheet -- analog input range is
#   -10 V to +10 V and saturates outside it, so a sample at the rail is a floor, not a value.
SALEAE_ANALOG_RANGE_V = 10.0
SALEAE_SATURATION_MARGIN_V = 0.05

# A capture that never leaves this band is not showing a driven logic line.
FLOATING_PROBE_MAX_ABS_V = 0.30

# ...but "near zero" alone does not distinguish an unconnected clip from a wire genuinely tied
# to ground, and those two mean opposite things. NOISE separates them, by two orders of
# magnitude. Both figures measured on this analyzer 2026-08-30:
#
#   open clip, nothing connected   stdev 78.43 mV, 77 distinct codes in 1639 samples
#   connected, line held at 0 V    stdev  0.74 mV,  6 distinct codes in 786429 samples
#
# A 2 MOhm input with nothing on it acts as an antenna and wanders; a low-impedance tie to
# ground does not. 20 mV sits ~4x above the connected case and ~4x below the floating one.
FLOATING_NOISE_STDEV_V = 0.020

# A negative excursion this far below the pack's own ground is the signature worth catching:
# it forward-biases the pad's LOWER clamp, which is the direction all seven dead pads failed in.
NEGATIVE_ALARM_V = -0.30


def read_analog_csv(path: str) -> tuple[list[float], list[float], str]:
    """Return (times, volts, channel_name) from a Saleae raw analog CSV export."""
    times: list[float] = []
    volts: list[float] = []
    with open(path, newline="") as fh:
        reader = csv.reader(fh)
        try:
            header = next(reader)
        except StopIteration:
            raise SystemExit(f"ERROR {path} is empty -- the export wrote no rows.")
        if len(header) < 2:
            raise SystemExit(
                f"ERROR {path} has {len(header)} column(s); expected 'Time [s]' plus at least "
                "one channel. Export analog channels, not digital."
            )
        channel = header[1].strip()
        for row in reader:
            if len(row) < 2:
                continue
            try:
                times.append(float(row[0]))
                volts.append(float(row[1]))
            except ValueError:
                continue
    if not volts:
        raise SystemExit(f"ERROR {path} has a header but no numeric samples.")
    return times, volts, channel


def percentile(sorted_v: list[float], q: float) -> float:
    if not sorted_v:
        raise ValueError("empty")
    idx = int(round(q * (len(sorted_v) - 1)))
    return sorted_v[max(0, min(len(sorted_v) - 1, idx))]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", help="Saleae raw analog CSV export (e.g. .../analog.csv)")
    ap.add_argument("--label", default="", help="what was probed, for the printed record")
    args = ap.parse_args()

    times, volts, channel = read_analog_csv(args.csv)

    lo, hi = min(volts), max(volts)
    mean = statistics.mean(volts)
    ordered = sorted(volts)
    # The idle level is what the line sits at most of the time. A high percentile is the right
    # estimator because a half-duplex line spends its time idle and dips for start bits, so the
    # mean is pulled down by traffic and understates the level the pad actually has to survive.
    p99 = percentile(ordered, 0.99)
    p50 = percentile(ordered, 0.50)
    p01 = percentile(ordered, 0.01)
    span_s = (times[-1] - times[0]) if len(times) > 1 else 0.0
    neg_samples = sum(1 for v in volts if v < NEGATIVE_ALARM_V)
    sat_hi = sum(1 for v in volts if v >= SALEAE_ANALOG_RANGE_V - SALEAE_SATURATION_MARGIN_V)
    sat_lo = sum(1 for v in volts if v <= -SALEAE_ANALOG_RANGE_V + SALEAE_SATURATION_MARGIN_V)

    print("=== one-wire data line probe ===")
    if args.label:
        print(f"   probed  : {args.label}")
    print(f"   file    : {args.csv}")
    print(f"   channel : {channel}")
    print(f"   samples : {len(volts)} over {span_s:.3f} s")
    print(f"   min     : {lo:+.3f} V")
    print(f"   p01     : {p01:+.3f} V")
    print(f"   median  : {p50:+.3f} V")
    print(f"   p99     : {p99:+.3f} V   <- treat this as the idle level")
    print(f"   max     : {hi:+.3f} V")
    print(f"   mean    : {mean:+.3f} V")
    print()

    if sat_hi or sat_lo:
        print(f"!! SATURATED: {sat_hi} sample(s) at +{SALEAE_ANALOG_RANGE_V} V, "
              f"{sat_lo} at -{SALEAE_ANALOG_RANGE_V} V.")
        print("   The analyzer clips outside its analog range, so the real level is at least")
        print("   this far out and possibly much further. Treat as convicted, not measured.")
        print()

    verdict = 0

    stdev = statistics.pstdev(volts) if len(volts) > 1 else 0.0
    print(f"   stdev   : {stdev * 1000:.3f} mV")
    print()

    if max(abs(lo), abs(hi)) < FLOATING_PROBE_MAX_ABS_V:
        if stdev > FLOATING_NOISE_STDEV_V:
            print("=== NOT EVIDENCE -- PROBE IS FLOATING ===")
            print(f"   Everything stays inside +/-{FLOATING_PROBE_MAX_ABS_V} V and the line carries "
                  f"{stdev * 1000:.1f} mV of noise,")
            print(f"   above the {FLOATING_NOISE_STDEV_V * 1000:.0f} mV floating threshold. A 2 MOhm input with nothing")
            print("   on it wanders like an antenna. Check the clip and the ground lead.")
            return 1

        print("=== CANNOT ANSWER THE QUESTION -- DRIVER IS DE-ENERGISED ===")
        print(f"   The line is genuinely CONNECTED and genuinely at 0 V: {stdev * 1000:.2f} mV of noise is")
        print("   ~100x quieter than an open clip, so this is a low-impedance tie to ground, not")
        print("   a floating probe. But that is the expected reading for a driver with no rail.")
        print()
        print("   The pack's data-line reference is its own pin 4 (`3V3_In`), and pin 4 is fed FROM")
        print("   the node. With the harness unplugged from the node, pin 4 has no supply, so the")
        print("   pack's driver has no rail and the line rests at 0 V through the pack's measured")
        print("   15 kOhm pull-down. That happens whether the harness is lethal or benign.")
        print()
        print("   So a 0 V reading here CLEARS NOTHING. To see what the pack actually drives, its")
        print("   pin 4 has to be energised from 3.3 V while the data wire goes only to the")
        print("   analyzer. The base board's 3V3 regulator is on the BASE BOARD, not the Core")
        print("   [CIT-RAK19007], so a powered base board with NO CORE FITTED presents 3.3 V on")
        print("   VDD and exposes no nRF52840 pad at all. See docs/HARDWARE.md")
        print("   § 'Qualifying the pack harness'.")
        return 1

    if neg_samples:
        pct = 100.0 * neg_samples / len(volts)
        print(f"!! NEGATIVE EXCURSION: {neg_samples} sample(s) ({pct:.3f} %) below "
              f"{NEGATIVE_ALARM_V} V, floor {lo:+.3f} V.")
        print("   This is the direction that matches the failure signature. All seven dead pads")
        print("   measure a few ohms to GROUND, which is the lower ESD clamp's failure mode, and")
        print("   no mechanism proposed so far predicts it [CIT-NRF-GNDLIFT]. A driven-negative")
        print("   line does. Capture this before connecting anything.")
        print()
        verdict = 2

    if p99 > SALEAE_ANALOG_RANGE_V - SALEAE_SATURATION_MARGIN_V:
        print("=== THIS IS THE PIN KILLER ===")
        print(f"   Idle level is at or beyond the analyzer's +{SALEAE_ANALOG_RANGE_V} V range against a")
        print(f"   {PAD_ABS_MAX_V} V pad maximum. DO NOT CONNECT ANOTHER CORE TO THIS HARNESS.")
        verdict = 2
    elif p99 > 5.0:
        print("=== THIS IS THE PIN KILLER ===")
        print(f"   Idle level {p99:+.3f} V against a {PAD_ABS_MAX_V} V pad absolute maximum")
        print(f"   ({p99 / PAD_ABS_MAX_V:.1f}x over). DO NOT CONNECT ANOTHER CORE TO THIS HARNESS.")
        print("   A series resistor does not make this safe -- it needs a level translator, and")
        print("   a 1 kOhm resistor was already inline when SDA/P0.13 died (#101).")
        verdict = 2
    elif p99 > PAD_ABS_MAX_V:
        print("=== OUT OF SPEC ===")
        print(f"   Idle level {p99:+.3f} V exceeds the pad absolute maximum of {PAD_ABS_MAX_V} V.")
        print("   Every connection made so far has been overstressing the pad. Needs a level")
        print("   translator or an isolation switch, not a resistor (#101).")
        verdict = 2
    elif verdict == 0:
        print("=== WITHIN A POWERED PAD'S RATING ===")
        print(f"   Idle level {p99:+.3f} V, at or below the {PAD_ABS_MAX_V} V pad maximum")
        print(f"   (nominal logic rail is {NOMINAL_LOGIC_V} V).")
        print()
        print("   This CLEARS the harness as a gross-overvoltage source. It does NOT close #102:")
        print(f"   an unpowered pad's maximum is {PAD_ABS_MAX_UNPOWERED_V} V, so this level is still roughly")
        print(f"   {p99 / PAD_ABS_MAX_UNPOWERED_V:.0f}x over whenever the core is dark with the harness mated, which is")
        print("   what the connector-sequencing rule in docs/HARDWARE.md exists for.")

    print()
    print("Record the numbers above in docs/EVIDENCE.md with the date, the host, and this file")
    print("path. A verdict with no number attached is how this reached seven pads.")
    return verdict


if __name__ == "__main__":
    sys.exit(main())
