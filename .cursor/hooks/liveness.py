#!/usr/bin/env python3
"""Enforce the heartbeat contract in .cursor/rules/00-agent-liveness.mdc.

The rule asks for a progress line every 2-4 minutes. Nothing measured that, so it
stayed advisory and got skipped on exactly the long tasks it was written for. This
hook measures it and injects a reminder when the agent goes quiet.

Runs on postToolUse only. `afterAgentResponse` was the obvious reset signal and it
is useless here: measured 2026-08-04, it fires zero times while a turn is still in
flight, which is the entire window we care about. So the reset signal is the
transcript itself -- Cursor hands us `transcript_path`, the jsonl grows live, and a
rising count of assistant text blocks means the agent spoke.

The transcript carries no timestamps, hence the two-part state: the count answers
"did it speak", the wall clock answers "how long ago".

Every failure path exits 0 with no output. A broken heartbeat must never block work.
"""

import hashlib
import json
import os
import pathlib
import sys
import tempfile
import time

SILENCE_LIMIT_S = float(os.environ.get("CURSOR_LIVENESS_SILENCE_S", "180"))
RENUDGE_S = float(os.environ.get("CURSOR_LIVENESS_RENUDGE_S", "120"))

NUDGE = (
    "LIVENESS: {elapsed:.0f}s since your last user-visible message, past the "
    "{limit:.0f}s limit in .cursor/rules/00-agent-liveness.mdc. Emit one line of "
    "plain text before your next tool call: what just finished, what is running "
    "now. One concrete fact -- a count, a filename, a SHA. Not 'still working'."
)


def state_path(payload):
    ident = payload.get("conversation_id") or payload.get("session_id") or "global"
    digest = hashlib.sha1(str(ident).encode("utf-8")).hexdigest()[:16]
    return pathlib.Path(tempfile.gettempdir()) / f"cursor-liveness-{digest}.json"


def read_state(path):
    try:
        state = json.loads(path.read_text())
        return state if isinstance(state, dict) else {}
    except Exception:
        return {}


def write_state(path, state):
    # Atomic: concurrent tool calls in one conversation must not read a half file.
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(state))
    os.replace(tmp, path)


def count_agent_messages(transcript):
    """Assistant content blocks of type 'text' in the transcript so far."""
    total = 0
    with open(transcript, "r", errors="replace") as fh:
        for line in fh:
            line = line.strip()
            if not line or '"assistant"' not in line:
                continue
            try:
                row = json.loads(line)
            except Exception:
                continue
            if row.get("role") != "assistant":
                continue
            message = row.get("message")
            if not isinstance(message, dict):
                continue
            for block in message.get("content") or []:
                if isinstance(block, dict) and block.get("type") == "text":
                    total += 1
    return total


def main():
    try:
        payload = json.loads(sys.stdin.read() or "{}")
    except Exception:
        return
    if not isinstance(payload, dict):
        return

    transcript = payload.get("transcript_path")
    if not transcript or not os.path.isfile(transcript):
        return

    path = state_path(payload)
    state = read_state(path)
    now = time.time()

    # Size is a cheap change detector: the jsonl is append-only, so an unchanged
    # size means no new rows and therefore no new agent text. Skips the parse on
    # the common case of a tool-call chain with no speech in it.
    size = os.path.getsize(transcript)
    if size == state.get("size"):
        count = state.get("count", 0)
    else:
        count = count_agent_messages(transcript)

    last_count = state.get("count")
    last_spoke = state.get("last_spoke")

    first_run = not isinstance(last_count, int) or not isinstance(
        last_spoke, (int, float)
    )
    spoke_since_last_call = not first_run and count > last_count

    if first_run or spoke_since_last_call:
        write_state(path, {"count": count, "size": size, "last_spoke": now})
        return

    elapsed = now - last_spoke
    state.update({"count": count, "size": size})

    last_nudge = state.get("last_nudge")
    throttled = (
        isinstance(last_nudge, (int, float)) and now - last_nudge < RENUDGE_S
    )
    if elapsed < SILENCE_LIMIT_S or throttled:
        write_state(path, state)
        return

    state["last_nudge"] = now
    write_state(path, state)
    json.dump(
        {"additional_context": NUDGE.format(elapsed=elapsed, limit=SILENCE_LIMIT_S)},
        sys.stdout,
    )


if __name__ == "__main__":
    try:
        main()
    except Exception:
        pass
    sys.exit(0)
