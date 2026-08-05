/*
 * RS-485 bus scanner — a bench instrument, not part of the node.
 *
 * Compiled only into the `busscan` environment (FEATURE_BUS_SCAN=1) and never into a field
 * image. It sweeps baud rates and slave addresses and prints raw bytes, which is exactly the
 * behavior a deployed node must not have: it drives the bus with addresses the node has no
 * business talking to, and it holds the line for far longer than a read.
 *
 * It exists because the RK900 driver collapses every unproductive outcome into "timeout",
 * which cannot distinguish a sensor that said nothing from one that answered in a framing
 * this build cannot parse. Those two have opposite fixes — a code change versus a trip to
 * the bench with a meter — and guessing between them is how days get lost.
 *
 * It has already done its job: it established the RK900 answers at 9600 rather than the
 * 4800 the datasheet implies, and confirmed the register map (ADR-0006, docs/EVIDENCE.md).
 * It is kept because the next sensor, or the next harness, will raise the same question.
 */

#pragma once

#include "../build_features.h"

#if FEATURE_BUS_SCAN

namespace diagnostics {

// One full sweep: every candidate baud rate against every candidate slave address, printing
// what came back. Prints and returns; produces no reading and builds no uplink, so nothing
// downstream can mistake a raw byte for a measurement.
void bus_scan();

} // namespace diagnostics

#endif // FEATURE_BUS_SCAN
