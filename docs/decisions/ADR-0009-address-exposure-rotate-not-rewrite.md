# ADR-0009 — The leaked build host address is rotated and hardened, not erased from history

- **Status:** Accepted
- **Date:** 2026-08-28
- **Refs:** [#85](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/85),
  [#86](https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/86),
  `b7f3140`, `623ae0f`
- **Affects:** `scripts/preflight.sh`, `docs/EVIDENCE.md` provenance, any future history rewrite

## Context

On 2026-08-27, `d8b6bfe` put the build host's public IPv4 address into `README.md` in a
repository that `gh repo view` reports as `PUBLIC`. It sat on `origin/main` for roughly six
hours. Reviewing that commit found two more: `docs/EVIDENCE.md` carried a second public
address on the same subnet, once with the account name prefixed as `ntableman@<address>`.

`b7f3140` removed all three from the working tree and added a `scripts/preflight.sh` gate that
fails on any IPv4 literal in a tracked file. That closes the future. It does not close the
past, and the past is where the conflict lives.

Two of this project's own rules point in opposite directions:

- `.cursor/rules/20-citation-discipline.mdc` and `AGENTS.md` forbid committing anything that
  identifies infrastructure, and the repo is public.
- `docs/EVIDENCE.md` states that **a result without a host and a commit SHA is not evidence.**
  The commit SHA is the load-bearing half of every hardware claim this project has made.

Purging content from git history changes the SHA of the rewritten commit and of every
descendant, because a commit SHA is a hash of its content and ancestry. That is not an
implementation detail to work around; it is what a commit SHA *is*.

### The measurement

| Purge scope | Commits rewritten | Cited SHAs invalidated |
|---|---|---|
| Public addresses only (earliest carrier `4510763`) | 85 of 252 | **34** |
| Including the RFC1918 LAN address (earliest carrier `1db0ae2`, the initial commit) | 252 of 252 | 77 |

Among the 34 are the only three commits in this project's history that a **board asserted from
its own boot banner** — `1c2df3c`, `d568574`, `65f8615` — and `572bcfa`, which carries the
19.03 h soak. Those four are the strongest evidence artifacts that exist here, and the
banner-asserted ones are not reproducible: the string `1c2df3c` is compiled into firmware that
is currently in the woods. The device cannot be asked to print a different answer.

### What was actually exposed

`whois` puts both public addresses in a single `/16` registered to the **University of Maine
System** (`NetName: UMAINE-SYS`). The build host is a laptop on that campus network, and the
operator moves it between networks. Two things follow:

- The address is a **dynamic-in-practice campus allocation**, not a stable identifier of the
  machine. Its usefulness to an attacker decays to nothing as soon as the laptop is
  reassigned or moves — which is the normal case, and is why the operator's own description of
  the host is "this is a laptop, sometimes I am not home."
- The disclosure is therefore closer to the old RFC 1918 LAN literal, which has been in this
  repo since the initial commit and identifies nothing reachable, than to a credential. **No
  key, token, EUI, or AppKey was ever exposed;** `scripts/preflight.sh` has gated those
  separately since `ff76322`.

### Rewriting does not even achieve the goal

GitHub retains unreachable objects and serves them by SHA after a force-push. The old commits
stay fetchable until GitHub Support purges them on request. A force-push alone changes what
`main` points at, not what is retrievable.

## Options considered

| Option | Pros | Cons |
|---|---|---|
| **A. Rewrite history to purge the addresses** | The literals leave the repository | 85–252 commits rewritten; 34–77 documentation citations broken; the three banner-asserted SHAs become unresolvable, permanently and irreparably; old objects remain fetchable on GitHub anyway; requires a force-push to `main` |
| **B. Rewrite, plus a translation layer** — commit `filter-repo`'s old→new commit map, create `legacy-sha/<old>` annotated tags, and mechanically update the 34 citations | Literals leave; old SHAs stay *resolvable* via tag | The device-printed SHA is no longer a commit SHA but a pointer through a mapping file the device cannot corroborate; converts a self-verifying claim into a claim about a file we wrote ourselves; still needs a force-push; still does not remove the objects from GitHub |
| **C. Rotate and harden; leave history intact** (chosen) | Neutralizes the actual exposure; evidence chain untouched; no force-push; no re-clone; cheap | The literals remain visible in old commits, which reads as untidy in a public repo |

## Decision

**Option C.** The addresses stay in git history. The exposure is closed at the host instead of
in the record:

1. **Confirm the address no longer routes to the build host.** It is a campus DHCP-scale
   allocation on a laptop; if it has already moved, the historical literal is inert and this
   ADR is closed by observation. Operator-side, since no agent has the address.
2. **Do not expose SSH to the public internet.** Prefer an overlay network (Tailscale,
   WireGuard) so the build host has no publicly reachable service at all. If public SSH is
   kept: key-only authentication, `PasswordAuthentication no`, and a non-default port.
3. **Keep the tripwire.** `scripts/preflight.sh` fails on any IPv4 literal in a tracked file,
   so this specific mistake cannot recur silently. It found two leaks its first run.

`scripts/preflight.sh` additionally verifies that the irreplaceable evidence SHAs still
resolve. That gate exists so that if this decision is ever reversed, the damage announces
itself immediately instead of being discovered later by a reader who cannot find `1c2df3c`.

## Rationale

The tradeoff accepted is untidiness in exchange for provenance. Weighed against the deployment
goal — a node unattended in the woods indefinitely, where the only thing that makes a firmware
image trustworthy is being able to trace it to a commit and a measurement — an unverifiable
evidence chain is a worse outcome than a stale campus IP in a two-week-old commit.

The asymmetry is what decides it. The exposure is **self-limiting**: a laptop's campus address
stops being interesting on its own. The evidence loss is **permanent**: no future work
regenerates a boot banner from a node that has already been packed into a bag, and no amount of
later diligence reconstructs which commit `1c2df3c` was.

Option B deserves the explicit rejection rather than being ignored, because it looks like it
has no cost. It does. `docs/EVIDENCE.md`'s discipline is that the device and the repository
agree on a SHA without anyone mediating. A mapping file we maintain ourselves is exactly the
mediation that discipline exists to avoid, and it is the kind of thing that reads as fine for a
year and then cannot be trusted at the moment it matters.

## Consequences

- The two campus addresses remain readable in `d8b6bfe`, `089fbc4`, and `4510763`, and the
  RFC 1918 LAN literal in twelve commits back to `1db0ae2`. **This is deliberate. Do not
  "clean it up" in a later pass** — re-deriving this decision and reversing it silently is the
  specific failure this ADR exists to prevent. The addresses are named nowhere in the current
  tree, including in this ADR, because `scripts/preflight.sh` fails on any IPv4 literal in a
  tracked file. It caught an earlier draft of this very document, which is the gate working.
- Any future proposal to rewrite history must state, in the PR, how many commits and how many
  cited SHAs it invalidates, and must handle the three banner-asserted SHAs explicitly.
- The real mitigation is operator-side and is not verifiable from this repository. Until #85
  records that the address no longer routes or that SSH is off the public internet, the
  exposure is open, and the honest status is "disclosed and self-limiting", not "resolved".
- No firmware, payload, or power behavior changes.

## Evidence

- `b7f3140` — the three literals removed; the IPv4 gate added and observed catching the two
  `docs/EVIDENCE.md` leaks on its first execution.
- Measured 2026-08-28 on the workstation: `git rev-list --count 4510763^..HEAD` = 85,
  `git rev-list --count HEAD` = 252, and 34 of 77 real commit SHAs cited in tracked markdown
  are descendants of `4510763`.
- `whois` on the leaked public address, run 2026-08-28: a single `/16` with
  `NetName: UMAINE-SYS`, `Organization: University of Maine System`, `Country: US`. Rerun it
  against the address in `d8b6bfe` to reproduce.
- Falsifiable by: the address still routing to the build host, which would make the exposure
  live rather than self-limiting and would move the decision toward urgent rotation. That is
  the check in Decision step 1.

## Citations

- CITE(spec): Git internals — a commit object's name is the SHA-1/SHA-256 of its content, so
  rewriting content necessarily renames the commit and all descendants [CIT-GIT-OBJECTS] —
  https://git-scm.com/book/en/v2/Git-Internals-Git-Objects
- CITE(spec): RFC 1918 §3 — the `192.168` range is private address space, not globally
  routable, so the older LAN literal in this history identifies no reachable host
  [CIT-RFC1918] — https://datatracker.ietf.org/doc/html/rfc1918
- CITE(policy): GitHub — removing sensitive data requires contacting GitHub Support to purge
  cached views and unreachable objects; a force-push alone leaves them retrievable
  [CIT-GH-SENSITIVE] —
  https://docs.github.com/en/authentication/keeping-your-account-and-data-secure/removing-sensitive-data-from-a-repository
- CITE(prior-art): `git filter-repo` writes a complete old→new commit map, which is what
  Option B's translation layer would be built on [CIT-FILTER-REPO] —
  https://github.com/newren/git-filter-repo
- CITE(bench): `docs/EVIDENCE.md` 2026-08-14 — `1c2df3c` read back from the boot banner on
  hardware, the artifact a rewrite would invalidate.
