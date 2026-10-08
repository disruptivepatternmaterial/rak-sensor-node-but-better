/*
 * Uplink encoder — turns sensor readings into the byte string TTN expects.
 *
 * The wire format is Cayenne-LPP-shaped: each field is [channel][type][value], big-endian,
 * with no length prefix anywhere. That last detail is why this file has to be exactly
 * right. The decoder walks the buffer taking each type's width on faith, so a single field
 * encoded one byte too wide or narrow desynchronizes every field after it and the whole
 * uplink decodes to garbage rather than failing loudly.
 *
 * The channel and type numbers here are not free choices — they must match the live TTN
 * formatter. payload/schema.yaml is the machine-readable copy of this contract and
 * scripts/check_decoder_parity.py fails the build when the two drift apart.
 *
 * How much fits depends on the data rate, and not by a little. All nine fields come to 35
 * bytes, but the slowest US915 data rate allows only 11 — and adaptive data rate means the
 * network chooses, not us. A node at the edge of coverage gets moved to the slow rate
 * precisely when it is hardest to reach.
 *
 * Sending 35 bytes at that rate does not truncate: the send is refused outright. So the
 * encoder takes a byte budget and fills it in priority order, dropping the least important
 * fields rather than losing the entire uplink. Absent fields are already normal here — the
 * decoder treats a missing channel as no reading — so a short uplink degrades cleanly
 * instead of failing.
 *
 * CITE(spec): [CIT-CAYENNE-LPP] the channel/type TLV convention and big-endian values.
 * CITE(spec): [CIT-LORA-RP002] US915 data rates and the per-data-rate maximum payload —
 *   11 application bytes at DR0, which is what the priority ordering exists to survive.
 * CITE(sibling): [CIT-FWM-DECODER] rak-wx-station-default.js @ efc0e3c — the decoder that
 *   has to understand every byte produced here, including its per-type widths and
 *   divisors.
 * CITE(datasheet): [CIT-RK900] source scaling for the weather registers, which decides
 *   how much conversion has to happen before encoding.
 */

#pragma once

#include "reading.h"

#include <stddef.h>
#include <stdint.h>

// All nine fields: eight at four bytes plus the one-byte state of charge.
constexpr size_t kMaxPayloadBytes = 35;

// What the slowest US915 data rate allows. The MAC reports the real figure at run time and
// that is what gets used; this is the floor to design against and to test at.
constexpr size_t kMinDataRatePayloadBytes = 11;

class Payload {
  public:
    void clear()
    {
        m_len     = 0;
        m_dropped = 0;
        m_budget  = kMaxPayloadBytes;
    }

    // Appends every valid field and skips every invalid one. A field that was not read
    // contributes nothing at all — the decoder treats an absent channel as no data, which
    // is the only honest way to say "the sensor did not answer".
    //
    // These append in field order and are what the tests exercise. For a real uplink,
    // prefer build(), which orders by importance so that a tight budget drops the fields
    // that matter least.
    void add(const WeatherReading &w);
    void add(const BatteryReading &b);

    // Fills up to `budget` bytes in priority order. Anything that does not fit is left out
    // and counted in dropped(). A budget of zero means the full buffer.
    //
    // The order puts state of charge first, then wind speed, then air temperature. The
    // reasoning: for a node nobody can visit, whether it is about to die outranks any
    // single measurement, and those three together are exactly 11 bytes — so even at the
    // worst data rate an uplink still carries the node's health and the two readings the
    // station exists to take.
    void build(const WeatherReading &w, const BatteryReading &b, size_t budget = 0);

    const uint8_t *bytes() const { return m_buf; }
    size_t         length() const { return m_len; }
    bool           empty() const { return m_len == 0; }

    // Fields that were valid but did not fit. Non-zero means the uplink is a partial view,
    // which is worth logging — it is the visible symptom of a slow link.
    uint8_t dropped() const { return m_dropped; }

  private:
    void put_u8(uint8_t channel, uint8_t type, uint8_t value);
    void put_u16(uint8_t channel, uint8_t type, uint16_t value);
    void put_s16(uint8_t channel, uint8_t type, int16_t value);
    bool room_for(size_t n) const { return (m_len + n) <= m_budget; }

    uint8_t m_buf[kMaxPayloadBytes] = {0};
    size_t  m_len                   = 0;
    size_t  m_budget                = kMaxPayloadBytes;
    uint8_t m_dropped               = 0;
};
