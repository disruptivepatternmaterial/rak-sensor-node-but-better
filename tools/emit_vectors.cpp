/*
 * Emits real encoder output as hex, so the bytes the node would actually transmit can be
 * pushed through the live TTN decoder.
 *
 * The parity gate compares the encoder to the schema and the schema to the decoder. Both
 * links can hold while the chain still breaks — a shared misreading of the schema, or a
 * decoder behavior the schema does not describe, such as the installation offset applied
 * to wind direction. This program closes that gap by producing bytes rather than claims.
 *
 * Built and run by scripts/check_golden_vectors.py. Not part of the firmware image.
 */

#include "payload.h"
#include "reading.h"

#include <cstdio>
#include <cstring>

namespace {

void emit(const char *name, const Payload &p)
{
    printf("%s\t", name);
    for (size_t i = 0; i < p.length(); i++) {
        printf("%02X", p.bytes()[i]);
    }
    printf("\n");
}

WeatherReading full_weather()
{
    WeatherReading w;
    w.wind_speed.set(1234);    // 12.34 m/s
    w.wind_direction.set(45);  // decoder adds the 230 degree installation offset
    w.temperature.set(-155);   // -15.5 C, exercises the sign
    w.humidity.set(873);       // 87.3 %RH
    w.pressure.set(9871);      // 987.1 hPa
    return w;
}

BatteryReading full_pack()
{
    BatteryReading b;
    b.voltage.set(1174);      // 11.74 V
    b.current.set(-432);      // -4.32 A, exercises the sign
    b.soc.set(87);            // 87 %
    b.temperature.set(-201);  // -20.1 C
    return b;
}

} // namespace

int main()
{
    {
        Payload p;
        p.build(full_weather(), full_pack());
        emit("all_fields", p);
    }
    {
        // What the network allows at its slowest rate. Only the three highest-priority
        // fields fit, and which three is the part worth pinning down.
        Payload p;
        p.build(full_weather(), full_pack(), kMinDataRatePayloadBytes);
        emit("slowest_rate", p);
    }
    {
        Payload p;
        p.build(full_weather(), BatteryReading{});
        emit("weather_only", p);
    }
    {
        Payload p;
        p.build(WeatherReading{}, full_pack());
        emit("pack_only", p);
    }
    {
        // Nothing answered. The node sends no uplink at all in this case, but the encoder
        // producing an empty buffer rather than a buffer of zeros is the guarantee that
        // stops a dead sensor reading as a real measurement of zero.
        Payload p;
        p.build(WeatherReading{}, BatteryReading{});
        emit("nothing_valid", p);
    }
    return 0;
}
