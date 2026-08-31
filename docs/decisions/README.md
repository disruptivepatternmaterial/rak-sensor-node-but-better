# Architecture decision records

Short records of decisions that are expensive to reverse or easy to forget the reasoning
behind. Six months from now, in the woods, "why is it wired that way?" needs an answer
better than the git log.

## When to write one

Write an ADR when a choice:

- closes an open decision from the issue tracker;
- resolves a **conflict between sources** (rule 20 requires this — never silently pick a winner);
- changes the payload contract, the wiring, or the LoRaWAN parameters;
- would otherwise be re-litigated every time someone new reads the code.

Do not write one for routine implementation choices. Those belong in the commit message.

## How

Copy [`TEMPLATE.md`](TEMPLATE.md) to `ADR-NNNN-short-title.md`, take the next number, and
link it from the decision it closes. ADRs are **append-only**: superseding an ADR means
writing a new one and marking the old `Superseded by ADR-NNNN`. Never edit history to make
a past decision look better than it was.

Every ADR carries citations to the same standard as code (rule 20).

## Index

| ADR | Title | Status |
|---|---|---|
| [0001](ADR-0001-build-and-flash-on-build-host.md) | Author locally, build and flash on Heliotrope Ridge | Accepted |
| [0002](ADR-0002-payload-contract-conflicts.md) | Payload contract conflicts with the live TTN decoder | Accepted — all three conflicts decided; current sign closed 2026-08-13 |
| [0003](ADR-0003-firmware-framework.md) | Arduino + WisBlock-API-V2 as the firmware framework | Superseded in part by [0005](ADR-0005-direct-sx126x.md) |
| [0004](ADR-0004-bms-one-wire-path.md) | RAK9154 on one-wire; RAK5802 dedicated to the RK900 | Accepted |
| [0005](ADR-0005-direct-sx126x.md) | Use SX126x-Arduino directly rather than WisBlock-API-V2 | Accepted |
| [0006](ADR-0006-rk900-baud-and-register-map.md) | RK900-09 line rate: keep 9600, register map unchanged | Accepted — baud settled by measurement |
| [0007](ADR-0007-no-second-voltage-source.md) | No second voltage source, so the brownout hold is bounded | Accepted |
| [0008](ADR-0008-console-in-the-field-image.md) | The console stays in the field image | Accepted |
| [0009](ADR-0009-address-exposure-rotate-not-rewrite.md) | Leaked build host address is rotated and hardened, not erased from history | Accepted — mitigation is operator-side and open ([#85](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/85)) |

## Open decisions not yet ADR'd

Tracked as GitHub issues. The BMS path and the framework choice are both closed (ADR-0004,
ADR-0003); what remains open is enclosure and cable entry
([#20](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/20),
[#21](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/21)) and
the buck choice
([#2](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/2)).
