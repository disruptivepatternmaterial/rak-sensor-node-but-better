# Release status

What each release establishes on hardware, and what it does not. Moved out of `AGENTS.md` so it is read when relevant rather than loaded into every agent turn.

## Where v0.4.3 leaves things

**`1c2df3c` is in the field, tagged `v0.4.3`, and it named itself off the board.** The operator
chose on 2026-08-14 to ship the `#75` BOOT-allowance fix (`ec9725a`) rather than the longer-soaked
`572bcfa`, trading soak hours for the battery fix, and the node went to the woods the same
afternoon. **The identity gap is closed:** a single RESET press at 17:50:12Z with a capture held
open produced `commit   : 1c2df3c` in the boot banner, matching what was flashed, no `-dirty`. That
is the **third distinct commit** ever banner-asserted, after `d568574` and `65f8615` — it is *not*
the second, and any document saying so is wrong. The tag was cut on that basis, per
[`docs/RELEASE.md`](RELEASE.md).

**What the run establishes:** cycles 3–8 ran **unattended on the 900 s cadence**, ~1 h 15 m of
continuous correct cycling at ~908 s wake-to-wake (the ~8 s excess is awake time), both sensors
live on **every** cycle (RK900 23.1–25.4 °C / 56.3–64.9 %RH / 1002.3 hPa calm; the pack latched at
`0x01`, 11.75–11.76 V, −0.01 A, 78 %, 23.0 °C, **no BOOT spent**), every cycle closing
`sleep : 900 s` and sending 35 bytes on port 2. The soak independently logged the same reset as
`f_cnt=2496` — the number the banner printed as its restored counter — which also **observes the
`7b03d3a` counter-step fix on hardware** for the first time: +26 classified as one uplink inside the
32-frame reset reserve, `anomalies=0`, not 26 phantom transmissions ([`docs/EVIDENCE.md`](EVIDENCE.md)).

**What it does not establish, and do not let this erode.** A grep of the 81-line capture for
`brownout`, `provId`, `BOOT this`, `no confirmed latch`, `Unsampled`, `rejoin`, `keepalive` and
`silent at` returns **nothing**. So `#75`'s own failure gate never ran (not one probe miss in eight
cycles — the healthy path is confirmed, the consecutive-miss counter is not), `#62`'s re-latch path
was never entered (the pack *kept* its latch through the reset), and the brownout, rejoin and
keepalive paths were never reached. **Six clean cycles are not a soak.**

**Serial-capture traps, all four still true and all four cost time here:** `pio device monitor`
cannot run non-interactively (`termios.error` when stdout is redirected — use a raw `cat` or
`scripts/capture.py`), a 100–200 s capture lands inside the 900 s sleep and reads 0 bytes so a
capture must span **>900 s**, a `nohup` capture backgrounded over SSH does not survive the session
(use `screen`), and `scripts/capture.py` **refuses** the port while a previous capture holds it —
it names the holding pid, so kill the old one and confirm it is gone before reading a quiet log as
silence. The banner prints **only at boot**, so no wake will ever supply it; getting one needs a
single RESET press (a double-tap enters DFU and will not run the app).

**Check for a running soak before starting one.** Two workers each launched `soak_ttn.sh`
against the same device 14 minutes apart on 2026-08-14; two pollers on one device double the
query rate and are not additive evidence. The later run was killed, `latest-ttn` repointed at the
earlier and longer one, and the abandoned directory kept. A `pgrep` taken minutes earlier is
stale — re-check immediately before launching.

Of the work since `572bcfa`, `7b03d3a` (the soak reader's counter-step fix) is now **observed on
hardware**, and the `#75` fix `ec9725a` is observed **only on the healthy path** — it does not
misfire on a good pack and leaves the BOOT unspent, but its miss counter has never run. The
overridable build-host address (`81285eb`) and the ADR-0002 sign closure (`e8002d9`, no wire
change) remain **believed correct, unobserved**; so does a real charge current, which would settle
ADR-0002 empirically and has still never been captured.

Two guards were added after two workers died mid-task overnight, one leaving a dirty tree that
blocked a scheduled flash: `scripts/flash.sh` refuses to flash while a soak runs, and
`scripts/push.sh` refuses a fast-forward that would rewrite the running soak's own script
(a shell reads a script incrementally, so replacing it mid-run executes a fragment). Both name
their override. `scripts/remote.sh sync` already refused on a dirty tree at either end.

## Where v0.4.1 leaves things

`v0.4.1` (2026-08-12) is **ten fixes, compile-verified only.** No hardware ran any of them — the
board was asleep with USB detached all session. `env:rak4631`, `env:soak` and `env:battdiag` build
`SUCCESS` on Heliotrope Ridge and that is the whole claim
([`docs/EVIDENCE.md`](EVIDENCE.md), 2026-08-12 night).

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
([`docs/EVIDENCE.md`](EVIDENCE.md)).

**Treat everything not named above as *believed correct, unobserved*.** Several sit on the sleep, brownout and
rejoin paths, which are exactly what compiling cannot exercise. Do not describe any of them as
working until a bench capture says so.

**Bench fact you will otherwise lose twenty minutes to:** the field image detaches USB 180 s after
boot ([ADR-0008](decisions/ADR-0008-console-in-the-field-image.md)). A board left running past
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
