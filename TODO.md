# Open items

One running list. Anything that would otherwise be an "oh, one more thing" belongs here.

## Blocks the first uplink

- [ ] **TTN keys.** Copy `src/secrets.example.h` to `src/secrets.h` and paste DevEUI,
      JoinEUI, and AppKey from the TTN console. Without it the build warns and the node
      never joins. `secrets.h` is gitignored.
- [ ] **Buck converter part.** Still listed as "(separate)" in `docs/HARDWARE.md`. Pick one
      with idle draw in microamps, not milliamps — it runs 24/7 and a bad part outdraws the
      entire rest of the node.

## Answers that only the hardware can give

- [ ] **Battery current sign.** Charging is negative per the firmware spec and positive per
      the TTN decoder. Watch the pack charge in sunlight and see which way the number
      moves. Closes [ADR-0002](docs/decisions/ADR-0002-payload-contract-conflicts.md).
- [ ] **Battery temperature scale.** Encoded as tenths of a degree. If the first reading
      comes back around 2 °C on a 20 °C bench, it is whole degrees and needs multiplying by
      ten in `src/payload.cpp`.
- [ ] **Battery reply checksum convention.** The parser assumes the pack's reply is
      checksummed the same way as the request — XOR of everything between the delimiter and
      the checksum byte. The prior-art codec never checked the reply, so there was nothing
      to copy. If stage 2 reports `bad checksum`, the raw frame is logged in hex alongside
      the expected and received bytes; compare them before changing the parser, because the
      reply may simply be framed differently.
- [ ] **5-pin socket pinout.** `docs/HARDWARE.md` says pin 4 is a 3.3 V reference; the
      sibling repo says pins 4 and 5 are RS-485. Confirm with a meter before trusting
      either, and correct whichever is wrong.
- [ ] **Pack cutoff voltage and cell count.** The low-voltage gate stops transmitting at
      9.60 V and resumes at 10.20 V, inferred from the 10.8 V nominal rating implying three
      cells. Neither the cell count nor the pack's actual protection cutoff has been
      confirmed. Measure both and revisit `power.h` and
      [`docs/POWER_BUDGET.md`](docs/POWER_BUDGET.md).
- [ ] **Sleep current.** Measure it. Everything in `docs/POWER_BUDGET.md` downstream of this
      number is a placeholder until then.
- [ ] **Heater draw.** The pack's heater is not under firmware control and may dominate the
      winter budget. Measure once it is cold enough to switch on.

## Before it goes in the woods

- [ ] **Confirm session persistence on hardware.** The code is written
      (`src/session.cpp`), but "it compiles" is not "it works". Reset the board during
      stage 3 of [the first-flash checklist](docs/FIRST_FLASH.md) and confirm it resumes
      without a second join.
- [ ] **Freeze the payload.** `payload/schema.yaml` is still marked draft.
- [ ] **24-hour bench soak, then a 7-day shadow deployment** somewhere reachable.

## Raised by code review, not yet done

- [ ] **Confirm sleep is real sleep.** `power::sleep_seconds()` relies on the Arduino core's
      `delay()` parking the task so the idle task can drop the CPU into a low-power state.
      That is the documented behavior, but whether the FreeRTOS tick wakes the CPU every
      millisecond in this configuration has not been checked. If it does, sleep current
      will be far above target. Measuring it at stage 4 answers the question directly.
- [ ] **Modbus inter-character timeout.** A gap inside a reply is not detected, so bytes
      arriving late are concatenated into one frame. The CRC catches this and the
      transaction retries, so the effect is a wasted attempt rather than bad data — but
      timestamping each byte would turn it into a clean reject.
- [ ] **A golden-vector test through the real decoder.** The parity gate now checks the
      encoder against the schema and the schema against the decoder, which is a complete
      chain by inspection. Running actual firmware bytes through the JavaScript would
      prove it end to end rather than link by link.
- [ ] **Restore shorter intervals safely.** The minimum is 1800 s because that is safe at
      the slowest data rate. A guard that computes airtime from the current data rate could
      allow shorter intervals when coverage is good, without ever breaching fair use.
- [ ] **Confirm a downlink was applied.** There is no acknowledgement, so a setting change
      is invisible until the next uplink's timing implies it.

## Needs a matching change to the TTN decoder

Neither is required to ship; both are quality-of-life once the node is remote.

- [ ] Report the firmware version in the uplink, so the running build is visible from
      ingest without a site visit.
- [ ] Per-sensor validity flags. Today a missing field is the only signal that a sensor
      failed, which works but cannot distinguish "sensor broken" from "sensor omitted".
