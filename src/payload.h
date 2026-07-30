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
 * CITE(spec): [CIT-CAYENNE-LPP] the channel/type TLV convention and big-endian values.
 * CITE(sibling): [CIT-FWM-DECODER] rak-wx-station-default.js at forest-weather-machines
 *   efc0e3c — the decoder that has to understand every byte produced here, including its
 *   per-type widths and divisors.
 * CITE(datasheet): [CIT-RK900] source scaling for the weather registers, which decides
 *   how much conversion has to happen before encoding.
 */

#pragma once

#include "reading.h"

#include <stddef.h>
#include <stdint.h>

// Largest possible uplink: nine fields, each at most four bytes. Comfortably inside the
// smallest US915 payload allowance, which matters because the allowance shrinks as the
// network slows the node down.
constexpr size_t kMaxPayloadBytes = 40;

class Payload {
  public:
    void clear() { m_len = 0; }

    // Appends every valid field and skips every invalid one. A field that was not read
    // contributes nothing at all — the decoder treats an absent channel as no data, which
    // is the only honest way to say "the sensor did not answer".
    void add(const WeatherReading &w);
    void add(const BatteryReading &b);

    const uint8_t *bytes() const { return m_buf; }
    size_t         length() const { return m_len; }
    bool           empty() const { return m_len == 0; }

  private:
    void put_u8(uint8_t channel, uint8_t type, uint8_t value);
    void put_u16(uint8_t channel, uint8_t type, uint16_t value);
    void put_s16(uint8_t channel, uint8_t type, int16_t value);
    bool room_for(size_t n) const { return (m_len + n) <= kMaxPayloadBytes; }

    uint8_t m_buf[kMaxPayloadBytes] = {0};
    size_t  m_len                   = 0;
};
