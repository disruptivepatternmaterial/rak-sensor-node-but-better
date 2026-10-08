/*
 * Copy to secrets.h and fill in from the TTN console. secrets.h is gitignored and must
 * never be committed — an AppKey in git history is a compromised device.
 *
 * All three values are big-endian, matching how the TTN console displays them with the
 * "MSB" toggle on. Copy the digits straight across, left to right, without reversing
 * anything.
 *
 * This is worth being careful about because getting it wrong is nearly undiagnosable from the
 * network side. SX126x-Arduino wants MSB order and reverses the bytes itself when it builds
 * the join request; a different and widely-copied Arduino LoRaWAN library wants the EUIs
 * reversed, so reversed examples are easy to find. A reversed DevEUI describes a device that
 * does not exist, and an unrecognised join request is not answered and not reported — the
 * console shows no join attempt, no error, nothing at all, while the node transmits happily
 * forever. This cost a debugging session on 2026-07-31.
 *
 * The node prints its DevEUI in the boot banner for exactly this reason. Compare it against
 * the console before assuming a radio or gateway fault.
 */

#pragma once

#include <stdint.h>

// 8 bytes. TTN: Application > End devices > your device > DevEUI.
#define OTAA_DEVEUI {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}

// 8 bytes. TTN calls this the JoinEUI; it is the same field.
#define OTAA_APPEUI {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}

// 16 bytes. Shown once on creation in the TTN console.
#define OTAA_APPKEY {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, \
                     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
