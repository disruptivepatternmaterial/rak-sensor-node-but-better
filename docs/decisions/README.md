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

Copy an existing ADR to `ADR-NNNN-short-title.md`, take the next number, and
link it from the decision it closes. The old `TEMPLATE.md` went in `5a9d584`; recover it with
`git show 5a9d584^:docs/decisions/TEMPLATE.md` if you want the original headings.
ADRs are **append-only**: superseding an ADR means
writing a new one and marking the old `Superseded by ADR-NNNN`. Never edit history to make
a past decision look better than it was.

Every ADR carries citations to the same standard as code (rule 20).

## Index

ADR-0001 through ADR-0009 and `TEMPLATE.md` were removed from the working tree in `5a9d584`.
The decisions still stand and the code still cites them by number; only the files are gone.
Retrieve any of them from the commit before the removal:

```bash
git show 5a9d584^:docs/decisions/ADR-0004-bms-one-wire-path.md
git show 5a9d584^:docs/decisions/            # list them all
```

| ADR | Title | Status |
|---|---|---|
| 0001 | Author locally, build and flash on Heliotrope Ridge | Accepted — file removed in `5a9d584` |
| 0002 | Payload contract conflicts with the live TTN decoder | Accepted — all three conflicts decided; current sign closed 2026-08-13. File removed in `5a9d584` |
| 0003 | Arduino + WisBlock-API-V2 as the firmware framework | Superseded in part by 0005. File removed in `5a9d584` |
| 0004 | RAK9154 on one-wire; RAK5802 dedicated to the RK900 | Accepted — file removed in `5a9d584` |
| 0005 | Use SX126x-Arduino directly rather than WisBlock-API-V2 | Accepted — file removed in `5a9d584` |
| 0006 | RK900-09 line rate: keep 9600, register map unchanged | Accepted — baud settled by measurement. File removed in `5a9d584` |
| 0007 | No second voltage source, so the brownout hold is bounded | Accepted — file removed in `5a9d584` |
| 0008 | The console stays in the field image | Accepted — file removed in `5a9d584` |
| 0009 | Leaked build host address is rotated and hardened, not erased from history | Accepted — mitigation is operator-side and open ([#85](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/85)). File removed in `5a9d584` |
| [0010](ADR-0010-rak19007-vdd-source-conflict.md) | RAK19007 VDD source conflict | Accepted |

**ADR-0011 and ADR-0012 were never written.** `docs/HARDWARE.md` and `docs/FIRMWARE_SPEC.md`
cite them for the plug-handling rule and for the fault-tolerant one-wire transceiver, so those
two references describe decisions that exist only in the prose that cites them. That predates
`5a9d584`.

## Open decisions not yet ADR'd

Tracked as GitHub issues. The BMS path and the framework choice are both closed (ADR-0004,
ADR-0003); what remains open is enclosure and cable entry
([#20](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/20),
[#21](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/21)) and
the buck choice
([#2](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/2)).
