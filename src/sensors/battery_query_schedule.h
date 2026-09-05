#pragma once

#include <stdint.h>

struct BatteryFollowupQueryPlan {
    uint8_t first;
    uint8_t second;
};

// A missed direct probe does not identify the pack's state. Keep the remembered-address query
// and its alternate bounded to one attempt each, including a second query to the provisioned
// address after the recovery window has elapsed.
//
// CITE(bench): docs/EVIDENCE.md 2026-08-30 — the retry-bearing SDA revision `7ad5daa`
//   obtained live pack readings; the direct probe may miss even when the pack is present.
// CITE(spec): docs/FIRMWARE_SPEC.md §2.2 and §7 H7 — a silent BMS must remain recoverable
//   without turning every cycle into the expensive full ladder.
// CITE(policy): docs/POWER_BUDGET.md — lost data is preferable to a terminal failure, but
//   bounded recovery work remains necessary to keep an unattended node self-recovering.
constexpr BatteryFollowupQueryPlan battery_followup_query_plan(uint8_t remembered_id,
                                                               uint8_t probe_id,
                                                               uint8_t broadcast_id)
{
    return BatteryFollowupQueryPlan{
        remembered_id, (remembered_id == broadcast_id) ? probe_id : broadcast_id};
}
