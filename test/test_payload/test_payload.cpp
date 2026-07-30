/*
 * Payload encoder tests. These run on the build host, not the board.
 *
 * The encoder is worth testing off-target because its failures are silent. The TTN decoder
 * walks the uplink taking each field's width on faith — there is no length prefix and no
 * checksum inside the payload — so a field encoded one byte too wide shifts everything
 * after it and the whole message decodes to plausible-looking wrong numbers. Nothing
 * throws, nothing alerts, and the data is quietly garbage until somebody notices the wind
 * has been 300 m/s for a month.
 *
 * The expected bytes below come from payload/schema.yaml, which is itself checked against
 * the live decoder by scripts/check_decoder_parity.py.
 */

#include <unity.h>

#include "payload.h"
#include "reading.h"

void setUp() {}
void tearDown() {}

// A reading with no valid fields must produce no bytes at all. This is the null policy at
// its most basic: a sensor that did not answer contributes nothing, rather than zeroes
// that are indistinguishable from a real calm, cold, dry day.
void test_empty_reading_encodes_nothing()
{
    Payload  p;
    WeatherReading w;
    p.add(w);

    TEST_ASSERT_TRUE(p.empty());
    TEST_ASSERT_EQUAL_UINT32(0, p.length());
}

void test_partial_reading_omits_only_missing_fields()
{
    Payload        p;
    WeatherReading w;
    w.temperature.set(215); // 21.5 C
    p.add(w);

    // Exactly one field: channel, type, and two value bytes. If this is 8 or 20, some
    // other field is being emitted when it should not be.
    TEST_ASSERT_EQUAL_UINT32(4, p.length());
    TEST_ASSERT_EQUAL_UINT8(3, p.bytes()[0]);   // channel
    TEST_ASSERT_EQUAL_UINT8(103, p.bytes()[1]); // type
}

void test_values_are_big_endian()
{
    Payload        p;
    WeatherReading w;
    w.wind_speed.set(0x1234);
    p.add(w);

    TEST_ASSERT_EQUAL_UINT32(4, p.length());
    TEST_ASSERT_EQUAL_UINT8(0x12, p.bytes()[2]);
    TEST_ASSERT_EQUAL_UINT8(0x34, p.bytes()[3]);
}

// Below freezing has to survive the trip. A sign handled wrongly here reads as roughly
// 6500 degrees at the other end, which is at least obvious — but the same bug on battery
// current would look like a plausible charge rate.
void test_negative_temperature_is_twos_complement()
{
    Payload        p;
    WeatherReading w;
    w.temperature.set(-155); // -15.5 C
    p.add(w);

    const uint16_t encoded = ((uint16_t)p.bytes()[2] << 8) | p.bytes()[3];
    TEST_ASSERT_EQUAL_UINT16(0xFF65, encoded);
    TEST_ASSERT_EQUAL_INT16(-155, (int16_t)encoded);
}

// Humidity must go out as type 112. Type 104 also decodes without error but lands under a
// key nothing downstream reads, so the value arrives intact and then vanishes.
void test_humidity_uses_type_112()
{
    Payload        p;
    WeatherReading w;
    w.humidity.set(655);
    p.add(w);

    TEST_ASSERT_EQUAL_UINT8(4, p.bytes()[0]);
    TEST_ASSERT_EQUAL_UINT8(112, p.bytes()[1]);
}

// State of charge is the only single-byte field. Encoding it as two bytes would shift
// every field after it.
void test_soc_is_one_byte()
{
    Payload        p;
    BatteryReading b;
    b.soc.set(87);
    p.add(b);

    TEST_ASSERT_EQUAL_UINT32(3, p.length());
    TEST_ASSERT_EQUAL_UINT8(23, p.bytes()[0]);
    TEST_ASSERT_EQUAL_UINT8(184, p.bytes()[1]);
    TEST_ASSERT_EQUAL_UINT8(87, p.bytes()[2]);
}

// The realistic case: both sensors good. Field order and total length are the contract.
void test_full_uplink_layout()
{
    Payload        p;
    WeatherReading w;
    w.wind_speed.set(342);
    w.wind_direction.set(197);
    w.temperature.set(215);
    w.humidity.set(655);
    w.pressure.set(10132);

    BatteryReading b;
    b.voltage.set(1247);
    b.current.set(-85);
    b.soc.set(87);
    b.temperature.set(180);

    p.add(w);
    p.add(b);

    // Five weather fields at 4 bytes, three battery fields at 4 bytes, plus a 1-byte SoC
    // field at 3 bytes total.
    TEST_ASSERT_EQUAL_UINT32((5 * 4) + (3 * 4) + 3, p.length());

    const uint8_t expected_channels[] = {1, 2, 3, 4, 5, 21, 22, 23, 24};
    size_t        offset              = 0;
    for (size_t i = 0; i < sizeof(expected_channels); i++) {
        TEST_ASSERT_EQUAL_UINT8(expected_channels[i], p.bytes()[offset]);
        offset += (p.bytes()[offset + 1] == 184) ? 3 : 4;
    }
    TEST_ASSERT_EQUAL_UINT32(p.length(), offset);
}

// The buffer must never be overrun. Repeated adds past capacity should drop fields rather
// than write past the end — a stack smash on a remote node is unrecoverable.
void test_encoder_respects_capacity()
{
    Payload        p;
    WeatherReading w;
    w.wind_speed.set(1);
    w.wind_direction.set(1);
    w.temperature.set(1);
    w.humidity.set(1);
    w.pressure.set(1);

    for (int i = 0; i < 20; i++) {
        p.add(w);
    }
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(kMaxPayloadBytes, p.length());
}

void test_clear_resets_buffer()
{
    Payload        p;
    WeatherReading w;
    w.pressure.set(10000);
    p.add(w);
    TEST_ASSERT_FALSE(p.empty());

    p.clear();
    TEST_ASSERT_TRUE(p.empty());
    TEST_ASSERT_EQUAL_UINT32(0, p.length());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_reading_encodes_nothing);
    RUN_TEST(test_partial_reading_omits_only_missing_fields);
    RUN_TEST(test_values_are_big_endian);
    RUN_TEST(test_negative_temperature_is_twos_complement);
    RUN_TEST(test_humidity_uses_type_112);
    RUN_TEST(test_soc_is_one_byte);
    RUN_TEST(test_full_uplink_layout);
    RUN_TEST(test_encoder_respects_capacity);
    RUN_TEST(test_clear_resets_buffer);
    return UNITY_END();
}
