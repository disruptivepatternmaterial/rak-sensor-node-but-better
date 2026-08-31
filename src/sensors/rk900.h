/*
 * RK900-09 weather station — wind speed, direction, air temperature, humidity, pressure.
 *
 * Owns the RS-485 port for the duration of a read, and hands it back afterwards. Nothing else
 * on the node touches Serial1: ADR-0004 gave the RAK5802 to this sensor exclusively at a fixed
 * line rate, so there is no bus arbitration and no baud switching to get wrong.
 *
 * The line rate is a measured value that contradicts the RAKwireless datasheet, so it is sourced
 * here beside the claim rather than only in the block below.
 *
 * CITE(bench): [CIT-RK900-BAUD-2026-08-03] — this physical unit returned a CRC-valid reply at
 *   9600 only, and zero bytes at 4800, across four consecutive sweeps.
 * CITE(datasheet): [CIT-RK900-PROTO] Rika's own protocol document gives the factory default as
 *   9600 8N1, so the measurement agrees with the manufacturer and it is RAKwireless's 4800 that
 *   is the outlier. ADR-0006 records the conflict and which source the firmware follows.
 * The rate is set in exactly one place, `kBaud` in rk900.cpp. The 4800 in [CIT-RK900] below is
 * what the RAKwireless datasheet says and is cited as such — it is not what this node transmits.
 *
 * It no longer owns WB_IO2 exclusively. When the pack's one-wire data line is routed through the
 * RAK5802's SDA terminal, Battery::read() raises the same rail for the duration of its
 * transaction and drops it again — see SwitchedRailHold in sensors/battery.cpp. The two never
 * overlap, because main.cpp completes the weather read before starting the battery read, but the
 * pin is shared and must not be treated as this driver's alone.
 *
 * CITE(datasheet): [CIT-RK900] RK900-09 register map — 4800 8N1, Modbus slave 0x01,
 *   holding registers 0x0000-0x0004, and the per-register scaling applied by the encoder.
 * CITE(datasheet): [CIT-RAK5802] the RS-485 module sits in the IO slot on Serial1; its
 *   transceiver is fed from the switched 3V3_S rail controlled by WB_IO2.
 * CITE(sibling): [CIT-FWM-RAK2560] at forest-weather-machines efc0e3c — the deployed
 *   RAK2560 Sensor Hub reads this same sensor with these same settings, so the values are
 *   field-proven rather than datasheet-only.
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
