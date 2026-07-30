/*
 * Copy to secrets.h and fill in from the TTN console. secrets.h is gitignored and must
 * never be committed — an AppKey in git history is a compromised device.
 *
 * All three values are big-endian, matching how the TTN console displays them with the
 * "MSB" toggle on.
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
