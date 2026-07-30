# Spec review — downlink surface and multi-day outage resilience

> **Point-in-time record.** Findings 1, 2, and 3 have since been closed in firmware:
> command framing with a leading opcode (`src/radio.cpp`), session persistence across reset
> (`src/session.cpp`), and a minimum interval raised to 1800 s to stay inside fair use
> (`src/config.h`). The rest remain open and are tracked as
> [GitHub Issues](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues).
> The findings below are left as written — they are the reasoning that led to those changes.

- **Date:** 2026-07-30
- **Reviewed:** `docs/FIRMWARE_SPEC.md` §3, §4, §5, §7 against `.cursor/rules/40-lorawan-compliance.mdc`, `.cursor/rules/50-power-management.mdc`, and the citation registry
- **Not reviewed:** firmware. Stage 0 is a blink and a serial banner; there is nothing to review yet.
- **Trigger:** hardware arriving; the questions "are all the downlinks there?" and "will it reconnect after the gateway is down for days?"

## Why review the spec instead of the code

Every finding below is a decision, not a bug. Decisions are cheap now and expensive after
there is code shaped around them — and two of them (session persistence, watchdog scope)
are the kind that work perfectly on a bench and fail in week three in the woods, which is
the worst possible time to find out.

## Summary

The spec is sound on sensors and payload. **The downlink surface and the outage-recovery
behavior are the two thin areas**, and they are exactly the two the node's survival depends
on once it is a hike away.

| # | Finding | Severity |
|---|---|---|
| 1 | Downlink is a single bare integer with no command framing — cannot be extended without breaking compatibility | **High** |
| 2 | No LoRaWAN session persistence across reset; a reboot means a fresh OTAA join | **High** |
| 3 | The 300 s minimum interval exceeds TTN fair use at the data rates this node will actually use | **High** |
| 4 | Watchdog cannot be stopped and its timeout is fixed at boot, but the sleep interval is downlink-settable | **High** |
| 5 | No way to confirm a downlink was applied | Medium |
| 6 | No link-loss detection — the node cannot tell a working network from a dead one | Medium |
| 7 | ADR makes a multi-day outage progressively more expensive, exactly when the battery can least afford it | Medium |
| 8 | A long interval means one reconfiguration opportunity per day | Medium (operational) |
| 9 | §9 names a PlatformIO environment that no longer exists | Low |

---

## 1. The downlink command surface is one command wide

§4 defines the entire downlink as `interval_s uint32 BE` on an unspecified fPort ("lock
fPort in impl"). Two problems.

**It cannot grow.** A bare 4-byte integer has no room for a second command. The moment a
second one is wanted, the format changes, and any node already in the field decodes the new
message as an interval. There is no version field and no opcode.

**The spec already contradicts itself about this.** §2.1 says wind direction offset is
applied in the decoder _"unless downlink sets offset"_ — describing a downlink command that
§4 does not define. So the one-command design is already known to be insufficient.

Recommendation: a `[opcode:u8][length:u8][value...]` structure on one locked fPort, with
unknown opcodes ignored rather than rejected, so old firmware tolerates new commands.

### Proposed command set — revised down 2026-07-30

**The first version of this section proposed seven commands. That was wrong for this
deployment**, and the operator was right to push back. The goal is a node nobody ever
touches. Commands are not insurance for that goal; they are code that runs unattended,
against a device with no console, where a misapplied setting is indistinguishable from a
hardware fault. Every command is a way to break it remotely.

The corrected principle: **the node should never need a command. Add framing so commands
are possible, but implement almost none of them.**

| Opcode | Command | Verdict |
|---|---|---|
| `0x01` | Set uplink interval (u32 s) | **Implement.** The one genuine tuning knob — trades data rate against winter power, and the right value is not knowable until the pack has been through a season |
| `0x03` | Request immediate status uplink | **Implement.** Read-only, cannot misconfigure anything, and it is the only way to ask "are you alive, what do you think your settings are" without waiting a full interval |
| `0x02` | Wind-direction offset | **Defer.** Fix in the TTN decoder instead — a server-side change is reversible in seconds and cannot brick anything |
| `0x04` | Reboot | **Drop.** The watchdog covers hangs automatically. A reboot command exists mainly to recover from bugs that should be fixed by not having them |
| `0x05` | Force rejoin | **Drop.** Session persistence plus capped backoff (finding 2) makes rejoin automatic. Manual rejoin is a worse version of that |
| `0x06` | Enable/disable a sensor | **Drop.** A dead sensor should already null its fields and cost only its timeout. Not worth a remote failure mode |
| `0x07` | Set low-battery cutoff | **Drop.** Compile it in. A remotely settable cutoff is a remotely settable way to flatten the pack, and a flat pack may not be self-recoverable [CIT-RAK9154-SOLAR] |

Two commands, one of which is read-only. The framing still needs to exist — an opcode byte
with unknown opcodes ignored — so the option is open later without breaking compatibility.
That costs one byte and keeps the door unlocked without walking through it.

**Resilience is not the same thing as configurability**, and this deployment needs the
first. Findings 2, 4 and 7 are what actually keep the node alive; they are all automatic
behaviors requiring no operator at all.

## 2. A reboot throws away the LoRaWAN session

H5 says _"Interval + keys path survives power loss."_ **Keys are not a session.** OTAA keys
(DevEUI/AppEUI/AppKey) let a node join. What it needs to persist to _resume_ is the session:
DevAddr, NwkSKey, AppSKey, and the uplink frame counter.

Two consequences, both bad:

- **Join storm.** Every watchdog reset and every brownout triggers a full OTAA join. Joins
  are the most expensive thing this node does — and per [CIT-RAK-SLEEP], a node that is
  trying to join and cannot **never sleeps**. A reset loop during an outage is the single
  most plausible way this node dies in the field.
- **Frame counter replay rejection.** If a session is restored without also restoring
  `FCntUp`, the network sees uplinks with counters it has already accepted and silently
  discards them. The node transmits happily and nothing arrives. This failure is invisible
  from the node's side and is a classic multi-day debugging trap.

Recommendation: persist session and `FCntUp` to flash, restore on boot, and only fall back
to OTAA when restore fails. Write `FCntUp` in a wear-conscious way — H3 already warns
against flash thrash, and this is the write that would cause it.

## 3. The 300 s interval floor is not safe at real-world data rates

§4 allows intervals from 300 s. That is 288 uplinks/day.

TTN's fair use policy is **30 s of uplink airtime per node per day** [CIT-TTN-FUP]. The
community derivation gives roughly **20 messages/day at SF12 and ~500/day at SF7 for a
10-byte payload** [CIT-TTN-FUP-EXPLAINED]. On TTN's US915 plan, uplinks run **SF7BW125
through SF10BW125** [CIT-TTN-FREQ], so SF10 is the realistic floor — and a node in trees
several hundred metres from a gateway is far more likely to sit near SF10 than SF7. Our
payload is also larger than 10 bytes.

So 288 uplinks/day is comfortably inside budget only at the best data rate this node will
rarely enjoy, and multiples over budget at the rate it will likely settle on. The default
of 3600 s (24/day) is fine. **The floor is the problem, not the default.**

Recommendation: raise the floor, or better, make the guard airtime-based rather than
time-based — compute the airtime of the payload at the current data rate, reject an
interval that would exceed the daily budget, and clamp instead of accepting. This also
protects against a mis-sent downlink flattening the pack.

## 4. The watchdog cannot be stopped, but the sleep interval is configurable

On the nRF52840, once `TASKS_START` is written the WDT **cannot be stopped**, and `CRV`,
`RREN`, and `CONFIG` are locked until a reset [CIT-NRF-WDT]. Timeout is
`(CRV + 1) / 32768` seconds.

This interacts badly with a downlink-settable interval. `CONFIG.SLEEP` decides whether the
watchdog keeps counting while the CPU sleeps:

- **If it counts during sleep**, the timeout must exceed the longest possible sleep. With
  intervals allowed up to 86400 s, that forces a ~24 h watchdog — which is useless as a
  hang detector, since H1 wants it to catch a stuck Modbus read. And the timeout is fixed
  at boot, so a downlink that sets an interval longer than it resets the node mid-sleep,
  every single cycle, forever.
- **If it pauses during sleep**, it guards exactly the awake window — the Modbus polls and
  the transmit — which is precisely where H1's hangs occur, and the sleep length stops
  mattering at all.

Recommendation: configure the watchdog to pause during sleep, sized to the awake window
(seconds, not hours). The trade-off to accept knowingly: a fault that leaves the CPU asleep
forever is then invisible to it, so the wake timer needs its own backstop.

## 5. Nothing confirms a downlink was applied

§4 says apply on next wake and ignore invalid downlinks. Neither path produces any
observable result. Send an interval change and you cannot tell whether it was received,
whether it was rejected as invalid, or whether the node simply never got it.

This compounds with an existing call-out from the parity gate: **no channel currently
carries a firmware version**, so ingest cannot see what the node is running either.

Recommendation: echo the active configuration — interval, firmware version, and a downlink
counter — in the uplink, either every time or in response to opcode `0x03`. This needs a
TTN formatter change, which the parity gate is already flagging.

## 6. The node cannot tell whether the network is alive

Class A with unconfirmed uplinks gives **zero** feedback. A node transmitting into a dead
gateway behaves identically to one that is working. It cannot know, so it cannot react —
no backoff, no power saving, no distress behavior.

Confirmed uplinks are the obvious fix and the wrong one: every ACK spends from the **10
downlinks per node per day** budget, and that cap includes ACKs [CIT-TTN-FUP-EXPLAINED].

Recommendation: a periodic `LinkCheckReq` — a MAC-layer command whose answer reports
whether the network heard you and via how many gateways [CIT-LW-LINK]. The answer still
arrives in a downlink so it is not free; budget it at roughly once a day. That is enough
to distinguish "gateway down" from "working" and to drive finding 7.

## 7. ADR makes an outage progressively more expensive

ADR is ON (§3), which is right for battery life in normal operation. Under a multi-day
outage it works against us: with no downlinks arriving, the LoRaWAN ADR backoff steps the
data rate **down** and transmit power **up** [CIT-LW-LINK]. Slower data rate means longer
airtime, and both mean more current per uplink — the node spends progressively more energy
per attempt precisely while it is getting nothing back.

H4 says "bounded backoff; survive multi-day no-GW" but does not say what bounded means.

Recommendation: make it explicit. After N consecutive cycles with no link confirmation
(finding 6), stop honoring the interval and fall back to a slow retry cadence — exponential
backoff capped at something like 6 h — so a five-day outage costs a handful of transmissions
instead of a hundred increasingly expensive ones. Recovery must remain automatic: the
capped cadence has to keep trying forever, or the node will not come back when the gateway
does.

## 8. A long interval means one chance a day to fix it

Class A receive windows open only just after an uplink. At the maximum allowed 86400 s, the
node is reachable **once per day, for about two seconds**. Not a defect, but it sets the
recovery time for any bad configuration and is worth stating in the spec so nobody is
surprised in the field.

This cuts against a rich command set rather than for one. When the reconfiguration window
is that scarce, the node cannot depend on being reconfigured — it has to recover on its
own. That is the reasoning behind the revised two-command surface above, and behind
treating findings 2, 4 and 7 as the real work.

## 9. Stale environment name

§9 says compile for `wiscore_rak4631`. The environment is `rak4631` and the board is
`rak4630`. Fixed in this change.

---

## Recommended sequencing

None of this blocks bring-up. Stages 0 and 1 touch no LoRaWAN at all.

| When | Finding |
|---|---|
| Before Stage 2 (first join) | 2 (session persistence), 4 (watchdog scope) |
| Before the payload freeze | 1 (command framing), 3 (airtime floor), 5 (config echo) |
| Before the 7-day field shadow | 6 (link check), 7 (backoff policy) |
| Anytime | 8 (document), 9 (done) |

Findings 1, 3, and 4 change `docs/FIRMWARE_SPEC.md` and should each land as an ADR, since
each picks a winner between defensible alternatives.
