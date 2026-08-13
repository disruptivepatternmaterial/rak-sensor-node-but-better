# Downlink command matrix

How to prove the downlink path end to end without sitting on an SSH session for two hours.

`scripts/downlink_matrix.sh` drives every downlink case in
[`docs/FIRMWARE_SPEC.md`](FIRMWARE_SPEC.md) §4 against the board on the build host and records
each one as `PASS`, `FAIL` or `TIMEOUT` with the **verbatim console line** that decided it.

## Why a script and not a session

A Class A node hears a downlink only in the RX window that follows an uplink
([CITE(spec): LoRaWAN Link Layer, Class A receive slots][CIT-LW-LINK]), so a single case
costs up to two reporting intervals. At the 900 s fair-use floor
([CITE(policy): TTN Fair Use Policy][CIT-TTN-FUP]) the full matrix is roughly two hours of wall
clock. Two attempts to drive it by hand died with the SSH connection and produced no results.
This one runs under `nohup`, survives `SIGHUP`, and is polled rather than watched.

## Running it

On the build host, with a serial reader already appending to a console log (do **not** start a
second reader — one process per `/dev/cu.usbmodem*`, check `lsof` first):

```bash
cd ~/Documents/GitHub/rak-sensor-node-but-better
nohup scripts/downlink_matrix.sh run >>/tmp/downlink_matrix.log 2>&1 &
```

Environment overrides: `APP`, `DEV`, `CONSOLE`, `LOG`, `EVLOG`, `TIMEOUT_LONG`, `TIMEOUT_SHORT`.

## Polling it

| Want | Do |
|---|---|
| Is it alive, what has finished | `scripts/downlink_matrix.sh status` |
| Results only | `grep -E '=== (CASE . RESULT=|MATRIX )' /tmp/downlink_matrix.log` |
| Is it alive but slow | `grep heartbeat /tmp/downlink_matrix.log \| tail -3` (one per 60 s while waiting) |
| Did TTN actually send it | `grep -i downlink /tmp/downlink_matrix_events.log \| tail` |
| Finished? | `grep -c 'MATRIX DONE' /tmp/downlink_matrix.log` |

Sentinels are stable and greppable: `=== MATRIX START ===`, `=== CASE <id> START ===`,
`=== CASE <id> RESULT=PASS|FAIL|TIMEOUT ===`, `=== MATRIX DONE ===`.

## The cases

Case **b** runs first on purpose: it lowers the reporting interval to the 900 s floor, which
halves the latency of every case after it, and it is a required case in its own right.

| Case | Push | Expected console line (read out of `src/radio.cpp`) |
|---|---|---|
| b | FPort 10, `0100000384` | `downlink — set interval 900 s`, then `config  : interval now 900 s`, then a `wait    : 900 s` line |
| a | FPort 10, `03` | `downlink — status requested`, then `sent 35 bytes on port 2` |
| c | FPort 10, `010000` | `downlink — opcode 0x01 with wrong length 3, ignored` — **not** `unknown opcode` ([#64]) |
| d | FPort 10, `03000000` | `downlink — opcode 0x03 with wrong length 4, ignored` ([#63]) |
| e | FPort 10, `7F` | `downlink — unknown opcode 0x7F, ignored` |
| f | FPort **1**, `03` | `radio   : ignoring 1 bytes on port 1` |
| g | FPort 10, `03` twice | `status requested` twice, in **separate** cycles — one per RX window |
| h | nothing | cycle numbers monotonic across the whole run, no boot banner mid-run |

Every expected string was read from `src/radio.cpp` and `src/config.cpp` before being encoded.
If a firmware message is reworded, this table and the script both change or the matrix reports
a false `TIMEOUT`.

## What it deliberately does not do

- **It never fabricates.** A case whose line never appears is `TIMEOUT` with an empty
  observation, never a pass and never a guess at the cause.
- **It writes no evidence entry.** `docs/EVIDENCE.md` gets an entry only after the run
  finishes, written by whoever reads the log — recording a launch is how a false soak claim
  propagated through three documents on 2026-08-12.
- **It cannot prove [#62].** A pack already latched at `0x01` never exercises the re-latch path.

[CIT-TTN-FUP]: CITATIONS.md
[CIT-LW-LINK]: CITATIONS.md
[#62]: https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/62
[#63]: https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/63
[#64]: https://github.com/disruptivepatternmaterial/rak-sensor-node-but-better/issues/64
