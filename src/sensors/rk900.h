/*
 * RK900-09 weather station — wind speed, direction, air temperature, humidity, pressure.
 *
 * Owns the RS-485 port and the RAK5802's power rail for the duration of a read, and hands
 * both back afterwards. Nothing else on the node touches Serial1: ADR-0004 gave the
 * RAK5802 to this sensor exclusively, at a fixed 4800 baud, so there is no bus arbitration
 * and no baud switching to get wrong.
 *
 * CITE(datasheet): [CIT-RK900] RK900-09 register map — 4800 8N1, Modbus slave 0x01,
 *   holding registers 0x0000-0x0004, and the per-register scaling applied by the encoder.
 * CITE(datasheet): [CIT-RAK5802] the RS-485 module sits in the IO slot on Serial1; its
 *   transceiver is fed from the switched 3V3_S rail controlled by WB_IO2.
 * CITE(sibling): [CIT-FWM-RAK2560] the deployed RAK2560 Sensor Hub reads this same sensor
 *   with these same settings, so the values are field-proven rather than datasheet-only.
 */

#pragma once

#include "../reading.h"
#include "modbus.h"

class RK900 {
  public:
    // Powers the transceiver, opens the port, reads all five registers, then shuts both
    // down. Returns a reading with per-field validity; a total failure yields a reading
    // where `any()` is false rather than a struct full of zeros.
    WeatherReading read();

    // True if the last read produced at least one field. Used for the console summary and
    // for deciding whether the sensor is worth reporting on.
    ModbusResult last_result() const { return m_last; }

  private:
    void power_on();
    void power_off();

    ModbusResult m_last = ModbusResult::Timeout;
};
