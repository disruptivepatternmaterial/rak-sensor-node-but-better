/*
 * One-wire link scanner for the RAK9154 pack — a bench instrument, not part of the node.
 *
 * Compiled only into the `owscan` environment (FEATURE_ONEWIRE_SCAN=1) and never into a
 * field image. FEATURE_BATTERY is 0 in that environment so the scanner and the driver can
 * never contend for the pin.
 *
 * The driver's verdict for a bad cycle is "no reply, 0 bytes", which it produces whenever
 * the line never goes low inside the first-byte window. That one outcome covers two faults
 * with opposite fixes — the pack never hears the request, or the pack answers and the
 * receiver misses it. This measures the pin itself before assuming any protocol, so the two
 * can be told apart, then sweeps addresses and frame types.
 *
 * Deliberately not trusted to validate production behavior. It drives the line itself rather
 * than going through Battery, so agreement between the two is a coincidence to be checked,
 * not an invariant. The wake-byte count is the case in point: this scanner used to send four
 * wake bytes where the driver sends one, which meant every "the pack answers" result it
 * produced had been obtained with framing the production driver never transmits. The pack
 * tolerates both, so nothing was wrong — but nothing was proven about production either.
 * The count now matches the driver so that gap cannot reopen.
 */

#pragma once

#include "../build_features.h"

#if FEATURE_ONEWIRE_SCAN

#include <stdint.h>

namespace diagnostics {

// One full pass: edge census with and without the pull-up, passive listen at each candidate
// baud, then addressed requests across the frame types. Prints and returns; produces no
// reading and builds no uplink.
void onewire_scan(uint8_t pin);

} // namespace diagnostics

#endif // FEATURE_ONEWIRE_SCAN
