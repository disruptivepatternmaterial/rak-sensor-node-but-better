#!/usr/bin/env python3
"""Require operator approval for edits to the hook files through file tools.

The flash gate in flash_gate.py only holds if an agent cannot quietly rewrite or delete
it with Write/StrReplace/Delete. Any file operation whose input mentions .cursor/hooks
returns "ask" so the operator sees it. Everything else is allowed untouched.
"""

import json
import sys


def main() -> None:
    raw = sys.stdin.read()
    if ".cursor/hooks" in raw:
        print(json.dumps({
            "permission": "ask",
            "user_message": (
                "FLASH GATE: an agent is editing the hook files that gate flashing."
            ),
            "agent_message": (
                "Edits to .cursor/hooks require the operator's explicit approval."
            ),
        }))
        return
    print(json.dumps({"permission": "allow"}))


if __name__ == "__main__":
    main()
