#include "crc16.h"

namespace {

// CITE(spec): [CIT-MODBUS-SERIAL] reflected polynomial and seed.
constexpr uint16_t kPoly = 0xA001;
constexpr uint16_t kSeed = 0xFFFF;

} // namespace

uint16_t modbus_crc16(const uint8_t *data, size_t len)
{
    uint16_t crc = kSeed;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ kPoly) : (uint16_t)(crc >> 1);
        }
    }
    return crc;
}
