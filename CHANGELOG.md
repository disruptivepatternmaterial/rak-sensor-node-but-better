# Changelog

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).
Versioning per [`docs/RELEASE.md`](docs/RELEASE.md).

`1.0.0` is earned, not scheduled — it requires H1–H8 closed in
[`docs/EVIDENCE.md`](docs/EVIDENCE.md), including a ≥24 h bench soak and ≥7 d field shadow.

## [Unreleased]

### Fixed

- **A failed brownout-hold write silently reported success forever, and the hold was lost on the
  next reset.** `Config::set_brownout_engaged()` assigned `m_brownout_engaged` *before* calling
  `save()` and did not roll it back on failure — unlike `set_interval()` ten lines above, which
  does. So after one failed write, RAM held the intended value, the early-out
  `if (engaged == m_brownout_engaged) return true` matched on every subsequent call, and the write
  was never retried. The node then believed it had persisted a transmit-inhibit hold that existed
  only in RAM, and a reset resumed transmitting into a pack too low to support it. Found by
  adversarial review; the fix restores the previous value on failure so the next call retries.
- **Valid Modbus exception replies were reported as `BadFrame` and retried, and the exception
  handler was unreachable.** `Modbus::read_holding()` sized its receive loop to the *normal*
  reply length (`5 + 2N`), so a five-byte exception response left the loop waiting for bytes the
  slave would never send; the inter-byte-gap branch then fired and returned `BadFrame` before the
  exception check could run. A refusal that should have ended the transaction as a final answer
  instead consumed the full retry budget and was reported as line noise. The loop now shrinks its
  target to five bytes as soon as the function code comes back with the high bit set
  (`CIT-MODBUS-APP` §7).

### Removed

- **Nine dead constants and ~72 lines of citation prose in `src/sensors/battery.cpp`** left over
  from the parameter-write (`PARAMSET`) phase that was itself deleted earlier. Every one was
  referenced exactly once — by its own definition — including a 14-line block reasoning about
  RAK's 3000 ms acknowledgement budget for a write this firmware never sends. `kRuleDisable` and
  `kRulePeriodic` are genuinely live in the descriptor decode and were kept, with their citation.
- **Five review documents, 1,530 lines.** The retracted `2026-08-30_onewire_pin_failures.md` (its
  pad count and its mechanism were both wrong); `2026-08-12_console_sleep_question.md` (superseded
  by [ADR-0008](docs/decisions/ADR-0008-console-in-the-field-image.md), which is the durable
  record); `2026-07-30-DOWNLINK-AND-RESILIENCE.md` (superseded by
  [`docs/DOWNLINK_MATRIX.md`](docs/DOWNLINK_MATRIX.md), 8/8 on hardware); and both 2026-08-12
  adversarial reviews, whose accepted findings are issues. Kept: the RAK reference benchmark,
  which is the target of a `CITE(bench)` in `src/radio.cpp`, plus the spec-drift and cruft-pass
  reviews, which still have live inbound references. All inbound links were repaired and
  `scripts/preflight.sh` is green.

### Changed

- **Comment cruft pass across the firmware sources — no code changed, and that is verified rather
  than asserted.** Every changed `.c`/`.h` file was comment-stripped and compared against `HEAD`;
  the token stream is byte-identical in all eleven, so the image cannot move. Three kinds of rot
  went out: the `owscan` deletion tombstone, which was recited at full length in four places
  (`src/build_features.h` 51 lines, `src/main.cpp` in three spots, `platformio.ini` ~98 lines) when
  [`AGENTS.md`](AGENTS.md) already holds the canonical copy and auto-loads; roughly twenty
  change-narration comments that described what the code *used to* do rather than what it does now,
  rewritten to state the current invariant while keeping every issue reference; and stale
  references to deleted machinery. Three of these were actively misleading, not merely verbose:
  `src/sensors/battery.h` carried a **dangling half-sentence** left by an incomplete edit
  (`"BOOT once, then keep answering…"`) directly contradicting the line below it, which says the
  window deliberately transmits nothing — the exact confusion behind
  [#62](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/62);
  `platformio.ini` **contradicted itself**, pointing readers at `env:owcensus` as the replacement
  for the deleted scans ~60 lines before explaining that `env:owcensus` was deleted too; and
  [`README.md`](README.md) still **advertised `owscan` as a runnable diagnostic environment**, so
  the one user-facing list of environments named a target that no longer exists and must never
  exist. All 449 `CITE` markers still resolve and `scripts/preflight.sh` is green. The remaining
  bloat is structural, not textual — `src/sensors/battery.cpp` is 1,901 lines at 64% comment and
  needs splitting, filed as [#103](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/103);
  six behaviour-preserving code dedups that need a build-host compile are
  [#104](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/104).

### Fixed

- **`src/sensors/rk900.h` told the reader the RS-485 port runs at 4800 baud. It runs at 9600.**
  `kBaud = 9600` in `rk900.cpp`, settled by measurement in
  [ADR-0006](docs/decisions/ADR-0006-rk900-baud-and-register-map.md) — this physical unit replies
  at 9600 and returns **zero bytes at 4800** across four consecutive sweeps. The header asserted
  the datasheet's 4800 as though it were the firmware's setting, on the one constant that decides
  whether the sensor answers at all, which is the spec-parity failure
  [`.cursor/rules/30-change-workflow.mdc`](.cursor/rules/30-change-workflow.mdc) exists to catch.
  The `[CIT-RK900]` citation still says 4800 because the datasheet does; the prose no longer
  claims the node transmits it. Also removed a stale description of `FEATURE_ONEWIRE_SPLIT` in
  `src/sensors/battery.h` that documented a two-pin topology as a live build option — the flag has
  no definition anywhere, only a tombstone in `battery.cpp`.

- **Recorded why the join backoff's in-loop bound cannot be removed.** A review proposed deleting
  the `seconds < kBackoffMaxSeconds` term in `Radio::backoff_seconds()` as redundant with the
  trailing clamp. It is not: `m_failures` is zeroed only by a successful send or join, and the
  rejoin escape leaves it alone, so an unreachable gateway makes it climb monotonically — 24
  failures within a day at the 3600 s cap. Without the in-loop bound, `900 << 23` overflows
  `uint32_t` and the backoff **wraps to a small value**, so the node would transmit *more* often
  the longer it had been failing, breaching [CIT-TTN-FUP] and `FIRMWARE_SPEC.md` §7 H1. The
  trailing clamp cannot catch it — it only sees the wrapped value. No code changed; the invariant
  is now written down at the line so it does not get "simplified" later.

### Removed

- **The one-wire scan diagnostic is deleted for driving an unqualified pad at 14× the production
  rate.** It is also the leading candidate for the seven destroyed pads, but **that is not
  established** and the deletion does not rest on it.
  `src/diagnostics/owscan.{h,cpp}`, `FEATURE_ONEWIRE_SCAN`, `OWSCAN_CENSUS_ONLY`, `OWSCAN_PIN`
  and the five `owscan*`/`owcensus` environments are gone. Seven GPIO pads across two RAK4631
  cores, every one the pad carrying the pack's data line ([#102](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/102)). Phase 0 drove 192 bytes per
  cycle — 64 × `0x55` at three baud rates, cycling in seconds — against ~14 bytes per 900 s from
  the production read, and `0x55` toggles every bit, the worst case for the contention path in
  [#99](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/99). **The operator never invoked those environments; agents flashed them over SSH,
  repeatedly**, which is why the code is deleted rather than commented — a warning is read by
  exactly the sessions that already ignored one. It was also the weaker instrument: two
  multimeter readings settled in a minute what the firmware census got wrong across several
  sessions and several discarded cores, and a firmware census structurally cannot make that
  comparison, because reaching a pad means driving it.

### Added

- **The pack's data line is measured, ending the project's oldest unsourced assumption.** Its
  logic level had never been established on any node — it was taken to be 3.3 V because pin 4 is
  *labelled* `3V3_In`. With pin 4 energised from a bench supply and no Core in the loop, the line
  idles at **+3.3118 V**, drives low to **+0.0867 V**, peaks at **+3.318 V** against the pad's
  3.600 V maximum, and never goes negative across 9,520 edges. So the pack cannot overdrive the
  pad, and structurally never could: its driver sits on pin 4, which the build feeds from the
  node's own `VDD`. Two things the same numbers do *not* clear — the pack is an **active** low-side
  driver rather than a pull-down, and the line idles 11× over an *unpowered* pad's 0.3 V limit.
- **The pack's announcement frame decoded off the wire.** It broadcasts 92 bytes every ~2.2 s
  **unprompted** at 9600 8N1, identifying itself as ASCII `"RAK2560-io"`, with six consecutive
  ID/value triplets. All nine captured frames were byte-identical, so this is an announcement and
  not telemetry — listen-only cannot be assumed sufficient.
- **`scripts/owprobe.py` turns a Saleae analog export into a verdict** against the nRF52840's real
  limits, and refuses to call a capture evidence when it cannot be. It separates the three
  near-zero cases by **noise** rather than magnitude — floating clip above 20 mV stdev,
  de-energised driver below it, genuine driven low — because an open clip and a healthy line at
  0 V are indistinguishable by level alone, and reading one as the other is how a core dies.
- **`FEATURE_BATTERY_PIN_SCL`**, `WB_I2C1_SCL`/P0.14, held in reserve as the last pad on node
  002's core. Deliberately selected by **no build environment**, so nothing can move the link to
  the last good pad before the transmit path is fixed.
- **`scripts/flash.sh --wait`** sits on the build host polling once a second and starts the upload
  the moment the node attaches, with the poll and the upload in one SSH session so there is no
  round trip inside the wake window. A sleeping node is absent from the USB bus entirely ([#60](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/60)),
  and catching that window by hand was previously the operator's problem.

- **The boot banner now names why the node reset.** `RESETREAS` was read and everything except
  the watchdog bit discarded, so a reset was anonymous and its cause had to be inferred from
  frame-counter arithmetic afterwards. That inference was wrong repeatedly on 2026-08-30:
  uniform `+32` counter steps were read as a failing node when they were the `kCounterMargin`
  signature of resets caused by **a console attach**, three times out of three. All nine bits
  are decoded now, and an all-zero word is the load-bearing case — `RESETREAS` is cleared by a
  power-on reset, so no bits set means the rail dropped, which is otherwise indistinguishable
  from a healthy first boot.
- **`scripts/owscope.py` measures the one-wire line with a Saleae Logic instead of inferring
  it.** From the node's side, `no reply, 0 bytes` is the same output for three different
  faults: a pack that never spoke, a pack that spoke below the pin's input-high threshold, and
  a damaged pin. Three sessions argued between them from that one string. The script drives
  Logic 2's automation server and reports edge count, resting analog level against the 2.31 V
  VIH, and shortest pulse width as a baud check. Analog rather than digital deliberately — a
  digital capture thresholds away the quantity in question. Logic 2's MCP server is also usable
  directly; see `docs/HARDWARE.md`.

### Fixed

- **`3V3_S` is now held up while the one-wire data line runs through the RAK5802.** When the
  pack's data line enters via the module's SDA spring terminal, it passes through a module on
  the switched rail. The cycle reads the RK900 first, and `RK900::power_off()` drops `WB_IO2`
  and never raises it, so `Battery::read()` was driving 3.3 V into an unpowered module every
  cycle. A scope guard, not paired calls, because `read()` returns from several places and
  forgetting one leaves the RAK5802 powered through sleep — about a milliamp, which dominates
  the budget at an hourly cadence. **This is not the explanation for the destroyed pads**: the
  SDA routing postdates two of the failures by roughly twelve hours, and A1 died with the wire
  on the always-on header. Fixed because driving a signal into an unpowered module is wrong on
  its own terms.

### Changed

- **Battery one-wire moved `IO1` → `A1` → `SDA`, and none of those pads survived.** The
  previous entry here recorded the move to `A1` as a fix; it was not. **Seven GPIO pads have
  now been destroyed across two cores** — four on the core currently in node 002, three on its
  predecessor — and every one was the pin carrying the pack's data line at the time. A 1 kΩ
  series resistor was inline when `SDA`/P0.13 died, so **series resistance is refuted as
  sufficient protection**, and node 002's harness has been replaced three times, so the harness
  is not the constant either.

  Critically, `src/sensors/battery.cpp` is **unchanged since `1c2df3c` (v0.4.3)** — the image
  running without incident on node 001 in the field. The push-pull `pinMode(tx, OUTPUT)` in
  `SoftwareHalfSerial::setTX()`, the four wake bytes, the provisioning ladder and the `0x55`
  timing sweeps are common to both nodes, which refutes every firmware explanation for the
  divergence, including the bus-contention theory raised and discarded during the session.
  **The cause is unestablished**, and the remaining untested variables are node 002's pack and
  the bench procedure of attaching a host USB cable while the pack is live.
  ([#96](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/96),
  [#102](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/102))
- **Node 002 works as a wind-only node.** On the SDA field image it reads the RK900
  (`0.00 m/s, 26.3 °C, 43.7 %RH, 1007.8 hPa`), transmits 20 bytes on port 2, and closes the
  cycle on a normal sleep. Battery telemetry on this core is finished — the pads are gone.
- **The interval floor is 900 s and downlinks below it are correctly refused.** Recorded
  because an afternoon was spent wondering why the cadence never changed:
  `config : rejected interval 300 s (allowed 900-86400)`.

## [0.4.4] — 2026-08-28

`0.4.4` is the hardened device-test image for rebuilding node 002 and bringing up node 003.
It is **not deployment-qualified**: the new brownout, session-repair, and fault paths still
require device fault injection, and H8 still requires a 24 h bench soak plus 7 d field shadow.

### Security

- **The build host's public IP was in tracked files and is now out, with a gate to keep it
  out.** `README.md` carried it, and two `docs/EVIDENCE.md` entries carried a second public
  address on the same subnet, one with the account name attached. The repository is public.
  `scripts/preflight.sh` now fails on any IPv4 literal in a tracked file — the rule had been
  written down in four places and enforced in none, and the new gate found the two
  `EVIDENCE.md` leaks on its first execution. **History is deliberately not rewritten**: a
  purge of just the public addresses would rewrite 85 of 252 commits and invalidate 34 commit
  SHAs cited in this project's own documentation, including `1c2df3c`, `d568574` and
  `65f8615` — the only three commits a board has ever asserted from its own boot banner — and
  `572bcfa`, the 19.03 h soak. The decision, the options rejected, and the mitigation that
  does address the exposure are recorded in
  [ADR-0009](docs/decisions/ADR-0009-address-exposure-rotate-not-rewrite.md); tracking in
  [#85](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/85).
- **`scripts/preflight.sh` now also verifies that the irreplaceable evidence commits still
  resolve** — the three banner-asserted SHAs plus `572bcfa` and `4510763`. It is the tripwire
  for ADR-0009: if anyone ever does rewrite history, the broken evidence chain fails the gate
  immediately instead of being discovered later by a reader who cannot find `1c2df3c`.

### Added

- **`scripts/register_device.sh` — registering a node is now scripted, and the byte order is
  checked by a machine rather than by a human reading two hex strings.** The three checks in
  [#23](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/23) were
  a manual checklist; they are now `verify` (TTN versus `src/secrets.h`), `banner` (the boot
  banner versus `src/secrets.h`, which is the value actually compiled into the running image)
  and `session` (has the Network Server ever accepted a join). `gen` produces MSB-order keys
  with a locally-administered DevEUI, and `create` registers the device using the same bytes
  already in `secrets.h` rather than generating new ones, removing the transcription step the
  bug lives in.

  The useful part is not the automation, it is the diagnosis. `SX126x-Arduino` takes the DevEUI
  and JoinEUI most-significant-byte-first and reverses them itself; a widely-copied Arduino
  LoRaWAN library wants them reversed, so reversed examples are the easy thing to find. A
  reversed DevEUI describes a device that does not exist, and an unrecognised join request is
  neither answered nor logged — no join attempt, no error, nothing to bisect, while the node
  transmits perfectly and forever. It cost a session on 2026-07-31. `verify` reports
  byte-reversal **as byte-reversal, by name**, distinguishing it from "wrong device", which is
  precisely the distinction the TTN console cannot make because both present as nothing at all.
  `check` does the same comparison offline against a pasted console value, so it needs no
  credentials.

  Gated by its own `selftest` — ten cases including a deliberate reversal, a reversal that must
  *not* be misreported as a mismatch, a truncated header, a missing header, and a banner with no
  DevEUI line — wired into `scripts/preflight.sh`. Verified in both directions: breaking the
  reversal branch turns the run red, restoring it turns it green.
  [#76](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/76)
  shipped a check that could not fail, and this one is trusted immediately before a device goes
  in the woods. Writing the selftest also caught a real bug in the script: `die` inside `$(...)`
  only exits the subshell, so a missing `secrets.h` was reported as "80 hex digits, expected 16"
  with the error text itself parsed as hex.

### Fixed

- **Transmitting indefinitely past an unremovable frame-counter ceiling made the next reset
  outage grow without bound.** TTN ignores lower counters until `FCntUp` exceeds the highest
  value previously accepted, so six months of post-ceiling uplinks could produce six months of
  locally successful but network-discarded frames after one watchdog reset. Repeated checkpoint
  failure plus a removal I/O error now triggers a safe-voltage filesystem format, rewrites the
  in-RAM Config, and re-anchors the live session. A failed format retries behind a 96-call
  cooldown rather than becoming a permanent hold or thrashing every wake. A brownout keepalive
  never formats the whole filesystem: it retries only the small operation until a healthy cycle
  can repair it.
  ([#90](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/90))
- **The filesystem wrappers hid the errors replay protection depends on.** `exists()` returns
  false for both “absent” and I/O error, so `forget()` could clear its RAM ceiling while a stale
  file survived. `File::flush()` and `File::close()` return void and discard commit errors, so
  the atomic writer could rename an uncommitted staging file over a valid record. Config and
  session now share one raw-LittleFS primitive that checks open, write, sync, close, and rename;
  reads distinguish absent, malformed, and I/O failure; removal accepts only `OK` or `NOENT`;
  and a format is followed by an explicit remount even when Adafruit's internal mounted flag was
  cleared by an earlier failed attempt. A failed write preserves the live record.
- **The first filesystem mount could erase all persistence before the node had any voltage
  evidence.** `InternalFileSystem::begin()` is a destructive recovery wrapper: one failed mount
  erases all seven flash pages, formats, and retries. `Config::begin()` called it before
  `brownout.begin()` and before the first pack reading. Boot now calls the non-destructive base
  mount; only the voltage-gated session repair path may format.
- **Three local radio-send failures could create a fresh session while an old session remained
  on flash.** If saving the rejoin then failed, the next reset restored credentials TTN had
  invalidated, and unconfirmed sends looked locally successful forever. Rejoin now proceeds
  only after the stored session is proven gone; otherwise it is blocked until safe filesystem
  repair. The same guard covers unreadable records and every failed restore component, not just
  the uplink counter.
- **The registration verifier could approve credentials the firmware did not use.** Its source
  parser matched a commented-out `OTAA_DEVEUI` before the active definition, `create` hardcoded
  an all-zero JoinEUI instead of reading `OTAA_APPEUI`, `verify` checked only DevEUI, and
  `banner` checked the oldest identity in a capture spanning multiple boots. It now reads the
  C++ preprocessor's active macro table, creates and verifies both EUIs, and checks the latest
  boot banner. The selftest grows from 10 to 17 cases, including mocked TTN create/get calls;
  preflight requires the exact `17/17` completion sentinel rather than printing an unchecked
  count. Found by the multi-model deployment review.
  ([#23](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/23))
- **A pack oscillating across the 9.60 V inhibit floor could suppress the keepalive forever.**
  Every measured-low cycle disarmed the keepalive and reset its clock; the next in-band cycle
  re-armed it at zero. A 9.5 V / 9.7 V pattern therefore never reached the 24-cycle bound, so
  the Class A node stayed mute and uncommandable for the entire low-voltage period. Low cycles
  now suppress only the current transmission while preserving elapsed hold time; the first
  later in-band cycle sends if the bound has expired. No keepalive is ever sent on a cycle
  measured at or below the floor. The adjacent intermittent-link path is covered too: four
  unreadable cycles change whether the existing clock may transmit without restarting it, so a
  link answering once every five cycles cannot erase elapsed hold time. Found by the
  unattended-year adversarial review.
  ([#45](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/45))
- **The frame-counter ceiling was the one hold in this firmware with no exit at all, and it
  could mute a perfectly healthy node in about 8 hours.** `session::counter_headroom_ok()`
  refuses an uplink when the live counter reaches the stored ceiling and the write that would
  advance it fails. If that write fails *permanently* — worn page, full filesystem, unmountable
  image — it refuses every uplink afterwards, forever. This needs no brownout, no unreadable
  pack and no hold: a healthy node reaches it roughly `kCounterMargin` = 32 uplinks after its
  last successful save, about 8 h at the 900 s field cadence. Being Class A, the mute node is
  also uncommandable, which is the state `AGENTS.md` says must never be reached, and the only
  exit was the write that was already failing. The refusal is now a ladder: three consecutive
  failures (a transient must not cost a rejoin), then `session::forget()` — a node with nothing
  stored has nothing to replay, so the check becomes unconditionally true and the node keeps
  transmitting, joining fresh after the next reset.
  ([#74](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/74),
  [#68](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/68))
- **`session::forget()` cleared its in-RAM state without checking that the file was actually
  removed** — the silent-replay hazard #68 predicted, sitting in the function the escape above
  depends on. Removal is itself a filesystem write, so on the broken filesystem that makes the
  escape necessary it is the operation most likely to fail; clearing `s_have_stored_session`
  anyway would let the node transmit past a ceiling a reset still resumes from, which is exactly
  the replay the ceiling exists to prevent. It now returns whether the file is gone and leaves
  the ceiling binding when it is not. The one caller that ignores the result (`restore()`, on a
  rejected counter) is self-correcting and says so — it already returns false, so the join's own
  `save()` overwrites the file that could not be removed.
- **When neither the write nor the removal works, the node now transmits anyway.** The two
  outcomes are not symmetric and treating them as equally bad is what produced a terminal state:
  staying mute is permanent, because nothing in the field repairs a worn flash page and an
  uncommandable node cannot be told anything, while transmitting past the ceiling costs nothing
  until a reset happens and then loses frames only until the counter climbs back past the
  highest value already sent — bounded, then it heals unaided. A temporary outage beats a
  permanent one. Logged once rather than per uplink, since it stays true for the rest of the
  deployment.
- **CI had been red on every run for a day, and the decoder-parity gate in CI was checking the
  wrong decoder.** `6af5964` re-pinned `payload/schema.yaml` to the restructured upstream
  formatter (`058bd69` / `717afceb…`) but never refreshed the vendored fallback copy in
  `payload/reference/`, which stayed at the superseded `9c58c2b9…`. Every machine with the
  private sibling repo cloned read the live file and printed `PREFLIGHT OK`; GitHub Actions
  cannot reach that repo, so it fell back to the stale copy — and before failing on the hash it
  had already compared all nine fields against the **wrong** decoder, printing
  `checked 9 fields against 19 decoder types` while doing it. The copy is refreshed, and
  `scripts/check_decoder_parity.py` now hashes the vendored copy against `pinned_sha256` on
  **every** run rather than only when it is the fallback. The checker had asserted in a comment
  that the copy "matches the pin by construction" and never verified it; on a live run its bytes
  were never read at all, which is why the drift was invisible everywhere it could have been
  caught. Regression-tested by restoring the exact stale file: it now fails locally.
  ([#81](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/81))
- **The preflight null-policy gate emitted six permanent false positives and could not see a
  real violation.** It searched for `= 0` on failure-shaped lines, which matched only counter
  resets (`m_invalid_reads`, `m_failures`) and member initialisers. The null policy is enforced
  by a *type* — `Maybe<T>` in `src/reading.h`, where `value` is meaningless unless `valid` is
  true — so `value = 0` is what `Maybe::clear()` does deliberately and was never the risk. The
  gate now fails on the two things that would actually put a fabricated zero on the wire:
  `Maybe::set(0)`, which marks a reading valid carrying a zero nobody measured, and any direct
  write to `.value` / `.valid` outside `reading.h` that reaches around the type's invariant.
  Both were verified to fire on a planted violation, and comment lines are excluded — the first
  draft flagged a comment in `src/sensors/battery.cpp` quoting RUI3 source, which would have
  traded six false positives for one. Six permanent warnings on a gate is how a gate becomes
  furniture ([rule 20](.cursor/rules/20-citation-discipline.mdc)).
  ([#72](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/72))
- **`scripts/soak_ttn.sh` treated a node transmitting far too often as perfectly healthy.**
  Silence was an anomaly and an unexplainable counter burst was an anomaly, but a clean `+1`
  step arriving minutes early was logged as an uplink and left `anomalies=0`. That is the FUP
  failure mode: the airtime budget is spent by *frequency*, and a node whose interval was set
  to 60 s by a mistaken downlink — a path proven remotely reachable and persistent across a
  reflash on 2026-08-13 — delivers flawlessly while burning the 30 s/24 h allowance ~15x
  faster, reading as the healthiest run on record. New `SOAK ANOMALY cadence-fast` fires below
  half the expected cadence, with a `GAP_VALID` guard so the mid-cycle baseline, the silence
  re-arm, and the counter-reset branch cannot manufacture anomalies out of gaps that measure
  nothing. `SOAK_FCNT_CMD` was added so the anomaly branches — the entire value of the harness,
  and the part #76 showed is easy to get wrong — can be exercised without a board or 24 h;
  three scenarios were run (too-fast, healthy, silence-then-recovery) and the healthy one
  reports zero anomalies.
  ([#76](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/76))
- **`scripts/capture.py` produced logs that could not name the image they captured.** Every
  serial line already landed in the log verbatim, so a boot banner was never missing — but
  nothing surfaced it, and nothing said so when it was absent. A capture taken mid-run without
  a reset contains no banner at all and, read back later, is indistinguishable from one that
  does: both are just serial text. It now emits `=== CAPTURE BANNER commit=<sha> ===` whenever
  the board names its image, and the closing `CAPTURE DONE` line carries
  `banner_commit=<sha|NOT OBSERVED|AMBIGUOUS (…)>`. Ambiguous is the case worth having: a
  reflash inside the capture window means one label would attribute half the log to the wrong
  image, which is worse than no label, so both SHAs are listed instead. The soak harnesses
  already did this — `_soak_monitor.py` records `banner_commit` and raises `commit-mismatch`
  against the launch SHA, and `soak_ttn.sh` captures it up front — so this closes the last
  harness that could not attribute its own output. Verified over a FIFO standing in for the
  device, across all three outcomes.
  ([#73](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/73))
- **`scripts/soak.sh` usage text printed four lines of its own source**, including the host
  fallback assignment, because `sed -n '2,28p'` ran past the header block.
- **`scripts/soak.sh status` printed the anomaly count twice on every healthy run.**
  `grep -c` prints `0` *and* exits 1, so the `|| echo 0` fallback fired on top of grep's own
  output. The anomaly count is what an operator reads to decide a soak is healthy.
- **The hint printed after `soak.sh start` was unreachable**, because `cmd_status` exits
  inside `here_or_there` on the relay path.
- `cmd_fetch` reported nothing when `scp` failed, and interpolated its run name into a remote
  command unquoted.

### Changed

- **`lmh_reset_mac()` is documented for what it does, and the sub-band re-selection beside it
  is no longer a guess.** The comment at `src/radio.cpp` called it the library's "MAC
  re-initialisation entry point"; its entire body is `ResetMacCounters()`, which re-initialises
  nothing. Read from the library source: it clears the ADR and ack-retry state and the pending
  MAC-command buffers, returns the data rate, TX power, RX2 channel, dwell times and EIRP to
  region defaults, and deliberately leaves the frame counters alone — `UpLinkCounter` and
  `DownLinkCounter` are commented out, and the sibling `ResetMacParameters()` is the one that
  zeros them. Two things fall out. It forces the data rate back to default, which is why the
  library saves and restores `ChannelsDatarate` around its own call; here that reset is wanted,
  because three consecutive failures are exactly when an ADR-negotiated rate should stop being
  trusted (relevant to
  [#69](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/69)).
  And it ends in `RegionInitDefaults(INIT_TYPE_APP_DEFAULTS)`, which is precisely what restores
  the 72-channel US915 default — so the adjacent `lmh_setSubBandChannels()` call is *required*,
  not defensive, and the comment that hedged "if it restored the region default" now states the
  mechanism. Comment-only; no behavior change.
  ([#70](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/70))
- **`wx3-harness` is gone.** It was the silent fallback for every remote operation in
  `remote.sh`, `push.sh`, `soak.sh` and `build.sh`, and the operator does not recognize the
  name. With no environment variable set — the state of a fresh shell — the scripts were
  connecting to a host nobody could identify, and the failure was being reported as a dead
  build host. There is now **no default**: `BUILD_HOST`, then `RAK_BUILD_HOST`, then the first
  line of the untracked `~/.rak-build-host`, and with none set the scripts fail by name at
  first remote use. Checked there rather than at parse time, so `soak.sh --local` on the build
  host still starts. Stale `~/.ssh/config` entry tracked in
  [#86](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/86).
- **The "unstable address" doctrine is retired.** The build host is a laptop that is not
  always on the same network. `10-environments.mdc`, `AGENTS.md` and `docs/ENVIRONMENTS.md`
  said the address was "not stable", that "two sessions have been burned", and that the host
  "vanishes without warning", which trained agents to open an investigation instead of asking
  for an address.
- **Corrected: this workstation can push to GitHub.** `gh` here is active as
  `disruptivepatternmaterial`, and `git push --dry-run origin HEAD:refs/heads/main` resolves
  cleanly. The 403 is real but belongs to the SSH keys, while `origin` uses the HTTPS
  credential helper. Three files claimed flatly that pushing was impossible and that
  `scripts/push.sh` was the only option, which made an unreachable build host look like it
  stranded commits.
- `docs/ENVIRONMENTS.md` "Known state (2026-07-30)" replaced with pointers to
  [`docs/STATUS.md`](docs/STATUS.md) and [`docs/EVIDENCE.md`](docs/EVIDENCE.md). It still
  claimed nothing had run on hardware and no RAK4631 was on the build host USB, long after two
  nodes had been flashed and joined.

## [0.4.3] — 2026-08-14

**This release has now run on hardware, and the board named itself.** `1c2df3c` was flashed as
`env:soak` (byte-identical to `env:rak4631`,
[ADR-0008](docs/decisions/ADR-0008-console-in-the-field-image.md)) and, after a single RESET press
at 17:50:12Z with a capture held open, printed `commit   : 1c2df3c` in its own boot banner —
matching what was flashed, with no `-dirty` suffix. That is the standard `AGENTS.md` requires: a
SHA read off the device, not asserted by the tooling. It is the **third distinct commit** ever to
do so, after `d568574` and `65f8615`. **The tag is cut on that basis** — the earlier text here said
no tag would be cut until the image had run on the board, and it now has.

What was observed: **cycles 3 through 8 ran unattended on the 900 s cadence**, about 1 h 15 m of
continuous correct cycling at ~908 s wake-to-wake, with **both sensors live on every cycle** (RK900
23.1–25.4 °C / 56.3–64.9 %RH / 1002.3 hPa / calm; the RAK9154 pack latched at `0x01` reporting
11.75–11.76 V, −0.01 A, 78 %, 23.0 °C) and **every cycle closing `sleep : 900 s`** and sending 35
bytes on port 2. Full raw capture in [`docs/EVIDENCE.md`](docs/EVIDENCE.md).

`H8` is **still not met, and this release does not advance it.** The longest bench soak remains
**19.03 h on `572bcfa`**, stopped deliberately short of 24 h to ship the `#75` fix in this release;
the soak on `1c2df3c` had a **deliberate RESET inside its window** (`resets=1`), so it is not an
uninterrupted 24 h either, and a partial run on one image cannot be topped up by another. The
field deployment on 2026-08-14 is **day zero of the ≥7 d shadow — a beginning, not an
achievement.** Sleep current remains unmeasured. Status stays `🚧 NOT YET DEPLOYED`.

**Which of the `0.4.1`/`0.4.3` fixes this run actually exercised, and which it did not.** A grep of
the capture for `brownout`, `provId`, `BOOT this`, `no confirmed latch`, `Unsampled`, `rejoin`,
`keepalive` and `silent at` returns nothing at all, and that absence is the honest boundary:

- **Exercised — the battery read path.** The pack answered `at 0x01` on all eight cycles and
  reported live values with **no BOOT spent**, which is the outcome `ec9725a` was written to
  produce. The healthy path is confirmed working end to end.
- **Exercised — the soak reader's frame-counter-step fix (`7b03d3a`).** It classified the +26 jump
  across the reset as one uplink consuming the 32-frame reset reserve and held `anomalies=0`,
  rather than reporting 26 phantom transmissions. Previously believed correct, now observed.
- **Not exercised, still believed correct — `ec9725a`'s own failure gate.** There was not one
  transient probe miss in eight cycles, so the consecutive-miss counter the fix adds never ran.
  A healthy pack cannot test it.
- **Not exercised — the brownout, rejoin and keepalive paths.** The pack sat at 11.75 V so no
  brownout engaged; the session was *restored*, not rejoined; every cycle produced a real uplink so
  no keepalive was due. Compiling cannot reach these, and neither can one clean afternoon.

Versioned **PATCH**: every change is a regression fix, a tooling fix, or documentation. No new
firmware capability, and no payload channel or type changed, so
[`.cursor/rules/60-decoder-parity.mdc`](.cursor/rules/60-decoder-parity.mdc) implies no paired
TTN formatter change. The one payload-adjacent change is `batt_current` moving from
`status: BLOCKED` to `proposed` in `payload/schema.yaml`, which records a convention the wire
already followed rather than altering it.

Open defects this release does **not** fix:
[#62](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/62) (the
pack re-latch path is still unproven),
[#68](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/68),
[#72](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/72) and
[#74](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/74).

### Decided

- **The battery-current sign convention is closed, and the payload freeze is unblocked.**
  Positive means the pack is **charging**, negative means **discharging** — the operator's
  decision on 2026-08-13, adopting the convention the pack itself reports. Recorded in
  [ADR-0002](docs/decisions/ADR-0002-payload-contract-conflicts.md), which moves from Open to
  Accepted; `batt_current` in `payload/schema.yaml` moves from `status: BLOCKED` to `proposed`
  with the convention spelled out, so `scripts/preflight.sh` reaches `PREFLIGHT OK` instead of
  `=== PREFLIGHT BLOCKED ===` for the first time since the gate was added. This had blocked
  `FIRMWARE_SPEC.md` §6 for six weeks.

  **Nothing on the wire changed, and that is the point.** The resolution was expected to
  require a paired change to the live TTN formatter. Read from code it required none: the pack
  word is parsed verbatim (`src/sensors/battery_frame.cpp`), emitted verbatim
  (`src/payload.cpp` `put_s16`), and the decoder's `WX_TYPES[185]` only sign-extends and
  divides by 100 with no negation anywhere in `arrayToDecimal` — its header has documented
  "positive = charging" all along. Choosing the pack's convention therefore means **no sign
  transform exists anywhere between the pack's register and the TTN record**, so there is no
  site at which the record can silently invert and no cutover window in which the two repos
  could disagree. The node kept transmitting on its 15-minute cycle throughout.

  The claim that lost was our own: `FIRMWARE_SPEC.md` §2.2's "negative = charging per field
  docs", a field note that most likely described the RAK2560 Hub's re-encoding rather than the
  pack's raw register. It is corrected, and per rule 20 the conflict stays documented in
  ADR-0002 rather than being erased. The `do not write charge/discharge logic against the
  sign` prohibitions in rules 50 and 60 are lifted and replaced with the opposite instruction:
  do not add an inversion on either side. A wrong flip here does not throw and does not drop
  the uplink — it quietly reverses charge and discharge, which is worse, because nothing fails
  visibly.

  **Not observed on hardware.** This is a decision about which convention the project records,
  not a bench result. The only reading in evidence is -0.01 A on a discharging pack — one LSB,
  consistent with the decision without proving it. A charge current well clear of the LSB has
  still never been captured, and no evidence entry claims it has.

### Fixed

- **A transient probe miss no longer spends the pack's only BOOT, and a genuine failure now gets
  one again.** `boot_once()` fired on the first cycle whose direct probe went unanswered and then
  latched for the rest of the *power cycle*, which on the field image is months. On `65f8615` a
  healthy pack — live in all twenty cycles of a `battdiag` capture — missed exactly one probe and
  answered the push listen in that same cycle with `11.92 V`, and that one miss both sent the
  vendor protocol's reboot verb to a working pack and consumed the deployment's only nudge
  ([#75](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/75)). The
  mirror-image residual was that a pack going mute weeks later had nothing left to nudge it with
  ([#71](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/71)).
  Renamed `boot_if_warranted()`, and now three gates must agree: three consecutive cycles with no
  reading of any kind (`kSilentCyclesBeforeBoot`), one BOOT per *failure episode* rather than per
  power cycle — re-armed by the next genuine reading — and a hard rate floor of one BOOT per 96
  cycles (`kBootMinSpacingCycles`, 24 h at the 900 s interval) that no reading clears. Worst case
  at 900 s: a pack answering normally sends zero BOOTs ever; a pack absent since power-on sends
  exactly one, ~45 min in; a pack flapping three-silent-then-one-reading is capped at one per
  24 h. The one-shot behavior from `342d994` that this replaces was there for
  [#62](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/62) — that
  reasoning is kept, not undone: BOOT is still never sent to a pack that is answering, and is now
  rate-limited on top. Both thresholds are **chosen engineering margins sourced to the 20-cycle
  bench capture**, not specified values — nothing in the vendor protocol says how many missed
  probes mean a wedged pack.
  **Unflashed and unobserved:** compile-verified only, on a board that must not be touched while
  a soak runs. The check that would prove it is a `battdiag` capture in which a transient miss
  produces *no* BOOT, plus one in which sustained silence produces exactly one BOOT per episode.

- **A soak can no longer be destroyed by a routine push or a scheduled flash.** Both scripts that
  reach the build host now detect a running soak first. The mechanism is not obvious and is worth
  stating once: a shell reads a script incrementally and keeps a byte offset into the open file
  ([`CIT-POSIX-SH`](docs/CITATIONS.md), POSIX.1-2024 §2.3), and `git merge --ff-only` *replaces* a
  file rather than editing it in place, so fast-forwarding the build host mid-soak makes the
  running shell resume at its old offset inside new contents and execute a fragment of a line.
  The symptom is a syntax error hours into a run. This nearly happened: `7b03d3a` modifies
  `scripts/soak_ttn.sh`, which is the exact script the 24 h TTN soak is executing.
  - `scripts/push.sh` still relays commits to the non-checked-out ref — always safe, since that
    never touches a working tree — and then decides about step 2/2 by **comparing the incoming
    diff against the scripts the running soak actually has open**. A docs or `CHANGELOG` push
    proceeds; a push that would rewrite the soak's own script refuses and parks the commits on
    the relay ref. The guard is narrow on purpose: one that blocked every push for 24 h would be
    overridden by reflex, and a guard overridden by reflex protects nothing. Override with
    `ALLOW_PUSH_DURING_SOAK=1`.
  - `scripts/flash.sh` refuses outright, because flashing resets the board and ends the run
    regardless of which files changed. Override with `ALLOW_FLASH_DURING_SOAK=1`. This closes a
    real near-miss: on 2026-08-14 a scheduled reflash ran against a host with an 18 h soak still
    in flight and was only averted by a worker noticing an unrelated dirty tree.

### Fixed — tooling

- **`scripts/check_citations.py` no longer reports "has no URL" for a citation whose URL is on the
  next line of a source file.** The continuation-folding logic added for the ADR-0006 sibling-SHA
  false positive required a continuation to be *whitespace*-indented, which is true of markdown
  but never of a `.sh`, `.py`, `.c` or `.h` file, where a wrapped citation begins at column 0 with
  `#` or `//`. So every multi-line citation in a source file read as unsourced. This was
  misfiling itself as a known false positive while being a real checker defect — the failure
  `scripts/push.sh:106: CITE(spec) has no URL` was reported over a URL sitting one line below it.
  The fold now strips an optional comment marker before testing for the indent. A citation that
  genuinely points nowhere still fails, and the run reports 392 markers with no new warnings.
- **The GNU Bash manual is replaced by POSIX.1-2024 as the cited authority for incremental script
  reading.** `gnu.org` returns **HTTP 403** to automated fetches, and rule 20 forbids committing a
  citation that cannot be verified. The Open Group's Shell Command Language §2.3 states the same
  fact normatively — "The shell shall read its input in terms of lines" — and is fetchable, so it
  is registered as [`CIT-POSIX-SH`](docs/CITATIONS.md) and the 403 is recorded next to it.

## [0.4.2] — 2026-08-13

**Release candidate, not a deployment.** This tag exists so a soaking board can be traced to an
exact commit. It is a candidate on three counts and no more: the eight-case downlink matrix
passed on the physical node, both sensors read inside the field image in one cycle, and that
cycle reached `sleep   : 900 s` and woke from it. Everything beyond those three is unproven.

`H8` is **not met** — at the moment this tag was cut, `docs/EVIDENCE.md` records **zero soak
hours**, no ≥24 h bench run and no ≥7 d field shadow. Sleep current has never been measured
(the pack's telemetry LSB is 10 mA and the budget turns on ~1 mA, so pack telemetry cannot
measure it). [ADR-0002](docs/decisions/ADR-0002-payload-contract-conflicts.md) is open: the
battery-current sign is still contradictory between the spec and the live decoder, and
`payload/schema.yaml` still carries `batt_current` as `BLOCKED`, which is why
`scripts/preflight.sh` legitimately ends `=== PREFLIGHT BLOCKED ===`. Open defects that this
release does **not** fix:
[#62](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/62) (the
pack re-latch path is still unproven),
[#68](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/68), and
[#74](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/74) (a
permanently failing session write can mute an otherwise healthy node).

Versioned **PATCH**, not MINOR: every change since `0.4.1` is a regression fix, a refactor with
no behavior change, or documentation. No new capability was added, and no payload channel or
type changed, so [`.cursor/rules/60-decoder-parity.mdc`](.cursor/rules/60-decoder-parity.mdc)
implies no paired TTN formatter change.


**Now partly hardware-verified.** `d568574` was flashed to the board as `env:soak` — the field
image — on 2026-08-13 and its boot banner read back `commit   : d568574`, which is the first time
any image in this repository has identified its own commit on hardware. Later the same day
`65f8615` was flashed as both `env:battdiag` and `env:soak`, and both banners read back
`commit   : 65f8615`.

### Verified on hardware

- **The battery ladder runs 20 consecutive cycles at `65f8615` with live values and no reset.**
  `env:battdiag`, ~10 s cycles: every cycle reported `11.92 V  -0.01 A  84%  24.0 C`, the counter
  advanced `[cycle 1]` … `[cycle 20]` with no second boot banner, and `provId 0xFF` never appeared.
  This is the first hardware evidence for any of the eleven fixes that landed after `d568574`, and
  it is a survival result rather than a per-fix one.
- **The field image at `65f8615` reaches sleep and wakes from it.** Cycle 2 arrived ~900 s after
  cycle 1 with no boot banner in between, both sensors read again with moved values
  (`999.2` -> `999.1 hPa`, `11.92` -> `11.91 V`) and a second uplink went out. A host poll also
  showed the USB console surviving the whole sleep/wake cycle while a reader was attached, which
  confirms the detach in #60 is conditional on nobody watching.
- **The field image at `65f8615` still reaches sleep.** One `env:soak` cycle read both sensors,
  restored session `0x260CE734` rather than rejoining, sent 35 bytes on port 2, and closed
  `sleep   : 900 s`. The 900 s interval persisted by the downlink matrix survived two further
  reflashes.

### Found on hardware, not fixed

- **One transient probe miss spends the power cycle's only BOOT on a healthy pack**
  ([#75](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/75)).
  On cycle 10 of 20, phase 0 drew no matched reply — the sendat sequence byte jumps `09` → `0C`,
  so two probes really did go unanswered — printed
  `pack silent at its id — one BOOT this power cycle`, and rebooted a pack that answered the push
  listen in the same cycle with a live reading. This is **not** the defect `e070708` fixed and not
  the [#62](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/62)
  re-latch path; it is the recovery ladder reacting to a single miss instead of a run of them. In
  the field a power cycle lasts months, so the one BOOT is spent long before the failure it exists
  for.

### Still compile-verified only after this session

- **`e070708` (routine empty battery reply must not reboot the pack) was not exercised.** The
  capture had **zero** post-boot `Unsampled` cycles — the pack had been polled continuously by the
  preceding flash and was already sampling, so cycle 1 read live. The defect condition never arose.
- **`da655e9` (RK900 summary must not print a pressure it refused to encode) was not exercised.**
  The one field-image cycle captured read a real `999.2 hPa`, so the refused-pressure branch was
  never entered.

- **The downlink command matrix passes 8 of 8 on the physical node.** `scripts/downlink_matrix.sh`
  drove valid set-interval and request-status, both malformed-length rejections, an unknown opcode,
  a valid command on the wrong FPort, and two commands queued at once, then checked the node had
  not reset. Every case matched a console line emitted from inside `Radio::take_downlink()`, so
  **`take_downlink()` is observed on hardware for the first time** — the acceptance criterion that
  kept [#54](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/54)
  open. The applied interval is visible changing from 1800 s to 900 s and persisting across a
  reflash and power cycle. Ran on a `stage3` bench image with `FEATURE_SLEEP=0`, so this is
  evidence about the shared downlink path and **not** about the sleep path. Raw log, per-case
  console quotes and the caveats are in
  [`docs/EVIDENCE.md`](docs/EVIDENCE.md). The length-checking fixes shipped in `0.4.1` on compile
  evidence alone ([#63](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/63),
  [#64](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/64)) are
  now confirmed on hardware.
- **The field image reaches sleep, and wakes from it.** Two cycles of `env:soak` at `d568574`:
  both sensors read, an uplink went out, and each cycle ended `sleep   : 900 s` rather than
  `wait    : N s (sleep disabled)`. The second cycle arrived ~900 s later with no boot banner in
  between, so the node woke from sleep instead of resetting through it. Still two cycles, not a
  soak — [`docs/EVIDENCE.md`](docs/EVIDENCE.md) records zero soak hours and that is unchanged.

### Fixed

- **The boot counter advances again during the reset loop it exists to detect.** Deferring its
  flash write out of `Config::begin()` put it in the main cycle *after* both sensor reads — the
  two calls that can hang, and a hang is what the watchdog resets on. A node stuck resetting
  inside a read therefore never reached the write, so the stored count stopped moving in exactly
  the failure the counter names, and the one remote signal separating "reset loop" from "dead
  sensor" was dead. `print_banner()`'s watchdog warning does not cover it: that needs a console,
  and the field image detaches USB after 180 s. The write moves to `setup()`, immediately after
  `brownout.begin()` restores the gate and before anything that can hang. The deferral itself is
  kept — the gate is still in front of the write, answering from the persisted bit, which is the
  conservative direction. **Compile-verified only — unflashed and unobserved.**

- **The frame-counter checkpoint permit and its contract now describe the same firmware, and the
  write-frequency figure is corrected.** `session.h` said the permit was "never for a hold backed
  by a measured low voltage", while `main.cpp` granted it on any armed keepalive — including the
  hold on a pack answering from inside the 9.60–10.20 V hysteresis band. The code is correct and
  the comment was stale: `power.cpp` disarms the keepalive outright at or below the 9.60 V
  transmit-inhibit floor, so the pack #38 is about transmits nothing and takes no write, and the
  in-band pack is above that floor and merely unrecovered. Narrowing the grant to the no-evidence
  hold was considered and rejected — it would have permitted only the blind write `power.h` calls
  the unbounded risk while refusing the measured one, and would have emptied the counter reserve
  about eight days into the winter band-hover that the in-band keepalive exists to survive.
  `session.h`, `session.cpp`, `power.h` and `main.cpp` now agree, and `power.h` names the one
  documented exception to its own gate instead of implying there is none. The "roughly monthly"
  figure was wrong: one write per 32 keepalives at one keepalive per 24 cycles is 768 cycles —
  about **8 days** at the 900 s field cadence, about 32 days at the 3600 s default, against every
  32 cycles on the healthy path. Comment-only; no behavior change. Refs
  [#38](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/38),
  [#51](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/51).

- **The downlink matrix harness can no longer record PASS with no answering uplink.** In
  `case_a`, `wait_for` was called for the FPort 2 uplink and its exit status discarded, so a
  timeout still recorded PASS — with the literal string `<no uplink observed>` pasted into the
  evidence field of the passing row. The status now decides: PASS on the observed uplink, FAIL
  otherwise. Tonight's `case_a` result was independently re-verified against the raw console,
  so the recorded evidence stands; the harness simply must not be able to report green over a
  missing observation. `case_a` was the only unchecked call — the other six `wait_for` sites
  already test it.

- **The RK900 console summary no longer prints a pressure the uplink deliberately omitted.**
  The encoder correctly leaves the field null when register `0x0004` reads `0`, and the very
  next line printed `0.0 hPa` as though it had been measured — so a bench capture contradicted
  the payload and pointed the next debugger at the decoder or the register map rather than at
  the barometer. The line now reads `pressure null` in that case. Introduced by `6c9bdc0`
  earlier the same night. Diagnostic output only; the encoded payload is unchanged.

- **A set-interval downlink taken during a brownout hold is no longer dropped on the second
  retry, after the console said it would persist.** `Config::set_interval_seconds()` assigned
  `m_interval` before attempting the write and left it there when the write failed, so the next
  retry matched the unwritten value at the `seconds == m_interval` short-circuit, wrote nothing,
  and returned true — and `main.cpp` cleared its pending value believing the command had landed.
  The commanded cadence was then live only until the next reset, having been reported as saved.
  Reachable on any failing write, including every save on a node whose filesystem did not mount,
  where `save()` returns false without touching flash. The value is now staged and rolled back on
  failure, so a true return means the value is on flash and nothing else; the RAM-apply stays in
  `main.cpp`, which also now honors the return value on the gate-allowed path where it was
  previously discarded. The retry is bounded to three flash attempts so the fix cannot turn a
  broken filesystem into a settings-page rewrite on every wake — H3's thrash rule arriving through
  the door opened to fix a different defect. After that the cadence stays applied in RAM and the
  console says exactly that. Refs
  [#65](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/65).
  **Compile-verified only — unflashed and unobserved.**

- **A second kind of battery failure no longer postpones an already-scheduled recovery
  retry.** Both threshold branches assigned `m_next_full_cycle` unconditionally, so crossing
  the second threshold moved a retry that was already pending: three silent cycles schedule
  the full ladder for cycle 27, six empty records crossing at cycle 26 push it to cycle 50 —
  nearly six more hours at the 900 s field interval in which the only path that can recover
  the pack does not run. The deadline is now armed once and never moved, re-armed only after
  it fires, and cleared by a real reading. The two streak counters also reset when the other
  kind of failure occurs, so the console's "consecutive" is now true and the gate no longer
  sums two streaks that were never concurrent; a new total-stalled counter carries the hard
  power bound that the streaks alone cannot. The bound is unchanged in size: at most six
  expensive cycles after the last good reading, then one every 24 cycles — six hours at
  900 s. An absent pack costs exactly what it did before.
  **Compile-verified only — unflashed and unobserved.**

- **A routine empty battery reply no longer reboots a healthy pack, or burns the one BOOT this
  power cycle is allowed.** Phase 0 required `Ok || m_pack_latched` to call the direct probe
  answered, and `m_pack_latched` is false after every MCU reset — so the `Unsampled` reply the
  pack sends for roughly its first two cycles while it samples was treated as no answer at all.
  That path calls `boot_once()`, which prints `pack silent at its id — one BOOT this power
  cycle` and sends the reference's reboot verb to a pack that had just answered a matched SENDAT
  from `0x01`, and spends the single allowed BOOT before any real failure can ask for it. A
  matched response settles the address whether or not the record carries a measurement: an
  unprovisioned pack answers `0xFF` and returns zero bytes from `0x01`. The push listen, which
  is where the placeholder record actually gets resolved, is gated on `m_last != Ok` and is
  unaffected. Interaction between `342d994` and `955fc01`; bears on
  [#62](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/62) and
  [#71](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/71).
  **Compile-verified only — unflashed and unobserved.**

### Added

- **The boot banner names the commit it was built from** — `commit   : a7381e7`, with `-dirty`
  appended when the tree carried anything uncommitted, and `unknown` when there is no git
  history to read. Injected at build time by `scripts/pio_git_rev.py`, a PlatformIO `pre:`
  extra script that appends one `-D FIRMWARE_COMMIT="…"`; every environment carries it,
  `native` included. `docs/EVIDENCE.md` cannot accept a result without a SHA, and until now the
  firmware could not state one: the banner printed a version and a `__DATE__`/`__TIME__` stamp,
  so two builds of `0.4.1` read identically on the console and a board already in the field
  could not be matched to a commit at all. Today's `stage3` entry had to record its SHA as
  *inferred* from a build timestamp; the next one will not.
  Costs nothing when `FEATURE_CONSOLE=0` — the `LOGF` macro discards its arguments without
  expanding them, so the string never reaches the image — and touches neither the sleep nor the
  USB path, so the field image's power behaviour is unchanged (rule 50,
  [ADR-0008](docs/decisions/ADR-0008-console-in-the-field-image.md)).
  Deliberately **not** in the uplink: that is a payload contract change needing a paired TTN
  formatter change (rule 60), and `batt_current` already blocks the payload freeze.

### Documentation

- [`docs/EVIDENCE.md`](docs/EVIDENCE.md) states how to obtain the SHA from a running board now
  that the banner carries it, and what to do with an older capture whose banner predates the
  change: record the build timestamp and mark the SHA **inferred**, never assert it.

## [0.4.1] — 2026-08-12

**A hardening pass, compile-verified only — nothing in it ran on hardware.** `env:rak4631`,
`env:soak` and `env:battdiag` each built `SUCCESS` on Heliotrope Ridge and that was the whole of
the verification. Several fixes sit on the sleep, brownout and rejoin paths, which a compiler
cannot exercise, so this release is **further from deployable than the last, not closer**: the
code is now believed correct where it was previously known wrong. PATCH per `docs/RELEASE.md` —
all bug fixes and timing corrections, no payload change, so no paired TTN formatter change.

### Fixed

- Modbus pre-transaction drain is bounded and feeds the watchdog; an unbounded
  `while (m_serial.available())` could hang a read until the watchdog reset the node with
  `WB_IO2` still HIGH.
- A CRC-valid all-zero RK900 reply is no longer encoded as real weather — it is `Unsampled` and
  contributes no fields. Genuine zeros (calm wind, due north, 0.0 °C) unaffected.
- A pack answering with an empty record no longer gates off the push listen that would fill it.
  `Unsampled` gets its own bounded allowance instead of counting as both "address works" and
  "pack is silent". (#62, #39)
- The battery driver no longer reboots the pack on every re-latch attempt — `acquire_pid()` had
  opened each provisioning window with a BOOT broadcast.
- A failed re-latch no longer prints as success or claims an address the pack never confirmed.
- A set-interval downlink delivered during a brownout hold is no longer discarded. (#61)
- A corrupted downlink can no longer pass itself off as a status request. (#63, #64)
- A failed join or uplink no longer retries far faster than the fair-use floor. (#41)
- The rejoin escape no longer leaves the radio on the wrong eight channels. (#42)
- The boot counter no longer writes flash before the brownout gate exists. (#43)
- A brownout keepalive no longer exhausts the frame counter and silences the node permanently.
  (#45)
- The brownout hold no longer disables the provisioning handshake that is its only exit. (#44)
- The stored frame counter can no longer fall behind what was transmitted. (#46)
- A brownout hold the node cannot lift by itself is now bounded rather than permanent. (#45)
- A dead one-wire link no longer silences a healthy node forever. (#38)
- Brownout protection no longer fails open, and the gate now actually reaches the battery driver.
- The fair-use guard checks the effective uplink cadence rather than a single flag. (#40)
- The USB console no longer dies after the first sleep. (#47)
- The join backoff message reports the real next attempt.
- `busscan` no longer sweeps a slave that cannot answer it.
- `m_ever_sampled` no longer advertises a deleted configuration path.
- `scripts/flash.sh` no longer reports FLASH FAILED when the image landed. (#33)
- Two honesty fixes in the uplink path.

### Changed

- One-wire receive timeouts carry their sourcing, and the SENDAT turnaround uses `kTurnaroundMs`
  rather than a bare `delay(2)`.
- The sleep wait is a bounded `xSemaphoreTake()` rather than a `delay(1000)` loop.
- `lmh_reset_mac()` added on the rejoin path. (#34)
- Minimum reporting interval lowered from 1800 s to 900 s. 900 s is ~36 s of daily airtime at
  DR0, **over** the ~30 s TTN allowance, and ~6 s at DR3. So the floor is compliant at DR3 or
  better and marginal at DR0 — a coverage-dependent condition, not the unconditional guarantee
  the 1800 s floor gave. A node observed sitting at DR0 should have its interval raised.
- The decoder-parity gate catches three further classes of drift.
- The off-target test suite compiles at `gnu++11`, the standard the device is held to. A struct
  with default member initialisers is an aggregate under C++14 but not C++11, so a full host pass
  once went green on code that could not build for the board at all.
- `FEATURE_CONSOLE=0` builds again. (#51)
- `owscan`'s verdict no longer reads as proof the handshake works — it never answers a VER3
  announcement, so it cannot latch or observe a pid.

### Documentation

- Every document reconciled against `docs/EVIDENCE.md`, which is the authority. Corrected: no
  soak had ever run, and the H1–H8 table now separates "implemented in source" from "gate
  closed". `FIRMWARE_SPEC.md` §5 no longer states the withdrawn console rationale as fact, and §9
  first-light items are recorded closed. Status wording in `README.md` and `AGENTS.md` updated.
  The 2026-08-12 review reports are linked from the docs that matter. (#24, #54, #65)

## [0.4.0] — 2026-08-05

**Battery telemetry works on hardware.** The RAK9154 reports real values over the one-wire link
— `12.23 V, +0.00 A, 98%, 23.0 °C` across seven consecutive cycles. Backward compatible with the
existing payload, so no paired TTN formatter change.

### Added

- The RAK9154 reports real values; the success path hex-dumps its frame and raw sensor integers
  are logged unscaled, so a scaling error is separable from a protocol error.
- New `battdiag` build environment — a ~10 s cycle for pack questions, versus `stage3`'s 1800 s.
- Real encoder bytes are checked through the real TTN decoder.
- A bench-only 60 s reading cadence, `FEATURE_BENCH_INTERVAL`. (#25)

### Changed

- Awake time drops from ~50 s to ~5 s per cycle, and the provisioning window is capped at 5 s
  from 45 s. A provisioned pack no longer spends 45 s per wake listening for an announcement
  that will not come.
- The two bench scanners moved out of `main.cpp` into `src/diagnostics/`.
- Open items moved to GitHub Issues.

### Fixed

- The battery console log inverted every current between −0.99 A and −0.01 A.
- A truncated record no longer reports as an unknown one.
- The watchdog is fed inside `acquire_pid()` and `receive()`.
- **The DevEUI was byte-reversed, so no join could ever succeed.**
- The node transmits proof of life when both sensors are silent.
- Modbus notices a gap inside a reply.
- `scripts/flash.sh` can no longer report a failed flash as `=== FLASH OK ===`. (#27)
- `scripts/build.sh` refuses an `upload` target; `scripts/remote.sh sync` no longer reads an
  unreachable build host as a clean tree.

### Removed

Negative results — **do not re-attempt these:**

- The raw-Modbus path and `FEATURE_BATTERY_MODBUS`.
- The PARAMGET/PARAMSET pass (~210 lines) and `FEATURE_BATTERY_PARAM_PASS`.
- `FEATURE_ONEWIRE_SPLIT` (~90 lines), `FEATURE_ONEWIRE_RAIL_CYCLE`, `src/owprobe.h`, and four
  write-only members.

Open after this release: #36, #37.

## [0.2.1] — 2026-07-30

Supersedes 0.2.0, which was tagged at a commit that does not pass CI. Use this one.

### Fixed

- **The off-target tests build again** (`src/build_features.h`, renamed from
  `src/features.h`). The C library has its own `features.h` and includes it from inside
  nearly every system header. The test build puts `src/` on the include path, so the C
  library found ours and compiled without any of its own configuration — producing hundreds
  of errors inside `stdio.h`, `stdint.h`, and `cmath`, not one of which named the file
  responsible. The header no longer pulls in `Arduino.h` on a machine that has no Arduino.
- **`scripts/build.sh` runs the off-target tests before compiling**, wiping their build
  directory first. A stale object had been hiding a missing include, which is why the build
  host reported success on a commit CI rejected. A green build host that disagrees with CI
  is worse than a red one, because it is the machine nobody re-checks.
- **`scripts/push.sh` relays tags.** A release tag left behind on the workstation leaves the
  GitHub release pointing at a commit nobody can fetch.

## [0.2.0] — 2026-07-30

**First real firmware.** The project compiles for hardware for the first time; nothing in this
release had been run on a board.

### Added

- The LoRaWAN session survives a reset.
- Battery temperature is decoded.
- The firmware, in modules, plus staged build environments so each stage adds exactly one
  failure domain.
- Tests that run without hardware, and [`docs/FIRST_FLASH.md`](docs/FIRST_FLASH.md).
- [`rakwireless/`](rakwireless/) — vendored RAK4631 board definition, absent from the PlatformIO
  registry.
- Release gates: [`payload/schema.yaml`](payload/schema.yaml) as the firmware-side payload truth,
  [`check_decoder_parity.py`](scripts/check_decoder_parity.py),
  [`check_citations.py`](scripts/check_citations.py), and the
  [`scripts/remote.sh`](scripts/remote.sh) / [`build.sh`](scripts/build.sh) build-host wrappers.
- [`AGENTS.md`](AGENTS.md), the [`.cursor/rules/`](.cursor/rules/) discipline baseline, issue and
  PR templates carrying the citation gate, and CI.
- ADRs [0003](docs/decisions/ADR-0003-firmware-framework.md) (Arduino + WisBlock-API-V2) and
  [0004](docs/decisions/ADR-0004-bms-one-wire-path.md) — **the RAK9154 goes on one-wire, not the
  shared RS-485 bus.**
- A downlink-and-resilience review (since deleted; its conclusions are the downlink behavior in
  [`docs/FIRMWARE_SPEC.md`](docs/FIRMWARE_SPEC.md) and the 8/8 matrix in
  [`docs/DOWNLINK_MATRIX.md`](docs/DOWNLINK_MATRIX.md)).

### Changed

- [`docs/POWER_BUDGET.md`](docs/POWER_BUDGET.md) said the system had no solar. It was wrong — the
  pack is solar-recharged, which changes the whole budget.
- Cut the proposed downlink command set from seven commands to two.

### Fixed

- **The node now transmits on the channels the network is listening to**, and downlinks can
  actually arrive.
- A milliamp reclaimed during sleep.
- The pack parser stops rather than guessing.
- Battery temperature scale corrected in the documentation. (#4)
- [`AGENTS.md`](AGENTS.md) implied `rak-4-5-wire` was a RAK4631 reference; it is **M5Stack
  NanoC6 (ESP32-C6)**.
- Sibling-repo links in [`docs/HARDWARE.md`](docs/HARDWARE.md) pointed at the wrong paths.
- [`scripts/flash.sh`](scripts/flash.sh) pinned `--upload-port` to the pre-reset port name.
- [`check_citations.py`](scripts/check_citations.py) skips vendored upstream files.
- The TTN formatter parity gate could never pass in CI. (#1)

### Known issues

- Battery current sign unresolved; humidity type 104 not interchangeable with 112; battery
  temperature needs ×10 before encoding.
- **A node that cannot join never sleeps** — join backoff is a power requirement, not just an
  airtime courtesy.
- The RS-485 bus needs two baud rates and has one transceiver.
- Stage 0 compiles but **has never been run on hardware.**
