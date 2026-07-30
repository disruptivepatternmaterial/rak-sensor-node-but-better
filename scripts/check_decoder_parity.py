#!/usr/bin/env python3
"""Verify the firmware payload schema against the live TTN uplink formatter.

The firmware encoder and the TTN JavaScript formatter are two halves of one contract
living in two repos. The decoder throws on the first unknown type byte and returns an
empty object, so a drifted encoder does not lose one field -- it discards the entire
uplink, silently, from a node that appears to be transmitting fine.

This runs on every build. See .cursor/rules/60-decoder-parity.mdc.

Exit codes: 0 = parity holds, 1 = drift or unverifiable.

Deliberately dependency-free: PyYAML is used when present, otherwise a minimal loader
for this schema's subset is used, because the workstation cannot install packages
(see .cursor/rules/10-environments.mdc).
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCHEMA_PATH = REPO_ROOT / "payload" / "schema.yaml"

GREEN, RED, YELLOW, BLUE, DIM, RESET = (
    ("\033[32m", "\033[31m", "\033[33m", "\033[34m", "\033[2m", "\033[0m")
    if sys.stdout.isatty() else ("", "", "", "", "", "")
)


# --------------------------------------------------------------------------- YAML

def load_yaml(path: Path) -> dict:
    text = path.read_text(encoding="utf-8")
    try:
        import yaml  # noqa: PLC0415
        return yaml.safe_load(text)
    except ImportError:
        return _mini_yaml(text)


def _scalar(raw: str):
    raw = raw.strip()
    if not raw:
        return ""
    if raw[0] in "\"'" and raw[-1] == raw[0] and len(raw) > 1:
        return raw[1:-1]
    low = raw.lower()
    if low in ("true", "yes"):
        return True
    if low in ("false", "no"):
        return False
    if low in ("null", "~", "none"):
        return None
    try:
        return int(raw)
    except ValueError:
        pass
    try:
        return float(raw)
    except ValueError:
        return raw


def _strip_comment(line: str) -> str:
    """Remove a trailing # comment that is not inside quotes."""
    out, quote = [], None
    for i, ch in enumerate(line):
        if quote:
            if ch == quote:
                quote = None
        elif ch in "\"'":
            quote = ch
        elif ch == "#" and (i == 0 or line[i - 1] in " \t"):
            break
        out.append(ch)
    return "".join(out).rstrip()


def _mini_yaml(text: str):
    """Minimal loader for the block-style subset used by payload/schema.yaml."""
    # (indent, is_list_item, key, value_or_None) tuples
    rows = []
    lines = text.splitlines()
    i = 0
    while i < len(lines):
        raw = lines[i]
        i += 1
        if not raw.strip() or raw.lstrip().startswith("#"):
            continue
        indent = len(raw) - len(raw.lstrip())
        body = _strip_comment(raw.strip())
        if not body:
            continue

        is_item = body.startswith("- ")
        if is_item:
            body = body[2:].strip()

        if ":" in body:
            key, _, rest = body.partition(":")
            key, rest = key.strip(), rest.strip()
            if rest in (">", "|", ">-", "|-"):  # block scalar
                buf, base = [], None
                while i < len(lines):
                    nxt = lines[i]
                    if not nxt.strip():
                        i += 1
                        buf.append("")
                        continue
                    ni = len(nxt) - len(nxt.lstrip())
                    if ni <= indent:
                        break
                    base = ni if base is None else base
                    buf.append(nxt[base:].rstrip())
                    i += 1
                joined = " ".join(p for p in buf if p) if rest.startswith(">") else "\n".join(buf)
                rows.append((indent, is_item, key, joined))
            else:
                rows.append((indent, is_item, key, _scalar(rest) if rest else None))
        else:
            rows.append((indent, is_item, None, _scalar(body)))
    return _build(rows, 0, len(rows), 0)[0]


def _build(rows, start, end, indent):
    """Build a dict (or list) from rows[start:end] at the given indent level."""
    if start >= end:
        return {}, start
    if rows[start][1]:  # list
        result, i = [], start
        while i < end and rows[i][0] == indent and rows[i][1]:
            _, _, key, val = rows[i]
            if key is None:
                result.append(val)
                i += 1
                continue
            item = {}
            if val is not None:
                item[key] = val
                i += 1
            else:
                i += 1
                j = i
                while j < end and rows[j][0] > indent:
                    j += 1
                item[key], _ = _build(rows, i, j, rows[i][0] if i < j else indent + 2)
                i = j
            # remaining keys of this list item sit at indent + 2
            while i < end and rows[i][0] > indent and not rows[i][1]:
                k_ind, _, k_key, k_val = rows[i]
                if k_val is not None:
                    item[k_key] = k_val
                    i += 1
                else:
                    i += 1
                    j = i
                    while j < end and rows[j][0] > k_ind:
                        j += 1
                    item[k_key], _ = _build(rows, i, j, rows[i][0] if i < j else k_ind + 2)
                    i = j
            result.append(item)
        return result, i

    result, i = {}, start
    while i < end and rows[i][0] == indent:
        _, _, key, val = rows[i]
        if val is not None:
            result[key] = val
            i += 1
            continue
        i += 1
        j = i
        while j < end and rows[j][0] > indent:
            j += 1
        result[key], _ = ({}, i) if i >= j else _build(rows, i, j, rows[i][0])
        i = j
    return result, i


# ------------------------------------------------------------------------ decoder

def find_decoder(schema: dict) -> Path | None:
    """Locate the formatter. FWM_REPO wins, then the schema's local_path."""
    rel = schema["decoder"]["path"]
    roots = []
    if os.environ.get("FWM_REPO"):
        roots.append(Path(os.environ["FWM_REPO"]).expanduser())
    roots.append(Path(schema["decoder"].get("local_path", "")).expanduser())
    roots.append(Path.home() / "Documents" / "GitHub" / "forest-weather-machines")
    roots.append(REPO_ROOT.parent / "forest-weather-machines")
    for root in roots:
        if root and (root / rel).is_file():
            return root / rel
    return None


def parse_wx_types(js: str) -> dict:
    block = re.search(r"var\s+WX_TYPES\s*=\s*\{(.*?)\n\};", js, re.S)
    if not block:
        return {}
    types = {}
    for m in re.finditer(
        r"(\d+)\s*:\s*\{\s*size:\s*(\d+)\s*,\s*name:\s*\"([^\"]+)\"\s*,"
        r"\s*signed:\s*(true|false)\s*,\s*divisor:\s*(\d+)",
        block.group(1),
    ):
        types[int(m.group(1))] = {
            "size": int(m.group(2)),
            "name": m.group(3),
            "signed": m.group(4) == "true",
            "divisor": int(m.group(5)),
        }
    return types


def parse_channel_names(js: str) -> dict:
    block = re.search(r"var\s+CHANNEL_NAMES\s*=\s*\{(.*?)\n\};", js, re.S)
    if not block:
        return {}
    return {
        m.group(1): m.group(2)
        for m in re.finditer(r"\"([^\"]+)\"\s*:\s*\"([^\"]+)\"", block.group(1))
    }


# -------------------------------------------------------------------------- check

def main() -> int:
    ap = argparse.ArgumentParser(description="TTN formatter parity gate")
    ap.add_argument("--schema", type=Path, default=SCHEMA_PATH)
    ap.add_argument("--repin", action="store_true",
                    help="Print the current decoder hash for re-pinning. Only use "
                         "AFTER manually re-verifying parity.")
    args = ap.parse_args()

    print(f"{BLUE}== TTN formatter parity =={RESET}")

    if not args.schema.is_file():
        print(f"{RED}FAIL{RESET} schema not found: {args.schema}")
        return 1
    schema = load_yaml(args.schema)

    decoder_path = find_decoder(schema)
    if decoder_path is None:
        print(f"{RED}FAIL{RESET} decoder not found: {schema['decoder']['path']}")
        print("      Parity is unproven, which is not the same as proven safe.")
        print("      Clone forest-weather-machines or set FWM_REPO=/path/to/repo")
        return 1

    js = decoder_path.read_text(encoding="utf-8")
    actual_sha = hashlib.sha256(js.encode("utf-8")).hexdigest()
    pinned_sha = str(schema["decoder"].get("pinned_sha256", "")).strip()

    print(f"{DIM}   decoder: {decoder_path}{RESET}")

    if args.repin:
        print(f"\n   pinned_sha256: {actual_sha}\n")
        return 0

    failures, warnings = [], []

    # --- Gate 1: has the formatter changed upstream?
    if actual_sha != pinned_sha:
        failures.append(
            "DECODER CHANGED UPSTREAM since the pinned hash.\n"
            f"        pinned: {pinned_sha}\n"
            f"        actual: {actual_sha}\n"
            "        The formatter moved. Re-verify parity by hand, then re-pin in\n"
            "        payload/schema.yaml (scripts/check_decoder_parity.py --repin).\n"
            "        Re-pinning WITHOUT reconciling is what this gate prevents."
        )

    wx_types = parse_wx_types(js)
    channel_names = parse_channel_names(js)
    if not wx_types or not channel_names:
        failures.append("Could not parse WX_TYPES / CHANNEL_NAMES — decoder structure changed.")
        _report(failures, warnings)
        return 1

    # --- Gate 2: field-by-field contract
    fields = schema.get("fields") or []
    for f in fields:
        label = f"{f['name']} (ch {f['channel']}, type {f['type']})"
        t = wx_types.get(f["type"])
        if t is None:
            failures.append(
                f"{label}: type {f['type']} is NOT in the decoder's WX_TYPES.\n"
                "        The decoder throws on unknown types and DISCARDS THE WHOLE UPLINK.\n"
                "        The formatter must change before this can ship."
            )
            continue

        for attr in ("size", "signed", "divisor"):
            if f[attr] != t[attr]:
                failures.append(
                    f"{label}: {attr} mismatch — schema={f[attr]} decoder={t[attr]}. "
                    "Values will decode incorrectly."
                )

        key = f"{t['name']}_{f['channel']}"
        mapped = channel_names.get(key)
        if mapped is None:
            failures.append(
                f"{label}: decoder has no CHANNEL_NAMES entry for '{key}'.\n"
                f"        Value would decode but land under the raw key '{key}',\n"
                "        missing every downstream consumer."
            )
        elif mapped != f["decoded_key"]:
            failures.append(
                f"{label}: decoded_key mismatch — schema='{f['decoded_key']}' "
                f"decoder='{mapped}'."
            )

        if str(f.get("status", "")).upper() == "BLOCKED":
            warnings.append(f"{label}: status BLOCKED — {_first_line(f.get('notes'))}")

    # --- Gate 3: pending formatter changes
    for item in schema.get("requires_formatter_change") or []:
        if str(item.get("status", "")).lower() == "open":
            warnings.append(
                f"FORMATTER CHANGE NEEDED: {item['description']} — "
                f"{_first_line(item.get('reason'))}"
            )

    print(f"{DIM}   checked {len(fields)} fields against "
          f"{len(wx_types)} decoder types{RESET}")
    _report(failures, warnings)
    return 1 if failures else 0


def _first_line(text) -> str:
    if not text:
        return ""
    s = " ".join(str(text).split())
    return s if len(s) <= 150 else s[:147] + "..."


def _report(failures, warnings) -> None:
    for w in warnings:
        print(f"{YELLOW}WARN{RESET} {w}")
    for f in failures:
        print(f"{RED}FAIL{RESET} {f}")
    if failures:
        print(f"\n{RED}Parity BROKEN — {len(failures)} issue(s). "
              f"Do not flash a field node.{RESET}")
    else:
        note = f" ({len(warnings)} call-out(s))" if warnings else ""
        print(f"{GREEN}PASS{RESET} payload schema matches the TTN formatter{note}")


if __name__ == "__main__":
    sys.exit(main())
