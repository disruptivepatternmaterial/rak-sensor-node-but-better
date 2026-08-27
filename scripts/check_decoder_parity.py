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

VENDORED_DECODER = REPO_ROOT / "payload" / "reference" / "rak-wx-station-default.js"


def find_decoder(schema: dict) -> tuple[Path | None, str]:
    """Locate the formatter.

    Prefers a live checkout of forest-weather-machines, because only the live file can
    reveal that the decoder moved upstream. Falls back to the pinned copy vendored in
    payload/reference/ so the gate still runs where that repo is not reachable -- most
    importantly GitHub Actions, since forest-weather-machines is private and the default
    workflow token is scoped to this repository alone.

    Returns (path, source) where source is "live" or "pinned".
    """
    rel = schema["decoder"]["path"]
    roots = []
    if os.environ.get("FWM_REPO"):
        roots.append(Path(os.environ["FWM_REPO"]).expanduser())
    roots.append(Path(schema["decoder"].get("local_path", "")).expanduser())
    roots.append(Path.home() / "Documents" / "GitHub" / "forest-weather-machines")
    roots.append(REPO_ROOT.parent / "forest-weather-machines")
    for root in roots:
        if root and (root / rel).is_file():
            return root / rel, "live"
    if VENDORED_DECODER.is_file():
        return VENDORED_DECODER, "pinned"
    return None, "none"


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
    """Map the decoder's `"typeName_channel"` key to the field name it decodes to.

    Two upstream shapes exist and both are read, because the formatter is in another
    repo and this gate must not go blind the next time it is restructured:

        CHANNELS      = { "wind_speed_1": { name: "wx_wind_speed", probe: "wx" }, ... }
        CHANNEL_NAMES = { "wind_speed_1": "wx_wind_speed", ... }

    `CHANNELS` is checked first because it is the current form; forest-weather-machines
    `3cfd281` replaced the flat string map with objects carrying `name`, `probe`, and
    `isSerial`. Only `name` is part of this firmware's contract: `probe` and `isSerial`
    exist to label the per-probe serial on channel 0, which this device never emits.

    CITE(sibling): forest-weather-machines @ 058bd69 —
      LoRaWAN/payload/rak-wx-station-default.js:52-75, the CHANNELS object map.
    CITE(sibling): forest-weather-machines @ 058bd69 —
      LoRaWAN/payload/rak-wx-station-default.js:223-243, lppDecodeToFlat() reads only
      `mapping.name` for a non-serial channel, so name is the whole contract here.
    """
    block = re.search(r"var\s+CHANNELS\s*=\s*\{(.*?)\n\};", js, re.S)
    if block:
        return {
            m.group(1): m.group(2)
            for m in re.finditer(
                r"\"([^\"]+)\"\s*:\s*\{[^{}]*?\bname\s*:\s*\"([^\"]+)\"",
                block.group(1),
            )
        }

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

    decoder_path, decoder_source = find_decoder(schema)
    if decoder_path is None:
        print(f"{RED}FAIL{RESET} decoder not found: {schema['decoder']['path']}")
        print("      Parity is unproven, which is not the same as proven safe.")
        print("      Clone forest-weather-machines, set FWM_REPO=/path/to/repo, or")
        print(f"      restore the pinned copy at "
              f"{VENDORED_DECODER.relative_to(REPO_ROOT)}")
        return 1

    js = decoder_path.read_text(encoding="utf-8")
    actual_sha = hashlib.sha256(js.encode("utf-8")).hexdigest()
    pinned_sha = str(schema["decoder"].get("pinned_sha256", "")).strip()

    print(f"{DIM}   decoder: {decoder_path}{RESET}")
    print(f"{DIM}   source:  {decoder_source}{RESET}")

    if args.repin:
        print(f"\n   pinned_sha256: {actual_sha}\n")
        return 0

    failures, warnings, blocked = [], [], []

    if decoder_source == "pinned":
        # The vendored copy matches the pin by construction, so gate 1 below is
        # vacuous here. Say so plainly rather than letting a green result imply more
        # than it proves: the field-by-field contract IS checked, but the question
        # "did the formatter move upstream?" is unanswerable without the live repo.
        warnings.append(
            "Checked against the PINNED copy in payload/reference/, not a live "
            "checkout.\n"
            "        The field-by-field contract is verified. Upstream drift is NOT --\n"
            "        forest-weather-machines was not reachable here. A run with the\n"
            "        sibling repo present (the build host, or any local clone) is what\n"
            "        proves the formatter has not moved."
        )

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

    # Named separately. A combined "could not parse WX_TYPES / CHANNEL_NAMES" message sent
    # issue #83 looking at both halves when only the channel map had moved, and it gave no
    # hint that the gate had stopped checking rather than found a mismatch.
    if not wx_types:
        failures.append(
            "Could not parse WX_TYPES from the formatter — its structure changed.\n"
            "        This gate is now BLIND to type/size/signedness/divisor drift.\n"
            "        Teach parse_wx_types() the new shape; do not re-pin around it."
        )
    if not channel_names:
        failures.append(
            "Could not parse a channel map (CHANNELS or CHANNEL_NAMES) from the "
            "formatter.\n"
            "        This gate is now BLIND to decoded_key drift.\n"
            "        Teach parse_channel_names() the new shape; do not re-pin around it."
        )
    if failures and (not wx_types or not channel_names):
        _report(failures, warnings, blocked)
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
            # Not a warning. A BLOCKED field is half of the ingest contract with no agreed
            # meaning, and folding it into the same bucket as "a formatter change is pending"
            # is how the run below came to print PASS over it. preflight.sh reads this label.
            blocked.append(f"{label}: status BLOCKED — {_first_line(f.get('notes'))}")

    # --- Gate 3b: does the firmware encoder actually agree with the schema?
    failures.extend(check_encoder(schema))

    # --- Gate 3: pending formatter changes
    for item in schema.get("requires_formatter_change") or []:
        if str(item.get("status", "")).lower() == "open":
            warnings.append(
                f"FORMATTER CHANGE NEEDED: {item['description']} — "
                f"{_first_line(item.get('reason'))}"
            )

    print(f"{DIM}   checked {len(fields)} fields against "
          f"{len(wx_types)} decoder types{RESET}")
    _report(failures, warnings, blocked)
    return 1 if failures else 0


ENCODER = REPO_ROOT / "src" / "payload.cpp"
ENCODER_HEADER = REPO_ROOT / "src" / "payload.h"

# constexpr uint8_t kChWindSpeed = 1, kTyWindSpeed = 190;
_CONST_RE = re.compile(
    r"kCh(\w+)\s*=\s*(\d+)\s*,\s*kTy\1\s*=\s*(\d+)"
)

# put_u16(kChWindSpeed, kTyWindSpeed, w.wind_speed.value);
#
# The channel and type names are captured INDEPENDENTLY — group 2 and group 3, not group 2
# and a backreference to it. The backreference version only matched a call whose two
# constants agreed, so `put_u16(kChWindSpeed, kTyHumidity, ...)` did not match at all and was
# dropped before any comparison ran: the one mistake most worth catching was the one the gate
# was structurally blind to. Cross-wiring is now a parse hit and a failure.
#
# The value argument is captured too, because the divisor lives in the decoder and the
# encoder's whole contract is that it does not scale. See check_encoder().
_CALL_RE = re.compile(
    r"put_(u8|u16|s16)\(\s*kCh(\w+)\s*,\s*kTy(\w+)\s*,\s*([^;]*?)\)\s*;"
)

# constexpr size_t kMaxPayloadBytes = 35;
_MAX_BYTES_RE = re.compile(r"kMaxPayloadBytes\s*=\s*(\d+)")
_MIN_DR_BYTES_RE = re.compile(r"kMinDataRatePayloadBytes\s*=\s*(\d+)")

# A value the encoder passes straight through: `w.wind_speed.value`, `b.soc.value`. Anything
# else — an arithmetic expression, a multiply, a cast around a computation — means the
# firmware is scaling a field whose divisor the decoder also applies.
_PASSTHROUGH_RE = re.compile(r"^[A-Za-z_]\w*(?:\.\w+)*\.value$")

# Width and signedness implied by each emitter, plus the two header bytes every field
# carries on the wire. [channel][type][value...], no length prefix anywhere.
_EMITTERS = {
    "u8":  (1, False),
    "u16": (2, False),
    "s16": (2, True),
}
_HEADER_BYTES = 2


def check_encoder(schema) -> list:
    """Compare src/payload.cpp against the schema.

    Without this the chain has a hole in the middle. The schema is checked against the
    JavaScript decoder, but nothing checks the firmware that actually produces the bytes,
    so the encoder can drift while the gate stays green. Changing one type number in
    payload.cpp would ship an uplink that decodes into the wrong fields entirely, with
    every check passing.
    """
    if not ENCODER.is_file():
        return [f"encoder not found: {ENCODER.relative_to(REPO_ROOT)}"]

    src = ENCODER.read_text(encoding="utf-8")

    consts = {m.group(1): (int(m.group(2)), int(m.group(3)))
              for m in _CONST_RE.finditer(src)}
    if not consts:
        return ["Could not parse channel/type constants from src/payload.cpp — "
                "the encoder's structure changed and this gate went blind."]

    failures = []

    # name -> (channel, type, size, signed), deduplicated across add() and build().
    encoded = {}
    for m in _CALL_RE.finditer(src):
        emitter, ch_name, ty_name, value_arg = m.groups()

        for kind, n in (("kCh", ch_name), ("kTy", ty_name)):
            if n not in consts:
                failures.append(
                    f"encoder calls put_{emitter}() with {kind}{n}, which is not declared "
                    "in src/payload.cpp.\n"
                    "        Previously this call was skipped silently and the field went "
                    "unchecked."
                )
        if ch_name not in consts or ty_name not in consts:
            continue

        channel = consts[ch_name][0]
        type_id = consts[ty_name][1]
        size, signed = _EMITTERS[emitter]

        if ch_name != ty_name:
            # Cross-wired: the channel of one field carrying the type of another. The uplink
            # decodes under the wrong field with the wrong width and divisor, and because
            # there is no length prefix every field after it shifts too.
            failures.append(
                f"encoder cross-wires kCh{ch_name} (channel {channel}) with "
                f"kTy{ty_name} (type {type_id}) in a put_{emitter}() call.\n"
                "        Channel and type must come from the same field. This decodes under "
                "the wrong field and shifts every field after it."
            )

        if not _PASSTHROUGH_RE.match(value_arg.strip()):
            # The encoder's contract is that it does not scale: every divisor is applied by
            # the decoder, and payload.cpp:51-53 says so explicitly. An expression here means
            # a factor is being applied on both sides, which is the 10x class of error that
            # arrives as a plausible-looking wrong number rather than a failure.
            failures.append(
                f"encoder passes a computed value to put_{emitter}(kCh{ch_name}, ...): "
                f"'{value_arg.strip()}'.\n"
                "        The decoder owns every divisor (see payload.cpp:51-53), so scaling "
                "here double-applies it — a 10x error that decodes as a plausible number.\n"
                "        If the scaling is genuinely required, record it in "
                "payload/schema.yaml and teach this gate about it."
            )

        encoded[ch_name] = (channel, type_id, size, signed)

    if not encoded:
        failures.append(
            "Found channel/type constants in src/payload.cpp but no emit calls using "
            "them — this gate cannot see what the firmware sends."
        )
        return failures

    by_channel = {f["channel"]: f for f in (schema.get("fields") or [])}

    for name, (channel, type_id, size, signed) in sorted(encoded.items()):
        field = by_channel.get(channel)
        if field is None:
            failures.append(
                f"encoder sends channel {channel} (type {type_id}, from kCh{name}) "
                "which is NOT in payload/schema.yaml.\n"
                "        The decoder throws on an unknown type and discards the WHOLE "
                "uplink."
            )
            continue

        if field["type"] != type_id:
            failures.append(
                f"encoder/schema type mismatch on channel {channel} (kCh{name}): "
                f"encoder={type_id} schema={field['type']}.\n"
                "        The uplink would decode under the wrong field, silently."
            )
        if field["size"] != size:
            failures.append(
                f"encoder/schema size mismatch on channel {channel} (kCh{name}): "
                f"encoder={size} schema={field['size']}.\n"
                "        There is no length prefix on the wire — a wrong width shifts "
                "every field after it."
            )
        if bool(field["signed"]) != signed:
            failures.append(
                f"encoder/schema signedness mismatch on channel {channel} (kCh{name}): "
                f"encoder={'signed' if signed else 'unsigned'} "
                f"schema={'signed' if field['signed'] else 'unsigned'}.\n"
                "        Negative readings would arrive as large positive ones."
            )

    missing = sorted(set(by_channel) - {c for c, _, _, _ in encoded.values()})
    if missing:
        failures.append(
            f"schema declares channel(s) {missing} that src/payload.cpp never sends. "
            "Either the encoder lost a field or the schema is stale."
        )

    failures.extend(_check_total_length(encoded))
    failures.extend(_check_divisor_consistency(schema))

    print(f"{DIM}   encoder: {len(encoded)} emitted field(s) checked against "
          f"the schema{RESET}")
    return failures


def _check_total_length(encoded) -> list:
    """Does the buffer the firmware declares actually hold what the firmware emits?

    Nothing checked this. Every field is [channel][type][value] with no length prefix, and
    put_*() drops a field that does not fit by incrementing m_dropped — so a buffer one byte
    too small does not fail, it silently sheds the lowest-priority field on every single
    uplink. That reads as a sensor fault from the network side and would be chased in the
    wrong place entirely.
    """
    failures = []
    if not ENCODER_HEADER.is_file():
        return [f"encoder header not found: {ENCODER_HEADER.relative_to(REPO_ROOT)}"]

    header = ENCODER_HEADER.read_text(encoding="utf-8")
    m_max = _MAX_BYTES_RE.search(header)
    m_min = _MIN_DR_BYTES_RE.search(header)
    if not m_max or not m_min:
        return ["Could not parse kMaxPayloadBytes / kMinDataRatePayloadBytes from "
                "src/payload.h — the total-length check went blind."]

    declared = int(m_max.group(1))
    min_dr = int(m_min.group(1))
    needed = sum(_HEADER_BYTES + size for _, _, size, _ in encoded.values())

    if needed > declared:
        consequence = (
            "        Too small: put_*() sheds the lowest-priority field on every uplink "
            "rather than failing,\n"
            "        which reads as a sensor fault and gets chased in the wrong place."
        )
    elif needed < declared:
        consequence = (
            "        Too large: harmless on the wire, but payload.h's own field-count "
            "comment and the\n"
            "        airtime budget are now both wrong."
        )
    else:
        consequence = None

    if consequence is not None:
        failures.append(
            f"payload length mismatch — the {len(encoded)} emitted field(s) need "
            f"{needed} bytes, kMaxPayloadBytes is {declared}.\n"
            f"{consequence}"
        )

    if min_dr > declared:
        failures.append(
            f"kMinDataRatePayloadBytes ({min_dr}) exceeds kMaxPayloadBytes ({declared}) — "
            "the slow-data-rate floor cannot be larger than the buffer."
        )

    print(f"{DIM}   encoder: {needed} byte(s) across {len(encoded)} field(s), "
          f"buffer {declared}, DR0 floor {min_dr}{RESET}")
    return failures


def _check_divisor_consistency(schema) -> list:
    """Two schema fields sharing a decoder type must agree on every property of that type.

    Gate 2 checks each field against the decoder independently, so two fields sharing a type
    are each compared to the same decoder entry and a disagreement between *them* is not
    reachable from that direction. It matters here because channels 3 and 24 — air temperature
    and pack temperature — both use type 103, and payload.cpp:101-106 records an open question
    about whether the pack reports tenths or whole degrees. If that is ever resolved by
    editing one field's divisor, the two would silently disagree and one of them would decode
    off by a factor of ten.
    """
    failures = []
    by_type = {}
    for f in schema.get("fields") or []:
        by_type.setdefault(f["type"], []).append(f)

    for type_id, group in sorted(by_type.items()):
        if len(group) < 2:
            continue
        for attr in ("size", "signed", "divisor"):
            values = {str(f[attr]) for f in group}
            if len(values) > 1:
                channels = ", ".join(str(f["channel"]) for f in group)
                failures.append(
                    f"schema fields on channels {channels} all use type {type_id} but "
                    f"disagree on {attr}: {sorted(values)}.\n"
                    "        One decoder type has one behavior. Whichever field is wrong "
                    "decodes off by that factor, as a plausible number."
                )
    return failures


def _first_line(text) -> str:
    if not text:
        return ""
    s = " ".join(str(text).split())
    return s if len(s) <= 150 else s[:147] + "..."


def _report(failures, warnings, blocked=()) -> None:
    for b in blocked:
        print(f"{RED}BLOCKED{RESET} {b}")
    for w in warnings:
        print(f"{YELLOW}WARN{RESET} {w}")
    for f in failures:
        print(f"{RED}FAIL{RESET} {f}")
    if failures:
        print(f"\n{RED}Parity BROKEN — {len(failures)} issue(s). "
              f"Do not flash a field node.{RESET}")
    else:
        note = f" ({len(warnings)} call-out(s))" if warnings else ""
        if blocked:
            print(f"{YELLOW}PASS, BUT BLOCKED{RESET} payload schema matches the TTN formatter"
                  f"{note} — but {len(blocked)} field(s) have NO AGREED MEANING on the wire. "
                  f"This is not a clean run; see preflight's 'payload contract' step.")
        else:
            print(f"{GREEN}PASS{RESET} payload schema matches the TTN formatter{note}")


if __name__ == "__main__":
    sys.exit(main())
