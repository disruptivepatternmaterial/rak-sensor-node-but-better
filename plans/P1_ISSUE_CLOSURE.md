# P1 — Sequenced closure of the 36 open issues

Status: **plan only.** A plan is not evidence. Project status remains `🚧 NOT YET DEPLOYED`
and stays there until [`docs/EVIDENCE.md`](../docs/EVIDENCE.md) closes H1–H8 and the soak in
[#14](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/14).

Written 2026-08-05 against `406df01` (workstation tree clean at authoring time — the
in-flight edits referenced when this plan was commissioned had already landed as `6faab3a`
and `406df01`). Issue bodies were read on the build host via `gh`; the triage below is from
the bodies, not the titles.

---

## 1. What the triage changed

The waves were originally grouped from titles. Reading the bodies moved eleven issues and
found two that are closeable now. Corrections, with the reason:

| # | Was | Is | Why |
|---|---|---|---|
| 35 | Wave 2 code pass | **Closeable now** | `b6bbf31` already removed the fabricated WisToolBox comments from `src/sensors/battery.cpp`. `grep -rn WisToolBox src/` returns nothing. Only remaining check: confirm the replacement text describes the Generic Probe IO adapter as the issue asks, then close. |
| 11 | Wave 1, in flight | **Probably closeable** | `docs/EVIDENCE.md` H5 already records session/DevAddr restore across reset observed 2026-07-31, and `AGENTS.md` lists stage 2 as done including "session restore across reset". The issue asks for exactly that. Either the ledger entry satisfies it or the entry needs one line saying it does not — **state which; do not assume.** |
| 31, 33 | Wave 2 | **Wave 0 — first** | Both are tooling defects that already cost sessions. `push.sh` failed an already-synchronized relay and blocked a flash on 2026-08-04; `flash.sh` reported FLASH FAILED on an image that had landed on 2026-08-03. Every later wave runs through these two scripts. Fix them before anything needs flashing. |
| 42 | Wave 2, alongside 36/37 | **Wave 2, before 36/37** | Extracting the frame parser is what makes the #36 and #37 fixes testable. Landing it second means writing those fixes twice. |
| 4 | Wave 1, in flight | **Wave 3 (cheap bench observation)** | Labelled `needs-hardware`. It needs no meter and no charge cycle — read the raw temperature record at room temperature: ~210 confirms tenths, ~21 means whole degrees and the ×10 returns to `src/payload.cpp`. It rides along with any bench session. **It changes the encoder, so it gates #13.** |
| 5 | Wave 4, charge cycle | **Wave 3 (bench capture)** | No charge/discharge needed. The checksum convention is answered by any captured reply frame; `bad checksum` logs the raw hex alongside expected and received. The seven consecutive good cycles on 2026-08-05 are weak evidence the assumption already holds — confirm against a logged frame rather than inferring it. |
| 25 | Wave 5 payload | **Tooling, blocked on #26** | Not a payload change at all. `FEATURE_BENCH_INTERVAL` is refused at compile time alongside `FEATURE_RADIO` because 1440 uplinks/day blows the TTN fair-use budget. It cannot land until #26 (private network) is answered. Operator-gated. |
| 22 | Wave 5 payload | **Deferred, not a payload change** | LoRaWAN 1.0.4 is a MAC/registration question. It touches no decoder field, so it does not gate #13. It is blocked on `SX126x-Arduino` having no `RejoinReq` handling and no `JoinNonce` tracking — it sends a random DevNonce. Nothing in this plan unblocks it. Candidate to close as deferred-pending-library-support. |
| 15 | Wave 5 payload | **Post-soak** | Changes cadence, not schema — no decoder entry. It loosens a fair-use guard using airtime arithmetic nothing has verified, so it wants measured airtime from the soak first. |
| 34 | Wave 2 | **Nice-to-have, off the critical path** | `busscan` sweeping register `0x0000` instead of `0x6000` produced a false "the line is dead" diagnosis on 2026-08-04. Real defect, but the one-wire path now works, so the RS-485 scan is no longer how the pack gets debugged. |
| 38, 39 | Wave 2 (code only) | **Wave 2 code, Wave 3/soak evidence** | The fixes are source-only, but their verification is bench: #38 is H3 (sag the supply, observe the skip) and #39 shows up as awake-time in the soak log. They are not closed by the code landing. |

**The "seven payload changes" is actually five.** Only #16, #17 and #18 carry the
`needs-decoder-change` label. #3 (current sign) and #4 (temperature scale) also change what
the encoder emits and therefore belong in the same bundle. #15, #22 and #25 do not touch the
decoder and are out of it. That makes the one-shot payload bundle **#3, #4, #16, #17, #18**,
immediately followed by **#13**.

Two issues whose state this plan will not assert: **#11** (see above) and **#43** — the
member `m_ever_sampled` still exists at `battery.h:181` and is read at `battery.cpp:1599`
only to guard its own write, so it remains effectively write-only, but `b6bbf31` rewrote the
surrounding comments and the issue's quoted comment text may no longer match. Re-read before
closing.

---

## 2. The waves

Every wave's "done" is an entry in [`docs/EVIDENCE.md`](../docs/EVIDENCE.md) carrying date,
commit SHA, host, what was measured, raw observation, and verdict. Anything short of that
leaves the issue open.

### Wave 0 — unblock the tooling (no hardware)

**Goal:** stop the scripts from lying about what happened, before any wave depends on them.

- **Closes:** #31, #33
- **Prerequisites:** none
- **Done looks like:** `scripts/push.sh` exits 0 when workstation, build host and GitHub are
  already synchronized and `from-workstation` is absent, and still fails on a real merge or
  push failure. `scripts/flash.sh` distinguishes a transport error after a completed write
  from a genuine failure — the acceptance case is the 2026-08-03 observation: board moves
  `239A:0029 → 239A:8029` and runs the new image while `adafruit-nrfutil` raises
  `PortNotOpenError`. Evidence entry: host Heliotrope Ridge, the SHA, both script runs.
- **Effort:** ~half a day.
- **Also closeable in this pass, on inspection only:** #35 (verify then close), #11 (decide
  from the ledger; if the entry does not cover it, it stays in Wave 1).

### Wave 1 — in flight, radio round trip

**Goal:** prove the node can be commanded and can recover.

- **Closes:** #10, #11 (if not already closed in Wave 0)
- **Prerequisites:** Wave 0 (#33 — a false FLASH FAILED costs an operator double-tap each
  time)
- **Done looks like:** an interval downlink sent and observed applied; then a board reset,
  then a second downlink observed applied on the restored session. That second step is what
  closes #10 — until it passes, a node that reboots in the field is deaf and nothing in the
  uplink stream shows it. Evidence entry with the TTN console excerpt and the serial capture.
- **Effort:** one bench session, ~2 h, plus waiting on cycles.

### Wave 2 — one code pass, no hardware

**Goal:** clear the source-level defects and the spec drift while the bench is free.

- **Closes:** #42, #36, #37, #38, #39, #43, #41, #24, #34
- **Prerequisites:** none, but land **#42 first** — it is what lets #36 and #37 ship with
  host tests.
- **Order within the wave:** #42 → #37 (high, data integrity: a partial record set returning
  `Ok` with stale values reports stale telemetry as fresh, which is worse than a null) → #36
  (high, unsound address validation) → #38 → #39 → #43 → #41 → #24 → #34.
- **Done looks like:** `scripts/preflight.sh` green; new host tests in `env:native` covering
  the truncated-frame case that `742502e` fixed; a build on Heliotrope Ridge with the SHA
  reported. **#38 and #39 are not closed here** — #38 needs the H3 brownout observation and
  #39 needs awake-time in the soak log. Close them in Wave 3 / the final gate respectively.
- **Effort:** 2–3 days. #41 is a docs edit but must land in the same PR as anything it
  describes, per the spec-parity gate.

### Wave 3 — one instrumented bench session, meter attached

**Goal:** replace every placeholder in [`docs/POWER_BUDGET.md`](../docs/POWER_BUDGET.md) with
a measurement, and settle the two cheap observations that gate the payload bundle.

- **Closes:** #8, #12, #40, #7, #4, #5, and the evidence half of #38 (H3). #9 only if it is
  cold enough for the heater to switch on.
- **Prerequisites:** Wave 0 (flash reliability), Wave 2 landed (so the measured firmware is
  the one that ships), battery power available — **sleep current measured over USB is
  meaningless**, per the ledger's own trap list.
- **Done looks like, per issue:**
  - **#8** — baseline sleep current on battery; whether `SPI_LORA.end()` actually removed the
    milliamp it was reasoned to remove; the delta to the deepest sleep state, so the
    session-rebuild cost can be judged against a number instead of a guess.
  - **#12** — the same trace answers it: a flat low current means `delay()` parks the task; a
    sawtooth at the FreeRTOS tick means it does not.
  - **#40** — two checks, only one needs the meter. Flash, let it sleep once, replug a laptop:
    if USB never re-enumerates, the only in-field diagnostic path is gone. Separately, watch
    watchdog tick accumulation across long sleeps.
  - **#7** — pack cutoff voltage and cell count measured, not inferred from the 10.8 V
    nominal. If the pack's own protection cuts in above our 9.60 V inhibit, the firmware
    never gets to stop transmitting and reconnecting the pack is a site visit.
  - **#4** — raw temperature record at known room temperature. ~210 = tenths, ~21 = whole
    degrees.
  - **#5** — a captured reply frame compared against the expected checksum. Do not loosen the
    check to make a frame pass; a loosened check lets corrupted readings through.
  - **#38 (H3)** — sag the supply, observe the transmit skip, observe no flash thrash.
- **Effort:** one long session, most of a day, plus write-up.

### Wave 4 — one charge/discharge cycle in sunlight

**Goal:** close the last thing blocking the payload schema.

- **Closes:** #3, and thereby conflict 1 in
  [ADR-0002](../docs/decisions/ADR-0002-payload-contract-conflicts.md)
- **Prerequisites:** Wave 3 (pack instrumented), sunlight, and patience — this one is
  weather-bound and cannot be compressed
- **Done looks like:** the pack observed charging, and the sign of `batt_current` recorded as
  it moves. `payload/schema.yaml` marks `batt_current` `BLOCKED` and the parity gate reports
  it on every build, so this cannot be quietly skipped. **No charge/discharge or brownout
  logic may depend on the sign until this closes.**
- **Effort:** one sunny day of observation; minutes of actual work.

### Wave 5 — the payload bundle, then freeze

**Goal:** make every change the decoder will ever see, in one commit, then lock it.

- **Closes:** #16, #17, #18 (and lands the encoder side of #3, #4), then **#13**
- **Prerequisites:** Wave 4 complete. #13 cannot precede a single decoder-affecting change.
- **Done looks like:** one PR against this repo and one paired PR against the TTN formatter in
  `forest-weather-machines`, merged together. `scripts/check_decoder_parity.py` green. A live
  uplink decoded by the deployed formatter and recorded in the ledger with the SHA. Then
  `payload/schema.yaml` moves from draft to frozen, `batt_current` moves off `BLOCKED`, and
  the decoder hash is re-pinned.
- **Effort:** 2–3 days including the sibling-repo change and the paired review.

### Final gate — soak and shadow

- **Closes:** #14, and the evidence half of #39
- **Prerequisites:** everything above, plus #2 (buck converter) in hand and #23 (devices
  registered) done
- **Done looks like:** ≥24 h continuous bench soak with a log, then ≥7 d field shadow
  somewhere a wedged node is a walk and not an expedition, with TTN ingest history. This is
  the only evidence that closes H1–H8. The failures worth catching here — a slow leak in the
  power budget, a session that degrades over days, a counter that wraps — are invisible in a
  short test by construction.
- **Effort:** 8 days of wall clock, near-zero of attention, but it cannot start until
  everything else is done.

### After the gate

- **#15** — derive the minimum interval from measured airtime at the current data rate
  instead of the fixed 1800 s floor. Wants soak-measured airtime first.
- **#22** — revisit only if `SX126x-Arduino` gains DevNonce persistence. Otherwise close as
  deferred.
- **#25** — unblocked only by #26.

---

## 3. The dependency graph

```
#31, #33  (tooling)
   │
   ├──> #10, #11  (radio round trip)
   │
   └──> #42 ──> #36, #37 ──┐
                #38, #39 ──┤
                #43, #41 ──┼──> Wave 2 landed
                #24, #34 ──┘        │
                                    v
                        #8, #12, #40, #7, #9   (meter session)
                        #4, #5                 (cheap bench observations)
                        #38 evidence (H3)
                                    │
                                    v
                              #3  (charge cycle, sunlight)
                                    │
                                    v
                    #16, #17, #18  + encoder side of #3, #4
                                    │
                                    v
                               #13  FREEZE          <-- blocks-deployment
                                    │
              #2 (buck) ────────────┤
              #23 (register) ───────┤
                                    v
                               #14  SOAK + SHADOW   <-- blocks-deployment
                                    │
                                    v
                          #15 (post-soak airtime)

  #26 ──> #25            (operator decision gates bench cadence)
  #22                    (blocked on library, no path from here)
```

**#13 must come after every decoder-affecting change.** That is the whole point of a freeze —
freezing early means either breaking the freeze or shipping a schema that does not carry
fields the firmware already has. #3, #4, #16, #17 and #18 all change what the encoder emits.
All five land first.

**#14 is last and gates deployment.** Nothing in the soak is worth running against firmware
that is still changing, and the soak is the only thing that produces H1–H8 evidence. It also
depends on two operator items that have nothing to do with code: the buck converter must be
chosen and fitted, and the two remaining devices must be registered.

---

## 4. Why the payload changes ship as one change

The payload is a **two-repo contract**. The encoder lives here; the TTN formatter lives in
`forest-weather-machines`. The decoder throws on an unknown type and discards the **entire**
uplink — a drifted encoder does not lose one field, it loses every field in every uplink.

So each of the five payload changes is not a small change with a small blast radius. Each one
is a coordinated merge across two repositories, and each one is an independent opportunity to
put a node into a state where it transmits perfectly and the network records nothing. Five
separate payload changes is five of those windows. One is one.

The node is hiked in and left for months. There is no rollback that does not involve a walk.

Practical consequence: #16, #17, #18 and the encoder side of #3 and #4 land in a single PR
here and a single paired PR in `forest-weather-machines`, merged together, verified by
`scripts/check_decoder_parity.py` and by one live decoded uplink recorded in the ledger.
Then #13 freezes it and re-pins the decoder hash.

---

## 5. Operator-owned — decisions and physical acts

None of these are agent work. Each one has something specific that is needed and something
specific that slips without it.

| # | What is needed | What slips without it |
|---|---|---|
| **#2** buck converter | Pick a part with **idle draw in microamps, not milliamps**, and fit it. It runs continuously, so a milliamp of quiescent current outdraws everything else on the node combined. | **The critical path for any real deployment.** Labelled `blocks-deployment`. Without it the power budget cannot be closed at all — the measured sleep current from #8 is meaningless if an unchosen part sits in parallel drawing more than the whole node. The #14 soak should not start until the deployment-configuration buck is in circuit. |
| **#23** register 002/003 | Register both devices with **MSB-order** DevEUI and AppEUI. `SX126x-Arduino` requires MSB-first and reverses the bytes itself. | Labelled `blocks-deployment`. Getting the order wrong is unusually expensive: an unrecognised DevEUI is neither answered nor logged, so the node transmits perfectly and the console shows no join attempt, no error, nothing to bisect. It cost a session on 2026-07-31. Without this there is only one node, so no shadow deployment with a spare. |
| **#19** BLE yes/no | A decision. | Sleep current target and the enclosure/antenna plan both depend on the answer. Undecided means #8's measurement may need repeating. |
| **#20** enclosure entries | One M8 is fitted; two cables need to come in. Decide the glands. | Blocks physical assembly, therefore blocks the #14 field shadow. |
| **#21** breather vent | Fit one. | Condensation inside a sealed box in the woods. Blocks the field shadow, not the bench soak. |
| **#26** private network | Decide whether a private LoRaWAN network is worth standing up for bench cadence testing. TTN fair-use limits do not apply on a private network; a FUP exception is not the route. | #25 stays blocked and stage-3 bring-up keeps waiting out the 1800 s floor between uplinks, which makes every radio iteration slow. Nothing else slips. |

---

## 6. Release-blocking versus nice-to-have

**The spine — labelled `blocks-deployment`:** #2, #13, #14, #23. Everything else in this plan
exists either to make one of these four possible or to avoid regretting it later.

**Release-blocking in practice, though not labelled:** #3 (the schema cannot freeze while
`batt_current` is `BLOCKED`), #4 (an encoder that is wrong by a factor of ten is frozen wrong),
#7 (if the pack's protection cuts in above our inhibit threshold, the firmware's brownout
logic never runs and recovery is a site visit), #8 (every downstream number in the power
budget is a placeholder until it is measured), #37 and #36 (stale telemetry reported as fresh,
and unsound address validation, on the path that just started working).

**Should land before deployment, but would not stop it:** #38, #39, #40, #41, #42, #5, #12,
#31, #33.

**Nice-to-have:** #34 (diagnostics for a bus that is no longer the debug path), #43 (stale
member and comment), #24 (misleading backoff message), #16, #17, #18 (real value, but the node
works without them — they are bundled with the freeze because bundling is cheaper than a
second decoder change later).

**Deferred with no path from here:** #22 (library), #25 (needs #26), #15 (wants soak data).

---

## 7. Recommended order, and what runs in parallel

| Slot | Work | Runs in parallel with |
|---|---|---|
| 1 | Wave 0 tooling (#31, #33). Close #35 on inspection; decide #11 from the ledger. | — |
| 2 | Wave 1 radio round trip (#10, #11) | Wave 2 code work — different people, different failure domains, one needs the bench and the other does not |
| 2 | Wave 2 code pass (#42 → #37 → #36 → #38 → #39 → #43 → #41 → #24 → #34) | Wave 1 |
| 3 | Wave 3 instrumented bench session (#8, #12, #40, #7, #4, #5, H3 for #38) | Nothing — one board, one meter, and the firmware under measurement must be still |
| 4 | Wave 4 charge cycle (#3) | Wave 5 can be *written* here but not merged; the sign is an input to it |
| 5 | Wave 5 payload bundle + freeze (#16, #17, #18, then #13) | Nothing decoder-adjacent, by construction |
| 6 | Final gate: #14 soak, then shadow | #15 can be prepared; operator items #19, #20, #21 must be done *before* this starts |
| 7 | Post-gate cleanup (#15; close or defer #22, #25) | — |

**Honest about parallelism:** slots 3 through 6 are essentially serial. There is one board,
one meter, and one pack. The only genuine parallelism is slot 2 (bench work alongside
source-only work) and the operator track, which can and should run from day one — **#2 in
particular should be decided now**, because it is on the critical path for slot 6 and has a
procurement lead time that no amount of agent work compresses.

Weather gates slot 4. If sunlight is not available, slots 5 and 6 wait, and the useful thing
to do meanwhile is Wave 2 depth: more host tests behind #42.

**Rough total:** roughly two weeks of work, plus 8 days of soak and shadow that mostly runs
itself, plus however long #2 and #23 take on the operator side. The soak cannot start until
everything above it lands, so the operator track is the one worth starting first.
