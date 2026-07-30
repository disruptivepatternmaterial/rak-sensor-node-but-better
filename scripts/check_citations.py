#!/usr/bin/env python3
"""Enforce citation discipline.

Every magic number in this firmware -- a register address, a baud rate, a timeout, an
RX window -- is a claim about physical reality. An unsourced claim is a guess, and
guesses are how you brick a node that is a hike away.

Checks:
  1. Every CITE(...) uses a known category and actually points somewhere.
  2. Every [CIT-KEY] reference resolves in docs/CITATIONS.md.
  3. Registry entries are not left unverified without being marked as such.
  4. With --diff BASE: changed firmware files meet the per-change minimums, and
     changed lines containing magic numbers carry an adjacent citation.

See .cursor/rules/20-citation-discipline.mdc.
Exit codes: 0 = pass, 1 = fail.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
REGISTRY = REPO_ROOT / "docs" / "CITATIONS.md"

CATEGORIES = {"datasheet", "spec", "policy", "prior-art", "sibling", "bench"}
# Literal placeholders that templates and docs legitimately contain.
PLACEHOLDER_KEYS = {"CIT-KEY", "CIT-NNNN"}
# Prior art shows something works; it does not establish correctness. It may never
# stand alone behind a register map, a timing value, or an RF parameter.
WEAK_ALONE = {"prior-art"}

CODE_EXT = {".c", ".cpp", ".h", ".hpp", ".ino"}
MIN_CITATIONS = 3
MIN_CATEGORIES = 2

# Vendored upstream code. Its provenance is the pinned commit and file hashes recorded
# in the directory's README, not inline citations -- and we do not edit it, so holding
# it to our sourcing bar would only produce noise that trains people to ignore the gate.
VENDORED_PREFIXES = ("rakwireless/",)


def is_vendored(rel: str) -> bool:
    return rel.startswith(VENDORED_PREFIXES)

CITE_RE = re.compile(r"CITE\(([a-z\-]+)\)\s*:\s*(.+)")
KEY_REF_RE = re.compile(r"\[(CIT-[A-Z0-9\-]+)\]")
KEY_DEF_RE = re.compile(r"^\|\s*`?(CIT-[A-Z0-9\-]+)`?\s*\|", re.M)

# Values that must never appear un-sourced in firmware.
MAGIC_RE = re.compile(
    r"\b(0x[0-9A-Fa-f]{2,4}"                 # register / slave addresses
    r"|4800|9600|19200|115200"               # baud rates
    r"|300|3600|86400"                       # interval bounds
    r"|902|915|923|928)\b"                   # US915 frequencies
)

GREEN, RED, YELLOW, BLUE, DIM, RESET = (
    ("\033[32m", "\033[31m", "\033[33m", "\033[34m", "\033[2m", "\033[0m")
    if sys.stdout.isatty() else ("", "", "", "", "", "")
)


def git(*args: str) -> str:
    try:
        return subprocess.run(
            ["git", *args], cwd=REPO_ROOT, capture_output=True, text=True, check=True
        ).stdout
    except subprocess.CalledProcessError:
        return ""


def tracked_files() -> list[Path]:
    return [
        REPO_ROOT / p
        for p in git("ls-files").splitlines()
        if p and (REPO_ROOT / p).is_file()
    ]


def load_registry() -> set[str]:
    if not REGISTRY.is_file():
        return set()
    return set(KEY_DEF_RE.findall(REGISTRY.read_text(encoding="utf-8")))


def main() -> int:
    ap = argparse.ArgumentParser(description="citation discipline gate")
    ap.add_argument("--diff", metavar="BASE",
                    help="enforce per-change minimums against BASE (e.g. origin/main)")
    args = ap.parse_args()

    print(f"{BLUE}== citation discipline =={RESET}")
    failures: list[str] = []
    warnings: list[str] = []

    keys = load_registry()
    if not keys:
        failures.append(f"citation registry missing or empty: {REGISTRY.relative_to(REPO_ROOT)}")
    else:
        print(f"{DIM}   registry: {len(keys)} source(s){RESET}")

    # --- 1 & 2: CITE syntax and key resolution across the tree
    cite_count = 0
    for path in tracked_files():
        if path.suffix.lower() in {".png", ".jpg", ".pdf", ".bin", ".hex", ".uf2"}:
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        rel = path.relative_to(REPO_ROOT)
        if is_vendored(rel.as_posix()):
            continue

        for n, line in enumerate(text.splitlines(), 1):
            m = CITE_RE.search(line)
            if m:
                cite_count += 1
                cat, body = m.group(1), m.group(2).strip()
                if cat not in CATEGORIES:
                    failures.append(
                        f"{rel}:{n}: unknown citation category '{cat}' "
                        f"(valid: {', '.join(sorted(CATEGORIES))})"
                    )
                has_target = ("http" in body or "CIT-" in body
                              or ".md" in body or "/" in body)
                if not has_target:
                    failures.append(
                        f"{rel}:{n}: CITE({cat}) has no URL, registry key, or path -- "
                        "a citation must point somewhere"
                    )
                if cat == "sibling" and not re.search(r"\b[0-9a-f]{7,40}\b", body):
                    warnings.append(
                        f"{rel}:{n}: sibling citation has no commit SHA -- "
                        "sibling repos move independently, so this is not reproducible"
                    )

            for key in KEY_REF_RE.findall(line):
                if key in PLACEHOLDER_KEYS:
                    continue
                if keys and key not in keys:
                    failures.append(
                        f"{rel}:{n}: [{key}] is not defined in docs/CITATIONS.md"
                    )

    print(f"{DIM}   found {cite_count} CITE marker(s) in tracked files{RESET}")

    # --- 3: every registry entry must be verified.
    # An unverified source does not get committed -- it gets verified or left out.
    # A registry that ships warnings trains people to ignore warnings.
    if REGISTRY.is_file():
        for n, line in enumerate(REGISTRY.read_text(encoding="utf-8").splitlines(), 1):
            if not (line.startswith("|") and "CIT-" in line):
                continue
            key = KEY_DEF_RE.findall(line + "\n")
            if not key:
                continue
            upper = line.upper()
            if "UNVERIFIED" in upper:
                failures.append(
                    f"docs/CITATIONS.md:{n}: {key[0]} is UNVERIFIED. Fetch the URL and "
                    "confirm it states the cited fact, or remove the entry. Unverified "
                    "sources are not committed."
                )
            elif "VERIFIED" not in upper and "PINNED" not in upper:
                # Sibling rows carry a pinned SHA instead of a verification date.
                if not re.search(r"\b[0-9a-f]{7,40}\b", line):
                    failures.append(
                        f"docs/CITATIONS.md:{n}: {key[0]} has no verification status. "
                        "Every entry must be VERIFIED with a date, or pinned to a SHA."
                    )

    # --- 4: per-change minimums
    if args.diff:
        changed = [
            p for p in git("diff", "--name-only", f"{args.diff}...HEAD").splitlines() if p
        ]
        code_changed = [
            p for p in changed
            if Path(p).suffix.lower() in CODE_EXT and not is_vendored(p)
        ]
        if code_changed:
            diff = git("diff", "-U0", f"{args.diff}...HEAD", "--", *code_changed)
            added = [
                ln[1:] for ln in diff.splitlines()
                if ln.startswith("+") and not ln.startswith("+++")
            ]
            cats: set[str] = set()
            n_cites = 0
            for ln in added:
                m = CITE_RE.search(ln)
                if m:
                    n_cites += 1
                    cats.add(m.group(1))

            if n_cites < MIN_CITATIONS or len(cats) < MIN_CATEGORIES:
                failures.append(
                    f"firmware change carries {n_cites} citation(s) across "
                    f"{len(cats)} categor{'y' if len(cats) == 1 else 'ies'}; "
                    f"minimum is {MIN_CITATIONS} across {MIN_CATEGORIES} "
                    f"(.cursor/rules/20-citation-discipline.mdc)"
                )
            if cats and cats <= WEAK_ALONE:
                failures.append(
                    "change is justified by prior-art alone. Prior art shows something "
                    "works; it does not establish correctness. Pair it with a datasheet "
                    "or spec citation."
                )

            # magic numbers on added lines need a citation within 2 lines
            for i, ln in enumerate(added):
                if MAGIC_RE.search(ln) and not CITE_RE.search(ln):
                    window = added[max(0, i - 2): i + 1]
                    if not any(CITE_RE.search(w) for w in window):
                        warnings.append(
                            f"un-sourced constant on an added line: {ln.strip()[:90]}"
                        )
        else:
            print(f"{DIM}   no firmware sources changed vs {args.diff}{RESET}")

    for w in warnings:
        print(f"{YELLOW}WARN{RESET} {w}")
    for f in failures:
        print(f"{RED}FAIL{RESET} {f}")

    if failures:
        print(f"\n{RED}Citation discipline BROKEN — {len(failures)} issue(s).{RESET}")
        return 1
    note = f" ({len(warnings)} warning(s))" if warnings else ""
    print(f"{GREEN}PASS{RESET} citation discipline{note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
