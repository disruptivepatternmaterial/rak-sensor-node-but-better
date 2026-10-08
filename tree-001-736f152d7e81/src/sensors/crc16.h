/*
 * Modbus RTU checksum.
 *
 * Split out from the Modbus driver for one reason: this function has no dependency on the
 * Arduino runtime, so it can be compiled and tested on a normal computer. A wrong checksum
 * fails in the least helpful way possible — every reply looks corrupt, the sensor looks
 * dead, and the wiring gets blamed.
 *
 * CITE(spec): [CIT-MODBUS-SERIAL] the RTU checksum is a reflected CRC-16 with polynomial
 *   0xA001, seeded to 0xFFFF, appended low byte first.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

uint16_t modbus_crc16(const uint8_t *data, size_t len);
