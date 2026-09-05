#include <unity.h>

#include "sensors/battery_query_schedule.h"

namespace {

constexpr uint8_t kProbeId     = 0x01;
constexpr uint8_t kBroadcastId = 0xFF;

} // namespace

void setUp() {}
void tearDown() {}

void test_unconfirmed_probe_is_retried_after_recovery_window()
{
    const BatteryFollowupQueryPlan plan =
        battery_followup_query_plan(kBroadcastId, kProbeId, kBroadcastId);

    TEST_ASSERT_EQUAL_UINT8(kBroadcastId, plan.first);
    TEST_ASSERT_EQUAL_UINT8(kProbeId, plan.second);
}

void test_remembered_probe_precedes_broadcast_fallback()
{
    const BatteryFollowupQueryPlan plan =
        battery_followup_query_plan(kProbeId, kProbeId, kBroadcastId);

    TEST_ASSERT_EQUAL_UINT8(kProbeId, plan.first);
    TEST_ASSERT_EQUAL_UINT8(kBroadcastId, plan.second);
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_unconfirmed_probe_is_retried_after_recovery_window);
    RUN_TEST(test_remembered_probe_precedes_broadcast_fallback);
    return UNITY_END();
}
