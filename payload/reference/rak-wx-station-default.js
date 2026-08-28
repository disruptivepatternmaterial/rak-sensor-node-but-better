/**
 * Payload decoder for RAK2560 WisNode Sensor Hub with any combination of:
 *
 *   1. RK900-09 Miniature Ultrasonic Weather Station probe
 *        wind_speed (ch1), wind_direction (ch2), temperature (ch3),
 *        humidity_prec (ch4), barometer (ch5)         → wx_* fields
 *   2. Atmospheric temperature & humidity probe
 *        temperature (ch1, type 103), humidity (ch2, type 104)
 *                                                     → env_* fields
 *   3. RAK9154 Solar Battery Lite
 *        dc_voltage_batt (ch21), dc_current_batt (ch22),
 *        capacity_batt (ch23), temperature (ch24),
 *        error code (ch25, type 243), BMS firmware (ch26, type 243)
 *                                                     → batt_* fields
 *
 *   Hub itself: hub_voltage (ch77, type 187).
 *
 * Each uplink frame carries ONE probe's readings plus that probe's serial
 * number (ch0, type 126). The serial is emitted as wx_serial / env_serial /
 * batt_serial depending on which probe's channels are present in the frame,
 * falling back to rak_serial when the frame has no probe data (e.g. a
 * serial + hub_voltage-only frame). This avoids a single rak_serial field
 * flip-flopping between probe serials on multi-probe hubs.
 *
 * LPP types decoded (channel byte, type byte, big-endian value):
 *   Wind Speed:      native 190 (0xBE) or generic 158 (0x9E)  2B × 0.01 → m/s
 *   Wind Direction:  native 191 (0xBF) or generic 159 (0x9F)  2B × 1   → °
 *   Temperature:     103 (0x67) signed                        2B × 0.1 → °C
 *   Humidity:        native 112 (0x70) 2B×0.1  or generic 104 (0x68) 1B×0.5 → %RH
 *   Pressure:        115 (0x73)                                2B × 0.1 → hPa
 *   Hub voltage:     187 (0xBB)                                2B × 0.01 → V
 *   Battery capacity 184 (0xB8) × 1 → %
 *   Battery current  185 (0xB9) × 0.01 → A (signed; positive = charging)
 *   Battery voltage  186 (0xBA) × 0.01 → V
 *   Probe serial     126 (0x7E) 3B
 *
 * WIND_DIR_OFFSET is the per-install north correction, edited per device:
 * this install's sensor north arrow points 230° true (SW), so
 * true = (raw + 230) % 360. Set to 0 for a correctly-oriented install.
 *
 * For TTN/TTS: End device payload formatter → Uplink → JavaScript
 * (set per device because WIND_DIR_OFFSET differs per install).
 */
"use strict";

var WIND_DIR_OFFSET = 230;

/**
 * Raw "typeName_channel" key → standard field name + owning probe.
 * The probe tag is used only to name the serial field for the frame.
 */
var CHANNELS = {
  // RK900-09 weather station probe
  "wind_speed_1": { name: "wx_wind_speed", probe: "wx" },
  "wind_direction_2": { name: "wx_wind_direction", probe: "wx" },
  "temperature_3": { name: "wx_temperature", probe: "wx" },
  "humidity_prec_4": { name: "wx_humidity", probe: "wx" },
  "barometer_5": { name: "wx_barometer", probe: "wx" },
  // Atmospheric temperature & humidity probe
  "temperature_1": { name: "env_temperature", probe: "env" },
  "humidity_2": { name: "env_humidity", probe: "env" },
  // RAK9154 Solar Battery Lite
  "dc_voltage_batt_21": { name: "batt_voltage", probe: "batt" },
  "dc_current_batt_22": { name: "batt_current", probe: "batt" },
  "capacity_batt_23": { name: "batt_capacity", probe: "batt" },
  "temperature_24": { name: "batt_temperature", probe: "batt" },
  // Type 243 (raw2byte) per RAK SensorHub.js: "Solar Battery Errors; BMS
  // Firmware version". Observed on live hubs: ch25 constant 0 (no errors),
  // ch26 constant 3 (BMS firmware v3).
  "raw2byte_25": { name: "batt_error_code", probe: "batt" },
  "raw2byte_26": { name: "batt_bms_firmware", probe: "batt" },
  // Sensor Hub
  "hub_voltage_77": { name: "hub_voltage" },
  "rak_serial_0": { name: "rak_serial", isSerial: true }
};

function decodeUplink(input) {
  var warnings = [];
  var errors = [];
  var bytes = ensureByteArray(input.bytes);
  if (bytes.length === 0) {
    return { data: {}, warnings: warnings, errors: ["Empty or invalid payload"] };
  }
  try {
    var data = lppDecodeToFlat(bytes, warnings);
    return { data: data, warnings: warnings, errors: errors };
  } catch (e) {
    return {
      data: {},
      warnings: warnings,
      errors: [e.message || "Decode failed"]
    };
  }
}

function ensureByteArray(bytes) {
  if (bytes === null || bytes === undefined) return [];
  if (typeof bytes === "string") {
    var s = bytes.replace(/\s/g, "");
    if (s.length === 0) return [];
    if (/^[0-9A-Fa-f]+$/.test(s) && s.length % 2 === 0) {
      var out = [];
      for (var i = 0; i < s.length; i += 2) {
        var n = parseInt(s.substr(i, 2), 16);
        if (isNaN(n)) return [];
        out.push(n & 0xff);
      }
      return out;
    }
    try {
      var binary = atob(s);
      var arr = [];
      for (var k = 0; k < binary.length; k++) arr.push(binary.charCodeAt(k) & 0xff);
      return arr;
    } catch (e) { return []; }
  }
  if (typeof bytes.length === "number") {
    var result = [];
    for (var j = 0; j < bytes.length; j++) {
      var b = bytes[j];
      // A non-numeric, NaN, fractional, or out-of-range element means the
      // frame is corrupt. Reject the whole frame (mirrors the un-decodable
      // string paths above) instead of masking to a fabricated byte
      // (256 & 0xff -> 0x00, -1 & 0xff -> 0xFF) that would decode into a
      // plausible wrong value.
      if (typeof b !== "number" || isNaN(b) || b < 0 || b > 0xff || b !== Math.floor(b)) {
        return [];
      }
      result.push(b);
    }
    return result;
  }
  return [];
}

var WX_TYPES = {
  103: { size: 2, name: "temperature", signed: true, divisor: 10 },
  104: { size: 1, name: "humidity", signed: false, divisor: 2 },
  112: { size: 2, name: "humidity_prec", signed: false, divisor: 10 },
  115: { size: 2, name: "barometer", signed: false, divisor: 10 },
  116: { size: 2, name: "voltage", signed: false, divisor: 100 },
  120: { size: 1, name: "percentage", signed: false, divisor: 1 },
  126: { size: 3, name: "rak_serial", signed: false, divisor: 1 },
  152: { size: 1, name: "capacity", signed: false, divisor: 1 },
  153: { size: 2, name: "dc_current", signed: false, divisor: 100 },
  154: { size: 2, name: "dc_voltage", signed: false, divisor: 100 },
  158: { size: 2, name: "wind_speed", signed: false, divisor: 100 },
  159: { size: 2, name: "wind_direction", signed: false, divisor: 1 },
  184: { size: 1, name: "capacity_batt", signed: false, divisor: 1 },
  185: { size: 2, name: "dc_current_batt", signed: true, divisor: 100 },
  186: { size: 2, name: "dc_voltage_batt", signed: false, divisor: 100 },
  187: { size: 2, name: "hub_voltage", signed: false, divisor: 100 },
  190: { size: 2, name: "wind_speed", signed: false, divisor: 100 },
  191: { size: 2, name: "wind_direction", signed: false, divisor: 1 },
  243: { size: 2, name: "raw2byte", signed: false, divisor: 1 }
};

function arrayToDecimal(stream, isSigned, divisor) {
  var value = 0;
  for (var i = 0; i < stream.length; i++) {
    if (stream[i] > 0xff) throw new Error("Byte value overflow");
    value = (value << 8) | stream[i];
  }
  if (isSigned) {
    var edge = 1 << (stream.length * 8);
    var max = (edge - 1) >> 1;
    value = (value > max) ? value - edge : value;
  }
  return value / divisor;
}

function lppDecode(bytes) {
  var sensors = [];
  var i = 0;
  while (i < bytes.length) {
    if (i + 2 > bytes.length) {
      throw new Error("Truncated payload: dangling channel byte at offset " + i);
    }
    var channel = bytes[i++];
    var typeId = bytes[i++];
    var type = WX_TYPES[typeId];
    if (!type) {
      throw new Error("Unknown sensor type: " + typeId);
    }
    // Reject truncated frames: bytes.slice() past the end returns a short
    // array and arrayToDecimal would compute a plausible wrong value from it.
    if (i + type.size > bytes.length) {
      throw new Error("Truncated payload: type " + typeId + " needs " + type.size +
        " bytes at offset " + i + ", only " + (bytes.length - i) + " left");
    }
    var raw = bytes.slice(i, i + type.size);
    i += type.size;
    var value = arrayToDecimal(raw, type.signed, type.divisor);
    sensors.push({ channel: channel, name: type.name, value: value });
  }
  return sensors;
}

function lppDecodeToFlat(bytes, warnings) {
  var sensors = lppDecode(bytes);
  var data = {};
  var probe = null;
  var mixedProbes = false;
  var hasSerial = false;
  var serialValue = null;

  for (var idx = 0; idx < sensors.length; idx++) {
    var s = sensors[idx];
    var key = s.name + "_" + s.channel;
    var mapping = CHANNELS[key];

    if (mapping && mapping.isSerial) {
      hasSerial = true;
      serialValue = s.value;
      continue;
    }

    var val = s.value;
    if (s.name === "wind_direction" && WIND_DIR_OFFSET !== 0) {
      val = (val + WIND_DIR_OFFSET) % 360;
    }

    if (mapping) {
      data[mapping.name] = val;
      if (mapping.probe) {
        if (probe === null) {
          probe = mapping.probe;
        } else if (probe !== mapping.probe) {
          mixedProbes = true;
        }
      }
    } else {
      // Unmapped channel/type: pass the reading through under its raw key
      // rather than dropping it, and flag it so the gap gets mapped.
      data[key] = val;
      warnings.push("Unmapped channel/type key: " + key);
    }
  }

  if (hasSerial) {
    var serialKey = (probe !== null && !mixedProbes) ? probe + "_serial" : "rak_serial";
    data[serialKey] = serialValue;
  }

  // NOTE (2026-07-02): a previous revision nulled wind_direction whenever
  // wind_speed was 0. That stripped a value the sensor actually reported
  // (no-fabricated-data rule); the vane direction now passes through as-is
  // and any calm-air interpretation belongs downstream.
  return data;
}
