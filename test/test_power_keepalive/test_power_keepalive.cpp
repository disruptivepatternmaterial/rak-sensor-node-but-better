#include <unity.h>

#include "power.h"

using power::detail::KeepaliveClock;

void setUp() {}
void tearDown() {}

void test_alternating_low_and_in_band_reaches_keepalive()
{
    KeepaliveClock clock;
    clock.start(false);

    for (uint16_t cycle = 1; cycle < power::kNoEvidenceKeepaliveCycles; ++cycle) {
        const bool in_band = (cycle % 2) == 0;
        clock.advance(in_band);
        TEST_ASSERT_FALSE(clock.due(true));
    }

    // The 24th held cycle is in-band. Low cycles suppressed their own opportunity but did not
    // erase elapsed time, so this one must open the Class A downlink window.
    clock.advance(true);
    TEST_ASSERT_TRUE(clock.due(true));
}

void test_continuously_low_never_transmits()
{
    KeepaliveClock clock;
    clock.start(false);

    for (uint16_t cycle = 0; cycle < 100; ++cycle) {
        clock.advance(false);
        TEST_ASSERT_FALSE(clock.due(true));
    }

    TEST_ASSERT_FALSE(clock.armed());
    TEST_ASSERT_EQUAL_UINT16(power::kNoEvidenceKeepaliveCycles, clock.held_cycles());
}

void test_keepalive_restarts_the_full_interval()
{
    KeepaliveClock clock;
    clock.start(true);

    for (uint16_t cycle = 0; cycle < power::kNoEvidenceKeepaliveCycles; ++cycle) {
        clock.advance(true);
    }
    TEST_ASSERT_TRUE(clock.due(true));

    clock.note_sent();
    TEST_ASSERT_FALSE(clock.due(true));

    for (uint16_t cycle = 1; cycle < power::kNoEvidenceKeepaliveCycles; ++cycle) {
        clock.advance(true);
        TEST_ASSERT_FALSE(clock.due(true));
    }

    clock.advance(true);
    TEST_ASSERT_TRUE(clock.due(true));
}

void test_elapsed_hold_does_not_bypass_current_low_evidence()
{
    KeepaliveClock clock;
    clock.start(true);

    for (uint16_t cycle = 0; cycle < power::kNoEvidenceKeepaliveCycles; ++cycle) {
        clock.advance(true);
    }
    TEST_ASSERT_TRUE(clock.due(true));

    // A current measured-low reading wins even after the interval expires.
    clock.advance(false);
    TEST_ASSERT_FALSE(clock.due(true));

    // Time was retained, so the next permissible cycle is due immediately.
    clock.advance(true);
    TEST_ASSERT_TRUE(clock.due(true));
}

void test_disengaged_hold_never_reports_due()
{
    KeepaliveClock clock;
    clock.start(true);
    for (uint16_t cycle = 0; cycle < power::kNoEvidenceKeepaliveCycles; ++cycle) {
        clock.advance(true);
    }

    TEST_ASSERT_FALSE(clock.due(false));
    clock.clear();
    TEST_ASSERT_FALSE(clock.armed());
    TEST_ASSERT_EQUAL_UINT16(0, clock.held_cycles());
}

void test_intermittent_evidence_cannot_restart_an_existing_hold()
{
    KeepaliveClock clock;
    clock.start(true);

    // Twenty held in-band cycles, then four unreadable cycles. The transition to no-evidence
    // changes permission only; it must not restart the hold clock.
    for (uint16_t cycle = 0; cycle < 20; ++cycle) {
        clock.advance(true);
    }
    for (uint16_t cycle = 0; cycle < 4; ++cycle) {
        clock.advance(false);
    }
    clock.set_permitted(true);

    TEST_ASSERT_EQUAL_UINT16(power::kNoEvidenceKeepaliveCycles, clock.held_cycles());
    TEST_ASSERT_TRUE(clock.due(true));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_alternating_low_and_in_band_reaches_keepalive);
    RUN_TEST(test_continuously_low_never_transmits);
    RUN_TEST(test_keepalive_restarts_the_full_interval);
    RUN_TEST(test_elapsed_hold_does_not_bypass_current_low_evidence);
    RUN_TEST(test_disengaged_hold_never_reports_due);
    RUN_TEST(test_intermittent_evidence_cannot_restart_an_existing_hold);
    return UNITY_END();
}
