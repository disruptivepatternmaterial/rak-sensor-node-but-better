#!/usr/bin/env python3
"""Enforce the heartbeat contract in .cursor/rules/00-agent-liveness.mdc.

The rule asks for a progress line every 2-4 minutes. Nothing measured that, so it
was advisory and got skipped. This hook measures it.

  afterAgentResponse -> the agent spoke; reset the clock
  postToolUse        -> if the agent has been silent past the threshold, attach
                        additional_context telling it to emit a status line now

Silence is measured from the last agent message, not from the last tool call, so a
long chain of tool calls with no user-visible text is exactly what trips it.

State lives in TMPDIR keyed by conversation id so parallel chats do not share a
clock. Every failure path exits 0 with no output: a broken heartbeat must never
block real work.
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

# Field name varies across hook events; take the first that carries a value.
CONVERSATION_KEYS = ("conversation_id", "conversationId", "chat_id", "chatId",
                     "thread_id", "threadId", "session_id", "sessionId")

NUDGE = (
    "LIVENESS: {elapsed:.0f}s since your last user-visible message, over the "
    "{limit:.0f}s limit in .cursor/rules/00-agent-liveness.mdc. Before your next "
    "tool call, emit one line of plain text: what just finished, what is running "
    "now. One concrete fact - a count, a filename, a SHA. Not 'still working'."
)


def state_path(payload):
    ident = ""
    for key in CONVERSATION_KEYS:
        value = payload.get(key)
        if isinstance(value, str) and value:
            ident = value
            break
    if not ident:
        ident = os.environ.get("PWD", "global")
    digest = hashlib.sha1(ident.encode("utf-8")).hexdigest()[:16]
    return pathlib.Path(tempfile.gettempdir()) / f"cursor-liveness-{digest}.json"


def read_state(path):
    try:
        return json.loads(path.read_text())
    except Exception:
        return {}


def write_state(path, state):
    # Atomic so a postToolUse racing an afterAgentResponse cannot read a half file.
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(state))
    os.replace(tmp, path)


def main():
    event = sys.argv[1] if len(sys.argv) > 1 else ""

    try:
        payload = json.loads(sys.stdin.read() or "{}")
    except Exception:
        payload = {}
    if not isinstance(payload, dict):
        payload = {}

    if os.environ.get("CURSOR_LIVENESS_DEBUG"):
        try:
            debug = pathlib.Path(tempfile.gettempdir()) / "cursor-liveness-debug.jsonl"
            with debug.open("a") as fh:
                fh.write(json.dumps({"event": event, "keys": sorted(payload)}) + "\n")
        except Exception:
            pass

    path = state_path(payload)
    now = time.time()
    state = read_state(path)

    if event == "afterAgentResponse":
        state["last_spoke"] = now
        state.pop("last_nudge", None)
        write_state(path, state)
        return

    if event != "postToolUse":
        return

    last_spoke = state.get("last_spoke")
    if not isinstance(last_spoke, (int, float)):
        state["last_spoke"] = now
        write_state(path, state)
        return

    elapsed = now - last_spoke
    if elapsed < SILENCE_LIMIT_S:
        return

    last_nudge = state.get("last_nudge")
    if isinstance(last_nudge, (int, float)) and now - last_nudge < RENUDGE_S:
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
