# Architecture decision records

Short records of decisions that are expensive to reverse or easy to forget the reasoning
behind. Six months from now, in the woods, "why is it wired that way?" needs an answer
better than the git log.

## When to write one

Write an ADR when a choice:

- closes one of the open decisions in [`../../plans/P0_HARDENED_NODE.md`](../../plans/P0_HARDENED_NODE.md);
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

## Open decisions not yet ADR'd

Carried in [`../../plans/P0_HARDENED_NODE.md`](../../plans/P0_HARDENED_NODE.md), to be
resolved in the first firmware PR: BMS path (4-pin Modbus + baud switch vs 5-pin one-wire),
enclosure choice, Probe IO junction, Arduino WisBlock-API-V2 vs RUI3, and TTN application
reuse.
