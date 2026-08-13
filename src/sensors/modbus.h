/*
 * Minimal Modbus-RTU master — read holding registers (function 0x03) and nothing else.
 *
 * Written rather than pulled in, for three reasons. This node issues exactly one kind of
 * request, so a general library is mostly unused code. Every failure has to be bounded and
 * observable, since a blocking read is how an unattended node livelocks
 * (docs/FIRMWARE_SPEC.md H6). And the RS-485 transceiver has to be powered down between
 * polls for the power budget, which means owning the port lifecycle.
 *
 * CITE(spec): [CIT-MODBUS-SERIAL] MODBUS over Serial Line v1.02 — RTU framing, the 3.5
 *   character inter-frame gap that delimits messages, and the CRC-16 polynomial.
 * CITE(spec): [CIT-MODBUS-APP] MODBUS Application Protocol v1.1b — function 0x03 request
 *   and response layout, and the 0x80-flagged exception response.
 * CITE(prior-art): [CIT-MODBUSMASTER] 4-20ma/ModbusMaster — the response-validation order
 *   used here (length, then address, then function, then CRC).
 */

#pragma once

#include "crc16.h"

#include <Arduino.h>
#include <stdint.h>

enum class ModbusResult : uint8_t {
    Ok = 0,
    Timeout,        // no reply, or a short one — sensor absent or unpowered
    BadCrc,         // reply corrupted, usually wiring or termination
    BadFrame,       // reply well-formed but not the answer to what was asked
    Exception,      // slave answered with an error code
    Unsampled,      // reply CRC-valid but the span is all zero — see rk900.cpp
};

const char *modbus_result_name(ModbusResult r);

class ModbusMaster {
  public:
    // `serial` must already be started at the correct baud. This class does not own the
    // port, because the RS-485 module's power rail is shared and sequenced elsewhere.
    ModbusMaster(HardwareSerial &serial, uint32_t baud)
        : m_serial(serial), m_baud(baud) {}

    // Reads `count` consecutive holding registers into `out` (big-endian per Modbus).
    // Retries on timeout and CRC failure only — an exception response means the request
    // itself was wrong, and repeating it just wastes the power budget.
    ModbusResult read_holding(uint8_t slave, uint16_t start, uint8_t count, uint16_t *out,
                              uint8_t retries = 2);

  private:
    ModbusResult transact(uint8_t slave, uint16_t start, uint8_t count, uint16_t *out);
    void         drain_and_settle();

    HardwareSerial &m_serial;
    uint32_t        m_baud;
};
