#!/usr/bin/env python3
"""Gate every flash-capable shell command behind explicit operator approval.

Nine GPIO pads and three cores were destroyed by diagnostic firmware that agents flashed
over SSH without the operator ever asking for it (#102). Rules and warning comments were
already in place and were ignored by exactly the sessions that did the flashing. This hook
is the control that does not depend on being read: any command that could put an image on
a board — directly or wrapped in ssh — returns permission "ask", so it cannot run until
the operator clicks approve on that specific command.

Compiles are not gated: `pio run` without an upload target cannot touch a pad.
Every failure path in this hook must block (hooks.json sets failClosed: true).
"""

import json
import re
import sys

# Matched case-insensitively against the full command string, which catches
# `ssh host 'zsh -l -c "... pio run -t upload ..."'` the same as a local run.
FLASH_PATTERNS = [
    # Any pio/platformio invocation that mentions upload or erase. Deliberately broad:
    # a false positive costs one approval click, a false negative costs a pad.
    r"\b(pio|platformio)\b.*\b(upload|erase)\b",
    r"\badafruit-nrfutil\b",                    # serial DFU flasher
    r"\bnrfutil\b",
    r"\bnrfjprog\b",                            # SWD flashers / debug probes
    r"\bpyocd\b",
    r"\bopenocd\b",
    r"\bjlink(exe)?\b",
    r"\bflash\.sh\b",                           # scripts/flash.sh, any path
    r"\bdfu\b.*\bserial\b|\bserial\b.*\bdfu\b", # adafruit-nrfutil dfu serial forms
    r"--touch[=\s]+1200",                       # 1200-baud touch = DFU entry
    r"\.uf2\b.*/Volumes/|/Volumes/.*\.uf2\b",   # copying a UF2 onto a mounted bootloader
]

# Editing the gate itself (or the hook config) via shell also needs the operator.
SELF_PATTERNS = [r"\.cursor/hooks"]


def main() -> None:
    raw = ""
    try:
        raw = sys.stdin.read()
        command = json.loads(raw).get("command", raw)
    except Exception:
        command = raw

    for pattern in FLASH_PATTERNS:
        if re.search(pattern, command, re.IGNORECASE):
            print(json.dumps({
                "permission": "ask",
                "user_message": (
                    "FLASH GATE: this command can put firmware on a board. "
                    "Approve only if you asked for this specific flash."
                ),
                "agent_message": (
                    "Flashing requires the operator's explicit approval (#102: nine pads "
                    "and three cores died under unauthorized agent flashes). Do not "
                    "rephrase or re-wrap the command to evade this gate; state what you "
                    "want to flash and why, and wait for the operator."
                ),
            }))
            return

    for pattern in SELF_PATTERNS:
        if re.search(pattern, command, re.IGNORECASE):
            print(json.dumps({
                "permission": "ask",
                "user_message": (
                    "FLASH GATE: this command touches the flash-gate hooks themselves."
                ),
                "agent_message": (
                    "Changing the flash gate requires the operator's explicit approval."
                ),
            }))
            return

    print(json.dumps({"permission": "allow"}))


if __name__ == "__main__":
    main()
