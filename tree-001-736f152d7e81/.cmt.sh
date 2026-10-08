#!/usr/bin/env bash
set -euo pipefail
cd ~/Documents/GitHub/rak-sensor-node-but-better

gh issue comment 20 --body-file - >/dev/null <<'EOF'
**The premise changed.** The no-solar 910406 was out of stock, so the shell in hand is the
solar Unify variant. Two consequences for this issue:

1. **Entry count is unknown again.** The option analysis above assumed 910406's single M8
   plus RP-SMA. Count what this shell actually has before choosing — if it already has two
   entries, option A stops requiring a drilled hole and becomes the obvious choice.
2. **It is roomier inside,** with space for the buck converter. That removes the main
   argument for option B's external junction box, which existed partly to keep the main
   enclosure uncluttered.

Option C (reuse the Sensor Hub shell) is probably moot now — this shell is larger than the
one C was proposed to escape.

Separately: the lid panel should be left **unconnected**. Reasoning recorded in
`docs/HARDWARE.md` — the RAK9154 has to stay the power source because the firmware reads its
telemetry over one-wire, and that is where the low-voltage gate and four uplink fields come
from. Worth confirming the lid lead is capped rather than left bare inside the box.
EOF
echo "commented"
