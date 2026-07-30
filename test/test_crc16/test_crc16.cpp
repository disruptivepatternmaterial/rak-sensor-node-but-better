/*
 * Modbus checksum tests.
 *
 * A wrong checksum is a miserable bug to diagnose on a hillside: every reply fails
 * validation, so the sensor looks unplugged and the wiring gets taken apart first. Pinning
 * it against published reference values costs nothing and removes it from the suspect list
 * permanently.
 *
 * CITE(spec): [CIT-MODBUS-SERIAL] reflected CRC-16, polynomial 0xA001, seed 0xFFFF, low
 *   byte transmitted first.
 * CITE(spec): [CIT-MODBUS-APP] the request framing the vectors below are built from.
 */

#include <unity.h>

#include "sensors/crc16.h"

void setUp() {}
void tearDown() {}

// The textbook example: slave 1, read one holding register from 0x0000. 0x0A84 is the
// value quoted in Modbus reference material, so this test also confirms the byte order
// convention rather than merely being self-consistent.
void test_known_reference_vector()
{
    const uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT_EQUAL_UINT16(0x0A84, modbus_crc16(frame, sizeof(frame)));
}

// The actual request this firmware sends every cycle.
void test_rk900_request_vector()
{
    const uint8_t frame[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x05};
    TEST_ASSERT_EQUAL_UINT16(0xC985, modbus_crc16(frame, sizeof(frame)));
}

// An empty buffer returns the seed unchanged. Guards against an implementation that
// initializes to zero instead.
void test_empty_input_returns_seed()
{
    TEST_ASSERT_EQUAL_UINT16(0xFFFF, modbus_crc16(nullptr, 0));
}

// A single zero byte still changes the value, which catches the classic mistake of
// skipping zero bytes as if they contributed nothing.
void test_single_zero_byte()
{
    const uint8_t frame[] = {0x00};
    TEST_ASSERT_EQUAL_UINT16(0x40BF, modbus_crc16(frame, sizeof(frame)));
}

// Appending the checksum low byte first makes the whole frame check to zero. This is how
// the receiver validates, so it is worth asserting directly.
void test_frame_with_appended_crc_checks_to_zero()
{
    const uint16_t crc = modbus_crc16((const uint8_t[]){0x01, 0x03, 0x00, 0x00, 0x00, 0x01}, 6);

    const uint8_t full[] = {0x01, 0x03, 0x00,
                            0x00, 0x00, 0x01,
                            (uint8_t)(crc & 0xFF), (uint8_t)(crc >> 8)};

    TEST_ASSERT_EQUAL_UINT16(0x0000, modbus_crc16(full, sizeof(full)));
}

// One flipped bit must change the result. A checksum that misses single-bit errors is
// worse than none, because it grants false confidence in corrupted readings.
void test_single_bit_flip_changes_result()
{
    const uint8_t good[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x05};
    const uint8_t bad[]  = {0x01, 0x03, 0x00, 0x00, 0x00, 0x04};

    TEST_ASSERT_NOT_EQUAL_UINT16(modbus_crc16(good, sizeof(good)),
                                 modbus_crc16(bad, sizeof(bad)));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_known_reference_vector);
    RUN_TEST(test_rk900_request_vector);
    RUN_TEST(test_empty_input_returns_seed);
    RUN_TEST(test_single_zero_byte);
    RUN_TEST(test_frame_with_appended_crc_checks_to_zero);
    RUN_TEST(test_single_bit_flip_changes_result);
    return UNITY_END();
}
