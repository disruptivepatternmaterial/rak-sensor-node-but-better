/*
 * RAK9154 one-wire frame codec tests.
 *
 * These exist because the most expensive defect this driver has produced was pure byte
 * classification: a 28-of-92-byte truncated PROVISION announcement was reported as an all-zero
 * SENDAT record set, which sent the battery investigation after the protocol instead of after
 * the receive path and cost days. It is reachable from a fixed byte array in microseconds, and
 * until battery_frame.cpp was split out of battery.cpp nothing could reach it without a pack on
 * the wire. Issue #42.
 *
 * Every buffer below is either a real capture or a real capture with one byte changed.
 *
 * CITE(bench): docs/EVIDENCE.md — the SENDAT reply and the truncated announcement captured on
 *   3d3425d, and the first live record set on 1a203d3 (12.23 V, +0.00 A, 98%, 23.0 C).
 * CITE(prior-art): [CIT-ONEWIRE-SERIAL] beegee-tokyo/RAK-OneWireSerial @ c58c0f0
 *   onewire_master_protocol.c — cal_chksum() and the frame validation order under test.
 */

#include <unity.h>

#include "sensors/battery_frame.h"

void setUp() {}
void tearDown() {}

// The bench SENDAT reply, byte for byte. Wake byte, delimiter, RUI3 length 0x0015 = 21, type 2
// (SENSORHUB), flag 1 (RSP), then the SensorHub header {dest 0x00 master, source 0x01, seq 0x04,
// hub_type 0x03 SENDAT, payload_len 0x10, payload_type 0x02} and four records.
//
// CITE(bench): docs/EVIDENCE.md — SENDAT sweep on 3d3425d, the 28-byte data reply.
static const uint8_t kSendat[] = {0xFF, 0x7E, 0x00, 0x15, 0x02, 0x01, 0x00, 0x01, 0x04,
                                  0x03, 0x10, 0x02, 0x15, 0xBA, 0x00, 0x00, 0x16, 0xB9,
                                  0x00, 0x00, 0x17, 0xB8, 0x00, 0x18, 0x67, 0x00, 0x00,
                                  0x27};

// The frame under test carries a sequence of 0x04 from source 0x01 to the master, so this is
// what an outstanding query to 0x01 with sequence 4 looks like.
static BatteryQueryMatch sendat_match()
{
    return BatteryQueryMatch{BatteryMatchMode::Response, 0x01, 0x04};
}

// Rebuild the trailing checksum after mutating a frame, so a test can change a value byte
// without accidentally also testing the checksum path.
static void refresh_checksum(uint8_t *buf, size_t delim)
{
    const size_t payload_len = ((size_t)buf[delim + 1] << 8) | buf[delim + 2];
    buf[delim + 5 + payload_len] = frame_chksum(buf, delim, payload_len);
}

// ---------------------------------------------------------------------------------------
// The capture that started all of this.
// ---------------------------------------------------------------------------------------

// 28 of the announcement's 92 bytes. The RUI3 length field says 0x0055 = 85, so the checksum
// byte would land at index 91 and the buffer stops at 28.
//
// This is the whole point of the exercise: it must classify as Truncated — a transport fault —
// and not as Unsampled, which is a claim about the pack's sampling state and sent the last
// reader to the wrong subsystem. It must also write nothing at all into the reading.
//
// CITE(bench): docs/EVIDENCE.md — announcement on 3d3425d declares length 0x0055 = 85; the
//   scanner returned 28 bytes of it.
void test_truncated_provision_is_truncated_not_unsampled()
{
    const uint8_t truncated[] = {0xFF, 0x7E, 0x00, 0x55, 0x02, 0x00, 0x00, 0xFF, 0x01,
                                 0x01, 0x50, 0x03, 0x01, 0x00, 0x00, 0x00, 0x52, 0x41,
                                 0x4B, 0x39, 0x31, 0x35, 0x34, 0x00, 0x00, 0x00, 0x00,
                                 0x00};

    BatteryReading    out;
    BatteryFrameNotes notes;
    const BatteryResult r =
        battery_decode_frame(truncated, sizeof(truncated), out, notes, sendat_match());

    TEST_ASSERT_EQUAL(BatteryResult::Truncated, r);
    TEST_ASSERT_NOT_EQUAL(BatteryResult::Unsampled, r);

    // Not one field written. A truncated read produces no reading, not a zero one.
    TEST_ASSERT_FALSE(out.any());
    TEST_ASSERT_FALSE(out.voltage.valid);
    TEST_ASSERT_FALSE(out.current.valid);
    TEST_ASSERT_FALSE(out.soc.valid);
    TEST_ASSERT_FALSE(out.temperature.valid);

    // The diagnostic that did not exist when this cost a week. Both numbers are measured from
    // the delimiter: 0x7E + 4 transport bytes + 85 payload + 1 checksum = 91.
    TEST_ASSERT_TRUE(notes.truncated_frame);
    TEST_ASSERT_EQUAL_UINT32(91, notes.declared);
    TEST_ASSERT_EQUAL_UINT32(sizeof(truncated), notes.arrived);
}

// A complete announcement, checksum valid, arriving where a SENDAT reply was expected. The pack
// is alive and framing correctly — it is announcing rather than answering — and that must be
// distinguishable from a corrupt frame.
void test_full_provision_announcement_is_provision_only()
{
    // 92 bytes: wake, delimiter, length 0x0055 = 85, type 2, flag 0 (REQ), then 85 payload
    // bytes and a checksum. Contents past the header are padding; the classification depends on
    // hub_type 0x01 (PROVISION), not on the descriptor tail.
    uint8_t prov[92] = {0};
    prov[0]          = 0xFF; // wake
    prov[1]          = 0x7E; // delimiter
    prov[2]          = 0x00; // length hi
    prov[3]          = 0x55; // length lo = 85
    prov[4]          = 0x02; // RUI3 type SENSORHUB
    prov[5]          = 0x00; // RUI3 flag REQ — the pack is asking, not answering
    prov[6]          = 0x00; // dest: master
    prov[7]          = 0xFF; // source: unprovisioned
    prov[8]          = 0x01; // sequence
    prov[9]          = 0x01; // hub_type PROVISION
    prov[10]         = 0x50; // hub payload_len
    prov[11]         = 0x03; // payload_type VER3
    refresh_checksum(prov, 1);

    BatteryReading      out;
    BatteryFrameNotes   notes;
    const BatteryResult r = battery_decode_frame(prov, sizeof(prov), out, notes, sendat_match());

    TEST_ASSERT_EQUAL(BatteryResult::ProvisionOnly, r);
    TEST_ASSERT_FALSE(out.any());
    TEST_ASSERT_FALSE(notes.truncated_frame);

    // And the frame is answerable — this is the condition that lets the drain return early
    // instead of waiting out the inter-byte gap and replying too late.
    TEST_ASSERT_TRUE(provision_ready(prov, sizeof(prov)));
}

// ---------------------------------------------------------------------------------------
// The happy path, and the byte order in it.
// ---------------------------------------------------------------------------------------

// The bench SENDAT reply decodes to Ok with the right values in the right order.
//
// The reply captured on 3d3425d carried all zeros, so this frame's records are the same shape
// with the values from the first live reading substituted: raw v=1222, i=0, soc=98, t=220 —
// 12.22 V, +0.00 A, 98%, 22.0 C. Flag is RSP, so the values are little-endian.
//
// CITE(bench): docs/EVIDENCE.md 2026-08-05 — raw v=1222 i=0 soc=98 t=220 alongside a pack
//   reporting 12.22 V / +0.00 A / 98% / 22.0 C. Temperature is tenths, so the decoder's /10 on
//   the TTN side is correct.
void test_live_sendat_decodes_with_little_endian_values()
{
    uint8_t frame[sizeof(kSendat)];
    for (size_t i = 0; i < sizeof(kSendat); i++) {
        frame[i] = kSendat[i];
    }

    // Records are { sid, ipso, value... } and they start at index 12: `15 BA v v` at 12-15,
    // `16 B9 i i` at 16-19, `17 B8 soc` at 20-22, `18 67 t t` at 23-26. The value bytes are
    // therefore two past the sid, not one — the capacity record is three bytes wide and shifts
    // everything behind it.
    frame[14] = 0xC6; // voltage low  — 1222 = 0x04C6
    frame[15] = 0x04; // voltage high
    frame[18] = 0x00; // current low  — 0
    frame[19] = 0x00; // current high
    frame[22] = 98;   // capacity, one byte
    frame[25] = 0xDC; // temperature low  — 220 = 0x00DC
    frame[26] = 0x00; // temperature high
    refresh_checksum(frame, 1);

    BatteryReading      out;
    BatteryFrameNotes   notes;
    const BatteryResult r =
        battery_decode_frame(frame, sizeof(frame), out, notes, sendat_match());

    TEST_ASSERT_EQUAL(BatteryResult::Ok, r);
    TEST_ASSERT_TRUE(out.voltage.valid);
    TEST_ASSERT_EQUAL_UINT16(1222, out.voltage.value); // 12.22 V
    TEST_ASSERT_TRUE(out.current.valid);
    TEST_ASSERT_EQUAL_INT16(0, out.current.value); // +0.00 A, a real idle current
    TEST_ASSERT_TRUE(out.soc.valid);
    TEST_ASSERT_EQUAL_UINT8(98, out.soc.value);
    TEST_ASSERT_TRUE(out.temperature.valid);
    TEST_ASSERT_EQUAL_INT16(220, out.temperature.value); // 22.0 C, tenths

    TEST_ASSERT_FALSE(notes.truncated_frame);
    TEST_ASSERT_FALSE(notes.truncated_record);
    TEST_ASSERT_FALSE(notes.unknown_record);
}

// The capture exactly as it arrived: every record zero. A pack powered by the cell it is
// measuring cannot be at 0.00 V and also be driving this wire, so this is the record template
// rather than a measurement, and it must not become an uplink.
void test_all_zero_record_template_is_unsampled_and_clears_fields()
{
    BatteryReading      out;
    BatteryFrameNotes   notes;
    const BatteryResult r =
        battery_decode_frame(kSendat, sizeof(kSendat), out, notes, sendat_match());

    TEST_ASSERT_EQUAL(BatteryResult::Unsampled, r);
    TEST_ASSERT_FALSE(out.any());
}

// One byte of the checksum changed. Nothing else about the frame is wrong, and it must still be
// refused: an unverified record scan will turn line noise into a plausible pack voltage.
void test_corrupt_checksum_is_rejected()
{
    uint8_t frame[sizeof(kSendat)];
    for (size_t i = 0; i < sizeof(kSendat); i++) {
        frame[i] = kSendat[i];
    }
    frame[sizeof(frame) - 1] ^= 0xFF; // the trailing checksum byte

    BatteryReading      out;
    BatteryFrameNotes   notes;
    const BatteryResult r =
        battery_decode_frame(frame, sizeof(frame), out, notes, sendat_match());

    TEST_ASSERT_EQUAL(BatteryResult::BadChecksum, r);
    TEST_ASSERT_FALSE(out.any());
}

// ---------------------------------------------------------------------------------------
// Issue #37 — a partial record set must not return Ok, and must not leave stale values.
// ---------------------------------------------------------------------------------------

// A recognised record type whose payload runs off the end of the frame.
//
// The temperature record needs two value bytes and gets one. The old walker broke out of the
// loop at that point and fell through to the success path, returning Ok on the strength of the
// voltage, current and capacity records ahead of the cut — reporting an incomplete frame as a
// complete reading. Rejecting the whole frame is the fix: if the transport cut the frame short,
// nothing in it is trustworthy enough to hike out on.
void test_incomplete_trailing_record_rejects_the_whole_frame()
{
    // The same records as the live frame, with the temperature record one byte short. Length is
    // 0x14 = 20 rather than 21, so the checksum lands one byte earlier and the frame verifies —
    // the fault is inside the record set, not in the framing.
    uint8_t frame[] = {0xFF, 0x7E, 0x00, 0x14, 0x02, 0x01, 0x00, 0x01, 0x04, 0x03, 0x10, 0x02,
                       0x15, 0xBA, 0xC6, 0x04,             // voltage 1222
                       0x16, 0xB9, 0x00, 0x00,             // current 0
                       0x17, 0xB8, 98,                     // capacity 98
                       0x18, 0x67, 0xDC,                   // temperature: one value byte only
                       0x00};
    refresh_checksum(frame, 1);

    BatteryReading      out;
    BatteryFrameNotes   notes;
    const BatteryResult r =
        battery_decode_frame(frame, sizeof(frame), out, notes, sendat_match());

    // Not Ok, and specifically not Ok-with-three-good-records.
    TEST_ASSERT_NOT_EQUAL(BatteryResult::Ok, r);
    TEST_ASSERT_EQUAL(BatteryResult::Truncated, r);
    TEST_ASSERT_TRUE(notes.truncated_record);
    TEST_ASSERT_EQUAL_UINT8(kIpsoTemperature, notes.truncated_record_type);

    // And the records that did decode are gone. This is the part that matters: a partial frame
    // yields no reading at all.
    TEST_ASSERT_FALSE(out.any());
    TEST_ASSERT_FALSE(out.voltage.valid);
    TEST_ASSERT_FALSE(out.soc.valid);
}

// The stale-value half of #37, stated directly: hand the decoder a reading that already holds
// last cycle's values, give it a frame it must refuse, and nothing of the old values may
// survive. A voltage that silently stops updating is worse than a null — it hides a failing
// pack behind a plausible number.
void test_rejected_frame_cannot_leave_values_from_a_previous_read()
{
    BatteryReading out;
    out.voltage.set(1222); // last cycle's healthy reading
    out.current.set(0);
    out.soc.set(98);
    out.temperature.set(220);

    // A buffer with no frame in it at all.
    const uint8_t noise[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99};

    BatteryFrameNotes   notes;
    const BatteryResult r = battery_decode_frame(noise, sizeof(noise), out, notes, sendat_match());

    TEST_ASSERT_NOT_EQUAL(BatteryResult::Ok, r);
    TEST_ASSERT_FALSE(out.any());
    TEST_ASSERT_FALSE(out.voltage.valid);
    TEST_ASSERT_EQUAL_UINT16(0, out.voltage.value);
}

// ---------------------------------------------------------------------------------------
// Issue #36 — a SENDAT frame has to prove it is the answer to the question we asked.
// ---------------------------------------------------------------------------------------

// A spontaneous push (flag REQ) arriving where a solicited reply was expected must not be taken
// as proof that the queried address is live. This is the frame that could falsely validate
// address 0x01 on a pack configured with a periodic rule.
void test_spontaneous_push_does_not_answer_a_solicited_query()
{
    uint8_t frame[sizeof(kSendat)];
    for (size_t i = 0; i < sizeof(kSendat); i++) {
        frame[i] = kSendat[i];
    }
    frame[5] = kRui3FlagReq; // the pack talking on its own initiative
    // Big-endian on the REQ path, so give it a non-zero value that is unambiguous either way.
    frame[14] = 0x04;
    frame[15] = 0xC6;
    refresh_checksum(frame, 1);

    BatteryReading      out;
    BatteryFrameNotes   notes;
    const BatteryResult r =
        battery_decode_frame(frame, sizeof(frame), out, notes, sendat_match());

    TEST_ASSERT_EQUAL(BatteryResult::Unmatched, r);
    TEST_ASSERT_TRUE(notes.unmatched);
    TEST_ASSERT_FALSE(out.any());

    // The same frame is exactly what the push listen exists to catch, so under Unsolicited
    // matching it decodes normally. The frame is fine; what changes is what it is evidence of.
    BatteryReading    push_out;
    BatteryFrameNotes push_notes;
    const BatteryQueryMatch push{BatteryMatchMode::Unsolicited, 0x01, 0};
    TEST_ASSERT_EQUAL(BatteryResult::Ok,
                      battery_decode_frame(frame, sizeof(frame), push_out, push_notes, push));
    TEST_ASSERT_EQUAL_UINT16(1222, push_out.voltage.value);
}

// A reply from a different probe, and a reply carrying somebody else's sequence. Both are
// structurally valid SENDAT responses to the master and neither answers our query.
void test_wrong_source_or_sequence_is_unmatched()
{
    uint8_t frame[sizeof(kSendat)];

    for (size_t i = 0; i < sizeof(kSendat); i++) {
        frame[i] = kSendat[i];
    }
    frame[7] = 0x02; // source: a different probe than the 0x01 we queried
    refresh_checksum(frame, 1);
    {
        BatteryReading    out;
        BatteryFrameNotes notes;
        TEST_ASSERT_EQUAL(BatteryResult::Unmatched, battery_decode_frame(frame, sizeof(frame),
                                                                        out, notes,
                                                                        sendat_match()));
    }

    for (size_t i = 0; i < sizeof(kSendat); i++) {
        frame[i] = kSendat[i];
    }
    frame[8] = 0x09; // sequence: not the 0x04 we sent — a stale reply from an earlier cycle
    refresh_checksum(frame, 1);
    {
        BatteryReading    out;
        BatteryFrameNotes notes;
        TEST_ASSERT_EQUAL(BatteryResult::Unmatched, battery_decode_frame(frame, sizeof(frame),
                                                                        out, notes,
                                                                        sendat_match()));
    }

    for (size_t i = 0; i < sizeof(kSendat); i++) {
        frame[i] = kSendat[i];
    }
    frame[6] = 0x07; // destination: addressed to somebody who is not the master
    refresh_checksum(frame, 1);
    {
        BatteryReading    out;
        BatteryFrameNotes notes;
        TEST_ASSERT_EQUAL(BatteryResult::Unmatched, battery_decode_frame(frame, sizeof(frame),
                                                                        out, notes,
                                                                        sendat_match()));
    }
}

// The matching must not throw away a good frame that arrives behind a bad one. The pack
// concatenates frames in a single read, so the scan has to keep going past a mismatch — this is
// the "discard and keep listening" half of #36.
void test_matching_reply_behind_an_unmatched_frame_is_still_found()
{
    uint8_t buf[sizeof(kSendat) * 2];
    size_t  at = 0;

    // First: a reply from the wrong probe.
    for (size_t i = 0; i < sizeof(kSendat); i++) {
        buf[at + i] = kSendat[i];
    }
    buf[at + 7] = 0x02;
    refresh_checksum(buf, at + 1);
    at += sizeof(kSendat);

    // Behind it: the reply we actually asked for, carrying a live voltage.
    for (size_t i = 0; i < sizeof(kSendat); i++) {
        buf[at + i] = kSendat[i];
    }
    buf[at + 14] = 0xC6;
    buf[at + 15] = 0x04;
    refresh_checksum(buf, at + 1);

    BatteryReading      out;
    BatteryFrameNotes   notes;
    const BatteryResult r = battery_decode_frame(buf, sizeof(buf), out, notes, sendat_match());

    TEST_ASSERT_EQUAL(BatteryResult::Ok, r);
    TEST_ASSERT_EQUAL_UINT16(1222, out.voltage.value);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_truncated_provision_is_truncated_not_unsampled);
    RUN_TEST(test_full_provision_announcement_is_provision_only);
    RUN_TEST(test_live_sendat_decodes_with_little_endian_values);
    RUN_TEST(test_all_zero_record_template_is_unsampled_and_clears_fields);
    RUN_TEST(test_corrupt_checksum_is_rejected);
    RUN_TEST(test_incomplete_trailing_record_rejects_the_whole_frame);
    RUN_TEST(test_rejected_frame_cannot_leave_values_from_a_previous_read);
    RUN_TEST(test_spontaneous_push_does_not_answer_a_solicited_query);
    RUN_TEST(test_wrong_source_or_sequence_is_unmatched);
    RUN_TEST(test_matching_reply_behind_an_unmatched_frame_is_still_found);
    return UNITY_END();
}
