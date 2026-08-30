#!/usr/bin/env python3
"""Capture the one-wire data line with a Saleae Logic and say who, if anyone, is talking.

Three sessions have argued about the RAK9154 one-wire link from the node's side alone, where
"no reply, 0 bytes" is indistinguishable between a pack that never spoke, a pack that spoke
too quietly for the pin to frame, and a pin that is damaged. A logic capture separates them,
so this script exists to replace inference with a measurement.

What it reports, and why each one settles a different argument:

  edges            zero edges means nobody drove the line at all — the pack is silent and our
                   receiver is exonerated.
  idle level       the analog value while the line rests. The nRF52840 needs 0.7 x VDD = 2.31 V
                   to guarantee a HIGH [CIT-NRF-GPIO-TOTAL]; the pack's 15 kohm pull-down
                   against the chip's 13 kohm internal pull-up lands near 1.8 V, which is below
                   it. A line with edges but a sub-threshold idle is a level problem, not a
                   protocol one, and no amount of firmware fixes it.
  shortest pulse   inverse of the baud. 9600 8N1 puts one bit at 104 us.

CITE(prior-art): Saleae Logic 2 automation API — Manager.connect(), CaptureConfiguration with
  TimedCaptureMode, and Capture.export_raw_data_csv(). The automation server is off by default
  and is enabled either in Preferences or with the --automation launch flag.
  https://saleae.github.io/logic2-automation/
CITE(prior-art): [CIT-NRF-GPIO-TOTAL] — VIH = 0.7 x VDD and the 11/13/16 kohm internal pull
  range, which is what makes the idle-level number above actionable rather than merely curious.
CITE(prior-art): [CIT-RAK-ONEWIRESERIAL] — the pack announces unprompted, so a passive capture
  with no node attached is a valid test of whether the pack transmits at all. Marked prior-art,
  not datasheet: the registry classifies it as a GitHub library rather than a vendor document.
"""

import argparse
import csv
import os
import sys

# Analog is what matters here. A digital-only capture would report a clean idle HIGH or LOW
# according to the threshold the analyzer happens to use, which is precisely the question being
# asked — so the level has to be measured, not thresholded.
DEFAULT_ANALOG_HZ = 1_562_500
DEFAULT_DIGITAL_HZ = 12_000_000

# 0.7 x VDD for VDD = 3.3 V. Below this the nRF52840 does not guarantee a HIGH is seen.
VIH = 2.31
# 0.3 x VDD. Above this a LOW is not guaranteed either; between the two is the forbidden zone.
VIL = 0.99


def summarize(csv_path: str, channel_label: str) -> int:
    """Read the exported analog CSV and print the three numbers that settle the argument."""
    samples = []
    with open(csv_path, newline="") as handle:
        reader = csv.reader(handle)
        header = next(reader, None)
        if header is None:
            print("no data in export")
            return 1
        for row in reader:
            if len(row) < 2:
                continue
            try:
                samples.append((float(row[0]), float(row[1])))
            except ValueError:
                continue

    if not samples:
        print("no samples in export")
        return 1

    voltages = [v for _, v in samples]
    vmin, vmax = min(voltages), max(voltages)
    # The resting level is the most common one. A line that is idle most of the time makes the
    # median a better estimate of "at rest" than the mean, which a burst of traffic would drag.
    ordered = sorted(voltages)
    median = ordered[len(ordered) // 2]

    # Count threshold crossings rather than sample-to-sample changes, so analog noise on a
    # resting line is not reported as traffic.
    edges = 0
    state = median > (VIH + VIL) / 2
    last_edge_t = None
    shortest = None
    for t, v in samples:
        new_state = state
        if state and v < VIL:
            new_state = False
        elif not state and v > VIH:
            new_state = True
        if new_state != state:
            edges += 1
            if last_edge_t is not None:
                width = t - last_edge_t
                if shortest is None or width < shortest:
                    shortest = width
            last_edge_t = t
            state = new_state

    span = samples[-1][0] - samples[0][0]
    print(f"channel        : {channel_label}")
    print(f"samples        : {len(samples)} over {span:.3f} s")
    print(f"idle (median)  : {median:.3f} V")
    print(f"range          : {vmin:.3f} V .. {vmax:.3f} V")
    print(f"edges          : {edges}")
    if shortest:
        print(f"shortest pulse : {shortest * 1e6:.1f} us  (~{1.0 / shortest:.0f} baud)")

    print()
    if edges == 0:
        print("VERDICT: nobody drove this line. The pack never transmitted, so a node-side")
        print("         'no reply, 0 bytes' is the correct and expected result.")
    elif median < VIH:
        print(f"VERDICT: there is traffic, but the line rests at {median:.2f} V, below the")
        print(f"         {VIH:.2f} V the nRF52840 needs to see a guaranteed HIGH. A UART start")
        print("         bit is a falling edge from HIGH, so framing is unreliable by level.")
        print("         This is a pull-up problem, not a firmware problem.")
    else:
        print("VERDICT: traffic present and the idle level is above threshold. The electrical")
        print("         layer is sound; look at framing, baud, and turnaround timing.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=20.0,
                        help="capture duration; must span at least one pack announcement")
    parser.add_argument("--channel", type=int, default=0, help="analog channel index")
    parser.add_argument("--out", default="/tmp/owscope",
                        help="output directory for the capture and its CSV export")
    parser.add_argument("--port", type=int, default=10430, help="Logic 2 automation port")
    parser.add_argument("--device", default=None,
                        help="Saleae device id; defaults to the first physical device found")
    args = parser.parse_args()

    try:
        from saleae import automation
    except ImportError:
        print("logic2-automation is not installed in this interpreter", file=sys.stderr)
        return 2

    os.makedirs(args.out, exist_ok=True)

    with automation.Manager.connect(port=args.port) as manager:
        # Logic 2 always offers simulation devices alongside any real hardware, and it refuses
        # to pick for you: with more than one candidate, start_capture() fails with
        # MissingDeviceError rather than choosing. So name the physical one explicitly. A
        # simulated capture would happily produce a clean square wave and prove nothing about
        # the wire, which is the only thing this script exists to measure.
        physical = [d for d in manager.get_devices(include_simulation_devices=False)
                    if not d.is_simulation]
        if not physical:
            print("no physical Saleae found — check it is on USB and that Logic 2 lists it",
                  file=sys.stderr)
            return 1
        # Resolve the id back to its descriptor rather than printing physical[0] blindly. A
        # --device that names a simulation device, or one that is not attached, would otherwise be
        # reported as whatever real hardware happened to be first in the list — and a capture
        # labelled with the wrong device is worse than a failure, because it looks like evidence.
        selected = physical[0]
        if args.device:
            matches = [d for d in physical if d.device_id == args.device]
            if not matches:
                print(f"--device {args.device} is not an attached physical device; "
                      f"available: {[d.device_id for d in physical]}", file=sys.stderr)
                return 1
            selected = matches[0]
        device_id = selected.device_id
        print(f"device         : {device_id} ({selected.device_type.name})")

        device_configuration = automation.LogicDeviceConfiguration(
            enabled_analog_channels=[args.channel],
            analog_sample_rate=DEFAULT_ANALOG_HZ,
        )
        capture_configuration = automation.CaptureConfiguration(
            capture_mode=automation.TimedCaptureMode(duration_seconds=args.seconds),
        )
        print(f"capturing analog channel {args.channel} for {args.seconds} s ...")
        with manager.start_capture(
            device_id=device_id,
            device_configuration=device_configuration,
            capture_configuration=capture_configuration,
        ) as capture:
            capture.wait()
            capture.export_raw_data_csv(directory=args.out, analog_channels=[args.channel])

    csv_path = None
    for root, _dirs, files in os.walk(args.out):
        for name in files:
            if name.endswith(".csv"):
                candidate = os.path.join(root, name)
                if csv_path is None or os.path.getmtime(candidate) > os.path.getmtime(csv_path):
                    csv_path = candidate
    if csv_path is None:
        print("no CSV was exported", file=sys.stderr)
        return 1

    print(f"export         : {csv_path}")
    return summarize(csv_path, f"analog {args.channel}")


if __name__ == "__main__":
    sys.exit(main())
