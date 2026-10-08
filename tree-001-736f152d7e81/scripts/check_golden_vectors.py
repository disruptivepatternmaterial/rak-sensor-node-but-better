#!/usr/bin/env python3
"""Push real encoder output through the real TTN decoder and check what comes out.

check_decoder_parity.py compares the encoder to the schema, and the schema to the
decoder. Both comparisons can pass while the chain is still broken, because they share an
assumption: that the schema describes the decoder completely. It does not. The decoder
adds a 230-degree installation offset to wind direction, which no schema field expresses.
A field-by-field comparison cannot see that; running the bytes can.

So this gate does not inspect anything. It compiles the encoder, asks it for the bytes it
would actually transmit, hands them to the JavaScript that ingest actually runs, and
compares the decoded result against values written by hand from the decoder's documented
behavior in tools/golden_vectors.json.

Requires g++ and node. Both are present on the build host and in CI; the workstation has
no node, so there the gate skips rather than failing (see .cursor/rules/10-environments.mdc).

Exit codes: 0 = the chain holds or the gate could not run, 1 = the chain is broken.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check_decoder_parity import find_decoder, load_yaml, SCHEMA_PATH  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
EMITTER_SRC = REPO_ROOT / "tools" / "emit_vectors.cpp"
EXPECTED_PATH = REPO_ROOT / "tools" / "golden_vectors.json"

GREEN, RED, YELLOW, BLUE, DIM, RESET = (
    ("\033[32m", "\033[31m", "\033[33m", "\033[34m", "\033[2m", "\033[0m")
    if sys.stdout.isatty() else ("", "", "", "", "", "")
)

# Decoded values are floats built from integer counts and a divisor, so they land on exact
# tenths and hundredths. Anything further out is a real disagreement, not rounding.
TOLERANCE = 1e-6


def emit_vectors() -> dict[str, str]:
    """Compile the encoder and ask it for the bytes it would transmit."""
    with tempfile.TemporaryDirectory() as tmp:
        binary = Path(tmp) / "emit_vectors"
        compile_cmd = [
            "g++", "-std=gnu++17", "-O0",
            "-I", str(REPO_ROOT / "src"),
            str(EMITTER_SRC),
            str(REPO_ROOT / "src" / "payload.cpp"),
            "-o", str(binary),
        ]
        proc = subprocess.run(compile_cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            raise RuntimeError(f"could not build the emitter:\n{proc.stderr}")

        proc = subprocess.run([str(binary)], capture_output=True, text=True)
        if proc.returncode != 0:
            raise RuntimeError(f"the emitter failed to run:\n{proc.stderr}")

    vectors = {}
    for line in proc.stdout.splitlines():
        if not line.strip():
            continue
        name, _, hex_bytes = line.partition("\t")
        vectors[name] = hex_bytes
    return vectors


def decode(decoder_path: Path, vectors: dict[str, str]) -> dict[str, dict]:
    """Run every vector through the decoder in one node process."""
    harness = f"""
const fs = require('fs');
const src = fs.readFileSync({json.dumps(str(decoder_path))}, 'utf8');
// The formatter is written for TTN, which evaluates it as a bare script and calls
// decodeUplink itself. Nothing exports it, so the same thing is done here.
const sandbox = {{}};
(new Function('module', 'exports', src + '\\nthis.decodeUplink = decodeUplink;'))
  .call(sandbox, {{}}, {{}});

const vectors = {json.dumps(vectors)};
const out = {{}};
for (const [name, hex] of Object.entries(vectors)) {{
  const bytes = [];
  for (let i = 0; i < hex.length; i += 2) bytes.push(parseInt(hex.substr(i, 2), 16));
  out[name] = sandbox.decodeUplink({{ bytes: bytes, fPort: 2 }});
}}
process.stdout.write(JSON.stringify(out));
"""
    proc = subprocess.run(["node", "-e", harness], capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(f"the decoder failed to run:\n{proc.stderr}")
    return json.loads(proc.stdout)


def compare(name: str, expected: dict, actual: dict) -> list[str]:
    failures = []
    want = {k: v for k, v in expected.items() if not k.startswith("_")}

    if actual.get("errors"):
        failures.append(f"{name}: the decoder rejected the uplink — {actual['errors']}")
        return failures

    got = actual.get("data", {})

    for key, value in want.items():
        if key not in got:
            failures.append(f"{name}: {key} missing from the decoded output")
        elif isinstance(value, (int, float)) and abs(float(got[key]) - float(value)) > TOLERANCE:
            failures.append(f"{name}: {key} decoded as {got[key]}, expected {value}")
        elif not isinstance(value, (int, float)) and got[key] != value:
            failures.append(f"{name}: {key} decoded as {got[key]!r}, expected {value!r}")

    # An unexpected field is as much a defect as a missing one: it means the node is
    # transmitting something nobody downstream is prepared for.
    for key in got:
        if key not in want:
            failures.append(f"{name}: decoded an unexpected {key} = {got[key]}")

    return failures


def main() -> int:
    print(f"{BLUE}== golden vectors through the live decoder =={RESET}")

    for tool in ("g++", "node"):
        if shutil.which(tool) is None:
            print(f"{YELLOW}SKIP{RESET} no {tool} here — this gate runs on the build host "
                  f"and in CI")
            return 0

    schema = load_yaml(SCHEMA_PATH)
    decoder_path, source = find_decoder(schema)
    if decoder_path is None:
        print(f"{RED}FAIL{RESET} no decoder found, live or pinned")
        return 1

    expected = json.loads(EXPECTED_PATH.read_text(encoding="utf-8"))
    expected = {k: v for k, v in expected.items() if not k.startswith("_")}

    try:
        vectors = emit_vectors()
        decoded = decode(decoder_path, vectors)
    except RuntimeError as exc:
        print(f"{RED}FAIL{RESET} {exc}")
        return 1

    print(f"{DIM}   decoder: {decoder_path}{RESET}")
    print(f"{DIM}   source:  {source}{RESET}")
    print(f"{DIM}   vectors: {len(vectors)} case(s) emitted by the real encoder{RESET}")

    failures = []

    # A case defined on one side only is the failure this whole gate exists to prevent:
    # something added to the encoder and never checked against ingest.
    for name in sorted(set(vectors) - set(expected)):
        failures.append(f"{name}: emitted by tools/emit_vectors.cpp with no entry in "
                        f"tools/golden_vectors.json")
    for name in sorted(set(expected) - set(vectors)):
        failures.append(f"{name}: expected in tools/golden_vectors.json but never emitted")

    for name in sorted(set(vectors) & set(expected)):
        if not vectors[name]:
            # No bytes at all. Correct when nothing was valid, and the absence of any
            # expected field is how that is declared.
            if {k for k in expected[name] if not k.startswith("_")}:
                failures.append(f"{name}: encoder produced no bytes but fields were expected")
            continue
        failures.extend(compare(name, expected[name], decoded[name]))

    if failures:
        print(f"{RED}FAIL{RESET} the encoder and the decoder disagree:")
        for f in failures:
            print(f"  - {f}")
        return 1

    checked = sum(len([k for k in v if not k.startswith("_")]) for v in expected.values())
    print(f"{GREEN}PASS{RESET} {checked} decoded value(s) across {len(vectors)} vector(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
