#!/usr/bin/env python3
"""Turn a Saleae analog export of the RAK9154 data line into a verdict.

Nine GPIO pads used for the pack data line are dead (#102), every one after agent-written
diagnostic firmware and none under the production image. No death was instrumented. This script
keeps the next measurement on analyzer inputs instead of another pad: does the wire leave an
nRF52840 pad's rail-relative absolute limits?

The thresholds are not opinions. They come from the parts:

  nRF52840 GPIO, powered      VDD + 0.3 V ~= 3.6 V     [CIT-NRF-GPIO]
  nRF52840 GPIO, unpowered    0.3 V                    [CIT-NRF-BACKPOWER]
  Saleae Logic Pro 8          -25 V .. +25 V abs max,  [CIT-SALEAE-LOGICPRO8]
                              -10 V .. +10 V analog range, 2 MOhm || 10 pF

The analyzer has roughly 7x the pad's tolerance and loads the line with 2 MOhm against the
pack's measured 15 kOhm pull-down. Its inputs, not a GPIO, take the exposure; differential mode
may fit a Core only to complete `VDD` while the data wire remains isolated from it. The analyzer
range saturates at +/-10 V: a reading pinned there means "at least 10 V", which already convicts.

Why a capture and not a meter: a valid three-channel connector capture crossed both local rails
for millisecond intervals; no pad death was instrumented. The earlier single-ended -8.1 V
interpretation had an invalid analyzer ground reference and is withdrawn.
CITE(bench): docs/EVIDENCE.md 2026-09-06 09:24 PDT and ground-reference correction.

Usage:
    scripts/owprobe.py /tmp/rak-owprobe/live/analog.csv
    scripts/owprobe.py /tmp/rak-owprobe/live/analog.csv --label "pack live, harness off node"
    scripts/owprobe.py /tmp/capture/binary/analog_0.bin \
        --vdd-bin /tmp/capture/binary/analog_1.bin --vdd-volts 3.3 --require-seconds 610
    scripts/owprobe.py /tmp/capture/binary/analog_0.bin \
        --vdd-bin /tmp/capture/binary/analog_1.bin \
        --gnd-bin /tmp/capture/binary/analog_2.bin --require-seconds 60

Exit status:
    0  line is within what a powered pad tolerates
    2  line is out of spec -- keep it isolated from every GPIO
    1  the capture could not be read, or is not evidence of anything

Exit 0 is a claim about a wire, so three-channel mode will not issue one unless the capture could
have shown a fault: three distinct files, at least one second, a plausible measured rail, and the
data line observed both HIGH and LOW. Exit 2 is checked first and is unconditional -- a capture
with no rail and no traffic still convicts if the line left the pad's limits. Guards verified by
scripts/tests/test_owprobe_guards.py.
"""

from __future__ import annotations

import argparse
from array import array
import csv
from dataclasses import dataclass
import math
import os
import statistics
import struct
import sys

# CITE(datasheet): [CIT-NRF-GPIO] nRF52840 Product Specification, GPIO -- absolute maximum on
#   any GPIO is VDD + 0.3 V. At VDD = 3.3 V that is 3.6 V, and absolute maximum is a damage
#   threshold, not an operating point.
PAD_ABS_MAX_V = 3.6

# CITE(prior-art): [CIT-NRF-BACKPOWER] Nordic DevZone -- "for an unpowered device, max GPIO
#   voltage is 0.3 V. Any voltage above this level will make the ESD protection diode conduct
#   and you will backpower the device via the GPIO."
PAD_ABS_MAX_UNPOWERED_V = 0.3

# The nominal rail used only in the legacy single-ended report. Differential mode requires the
# measured rail voltage on its command line and does not silently assume this value.
NOMINAL_LOGIC_V = 3.3

# CITE(datasheet): [CIT-NRF-GPIO] A powered GPIO must stay between GND - 0.3 V and
# VDD + 0.3 V. Expressed as VDD - data, the upper-rail bound is -0.3 V and the lower-rail
# bound is the measured VDD-to-GND voltage plus 0.3 V.
PAD_RAIL_MARGIN_V = 0.3

# CITE(datasheet): [CIT-NRF-GPIO-TOTAL] VIL(max) is 0.3 * VDD. A measured drop from VDD of
# at least (1 - 0.3) * VDD therefore establishes that the capture contains a guaranteed LOW
# instead of only an idle HIGH.
VIL_MAX_VDD_RATIO = 0.3

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

# A negative excursion this far below node ground forward-biases the pad's LOWER clamp, matching
# the direction in which all nine dead pads failed. No historical pad death was instrumented.
NEGATIVE_ALARM_V = -0.30

# ---------------------------------------------------------------------------------------
# Three-channel qualification floors.
#
# Until 2026-09-07 the three-channel path would print its pass banner for a capture in which
# data, VDD and GND were all flat 0 V, and for the same file passed three times: its only exits
# were a duration check defaulting to zero seconds, a saturation check, and a rail-violation
# check, none of which a dead capture trips. "I saw nothing" was reported as "within the GPIO's
# instantaneous rail limits" — from the one tool whose job is to keep the next measurement off a
# pad. The single-ended path already had a floating-probe check and an explicit de-energised
# verdict; these are the same bar.
#
# All four floors gate the PASS only. A rail violation is still reported first and still exits 2,
# so a de-energised capture that nonetheless caught a negative excursion — which is exactly the
# unpowered-mate capture — convicts rather than being dismissed as inconclusive.
# ---------------------------------------------------------------------------------------

# A capture shorter than this cannot have watched anything happen. Applied as a floor under
# --require-seconds rather than as its default, so the flag can only ever make it stricter.
THREE_CHANNEL_MIN_SPAN_S = 1.0

# CITE(datasheet): [CIT-NRF-GPIO] the rail-relative limits are meaningless without a rail. A
#   median VDD-to-GND outside this band means the board was not powered, the VDD clip was not on
#   VDD, or the two probes were on the same node — none of which the verdict can survive. The
#   band is deliberately wide: 3.3 V nominal with room for a sagging supply at the bottom and
#   the pad's own absolute maximum at the top.
RAIL_PLAUSIBLE_MIN_V = 2.5
RAIL_PLAUSIBLE_MAX_V = PAD_ABS_MAX_V

# CITE(datasheet): [CIT-NRF-GPIO-TOTAL] VIL(max) is 0.3 * VDD and VIH(min) is 0.7 * VDD. A
#   capture that never reaches both has not seen the line driven, so it establishes nothing
#   about what the line does when it is.
VIH_MIN_VDD_RATIO = 0.7


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


@dataclass(frozen=True)
class AnalogBinary:
    begin_time: float
    sample_rate: int
    downsample: int
    samples: array


def read_analog_binary(path: str) -> AnalogBinary:
    """Read one Saleae Logic Pro 8 analog binary export.

    CITE(datasheet): [CIT-SALEAE-BINARY-V0] Saleae's version-0 analog layout: little-endian
    identifier/version/type, begin time, sample rate, downsample, count, then float32 volts.
    """
    try:
        with open(path, "rb") as fh:
            identifier = fh.read(8)
            version, datatype = struct.unpack("<ii", fh.read(8))
            begin_time, sample_rate, downsample, count = struct.unpack("<dQQQ", fh.read(32))
            samples = array("f")
            samples.fromfile(fh, count)
    except (EOFError, OSError, struct.error) as exc:
        raise SystemExit(f"ERROR cannot read Saleae analog binary {path}: {exc}") from exc

    if identifier != b"<SALEAE>":
        raise SystemExit(f"ERROR {path} does not have the Saleae binary identifier.")
    if version != 0 or datatype != 1:
        raise SystemExit(
            f"ERROR {path} is Saleae version {version}, type {datatype}; expected analog "
            "binary version 0."
        )
    if not sample_rate or not downsample or len(samples) != count:
        raise SystemExit(f"ERROR {path} has invalid or incomplete sample geometry.")
    return AnalogBinary(begin_time, sample_rate, downsample, samples)


def analyze_differential(
    data_path: str,
    vdd_path: str,
    vdd_to_ground: float,
    require_seconds: float,
    label: str,
) -> int:
    """Time-align simultaneous VDD and data exports and check the pad's rail-relative limits."""
    data = read_analog_binary(data_path)
    vdd = read_analog_binary(vdd_path)
    geometry_data = (data.sample_rate, data.downsample, len(data.samples))
    geometry_vdd = (vdd.sample_rate, vdd.downsample, len(vdd.samples))
    if geometry_data != geometry_vdd:
        raise SystemExit(
            f"ERROR channel sample geometry differs: data={geometry_data}, VDD={geometry_vdd}."
        )
    if not math.isfinite(vdd_to_ground) or vdd_to_ground <= 0:
        raise SystemExit("ERROR --vdd-volts must be a measured positive VDD-to-GND voltage.")
    if not math.isfinite(require_seconds) or require_seconds < 0:
        raise SystemExit("ERROR --require-seconds must be finite and non-negative.")

    sample_period = data.downsample / data.sample_rate
    offset_samples = (data.begin_time - vdd.begin_time) / sample_period
    if abs(offset_samples) >= 1:
        raise SystemExit(
            f"ERROR channel timestamps differ by {offset_samples:.6f} samples; these do not "
            "look like simultaneous exports from one capture."
        )

    base = math.floor(offset_samples)
    fraction = offset_samples - base
    if math.isclose(fraction, 0.0, abs_tol=1e-9):
        fraction = 0.0
    first = max(0, -base)
    last = min(
        len(data.samples),
        len(vdd.samples) - base - (1 if fraction else 0),
    )
    pair_count = last - first
    if pair_count < 2:
        raise SystemExit("ERROR the two channels have no usable time-aligned overlap.")

    minimum = math.inf
    maximum = -math.inf
    minimum_index = maximum_index = -1
    upper_violations = lower_violations = 0
    saturation_samples = 0
    total = total_sq = 0.0
    ordered_sample: list[float] = []
    sample_stride = max(1, pair_count // 200_000)
    lower_bound = -PAD_RAIL_MARGIN_V
    upper_bound = vdd_to_ground + PAD_RAIL_MARGIN_V
    minimum_low_drop = (1.0 - VIL_MAX_VDD_RATIO) * vdd_to_ground

    for chunk_start in range(first, last, 500_000):
        chunk_end = min(chunk_start + 500_000, last)
        differences: list[float] = []
        for index in range(chunk_start, chunk_end):
            vdd_index = index + base
            vdd_value = vdd.samples[vdd_index]
            if fraction:
                vdd_value = (
                    (1.0 - fraction) * vdd_value
                    + fraction * vdd.samples[vdd_index + 1]
                )
            data_value = data.samples[index]
            if not math.isfinite(vdd_value) or not math.isfinite(data_value):
                raise SystemExit("ERROR a channel contains a non-finite voltage sample.")
            difference = vdd_value - data_value
            differences.append(difference)
            if abs(vdd_value) >= SALEAE_ANALOG_RANGE_V - SALEAE_SATURATION_MARGIN_V:
                saturation_samples += 1
            if abs(data_value) >= SALEAE_ANALOG_RANGE_V - SALEAE_SATURATION_MARGIN_V:
                saturation_samples += 1

        chunk_min = min(differences)
        chunk_max = max(differences)
        if chunk_min < minimum:
            minimum = chunk_min
            minimum_index = chunk_start + differences.index(chunk_min)
        if chunk_max > maximum:
            maximum = chunk_max
            maximum_index = chunk_start + differences.index(chunk_max)
        upper_violations += sum(value < lower_bound for value in differences)
        lower_violations += sum(value > upper_bound for value in differences)
        total += math.fsum(differences)
        total_sq += math.fsum(value * value for value in differences)
        ordered_sample.extend(
            differences[index]
            for index in range(0, len(differences), sample_stride)
        )

    span_s = (pair_count - 1) * sample_period
    mean = total / pair_count
    stdev = math.sqrt(max(0.0, total_sq / pair_count - mean * mean))
    ordered_sample.sort()
    p01 = percentile(ordered_sample, 0.01)
    p50 = percentile(ordered_sample, 0.50)
    p99 = percentile(ordered_sample, 0.99)

    print("=== powered-pad differential probe ===")
    if label:
        print(f"   probed           : {label}")
    print(f"   data file        : {data_path}")
    print(f"   VDD file         : {vdd_path}")
    print(f"   measured VDD     : {vdd_to_ground:.6f} V")
    print(f"   sample rate      : {data.sample_rate / data.downsample:.3f} S/s")
    print(f"   aligned pairs    : {pair_count} over {span_s:.6f} s")
    print(f"   timestamp offset : {(data.begin_time - vdd.begin_time) * 1e6:+.3f} us "
          f"({offset_samples:+.6f} sample)")
    print(f"   min VDD-data     : {minimum:+.6f} V at "
          f"{data.begin_time + minimum_index * sample_period:.6f} s")
    print(f"   p01 / median     : {p01:+.6f} V / {p50:+.6f} V")
    print(f"   p99              : {p99:+.6f} V")
    print(f"   max VDD-data     : {maximum:+.6f} V at "
          f"{data.begin_time + maximum_index * sample_period:.6f} s")
    print(f"   mean / stdev     : {mean:+.6f} V / {stdev:.6f} V")
    print(f"   data > VDD+0.3   : {upper_violations}")
    print(f"   data < GND-0.3   : {lower_violations}")
    print(f"   inferred data    : {vdd_to_ground - maximum:+.6f} V .. "
          f"{vdd_to_ground - minimum:+.6f} V against GND")
    print()

    if span_s < require_seconds:
        print("=== NOT EVIDENCE -- CAPTURE TOO SHORT ===")
        print(f"   Required {require_seconds:.3f} s; captured {span_s:.3f} s.")
        return 1
    if saturation_samples:
        print("=== OUT OF RANGE ===")
        print(f"   {saturation_samples} channel sample(s) reached the analyzer range margin.")
        return 2
    if maximum < minimum_low_drop:
        print("=== NOT EVIDENCE -- NO GUARANTEED LOW CAPTURED ===")
        print(f"   Maximum VDD-data was {maximum:.3f} V; at least "
              f"{minimum_low_drop:.2f} V is needed to establish a LOW.")
        return 1
    if upper_violations or lower_violations:
        print("=== OUTSIDE A POWERED PAD'S ABSOLUTE LIMITS ===")
        print(f"   Allowed VDD-data range: {lower_bound:+.3f} V .. {upper_bound:+.3f} V.")
        return 2

    print("=== WITHIN A POWERED PAD'S RAIL-RELATIVE LIMITS ===")
    print(f"   All {pair_count} aligned pairs stayed inside "
          f"{lower_bound:+.3f} V .. {upper_bound:+.3f} V.")
    return 0


def analyze_three_channel(
    data_path: str,
    vdd_path: str,
    gnd_path: str,
    require_seconds: float,
    label: str,
) -> int:
    """Check data against simultaneous, independently captured VDD and local ground."""
    # Three exports, three distinct files. Passing one file three times produces a perfectly
    # aligned capture in which every difference is identically zero, which sails through every
    # check below it — so it is refused here rather than diagnosed later.
    resolved = {
        "data": os.path.realpath(data_path),
        "VDD": os.path.realpath(vdd_path),
        "GND": os.path.realpath(gnd_path),
    }
    if len(set(resolved.values())) != 3:
        raise SystemExit(
            "ERROR the three channels must be three different files; got "
            + ", ".join(f"{name}={path}" for name, path in resolved.items())
        )

    data = read_analog_binary(data_path)
    vdd = read_analog_binary(vdd_path)
    gnd = read_analog_binary(gnd_path)
    geometry = (data.sample_rate, data.downsample, len(data.samples))
    for name, channel in (("VDD", vdd), ("GND", gnd)):
        other = (channel.sample_rate, channel.downsample, len(channel.samples))
        if other != geometry:
            raise SystemExit(
                f"ERROR channel sample geometry differs: data={geometry}, {name}={other}."
            )
    if not math.isfinite(require_seconds) or require_seconds < 0:
        raise SystemExit("ERROR --require-seconds must be finite and non-negative.")

    sample_period = data.downsample / data.sample_rate

    def alignment(channel: AnalogBinary, name: str) -> tuple[int, float]:
        offset = (data.begin_time - channel.begin_time) / sample_period
        if abs(offset) >= 1:
            raise SystemExit(
                f"ERROR {name} timestamp differs from data by {offset:.6f} samples; "
                "these do not look like simultaneous exports from one capture."
            )
        base = math.floor(offset)
        fraction = offset - base
        if math.isclose(fraction, 0.0, abs_tol=1e-9):
            fraction = 0.0
        return base, fraction

    vdd_base, vdd_fraction = alignment(vdd, "VDD")
    gnd_base, gnd_fraction = alignment(gnd, "GND")
    first = max(0, -vdd_base, -gnd_base)
    last = min(
        len(data.samples),
        len(vdd.samples) - vdd_base - (1 if vdd_fraction else 0),
        len(gnd.samples) - gnd_base - (1 if gnd_fraction else 0),
    )
    pair_count = last - first
    if pair_count < 2:
        raise SystemExit("ERROR the three channels have no usable time-aligned overlap.")

    def aligned(channel: AnalogBinary, index: int, base: int, fraction: float) -> float:
        other_index = index + base
        value = channel.samples[other_index]
        if fraction:
            value = (
                (1.0 - fraction) * value
                + fraction * channel.samples[other_index + 1]
            )
        return value

    data_gnd_min = vdd_gnd_min = data_vdd_min = math.inf
    data_gnd_max = vdd_gnd_max = data_vdd_max = -math.inf
    data_gnd_min_index = data_gnd_max_index = -1
    data_vdd_min_index = data_vdd_max_index = -1
    lower_violations = upper_violations = rail_inversions = 0
    lower_run = upper_run = longest_lower_run = longest_upper_run = 0
    saturation_samples = 0
    vdd_gnd_sum = 0.0

    for index in range(first, last):
        data_value = data.samples[index]
        vdd_value = aligned(vdd, index, vdd_base, vdd_fraction)
        gnd_value = aligned(gnd, index, gnd_base, gnd_fraction)
        if not all(math.isfinite(value) for value in (data_value, vdd_value, gnd_value)):
            raise SystemExit("ERROR a channel contains a non-finite voltage sample.")
        saturation_samples += sum(
            abs(value) >= SALEAE_ANALOG_RANGE_V - SALEAE_SATURATION_MARGIN_V
            for value in (data_value, vdd_value, gnd_value)
        )

        data_gnd = data_value - gnd_value
        vdd_gnd = vdd_value - gnd_value
        data_vdd = data_gnd - vdd_gnd

        if data_gnd < data_gnd_min:
            data_gnd_min, data_gnd_min_index = data_gnd, index
        if data_gnd > data_gnd_max:
            data_gnd_max, data_gnd_max_index = data_gnd, index
        vdd_gnd_min = min(vdd_gnd_min, vdd_gnd)
        vdd_gnd_max = max(vdd_gnd_max, vdd_gnd)
        vdd_gnd_sum += vdd_gnd
        if data_vdd < data_vdd_min:
            data_vdd_min, data_vdd_min_index = data_vdd, index
        if data_vdd > data_vdd_max:
            data_vdd_max, data_vdd_max_index = data_vdd, index

        if data_gnd < -PAD_RAIL_MARGIN_V:
            lower_violations += 1
            lower_run += 1
            longest_lower_run = max(longest_lower_run, lower_run)
        else:
            lower_run = 0
        if data_vdd > PAD_RAIL_MARGIN_V:
            upper_violations += 1
            upper_run += 1
            longest_upper_run = max(longest_upper_run, upper_run)
        else:
            upper_run = 0
        if vdd_gnd < -PAD_RAIL_MARGIN_V:
            rail_inversions += 1

    span_s = (pair_count - 1) * sample_period
    rail_mean = vdd_gnd_sum / pair_count
    required_span_s = max(require_seconds, THREE_CHANNEL_MIN_SPAN_S)
    rail_plausible = RAIL_PLAUSIBLE_MIN_V <= rail_mean <= RAIL_PLAUSIBLE_MAX_V
    saw_high = data_gnd_max >= VIH_MIN_VDD_RATIO * rail_mean
    saw_low = data_gnd_min <= VIL_MAX_VDD_RATIO * rail_mean

    def relative_time(index: int) -> float:
        return (index - first) * sample_period

    print("=== three-channel rail-relative probe ===")
    if label:
        print(f"   probed           : {label}")
    print(f"   data file        : {data_path}")
    print(f"   VDD file         : {vdd_path}")
    print(f"   GND file         : {gnd_path}")
    print(f"   sample rate      : {data.sample_rate / data.downsample:.3f} S/s")
    print(f"   aligned triples  : {pair_count} over {span_s:.6f} s")
    print(f"   min data-GND     : {data_gnd_min:+.6f} V at "
          f"{relative_time(data_gnd_min_index):.6f} s")
    print(f"   max data-GND     : {data_gnd_max:+.6f} V at "
          f"{relative_time(data_gnd_max_index):.6f} s")
    print(f"   VDD-GND range    : {vdd_gnd_min:+.6f} V .. {vdd_gnd_max:+.6f} V")
    print(f"   min data-VDD     : {data_vdd_min:+.6f} V at "
          f"{relative_time(data_vdd_min_index):.6f} s")
    print(f"   max data-VDD     : {data_vdd_max:+.6f} V at "
          f"{relative_time(data_vdd_max_index):.6f} s")
    print(f"   data < GND-0.3   : {lower_violations}; longest "
          f"{longest_lower_run * sample_period:.9f} s")
    print(f"   data > VDD+0.3   : {upper_violations}; longest "
          f"{longest_upper_run * sample_period:.9f} s")
    print(f"   VDD < GND-0.3    : {rail_inversions}")
    print(f"   mean VDD-GND     : {rail_mean:+.6f} V")
    print(f"   data reached HIGH: {'yes' if saw_high else 'NO'} "
          f"(>= {VIH_MIN_VDD_RATIO * rail_mean:+.3f} V)")
    print(f"   data reached LOW : {'yes' if saw_low else 'NO'} "
          f"(<= {VIL_MAX_VDD_RATIO * rail_mean:+.3f} V)")
    print()

    if span_s < required_span_s:
        print("=== NOT EVIDENCE -- CAPTURE TOO SHORT ===")
        print(f"   Required {required_span_s:.3f} s; captured {span_s:.3f} s.")
        return 1
    if saturation_samples:
        print("=== OUT OF RANGE ===")
        print(f"   {saturation_samples} raw channel sample(s) reached the analyzer range margin.")
        return 2

    # Convict before qualifying. An excursion past a pad's limits is decisive whatever else the
    # capture is missing — the unpowered-mate capture has no rail and no traffic, and it is the
    # most important measurement in this project.
    if lower_violations or upper_violations:
        print("=== OUTSIDE THE GPIO'S INSTANTANEOUS RAIL LIMITS ===")
        print("   Required: data >= GND-0.300 V and data <= VDD+0.300 V.")
        return 2

    # Everything below decides whether a clean capture is allowed to mean anything. A pass here
    # is read as "this wire may touch a pad", so silence must not reach it.
    if not rail_plausible:
        print("=== NOT EVIDENCE -- NO PLAUSIBLE RAIL ===")
        print(f"   Mean VDD-GND is {rail_mean:+.3f} V; a verdict against VDD+0.3 V needs "
              f"{RAIL_PLAUSIBLE_MIN_V:.1f}..{RAIL_PLAUSIBLE_MAX_V:.1f} V.")
        print("   The board was not powered, the VDD clip was not on VDD, or two probes were")
        print("   on the same node. This clears nothing: no violation was seen because there")
        print("   was nothing to violate.")
        return 1
    if not (saw_high and saw_low):
        print("=== NOT EVIDENCE -- LINE NEVER SEEN DRIVEN ===")
        missing = " and ".join(
            part for part, seen in (("a HIGH", saw_high), ("a LOW", saw_low)) if not seen
        )
        print(f"   The data channel never reached {missing} against the measured "
              f"{rail_mean:.3f} V rail.")
        print("   An idle or disconnected line stays inside every bound by doing nothing.")
        return 1

    print("=== WITHIN THE GPIO'S INSTANTANEOUS RAIL LIMITS ===")
    print(f"   All {pair_count} aligned triples passed both bounds, over {span_s:.3f} s")
    print(f"   against a {rail_mean:.3f} V rail, with the line observed both HIGH and LOW.")
    return 0


def percentile(sorted_v: list[float], q: float) -> float:
    if not sorted_v:
        raise ValueError("empty")
    idx = int(round(q * (len(sorted_v) - 1)))
    return sorted_v[max(0, min(len(sorted_v) - 1, idx))]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture", help="data-channel Saleae CSV or version-0 analog binary export")
    ap.add_argument(
        "--vdd-bin",
        help="simultaneous VDD-channel binary export; enables rail-relative differential mode",
    )
    ap.add_argument(
        "--vdd-volts",
        type=float,
        help="measured VDD-to-GND voltage; required for two-channel --vdd-bin mode",
    )
    ap.add_argument(
        "--gnd-bin",
        help="simultaneous local-ground binary export; enables three-channel rail checking",
    )
    ap.add_argument(
        "--require-seconds",
        type=float,
        default=0.0,
        help="minimum time-aligned duration required for a differential verdict",
    )
    ap.add_argument("--label", default="", help="what was probed, for the printed record")
    args = ap.parse_args()

    if args.gnd_bin:
        if not args.vdd_bin:
            print("ERROR --gnd-bin requires --vdd-bin.", file=sys.stderr)
            return 1
        if args.vdd_volts is not None:
            print("ERROR --vdd-volts is not used when --gnd-bin is present.", file=sys.stderr)
            return 1
        return analyze_three_channel(
            args.capture,
            args.vdd_bin,
            args.gnd_bin,
            args.require_seconds,
            args.label,
        )
    if args.vdd_bin:
        if args.vdd_volts is None:
            print("ERROR --vdd-volts is required with --vdd-bin.", file=sys.stderr)
            return 1
        return analyze_differential(
            args.capture,
            args.vdd_bin,
            args.vdd_volts,
            args.require_seconds,
            args.label,
        )
    if args.vdd_volts is not None or args.require_seconds:
        print("ERROR --vdd-volts and --require-seconds require --vdd-bin.", file=sys.stderr)
        return 1

    times, volts, channel = read_analog_csv(args.capture)

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
    high_samples = sum(1 for v in volts if v > PAD_ABS_MAX_V)
    sat_hi = sum(1 for v in volts if v >= SALEAE_ANALOG_RANGE_V - SALEAE_SATURATION_MARGIN_V)
    sat_lo = sum(1 for v in volts if v <= -SALEAE_ANALOG_RANGE_V + SALEAE_SATURATION_MARGIN_V)

    print("=== one-wire data line probe ===")
    if args.label:
        print(f"   probed  : {args.label}")
    print(f"   file    : {args.capture}")
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

    verdict = 2 if sat_hi or sat_lo else 0

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
        print("   So a 0 V reading here CLEARS NOTHING. To see what the pack actually drives,")
        print("   energise pin 4 from the current-limited external 3.3 V source specified in")
        print("   docs/BUILD.md, with no Core or base board in that measurement loop. A coreless")
        print("   RAK19007 VDD pad is open-circuit [CIT-RAK4631-SCH].")
        return 1

    if neg_samples:
        pct = 100.0 * neg_samples / len(volts)
        print(f"!! NEGATIVE EXCURSION: {neg_samples} sample(s) ({pct:.3f} %) below "
              f"{NEGATIVE_ALARM_V} V, floor {lo:+.3f} V.")
        print("   This exceeds the lower pad rail only if the single-ended ground reference was")
        print("   independently verified. Use --gnd-bin for a rail-relative connector verdict.")
        print("   Keep the wire isolated from every GPIO.")
        print()
        verdict = 2

    if high_samples:
        print("=== OUTSIDE A POWERED PAD'S ABSOLUTE LIMITS ===")
        pct = 100.0 * high_samples / len(volts)
        print(f"   {high_samples} sample(s) ({pct:.6f} %) exceeded +{PAD_ABS_MAX_V} V; "
              f"peak {hi:+.3f} V.")
        print("   Keep the wire isolated from every GPIO.")
        verdict = 2
    elif verdict == 0:
        print("=== WITHIN A POWERED PAD'S RATING ===")
        print(f"   Idle level {p99:+.3f} V, at or below the {PAD_ABS_MAX_V} V pad maximum")
        print(f"   (nominal logic rail is {NOMINAL_LOGIC_V} V).")
        print()
        print("   This clears only this powered, fixed-reference sample. It does NOT clear hot-plug")
        print("   or an unpowered pad:")
        print(f"   an unpowered pad's maximum is {PAD_ABS_MAX_UNPOWERED_V} V, so this level is still roughly")
        print(f"   {p99 / PAD_ABS_MAX_UNPOWERED_V:.0f}x over whenever the core is dark. Use simultaneous")
        print("   data, VDD, and local-ground channels before approving a GPIO connection.")

    print()
    print("Record the numbers above in docs/EVIDENCE.md with the date, the host, and this file")
    print("path. A verdict with no number attached is how this reached nine pads.")
    return verdict


if __name__ == "__main__":
    sys.exit(main())
