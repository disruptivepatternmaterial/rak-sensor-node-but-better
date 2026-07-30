# Summary

<!-- What changed and why. Reference the issue: Refs #N -->

## Spec alignment

<!-- Which section of docs/FIRMWARE_SPEC.md or docs/HARDWARE.md this implements.
     If the code is right and the spec is stale, the spec is updated IN THIS PR.
     Specs and firmware never drift. -->

- Implements:
- Spec updated in this PR: yes / no / n/a

## Citations

Per [`.cursor/rules/20-citation-discipline.mdc`](../.cursor/rules/20-citation-discipline.mdc):
**at least 3 citations across at least 2 categories** for any firmware behavior change.
Every register, baud rate, timeout, and RF parameter needs a `datasheet` or `spec` source.
Prior art alone is never sufficient.

- [ ] CITE(datasheet):
- [ ] CITE(spec):
- [ ] CITE(prior-art):
- [ ] All URLs were **fetched and confirmed**, not recalled
- [ ] New sources added to [`docs/CITATIONS.md`](../docs/CITATIONS.md)

## Gates

- [ ] `scripts/preflight.sh` passes
- [ ] `scripts/build.sh` passes — host and commit SHA reported below
- [ ] **TTN formatter parity** checked; if the payload changed, the paired
      `forest-weather-machines` formatter PR is linked below
- [ ] No secrets, keys, EUIs, or AppKeys added
- [ ] Failed sensor reads stay **null** — no fabricated zeros

Build host / commit:

```
host:
commit:
```

## LoRaWAN and power

Skip any line that does not apply.

- [ ] Class A only; no change to region, activation, or ADR without an ADR
- [ ] Confirmed uplinks still OFF
- [ ] Airtime impact considered against the 30 s/day budget [CIT-TTN-FUP]
- [ ] Downlinks validated: fPort, length, range 300–86400 s, persisted, applied next wake
- [ ] Every exit path still reaches sleep — including error and timeout paths
- [ ] `Serial.end()` / RS-485 disabled before sleep
- [ ] [`docs/POWER_BUDGET.md`](../docs/POWER_BUDGET.md) updated if power behavior changed

## Evidence

<!-- Measured results only. A projection is not evidence. Link the docs/EVIDENCE.md entry.
     Nothing may be described as deployed or working without one. -->

- Evidence entry:
- Hardening IDs affected (H1–H8):

## Docs

- [ ] `README.md` status still accurate (no aspirational "deployed" claims)
- [ ] `CHANGELOG.md` entry added
- [ ] Version bumped per [`docs/RELEASE.md`](../docs/RELEASE.md) if releasing
- [ ] ADR added for any decision that is expensive to reverse
