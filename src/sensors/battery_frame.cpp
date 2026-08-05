/*
 * RAK9154 one-wire frame codec. See battery_frame.h for why this is a separate translation
 * unit; the short version is that everything in here runs on the build host under
 * `pio test -e native`, and none of it needs a board.
 */

#include "battery_frame.h"

uint8_t frame_chksum(const uint8_t *buf, size_t delim, size_t payload_len)
{
    uint8_t c =
        (uint8_t)(__builtin_popcount(buf[delim + 3]) + __builtin_popcount(buf[delim + 4]));
    for (size_t i = 0; i < payload_len; i++) {
        c += __builtin_popcount(buf[delim + 5 + i]);
    }
    return c;
}

bool next_frame(const uint8_t *buf, size_t len, size_t from, SnHubFrame &f, ScanNotes &notes)
{
    notes.arrived = len;

    for (size_t d = from; d < len; d++) {
        if (buf[d] != kDelimiter) {
            continue;
        }
        // length[2] + type + flag, and at least the SensorHub header behind it.
        if (d + 5 + kHubHeaderBytes > len) {
            break; // nothing this short can hold a frame, and later candidates are shorter
        }
        if (buf[d + 3] != kRui3TypeSensorHub) {
            continue;
        }

        // LSB_COMB(hbyte, lbyte) = (lbyte << 8) + hbyte, with lbyte first on the wire.
        const size_t payload_len = ((size_t)buf[d + 1] << 8) | buf[d + 2];
        if (payload_len < kHubHeaderBytes) {
            continue;
        }
        const size_t payload = d + 5;
        const size_t cksum   = payload + payload_len;
        if (cksum >= len) {
            // Declared longer than what arrived. Recorded rather than silently skipped: this is
            // the exact shape of the pack's 92-byte announcement caught at 28 bytes, and
            // treating it as "no frame" is what made a transport fault look like bad data.
            //
            // Counted from the delimiter, not from the start of the buffer. The previous form
            // was `cksum + 1`, which is only right when the delimiter sits at buffer index 1 —
            // i.e. behind exactly one wake byte. Leading line noise, or the four wake bytes
            // this pack actually needs, made the diagnostic overstate the declared length by
            // the leading run. Both numbers below are now frame-relative and comparable.
            notes.truncated = true;
            notes.declared  = cksum - d + 1; // delimiter through the checksum byte
            continue;
        }
        if (frame_chksum(buf, d, payload_len) != buf[cksum]) {
            notes.bad_cksum = true;
            continue;
        }
        if (buf[payload + 3] == kHubTypeProvision) {
            notes.saw_provision = true;
        }

        f.delim            = d;
        f.payload          = payload;
        f.payload_len      = payload_len;
        f.cksum            = cksum;
        f.flag             = buf[d + 4];
        f.dest             = buf[payload + 0];
        f.source           = buf[payload + 1];
        f.sequence         = buf[payload + 2];
        f.hub_type         = buf[payload + 3];
        f.hub_payload_type = buf[payload + 5];
        return true;
    }
    return false;
}

bool provision_ready(const uint8_t *buf, size_t len)
{
    SnHubFrame f;
    ScanNotes  notes;
    size_t     from = 0;

    while (next_frame(buf, len, from, f, notes)) {
        from = f.delim + 1;
        if (f.hub_type == kHubTypeProvision && f.flag == kRui3FlagReq &&
            f.hub_payload_type == kPldProvVer3 && f.dest == kMasterId) {
            return true;
        }
    }
    return false;
}

bool frame_matches_query(const SnHubFrame &f, const BatteryQueryMatch &match)
{
    switch (match.mode) {
    case BatteryMatchMode::Any:
        return true;

    case BatteryMatchMode::Response:
        // All four fields, not a subset. Dropping any one of them re-opens the hole: a
        // spontaneous push has the right destination but the wrong flag, a reply from a
        // different probe has the right flag but the wrong source, and a stale reply left in
        // the buffer from the previous cycle has both and the wrong sequence.
        return f.flag == kRui3FlagRsp && f.dest == kMasterId && f.source == match.addr &&
               f.sequence == match.sequence;

    case BatteryMatchMode::Unsolicited:
        return f.flag == kRui3FlagReq && f.dest == kMasterId;
    }
    return false;
}

BatteryResult battery_decode_frame(const uint8_t *buf, size_t len, BatteryReading &out,
                                   BatteryFrameNotes &notes, const BatteryQueryMatch &match)
{
    // Cleared on entry, unconditionally.
    //
    // Every caller in battery.cpp happens to pass a fresh BatteryReading today, but "happens
    // to" is exactly the property that decays: the reading struct is reused across phases and
    // a future caller passing the previous cycle's values would have them survive a rejected
    // frame and be reported as fresh telemetry. For a node left in the woods for months, a
    // voltage that silently stops updating is worse than a null — it hides a failing pack
    // behind a plausible number. Issue #37.
    out = BatteryReading{};
    notes = BatteryFrameNotes{};

    // Pick the SENDAT frame that answers our query out of the buffer. There may be more than
    // one frame in there, and the data reply is not reliably the first — see next_frame().
    SnHubFrame f;
    ScanNotes  scan;
    bool       found = false;
    size_t     from  = 0;

    while (next_frame(buf, len, from, f, scan)) {
        from = f.delim + 1;
        if (f.hub_type != kHubTypeSendData) {
            continue;
        }
        if (!frame_matches_query(f, match)) {
            // Structurally valid, addressed to somebody, but not an answer to what we asked.
            // Discard it and keep scanning — the frame we want may be behind it in the same
            // buffer, which is routine on this bus. Issue #36.
            notes.unmatched          = true;
            notes.unmatched_flag     = f.flag;
            notes.unmatched_dest     = f.dest;
            notes.unmatched_source   = f.source;
            notes.unmatched_sequence = f.sequence;
            continue;
        }
        found = true;
        break;
    }

    notes.truncated_frame = scan.truncated;
    notes.declared        = scan.declared;
    notes.arrived         = scan.arrived;

    // Classify the failure instead of flattening it. Order matters: a truncated read is the
    // most specific and most actionable diagnosis, and it must not be masked by the checksum
    // failures that a truncated buffer also tends to produce further along.
    //
    // Nothing below this point can yield a reading, and `out` was cleared on entry — a
    // wrong-type or short frame produces no value at all, not a zero and not a stale one.
    if (!found) {
        if (scan.truncated) {
            return BatteryResult::Truncated;
        }
        if (notes.unmatched) {
            // Ranked above ProvisionOnly and BadChecksum: a SENDAT frame that did not answer
            // us is a different situation from no SENDAT at all, and it is the one that used
            // to be silently accepted.
            return BatteryResult::Unmatched;
        }
        if (scan.saw_provision) {
            // The pack is alive and framing correctly; it is announcing rather than answering.
            // This is its actual observed behaviour on this link and it is not a fault in the
            // frame, so it must not read as one.
            return BatteryResult::ProvisionOnly;
        }
        return scan.bad_cksum ? BatteryResult::BadChecksum : BatteryResult::BadFrame;
    }

    // The same records arrive in opposite byte orders depending on which way the exchange
    // went, and the reference driver decodes them differently in each case. A response to
    // our GET (flag = RSP) is little-endian; a push the pack sends unbidden (flag = REQ) is
    // big-endian. Getting this from the flag rather than assuming one of them is what stops
    // a 12.80 V pack being reported as 0.05 V.
    // CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 RAK9154Sensor.cpp — the
    //   SNHUBAPI_EVT_SDATA_REQ case (reached via protocol_list[SENDAT].rsp) decodes
    //   `(msg[2] << 8) + msg[1]`, while SNHUBAPI_EVT_REPORT (reached via .req) decodes
    //   `(msg[1] << 8) + msg[2]`. msg[0] is the IPSO type, so msg[1] is the first value byte.
    // CITE(bench): docs/EVIDENCE.md 2026-08-05 — the reply captured on 1a203d3 carries flag
    //   0x01 (RSP) and decodes on the little-endian path to 12.23 V / +0.00 A / 98% / 23.0 C,
    //   which the pack's own display corroborates. The byte order is now confirmed against a
    //   non-zero reading, not only against the reference.
    const bool lsb_first = (f.flag == kRui3FlagRsp);

    auto val16 = [&](size_t at) -> uint16_t {
        return lsb_first ? (uint16_t)(((uint16_t)buf[at + 1] << 8) | buf[at])
                         : (uint16_t)(((uint16_t)buf[at] << 8) | buf[at + 1]);
    };

    // Records occupy payload[6 ..], i.e. everything after the 6-byte SensorHub header up to
    // the checksum byte.
    const size_t records     = f.payload + kHubHeaderBytes;
    const size_t records_end = f.cksum;

    // Records are { sensor id, IPSO type, value }, the value width implied by the type. The
    // widths are not guessed — they are the `size` column of the reference's IPSO table, and
    // they check out exactly against the captured reply: four records in fifteen bytes as
    // 4 + 4 + 3 + 4, ending precisely on the checksum.
    //
    // CITE(prior-art): [CIT-ONEWIRE-SERIAL] @ c58c0f0 onewire_master_protocol.h
    //   SNHub_Api_sData_Snsr_t { sid, ipso, value[] }; onewire_master_protocol.c walks it as
    //   `sData = &sData->value[rakipso_tbl[sData->ipso].size]` with table sizes CAPACITY 1,
    //   DC_CURRENT 2, DC_VOLTAGE 2, TEMP_SENSOR 2.
    // CITE(bench): docs/EVIDENCE.md — SENDAT reply on 3d3425d: `15 BA 00 00 | 16 B9 00 00 |
    //   17 B8 00 | 18 67 00 00` then checksum. Sensor ids 0x15-0x18 lead each record, the
    //   IPSO type follows, and the strides land on the checksum byte with nothing left over.
    //
    // Whether any *physical* record in this frame carried a non-zero value. This is the
    // template detector, and it has to be accumulated during the walk rather than
    // reconstructed from `out` afterwards, because `out` cannot distinguish "the pack
    // reported 0" from "no record of that type was present". Only voltage, current, capacity
    // and temperature count toward it — see kIpsoBitValues16.
    bool any_nonzero = false;

    size_t i = records;
    while (i + 2 < records_end) {
        const uint8_t type = buf[i + 1];

        if (type == kIpsoCapacity) {
            out.soc.set(buf[i + 2]);
            any_nonzero |= (buf[i + 2] != 0);
            i += 3;
        } else if (type == kIpsoDcCurrent && (i + 3) < records_end) {
            out.current.set((int16_t)val16(i + 2));
            any_nonzero |= (val16(i + 2) != 0);
            i += 4;
        } else if (type == kIpsoDcVoltage && (i + 3) < records_end) {
            out.voltage.set(val16(i + 2));
            any_nonzero |= (val16(i + 2) != 0);
            i += 4;
        } else if (type == kIpsoTemperature && (i + 3) < records_end) {
            // Type 103 is the same code the pack's own LoRaWAN uplinks use for temperature,
            // a signed 16-bit value in tenths. Byte order follows the flag like the others.
            // CITE(bench): docs/EVIDENCE.md 2026-08-05 — raw t=220 alongside a pack reporting
            //   22.0 C confirms tenths, so the TTN decoder's /10 is correct.
            out.temperature.set((int16_t)val16(i + 2));
            any_nonzero |= (val16(i + 2) != 0);
            i += 4;
        } else if (type == kIpsoBitValues16 && (i + 3) < records_end) {
            // Stepped over, not stored and not counted. Decoding its width is what keeps the
            // walker aligned so any record behind it is still readable; everything else about
            // it is deliberately ignored. Recorded so the next capture shows what the pack
            // puts in these two words, which is currently unknown.
            if (notes.status_count < BatteryFrameNotes::kMaxStatusWords) {
                notes.status_sid[notes.status_count]   = buf[i];
                notes.status_value[notes.status_count] = val16(i + 2);
                notes.status_count++;
            }
            i += 4;
        } else if (type == kIpsoDcCurrent || type == kIpsoDcVoltage ||
                   type == kIpsoTemperature || type == kIpsoBitValues16) {
            // Recognized type, but the frame ends before its payload does — the four branches
            // above all require two value bytes and this one has fewer left.
            //
            // The whole frame is rejected, not just this record. Keeping the records decoded
            // ahead of the cut would return Ok on a frame that is demonstrably incomplete, and
            // a partially trusted record set is how a plausible-looking wrong number reaches
            // an uplink. It is also a transport fault rather than a data fault — the same
            // class as a declared length that overruns the buffer — so it reports as
            // Truncated and sends the next reader to the receive path. Issue #37.
            notes.truncated_record      = true;
            notes.truncated_record_type = type;
            notes.truncated_record_left = records_end - i;
            out                         = BatteryReading{};
            return BatteryResult::Truncated;
        } else {
            // A record type this build has never heard of has an unknown width, so there is no
            // way to find where the next record begins. Parsing stops rather than guessing:
            // skipping a byte at a time would leave the scan pointing into the middle of a
            // value and read the tail of one record as the head of another, producing a
            // voltage or temperature that looks entirely plausible and is simply wrong.
            //
            // Unlike the truncated-record branch above, what was decoded ahead of it is kept.
            // Those records were complete and checksum-covered; the frame is longer than this
            // build understands, which is answered by adding a decoder, not by distrusting
            // bytes that parsed cleanly.
            notes.unknown_record      = true;
            notes.unknown_record_type = type;
            break;
        }
    }

    if (!out.any()) {
        return BatteryResult::NoRecords;
    }

    // A verified frame full of zeros is not a measurement, and this is the one place that
    // distinction can still be made before the number becomes an uplink.
    //
    // The pack is powered by the cell it is reporting on. It cannot be at 0.00 V and also be
    // driving this wire, so a zero voltage means "I have no sample", not "the battery is
    // flat". The sentinel is "every physical record in this frame read zero", not "the voltage
    // read zero": a reply carrying capacity, current and temperature but no voltage record
    // would otherwise have no sentinel to trip, and a template full of placeholders would be
    // encoded as a real 0 A / 0 % / 0.0 degC reading.
    //
    // Judging the whole frame keeps every genuine zero — 0 A is a real idle current, 0 % is a
    // real (alarming) charge state, and 0.0 degC is an ordinary temperature in the woods — as
    // long as one other record in the same frame is non-zero, which for a pack powered by the
    // cell it measures is always true of the voltage.
    //
    // CITE(bench): docs/EVIDENCE.md — the SENDAT reply captured on 3d3425d verified its
    //   checksum and decoded cleanly to voltage 0, current 0, capacity 0, temperature 0 while
    //   the pack was demonstrably alive and metered at 11.6 V.
    // CITE(prior-art): [CIT-MESHTASTIC-9154] @ 02050a4 RAK9154Sensor.cpp — the reference
    //   consumer applies no such guard; it gets away with it because it re-reads forever on a
    //   50 ms tick. A node that wakes, reads once and sleeps for an hour has no second chance.
    if (!any_nonzero) {
        out = BatteryReading{};
        return BatteryResult::Unsampled;
    }

    return BatteryResult::Ok;
}
