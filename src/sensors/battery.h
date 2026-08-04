/*
 * RAK9154 Solar Battery Lite — pack voltage, current, charge, and temperature.
 *
 * Talks the RAK Sensor Hub single-wire protocol on the 5-pin socket: a half-duplex 9600
 * 8N1 UART on one open-drain line, carrying IPSO type-length-value records. This is the
 * link the RAK2560 Sensor Hub normally uses to read the pack, and the cable for it ships
 * in the box.
 *
 * CITE(prior-art): [CIT-ONEWIRE-SERIAL] beegee-tokyo/RAK-OneWireSerial — the wake byte
 *   0xFF, frame delimiter 0x7E, and the IPSO type table (values are the IPSO object id
 *   minus a 3200 offset, which is what makes them fit in one byte).
 * CITE(prior-art): [CIT-MESHTASTIC-9154] Meshtastic's RAK9154Sensor — the battery probe
 *   enumerates as probe id 0x01, with the master as 0x00.
 * CITE(sibling): [CIT-RAK45WIRE] rak-4-5-wire/firmware/nanoc6-onewire-poll @ efc0e3c —
 *   clean-room
 *   codec this one follows for framing and bit timing. It decoded only three TLV types;
 *   this implementation adds temperature, which the protocol carries as type 103 and that
 *   codec silently discarded as unrecognized.
 */

#pragma once

#include "../reading.h"

#include <stddef.h>
#include <stdint.h>

enum class BatteryResult : uint8_t {
    Ok = 0,
    NoReply,     // silent line — unplugged, or the pack's BMS is asleep
    ShortFrame,  // saw bytes but never a complete frame
    BadFrame,    // no delimiter, or a declared length that does not fit what arrived
    BadChecksum, // arrived intact-looking but does not verify — treated as no data
    NoRecords,   // frame verified but held nothing we understand
};

const char *battery_result_name(BatteryResult r);

class Battery {
  public:
    // `pin` is the single bridged data line. Held as configuration rather than a compile
    // time constant so the wiring can move without touching the protocol code.
    explicit Battery(uint8_t pin) : m_pin(pin) {}

    BatteryReading read();

    BatteryResult last_result() const { return m_last; }

  private:
    void   send_frame(uint8_t dest, uint8_t hub_type, uint8_t payload_type);
    void   send_boot();
    void   send_query();
    size_t receive(uint8_t *buf, size_t cap);
    void   tx_byte(uint8_t b);
    int    rx_byte(uint32_t timeout_us);

    // Verifies the frame before believing any of it, then fills `out`. Returns the reason
    // when it refuses. Validation matters more here than it looks: an unverified record
    // scan will happily turn line noise into a plausible pack voltage, and a wrong battery
    // reading is worse than none — it is the number a human uses to decide whether the
    // node needs rescuing.
    BatteryResult parse(const uint8_t *buf, size_t len, BatteryReading &out);

    uint8_t       m_pin;
    uint8_t       m_seq  = 0; // per-request sequence, mirrors menu.seq in the reference
    BatteryResult m_last = BatteryResult::NoReply;
};
