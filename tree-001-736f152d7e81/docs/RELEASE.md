# Versioning and release

A node in the woods can be running firmware nobody at the bench remembers building. The
point of this process is that **any device in the field can be traced to an exact commit.**

Workflow context: [`.cursor/rules/30-change-workflow.mdc`](../.cursor/rules/30-change-workflow.mdc)

## Scheme

Semantic Versioning, `MAJOR.MINOR.PATCH`, tagged `vX.Y.Z`.

| Bump | When |
|---|---|
| **MAJOR** | Breaks the ingest contract — payload channel/type changes, downlink fPort or format changes, anything requiring a paired TTN formatter change |
| **MINOR** | New capability that stays backward compatible with the decoder |
| **PATCH** | Bug fix, hardening, timing correction, docs |

Pre-field builds stay `0.x.y`. **`1.0.0` is earned, not scheduled**: it requires H1–H8
closed in [`EVIDENCE.md`](EVIDENCE.md), including the ≥24 h bench soak and ≥7 d field
shadow (`FIRMWARE_SPEC.md` §7 H8).

A payload change is a **MAJOR** bump even when the code diff is one byte, because the
consumer is a separate repo that must be updated in lockstep
([`.cursor/rules/60-decoder-parity.mdc`](../.cursor/rules/60-decoder-parity.mdc)).

## The version must be observable on the device

A version that only exists in git is useless when you are standing in the woods with a
laptop. It must appear in:

1. `platformio.ini` / a generated version header — the build's single source.
2. The **boot serial banner**: version, git short SHA, and build date.
3. The **uplink**, so ingest can tell which nodes are stale. This currently
   **requires a TTN formatter change** — no channel carries a version today. It is tracked
   under `requires_formatter_change` in [`../payload/schema.yaml`](../payload/schema.yaml).

Builds from a dirty tree must mark the SHA `-dirty`. An unmarked dirty build is
untraceable, which defeats the entire point.

## Release checklist

Nothing here is optional. Stale docs shipped with a release are worse than no docs, because
the next person trusts them.

```
 1. All changes merged; working tree clean
 2. scripts/preflight.sh            -> PASS  (includes TTN formatter parity)
 3. scripts/build.sh                -> BUILD OK on Heliotrope Ridge
 4. Evidence recorded in docs/EVIDENCE.md for anything claimed
 5. Update docs in the SAME commit:
      - README.md status (do not claim deployed without evidence)
      - docs/FIRMWARE_SPEC.md if behavior changed
      - docs/HARDWARE.md if wiring changed
      - payload/schema.yaml if the payload changed  (+ paired formatter PR)
      - docs/POWER_BUDGET.md if power behavior changed
      - CHANGELOG.md — new version section
 6. Bump the version; commit
 7. git tag vX.Y.Z && git push origin main && git push origin vX.Y.Z
 8. Create the GitHub release
 9. If the payload changed: land the forest-weather-machines formatter PR
    and re-pin decoder hash + SHA in payload/schema.yaml
10. Attach the built artifact and record the flashed version in EVIDENCE.md
```

Step 9 is the one that bites. **A firmware release with a payload change and no formatter
update produces a node that transmits perfectly and delivers nothing**, because the decoder
throws on the unknown type and discards the whole uplink.

## GitHub release

`gh` is available on both machines; on the build host `GITHUB_TOKEN` is in `~/.zprofile`
(remember the login-shell requirement — [`ENVIRONMENTS.md`](ENVIRONMENTS.md)).

```bash
gh release create vX.Y.Z --title "vX.Y.Z — short title" --notes-file <notes>
```

Never `git push --force` a tag to "fix" a release. Delete the tag and release, then
re-create them, so anyone who already fetched sees a clean history.

## Field deployment record

Flashing a field node is a release event. Record in [`EVIDENCE.md`](EVIDENCE.md): which
physical node, which version, which commit, the date, and who did it. Without that, the
next failure investigation starts by guessing what is on the device.
