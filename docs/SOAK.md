# Soak procedure — the 24 h bench run and the 7 d field shadow

[`FIRMWARE_SPEC.md`](FIRMWARE_SPEC.md) §7 H8 requires ≥ 24 h on the bench and ≥ 7 d of
field shadow before this node is trusted on a hike-in. **The soak is the measurement
opportunity, not a waiting period.** Several open questions can only be answered by
watching the node run for a long time, and a soak that produces nothing but "it did not
crash" has spent a day to learn one bit.

Run it instrumented and it settles [#8](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/8),
[#12](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/12),
[#40](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/40),
[#45](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/45)
and [#47](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/47)
— see [Which issues each result closes](#which-issues-each-result-closes).

Status stays **🚧 NOT YET DEPLOYED** until a completed run is recorded in
[`EVIDENCE.md`](EVIDENCE.md). Nothing in this document closes a gate by itself.

## Run it

```bash
scripts/soak.sh start 24h --label bench    # detached on the build host; survives your SSH exit
scripts/soak.sh status                     # last heartbeat, pid, anomaly count
scripts/soak.sh tail 60                    # recent events
scripts/soak.sh stop                       # ends early AND still writes the summary
scripts/soak.sh summary                    # the block to paste into EVIDENCE.md
scripts/soak.sh fetch latest               # bring the logs back to the workstation
scripts/soak.sh selftest 90                # prove the harness with no board attached
```

Two things worth knowing before you read the output:

- **The USB port disappears every sleep and that is healthy.** `power.cpp` detaches
  TinyUSB, so `/dev/cu.usbmodem*` ceases to exist until the next wake. A plain
  `cat /dev/cu.usbmodem*` dies at the first sleep and looks exactly like a dead node.
  The harness polls, reattaches, and counts the reattaches — a run with roughly as many
  attaches as cycles is a run that slept properly.
- **Serial cannot see a missed uplink.** The console reports what was handed to the
  radio, not what the network received. TTN's `last_f_cnt_up` is polled alongside, and
  every advance smaller than the number of frames logged is flagged as a gap. **A frame
  counter gap is the most valuable single signal a soak produces.** Never read a TTN
  listing through `head` — a truncated listing produced a wrong conclusion once already.

## Sleep current — measure the path the field node actually uses

This is the one thing the harness cannot do. Software can prove the node *entered*
sleep; only a meter can prove sleep is actually low-power rather than a busy loop with
the lights off ([#12](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/12)).

### The power path, because it decides the measurement point

Pack (~12 V) → **12 V→5 V buck** → **USB-C** → the RAK4631's USB-C port.
`docs/FIRMWARE_SPEC.md` §2 forbids feeding P+ to `BAT`, so the RAK19007 **battery JST is
not used at all** in this design.

Two consequences that a bench setup gets wrong by default:

- **Do not measure at the battery JST.** Injecting 3.7 V there profiles a rail the field
  node never runs on, and the resulting sleep-current figure would not describe the
  deployed hardware.
- **VBUS is permanently present in the field** whenever the pack has charge. The board
  always sees USB *power*; it never sees a USB *host*. Powering the board over USB-C
  during the measurement is therefore correct, not a compromise.

These are **two separate numbers**. Do not add them from one reading and do not quote one
as the other.

### Measurement 1 — buck no-load quiescent ([#2](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/2))

The buck's own idle draw is a parallel load that runs 24/7 and can exceed the node's own
average, which is why the part gets selected on this figure
([`POWER_BUDGET.md`](POWER_BUDGET.md)).

1. Bench supply at 12 V into the buck input.
2. Buck output **unloaded** — nothing connected downstream.
3. Read the **input** current.

Partially done already: on a Rigol supply the readout was effectively zero. That rules out
a mA-class part, and nothing more. Bench-supply readouts resolve at best around 1 mA, so
that result cannot distinguish ~50 µA from ~900 µA — the difference between a buck that is
irrelevant to the budget and one that dominates it. Getting the real figure needs a
**µA-capable instrument in series** with the buck input.

### Measurement 2 — node sleep current

Two valid measurement points. Pick one deliberately and record which:

| Point | What the number includes |
|---|---|
| Inline on the **USB-C line between buck and board** | The node alone, at 5 V. Comparable to per-subsystem budget figures. |
| Inline on the **buck's 12 V input**, node connected | Buck quiescent **plus** node, at 12 V. This is what actually drains the pack, and is the more useful number for runtime. |

The 12 V-input figure is the one to hold the deployment to; the USB-C figure is the one to
hold the firmware to. Measuring both and differencing them is a second route to
measurement 1.

**Resolution is the constraint, and it is the whole difficulty.** The sleep floor being
checked is tens of µA, so the instrument needs ~10 µA resolution or better. The USB inline
power meters on hand sit in exactly the right place electrically for the USB-C point, but
typical USB testers resolve around 10 mA and will simply read `0.00` across the entire
sleep window — which looks like a pass and measures nothing. A **Nordic PPK2 in ampere-meter
mode** is the reliable instrument here. If a PPK2 is not on hand, this measurement is
blocked; do not substitute a reading from an instrument that cannot resolve the threshold.

The 6 mA never-sleeps figure quoted below comes from the RAKwireless sleep-current report
[CIT-RAK-SLEEP].

### No computer attached during the measurement

`src/power.cpp` (the `console_in_use` branch) tests `(bool)Serial`. That is true only when
a **host has opened the CDC port** — not merely when VBUS is present. A dumb USB-C power
source does not enumerate, so in the field the branch evaluates false and the firmware
takes the USB-shutdown path, which is the intended unattended behaviour.

So the instruction is **not** "unplug USB" — it is **do not have a computer attached**.
Powering the board over USB-C from the buck (or from a PPK2 / dumb supply standing in for
it) is exactly what the field node does. A laptop on the other end of the cable keeps the
console alive through the sleep, deliberately, so a technician is not disconnected at the
first cycle — and that is a **different code path** from the one that runs in the woods.
Measure with a host attached and you get a confident number for firmware that will never
run unattended.

### What to expect

| Reading | What it means |
|---|---|
| Flat, tens of µA | Sleep is real. Record it in [`EVIDENCE.md`](EVIDENCE.md). |
| ~0.9 mA | A peripheral bus was left clocked — the SPI-left-running defect [CIT-NRF-PERIPH-SLEEP], closed in `power.cpp` but this is what its regression looks like. |
| ~6 mA | The transceiver was never slept, or the node is in a join loop and never sleeps at all [CIT-RAK-SLEEP]. |

**Acceptance threshold for this bench test: ≤ 20 µA mean during the sleep window.** That
is the bar this project is holding itself to, not a datasheet figure — the nRF52840's own
sleep-mode numbers are in [CIT-NRF-POWER] and the board carries more than the chip.
Anything in the hundreds of µA or above means something did not shut down, and
[`POWER_BUDGET.md`](POWER_BUDGET.md) explains why one stray milliamp matters roughly 25×
more than the same current spent awake.

### Read the trace, not the average

An average hides exactly the defects worth finding.

- **Does current step down at sleep entry and stay flat?** A clean step to a flat floor
  is sleep. A step down that creeps back up is a peripheral re-enabling itself.
- **Is there ~1 kHz ripple on the floor?** That is the FreeRTOS tick still running —
  tickless idle is off and the CPU is waking 1000×/s to do nothing
  ([#12](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/12),
  [#47](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/47)).
  The mean can look acceptable while this is happening.
- **Does the floor rise after the *first* sleep?** First-sleep-only correctness is a real
  failure mode: something initialised on wake is not being torn down again, so cycle 2
  onward costs more than cycle 1. Compare the floor of sleep #1 against sleep #5.
- **Do resets land mid-sleep?** A current spike in the middle of a sleep window, with the
  boot banner following it, is the watchdog firing during sleep
  ([#40](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/40)).
  The soak log's `watchdog-reset` anomaly and the trace timestamp together tell you
  whether the reset happened while awake or while asleep — which the log alone cannot.

Record the mean sleep current, the sleep-window duration it was taken over, the
awake-phase peak, **which of the two measurement points it was taken at**, and the
instrument. A number without its window and its measurement point is not a measurement.

## The network-side alternative — `scripts/soak_ttn.sh`

The serial harness has to hold the port open for the whole run, and that is not free. The
field image detaches USB about 180 s after boot **when no host is attached**
([ADR-0008](decisions/ADR-0008-console-in-the-field-image.md),
[#60](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/60)). A
reader that stays attached suppresses the detach, so a 24 h serial soak measures a bench
variant of the image rather than the one that ships.

`scripts/soak_ttn.sh` watches the same node from the network instead. It polls
`ttn-lw-cli end-devices get <app> <dev> --session.last-f-cnt-up` and records every advance of
the frame counter, so the node is observed without anything being attached to it:

```bash
nohup scripts/soak_ttn.sh 24h bench >/dev/null 2>&1 &
grep -E 'SOAK HEARTBEAT|SOAK UPLINK|SOAK ANOMALY' ~/soak-runs/latest-ttn/events.log | tail
```

Choose it when the question is *does the shipped image keep waking and transmitting for a day*.
Choose the serial harness when the question needs console detail — a boot banner, a watchdog
warning, a sensor value — and accept that the run is then about a console-attached variant.

The network side cannot see a reboot that recovers, because a restored session keeps counting;
it sees only what reaches TTN. It measures nothing about current draw. Neither harness measures
sleep current — that needs the meter procedure above.

## Pass / fail — 24 h bench soak

Derived from H8. All of these must hold; any one failing is a fail, not a caveat.

| # | Criterion | Fail looks like |
|---|---|---|
| B1 | Ran the full 24 h. `completed_full_duration: true` in `summary.json`. | The run ended early for any reason. |
| B2 | `watchdog_resets: 0`. | Any watchdog reset. One is a fail — it means something hung. |
| B3 | `unexpected_reboots: 0` and exactly one boot number for the whole run. | The boot counter advanced mid-run. |
| B4 | `cycles_seen` matches 86400 ÷ interval, ±1. | Cycles missing from the console. |
| B5 | `ttn_f_cnt_gaps: 0`, and `ttn_f_cnt_last − ttn_f_cnt_first` equals `uplinks_logged_on_serial` over the same span. | The network counted fewer frames than the console sent. |
| B6 | `cycles_without_battery` is 0 after the first two cycles (the pack needs ~2 cycles to start answering). | A cycle with no pack voltage once the pack is up. |
| B7 | `battery_v_min` above the brownout hold threshold, and `brownout_events: 0` on a charged pack. | Brownout engaged on a pack that should be fine. |
| B8 | Sleep current ≤ 20 µA, measured per the procedure above — powered over USB-C, no computer attached, on an instrument that resolves ~10 µA. | Hundreds of µA, or a ~1 kHz ripple on the floor. |
| B9 | If the node was starved of sensor data for 24 consecutive cycles, `keepalive_transmissions ≥ 1`. | 24+ quiet cycles and no transmission — the node went dark. |
| B10 | `anomaly_count: 0`, or every anomaly explained in the EVIDENCE.md entry. | An unexplained anomaly. |

B9 needs provoking; it will not happen on its own during a healthy bench run. Pull the
RS-485 pair and let the node run 24+ cycles with no sensor evidence, then confirm one
transmission goes out anyway
([#45](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/45)).
Do it as a separate short run so the 24 h clean run stays clean.

## Pass / fail — 7 d field shadow

Same node, deployed where it will live, running the field interval. "Shadow" means the
data is not yet trusted for anything — you are watching the node, not the weather.

| # | Criterion | Fail looks like |
|---|---|---|
| F1 | 7 consecutive days, no site visit, no power cycle. | Anyone touched it. Restart the clock. |
| F2 | Uplinks received ≥ 95 % of expected, counted from TTN `f_cnt` continuity, not from the console. | Frame counter gaps beyond 5 %. |
| F3 | Zero watchdog resets and zero unexplained reboots across the whole week. | Any reset with no explanation. |
| F4 | Pack voltage trend flat or rising across the week, checked against the solar conditions. | A monotonic decline — the node is outrunning the panel. |
| F5 | Any brownout hold that engaged also released on its own. | The node latched a hold and stayed there. |
| F6 | No period of silence longer than 24 cycles. | A silent stretch, whatever the reason. |
| F7 | Decoded payloads land in TTN with no decoder throw for the whole week. | Any decode failure — a drifted encoder discards the entire uplink, not one field. |

Overlapping the shadow with a `docs/EVIDENCE.md` entry per day is cheap and makes F4
readable at the end. `scripts/soak.sh start 7d --label field` works, but a field node is
not on USB — for the shadow the TTN half of the harness is the whole measurement, so run
it with the serial capture finding nothing and read `ttn_f_cnt_*`.

## Which issues each result closes

| Result the soak produces | Issue it settles |
|---|---|
| Measured mean sleep current with its window and its measurement point, no host attached | [#8](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/8) |
| Trace shows a flat floor with no ~1 kHz tick ripple | [#12](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/12) |
| Sleep floor after cycle 5 equals the floor after cycle 1; residual accounted for | [#47](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/47) |
| 24 h with zero watchdog resets, and no reset landing inside a sleep window | [#40](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/40) |
| 24+ deliberately starved cycles followed by one transmission | [#45](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/45) |
| The full instrumented run, bench and field, recorded in EVIDENCE.md | [#14](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/14) and H8 |

A result closes an issue when it is **in [`EVIDENCE.md`](EVIDENCE.md) with a host and a
commit SHA**. A result in a chat window closes nothing.

## What the harness cannot tell you

- **Sleep current.** Software cannot measure its own power. PPK2 only.
- **Whether a reset happened while asleep or while awake.** The log records the reset;
  only the current trace timestamps it inside a sleep window.
- **Whether an uplink was received but not decoded.** `f_cnt` advancing proves receipt,
  not a clean decode. Check the TTN console for decoder errors, and see
  `scripts/check_decoder_parity.py`.
- **Anything about the buck converter**, which is not chosen yet
  ([`POWER_BUDGET.md`](POWER_BUDGET.md)).
