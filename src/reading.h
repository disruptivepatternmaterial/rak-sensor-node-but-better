/*
 * Sensor reading types.
 *
 * Every field carries its own validity flag. This is the type-level expression of the
 * repo's null policy: a sensor that failed to read must never contribute a zero to the
 * uplink, because a fabricated 0 m/s wind or 0% state-of-charge is indistinguishable from
 * a real reading once it reaches ingest. Absent fields are simply omitted from the
 * payload, and the TTN decoder treats absence as "no data".
 *
 * Null policy: AGENTS.md · docs/FIRMWARE_SPEC.md §2.1 · payload/schema.yaml
 */

#pragma once

#include <stdint.h>

// A value that knows whether it exists. Deliberately not std::optional: no exceptions, no
// dynamic allocation, and it has to be trivially copyable for the encoder.
template <typename T>
struct Maybe {
    T    value = 0;
    bool valid = false;

    void set(T v)
    {
        value = v;
        valid = true;
    }

    void clear()
    {
        value = 0;
        valid = false;
    }
};

// Raw register values, scaled exactly as the sensor reports them. Conversion to the wire
// format happens in the encoder, not here — this struct stays a faithful record of what
// the hardware actually said.
struct WeatherReading {
    Maybe<uint16_t> wind_speed;     // x0.01 m/s
    Maybe<uint16_t> wind_direction; // degrees, raw — site offset belongs to the decoder
    Maybe<int16_t>  temperature;    // x0.1 degC
    Maybe<uint16_t> humidity;       // x0.1 %RH
    Maybe<uint16_t> pressure;       // x0.1 hPa

    bool any() const
    {
        return wind_speed.valid || wind_direction.valid || temperature.valid ||
               humidity.valid || pressure.valid;
    }
};

struct BatteryReading {
    Maybe<uint16_t> voltage;     // x0.01 V
    Maybe<int16_t>  current;     // x0.01 A — sign convention unresolved, see ADR-0002
    Maybe<uint8_t>  soc;         // %
    Maybe<int16_t>  temperature; // whole degC as reported by the BMS

    bool any() const
    {
        return voltage.valid || current.valid || soc.valid || temperature.valid;
    }
};
