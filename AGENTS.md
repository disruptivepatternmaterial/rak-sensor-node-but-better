# Agent notes

Read this first. The detailed rules live in [`.cursor/rules/`](.cursor/rules/) and load
automatically; this is the index and the short version.

## Non-negotiables

- Specs in `docs/` are the contract; do not invent pinouts or Modbus maps.
- Prefer libraries in [`docs/LIBRARIES.md`](docs/LIBRARIES.md).
- Cross-check RAK9154 against `forest-weather-machines/rak-4-5-wire` (local sibling, pinned at
 `efc0e3c`, `~/Documents/GitHub/forest-weather-machines`) — but note it is **M5Stack NanoC6
 (ESP32-C6) firmware, not RAK4631**, and **scope its authority precisely**. It is useless as an
 MCU-side reference, and there is no existing RAK4631 firmware to fork.

 | Path in the sibling @ `efc0e3c` | Authoritative for | Silent on |
 |---|---|---|
 | `firmware/nanoc6-onewire-poll/lib/RAK-OneWire/src/onewire_master_protocol.c` | The **whole one-wire wire format**: `cal_chksum()` popcount algorithm and its byte span (:310), the provisioning reply construction (:398-474), pid = slot index + 1 (:458), dest/source swap (:450-456), length validation (:779) | — (this is the real reference; read it before touching `src/sensors/battery.cpp`) |
 | `firmware/nanoc6-onewire-poll/src/onewire_protocol.cpp` | **Polling a pack that is already provisioned** — query, frame RX, response parse | The announcement handshake. It has **no provisioning handler at all**; it assumes some other master already latched the pid |
 | `firmware/nanoc6-rak9154-poll/src/main.cpp` | **Nothing — the file is 0 bytes.** | Everything. The Modbus claim below has no code behind it in this repo |

 The Modbus figures previously asserted here (slave `0x6E`, 9600 8N1, 21 registers from
 `0x6000`) are **not backed by any source in the sibling** at `efc0e3c`. Treat them as
 unverified until a datasheet or a working capture confirms them — do not cite the sibling
 for them.
- Never commit secrets, `*.env`, keys, or live OTAA AppKeys.
- No aspirational "deployed" claims without bench/TTN evidence — see [`docs/EVIDENCE.md`](docs/EVIDENCE.md).
- **Record outcomes, never launches.** Do not write an evidence entry for something still in
  flight. On 2026-08-12 a "24 h bench soak started" entry was committed **92 seconds before the
  harness gave up**, having never attached to the board; three other documents inherited the
  claim before it was caught. A thing that was started and immediately died is not a thing in
  progress — it is a failed attempt, and the honest record is the attempt plus its outcome.
  Wait for the outcome, then write it. **Verify before you propagate:** the false claim
  travelled because each document trusted the last one instead of the log.
- Null sensor readings stay null — never fabricate zeros.
- **Either solve it or file it.** Anything noticed and not fixed becomes a GitHub issue in
  the same pass, with a number a comment can cite. Caveats delivered in chat and "one more
  thing" trailers are not a record of anything — they are gone the moment the window
  scrolls. Do not reintroduce a checklist file; the tracker is the one place.

## The rules

| Rule | Covers |
|---|---|
| [00-agent-liveness](.cursor/rules/00-agent-liveness.mdc) | Report progress every 2–4 min; stale at 5; bounded retries; no unbounded calls |
| [10-environments](.cursor/rules/10-environments.mdc) | Author locally, build and flash on Heliotrope Ridge; the SSH PATH trap; git is the only transport |
| [20-citation-discipline](.cursor/rules/20-citation-discipline.mdc) | Multiple citations per change; `CITE(category)` format; no unsourced constants |
| [30-change-workflow](.cursor/rules/30-change-workflow.mdc) | Issue → research → implement → review → build → flash → evidence → docs → version → push |
| [40-lorawan-compliance](.cursor/rules/40-lorawan-compliance.mdc) | US915 Class A, airtime budget, downlink validation |
| [50-power-management](.cursor/rules/50-power-management.mdc) | Sleep path, `Serial.end()`, brownout, months unattended |
| [60-decoder-parity](.cursor/rules/60-decoder-parity.mdc) | Every build verifies the TTN formatter and calls out when it must change |

## Fast orientation

```bash
scripts/preflight.sh          # all local gates (same as CI)
scripts/remote.sh check       # build host reachability + toolchain
scripts/build.sh              # preflight + sync + compile on the build host
scripts/flash.sh              # build + USB flash on the build host (confirms first)
scripts/push.sh               # push to GitHub (this machine cannot push directly)
```

- **Two GitHub identities, and this machine has the wrong one.** The git CLI, all three
  SSH keys, and the keychain credential authenticate as the work account, which gets 403
  on this repo. Use `scripts/push.sh`, which relays through the build host. Do not "fix"
  the remote URL. Commits from the user that you did not write are **normal** — they push
  from their IDE, which has a separate GitHub sign-in. Fetch and carry on.

- **This machine cannot compile or flash.** No PlatformIO, no device on USB, `$HOME` is
  read-only. Everything that touches hardware runs on Heliotrope Ridge — and remote commands
  need `zsh -l -c` or `pio` will look like it is not installed. Use `scripts/remote.sh`.

- **The build host address is not stable, and the one in old transcripts is dead.**
  `192.168.10.223` was correct until 2026-08-12 and now returns `No route to host`; the host
  since answers on a **public** address kept deliberately out of this public repo. Two
  sessions have been burned concluding the host was down when it was not. Ask the operator
  for the address, `export RAK_BUILD_HOST=ntableman@<address>`, and confirm in one command
  before diagnosing anything:
  `ssh -o ConnectTimeout=8 -o BatchMode=yes "$RAK_BUILD_HOST" 'zsh -l -c "hostname"'` →
  `Heliotrope-Ridge`. Scripts resolve `${BUILD_HOST:-${RAK_BUILD_HOST:-wx3-harness}}`; the
  `wx3-harness` ssh alias is the permanent fix. See [`docs/ENVIRONMENTS.md`](docs/ENVIRONMENTS.md).
- **The payload is a two-repo contract.** The TTN formatter lives in
  `forest-weather-machines`. A drifted encoder does not lose one field; the decoder throws
  and discards the entire uplink. `scripts/check_decoder_parity.py` runs on every build.
- **Status is `🚧 NOT YET DEPLOYED`** and stays that way until [`docs/EVIDENCE.md`](docs/EVIDENCE.md)
  says otherwise. Stages 0-4 have all now run on hardware (join + uplink 2026-07-31, RK900 reply
  2026-08-03, battery 12.23 V 2026-08-05, and on 2026-08-12 both sensors in one field-image cycle
  plus network-side confirmation that the uplinks are landing at TTN and the first delivered
  downlink). **That does not change the status.** Deployment stays blocked until the H1-H8 gates
  and the ≥24 h soak / ≥7 d shadow in `docs/EVIDENCE.md` close — not merely on "every subsystem
  answered once." **19.03 h of real soak exist on `572bcfa`, and a new 24 h run is in flight on the flashed `1c2df3c`** — see the H8 row below
  before repeating either "zero soak hours" or "the soak passed."
- **The RAK4631 board definition is vendored** in [`rakwireless/`](rakwireless/) because it
  does not exist in the PlatformIO registry. Do not edit it, and do not "fix" the build by
  copying files into `~/.platformio` — see [`rakwireless/README.md`](rakwireless/README.md).

## Bring-up stages

Each stage adds exactly one new failure domain, so a failure has a short suspect list.

| Stage | Adds | State |
|---|---|---|
| 0 | LED + USB serial | run on hardware 2026-07-31 (`8d4a41c`) — firmware boots and prints over USB CDC ([`docs/EVIDENCE.md`](docs/EVIDENCE.md)) |
| 1 | RK900 Modbus over RAK5802 @ 9600 | proven at wire level 2026-08-03 (`998dc26`, `busscan`) — full 5-register frame read at **9600** (not 4800): 25.1 °C, 50.4 %RH, 1007.0 hPa, calm; register map confirmed ([ADR-0006](docs/decisions/ADR-0006-rk900-baud-and-register-map.md), [`docs/EVIDENCE.md`](docs/EVIDENCE.md)). Remaining: same read through the production `stage1` path |
| 2 | OTAA join + first uplink | done 2026-07-31 — join + accepted uplink, `puma-concolor-001`, session restore across reset ([`docs/EVIDENCE.md`](docs/EVIDENCE.md)) |
| 3 | RAK9154 battery telemetry over one-wire | **working on hardware 2026-08-05 (`1a203d3`, re-verified `b6bbf31`)** — pack latches pid `0x01` and reports `12.23 V, +0.00 A, 98%, 23.0 °C` across seven consecutive cycles ([`docs/EVIDENCE.md`](docs/EVIDENCE.md)). Root cause of the long stall was reply turnaround timing, not framing: answer no sooner than 2 ms after the pack's last byte (`kTurnaroundMs`) and lead every frame with four wake bytes. **Expect ~2 null cycles after boot while the pack samples — this line is load-bearing.** Re-confirmed 2026-08-12 (`b436aa9`): 20 consecutive `battdiag` cycles, 19 live, the one null being cycle 2, latched at `0x01` throughout with no `provId FF` anywhere in the capture. Several sessions read that null cycle as a provisioning failure because `stage3`'s 1800 s cycle means one capture window holds exactly one cycle — **use `battdiag` (~10 s) for any pack question, never `stage3`.** Open: [#36](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/36), [#37](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/37) |
| 4 | Field image: both sensors, one cycle | RK900 and the pack **both read in `rak4631` in the same cycle** 2026-08-12 (`4510763`) — first time observed. Sleep reached. Uplink transmitted (`radio : sent 35 bytes on port 2`) **and delivered**: the network-side record at `f4075c0` shows `dev_addr 260CE734`, session `started_at 2026-07-31`, `last_f_cnt_up` advancing, gateway `3356-gateway-002` at 13–14 dB SNR, and `f_cnt 1792` timestamped the same second as that console line. **First downlink ever delivered on hardware** the same day — a `0x03` status request, queue drained across one uplink. No join observed (session restored, not rejoined) ([`docs/EVIDENCE.md`](docs/EVIDENCE.md)) |
| — | Downlink matrix: all eight command cases | **8/8 PASS on hardware 2026-08-13** (inferred `f15a983`, `stage3`, sleep disabled) — valid `0x01` set-interval (1800 s → 900 s, persisted across a reflash), valid `0x03` status, wrong-length `0x01` and `0x03` both rejected as *wrong length* not *unknown opcode* ([#63](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/63), [#64](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/64)), unknown opcode `0x7F` ignored, valid command on the wrong FPort ignored by port, two queued commands drained one per cycle, and cycles 18–26 monotonic with no reset. `take_downlink()` observed for the first time, closing [#54](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/54) ([`docs/EVIDENCE.md`](docs/EVIDENCE.md)) |
| — | Field image on the board, sleep reached | `env:soak` (byte-identical to `env:rak4631`) flashed 2026-08-13 at **`d568574`, asserted from the banner** — the first board to name its own commit. One cycle: both sensors read, uplink sent, and the cycle closed `sleep   : 900 s`, not `wait    : N s (sleep disabled)`. One cycle is not a soak. Repeated 2026-08-13 at **`65f8615`**, banner-asserted again: both sensors, session `0x260CE734` restored not rejoined, 35 bytes on port 2, `sleep   : 900 s` ([`docs/EVIDENCE.md`](docs/EVIDENCE.md)) |
| — | H8 soak: ≥24 h bench, then ≥7 d field shadow | **STILL OPEN. Real hours now exist but not 24 of them.** The `rc-v0.4.2` run reached **19.03 h / 76 uplinks / 0 anomalies** on `572bcfa` and was stopped deliberately at 15:30Z to ship the #75 fix — a partial run on one image cannot be topped up by another ([`docs/EVIDENCE.md`](docs/EVIDENCE.md); data preserved at `~/soak-runs/20260813T202735Z_ttn_rc-v0.4.2/`). A **new 24 h soak on `1c2df3c` started 16:09:16Z** (pid 28695, label `rc-v0.4.3-1c2df3c`) and is confirmed recording — first uplink `f_cnt=2465`, `resets=0`. **Do not stop it, flash, or reset the board** without a decision; `scripts/flash.sh` and `scripts/push.sh` refuse on their own and name their overrides. Compiling is fine. **No evidence entry claims its outcome** — write that only when it ends, pass or fail. The ≥7 d field shadow has not begun. Everything below this sentence describes the harness, which predates any accumulated hour. The *harness* is built and is real progress — `scripts/soak.sh`, [`docs/SOAK.md`](docs/SOAK.md), and `env:soak`, now **byte-identical** to `env:rak4631` ([ADR-0008](docs/decisions/ADR-0008-console-in-the-field-image.md)), so a soak is evidence about the shipped image. Before the run above it had never produced a soak hour: the one earlier attempt, 2026-08-12 at `f626698`, waited 180 s for `/dev/cu.usbmodem*`, never attached, and gave up. Cause unestablished — **not** the `FEATURE_CONSOLE=0` change, which landed 1 h 44 m later and was itself reverted at `636e421`. Run `scripts/soak.sh selftest 90` before trusting the harness with 24 h. Interval is **900 s**, not the 1800 s the console printed — from network uplink timestamps, independent of the capture |

## Where v0.4.3 leaves things

**`1c2df3c` is on the board and transmitting, and it is the image going to the field.** The
operator chose on 2026-08-14 to ship the `#75` BOOT-allowance fix (`ec9725a`) rather than the
longer-soaked `572bcfa`, trading soak hours for the battery fix. Still **untagged** — the tag
waits on a banner-verified run, not on minutes of uptime.

**The one gap you must not paper over: the boot banner was never captured, so no SHA has been
read back off this board.** `FLASH OK` plus USB `239A:8029` prove *an* application is running,
not *which* — a resident older image enumerates identically. A 24 h soak on the new image
started 16:09:16Z and its first uplink (`f_cnt=2465`, `resets=0`) is real, which proves the
board joins and transmits, and still does not identify the image. Neither sensor has been
observed reading on `1c2df3c`, and `sleep : 900 s` has not been observed on it
([`docs/EVIDENCE.md`](docs/EVIDENCE.md)).

To close it: hold a **>900 s** capture open and have the operator press RESET **once** (a single
press reboots the app; a double-tap enters DFU and will not run it). Three traps already burned
this session and are written up in the evidence entry — `pio device monitor` cannot run
non-interactively, a 100–200 s capture lands inside the 900 s sleep and reads 0 bytes, and a
`nohup` capture backgrounded over SSH does not survive the session. Use `screen` and >900 s.

Everything since `572bcfa` remains **believed correct, unobserved on this image**: the #75 fix
(`ec9725a`), the frame-counter-step fix in the soak reader (`7b03d3a`), the overridable
build-host address (`81285eb`), and the ADR-0002 sign closure (`e8002d9`, no wire change).

Two guards were added after two workers died mid-task overnight, one leaving a dirty tree that
blocked a scheduled flash: `scripts/flash.sh` refuses to flash while a soak runs, and
`scripts/push.sh` refuses a fast-forward that would rewrite the running soak's own script
(a shell reads a script incrementally, so replacing it mid-run executes a fragment). Both name
their override. `scripts/remote.sh sync` already refused on a dirty tree at either end.

## Where v0.4.1 leaves things

`v0.4.1` (2026-08-12) is **ten fixes, compile-verified only.** No hardware ran any of them — the
board was asleep with USB detached all session. `env:rak4631`, `env:soak` and `env:battdiag` build
`SUCCESS` on Heliotrope Ridge and that is the whole claim
([`docs/EVIDENCE.md`](docs/EVIDENCE.md), 2026-08-12 night).

What changed: the no-evidence brownout hold that disabled its own exit ([#61](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/61), **closed**),
keepalive frame-counter starvation, an ungated boot-counter flash write, sub-band re-selection
after the rejoin escape, downlink length checking ([#63](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/63),
[#64](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/64)), a
set-interval downlink applied during a brownout hold ([#65](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/65)),
the backoff first step raised to the fair-use floor, the pack no longer rebooted on every re-latch
attempt ([#62](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/62)
root cause — **#62 stays open**, the re-latch is unproven), empty pack records no longer counted as
silence, an all-zero RK900 span refused instead of encoded as weather, and a bounded Modbus drain
that feeds the watchdog.

**2026-08-13 update: `65f8615` has now run on hardware, and the picture is partly better and
partly unchanged.** `env:battdiag` at `65f8615` ran 20 consecutive cycles with the pack live every
cycle (`11.92 V  -0.01 A  84%  24.0 C`) and no reset, and `env:soak` at `65f8615` read both
sensors, uplinked, and reached `sleep   : 900 s`. That is a **survival** result for the eleven
fixes as a set. It is not a per-fix result: the two that sit on the battery path had their defect
conditions never arise. `e070708` was not exercised because the capture had **zero** post-boot
`Unsampled` cycles — the pack was already sampling from the preceding flash — and `da655e9` was
not exercised because the RK900 read a real `999.2 hPa`. A **new** defect did surface:
[#75](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/75), one
transient probe miss spending the power cycle's only BOOT on a healthy pack
([`docs/EVIDENCE.md`](docs/EVIDENCE.md)).

**Treat everything not named above as *believed correct, unobserved*.** Several sit on the sleep, brownout and
rejoin paths, which are exactly what compiling cannot exercise. Do not describe any of them as
working until a bench capture says so.

**Bench fact you will otherwise lose twenty minutes to:** the field image detaches USB 180 s after
boot ([ADR-0008](docs/decisions/ADR-0008-console-in-the-field-image.md)). A board left running past
that grace has no serial port and **cannot be flashed — press RESET once**, then flash inside the
fresh window.

Open issues from this pass: [#62](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/62),
[#66](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/66),
[#67](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/67),
[#68](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/68),
[#69](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/69),
[#70](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/70),
[#71](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/71),
[#75](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/75).

## Open blockers

- **Sleep current is unmeasured and cannot be measured from the pack.** Its telemetry LSB is
  10 mA; [`docs/POWER_BUDGET.md`](docs/POWER_BUDGET.md) turns on ~1 mA. Do not quote a sleep
  current from pack telemetry — it is a resolution floor, not a measurement
  ([`docs/EVIDENCE.md`](docs/EVIDENCE.md) 2026-08-12).
- **Downlink handling is now fully exercised** — [#54](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/54)
  is **closed**. On 2026-08-13 `scripts/downlink_matrix.sh` drove eight cases against the board and
  all eight passed, including both malformed-length rejections and an unknown opcode; every case
  matched a line printed from inside `Radio::take_downlink()`, so that function is finally observed
  on hardware. It ran on a `stage3` image with sleep disabled — the downlink path is shared with
  the field image, the power path is not ([`docs/EVIDENCE.md`](docs/EVIDENCE.md)).
- Today's audits are in [`docs/reviews/`](docs/reviews/) — read them before re-deriving:
  [spec-versus-code drift](docs/reviews/2026-08-12_spec_drift.md),
  [RAK reference benchmark](docs/reviews/2026-08-12_rak_reference_benchmark.md),
  [adversarial review](docs/reviews/2026-08-12_adversarial_review.md) and
  [round 3](docs/reviews/2026-08-12_adversarial_review_round3.md),
  [deferred cruft pass](docs/reviews/2026-08-12_cruft_plan.md) ([#52](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/52)).
- **`scripts/preflight.sh` no longer prints `PREFLIGHT OK` over an unresolved payload field.**
  A `BLOCKED` entry in `payload/schema.yaml` ends the run with `=== PREFLIGHT BLOCKED ===`.
  Exit stays 0 so routine CI is not red for a deliberately open conflict; `--strict` exits 2
  for the release checklist. **No field is BLOCKED as of 2026-08-13** — `batt_current` was the
  last one and ADR-0002 closed it, so the run reaches `PREFLIGHT OK` again. If it says
  `BLOCKED`, something new opened; read the named field rather than assuming it is the old one. Two scans (secrets, null policy)
  were also *never executing* — `xargs` aborts with `sysconf(_SC_ARG_MAX) failed` here and empty
  output was read as clean. They use `git grep` now, and the null-policy heuristic is too broad
  on first real execution — six counter resets flagged as fabricated zeros, `radio.h:98` among
  them, all benign ([#72](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/72)).
  The ADR-0006 sibling-SHA warning ([#57](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/57))
  was a checker false positive — the SHA was on the citation's continuation line — and is closed.
- Remaining open decisions are in [`plans/P0_HARDENED_NODE.md`](plans/P0_HARDENED_NODE.md).
  Decision #1 (BMS bus) is closed by [ADR-0004](docs/decisions/ADR-0004-bms-one-wire-path.md);
  decision #4 (framework) by [ADR-0003](docs/decisions/ADR-0003-firmware-framework.md).

## The deployment goal, in one line

Unattended in the woods **indefinitely**, on a solar-recharged RAK9154. Nobody is going to
walk out and power-cycle it. Two consequences that outrank feature work:

- **Prefer deleting a failure mode over handling one.** ADR-0004 chose two separate sensor
  buses over one shared bus for exactly this reason.
- **Never let the pack reach a state it cannot recover from by itself.** Stop transmitting
  early and keep sleeping. A lost day of data is free; a hike is not.
  See [`docs/POWER_BUDGET.md`](docs/POWER_BUDGET.md).
