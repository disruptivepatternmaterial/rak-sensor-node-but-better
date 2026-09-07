#!/usr/bin/env python3
"""Require operator approval before an agent writes to docs/.

Nine dead pads were an electrical problem. The docs were a separate one: agents wrote
into docs/ unchecked until BUILD.md cited an ADR that was never written and named three
mutually exclusive parts for one job. The measurements were never the weak point.

This gate returns "ask" for any file tool whose target is under docs/, so the operator
sees the write before it lands. Everything else is allowed untouched.

Path detection is deliberate rather than a substring match on the whole payload: source
files legitimately cite docs/ paths in comments, and a gate that fires on every such edit
would be turned off within a day.

**No tool-name matcher, on purpose.** hooks.json used to filter this gate to a hard-coded
list of file tools, so any file tool absent from that list — a renamed one, a new one, a
patch-style variant — wrote into docs/ without asking. The list bought nothing, because
collect_paths() below already ignores every call that does not name a docs/ path, and it
cost a hole that widened silently whenever the tool surface changed. Removed 2026-09-07.

**Known gap, not closed by this hook:** it is a preToolUse hook, so it sees file tools
only. An agent that edits docs/ by running `python3` or `sed` from the shell goes to
beforeShellExecution, where only flash_gate.py is listening. Writing docs through a shell
command therefore bypasses this gate entirely.
"""

import json
import posixpath
import sys

# Keys the file tools use to name their target. Collected recursively so the hook does
# not depend on the exact nesting of the preToolUse payload.
PATH_KEYS = {
    "path",
    "file_path",
    "filePath",
    "absolute_path",
    "target_file",
    "target_notebook",
}

GUARDED_PREFIX = "docs/"


def collect_paths(node: object, found: list[str]) -> None:
    """Walk the payload and collect every value stored under a path-like key."""
    if isinstance(node, dict):
        for key, value in node.items():
            if key in PATH_KEYS and isinstance(value, str) and value:
                found.append(value)
            else:
                collect_paths(value, found)
    elif isinstance(node, list):
        for item in node:
            collect_paths(item, found)


def is_guarded(raw_path: str) -> bool:
    """True when the path names something inside the repo's docs/ tree."""
    normalized = posixpath.normpath(raw_path.replace("\\", "/"))
    # Absolute paths arrive as /Users/.../rak-sensor-node-but-better/docs/FILE.md;
    # relative ones as docs/FILE.md. Both must be caught, and neither may be matched
    # by a bare "docs" appearing elsewhere in the tree.
    return normalized == "docs" or f"/{GUARDED_PREFIX}" in f"/{normalized}/"


def main() -> None:
    raw = sys.stdin.read()

    try:
        payload = json.loads(raw)
    except (ValueError, TypeError):
        # Unparseable input is not a reason to wave a write through, but it is also not
        # evidence of a docs write. Ask only when the text plainly names a docs path.
        if GUARDED_PREFIX in raw:
            print(json.dumps({
                "permission": "ask",
                "agent_message": (
                    "The docs gate could not parse this tool call and the payload names "
                    "a docs/ path. The operator must approve it."
                ),
                "user_message": (
                    "DOCS GATE: unreadable tool call that mentions docs/. Approve only if "
                    "you expect a documentation write."
                ),
            }))
            return
        print(json.dumps({"permission": "allow"}))
        return

    targets: list[str] = []
    collect_paths(payload, targets)
    guarded = [target for target in targets if is_guarded(target)]

    if guarded:
        listing = ", ".join(sorted(set(guarded)))
        print(json.dumps({
            "permission": "ask",
            "agent_message": (
                "Writes under docs/ require the operator's explicit approval. "
                f"Pending target(s): {listing}"
            ),
            "user_message": f"DOCS GATE: an agent is writing to {listing}",
        }))
        return

    print(json.dumps({"permission": "allow"}))


if __name__ == "__main__":
    main()
