/**
 * Payload decoder for RAK2560 WisNode Sensor Hub + RK900-09 Miniature Ultrasonic Weather Station.
 * Device: https://docs.rakwireless.com/product-categories/wisnode/weather-station/datasheet/
 *
 * Supports both native RK900-09 probe types AND Generic Probe IO types:
 *
 *   Wind Speed:      native 190 (0xBE) or generic 158 (0x9E)  2B × 0.01 → m/s
 *   Wind Direction:  native 191 (0xBF) or generic 159 (0x9F)  2B × 1   → °
 *   Temperature:     103 (0x67)                                2B × 0.1 → °C
 *   Humidity:        native 112 (0x70) 2B×0.1  or generic 104 (0x68) 1B×0.5 → %RH
 *   Pressure:        115 (0x73)                                2B × 0.1 → hPa
 *   Hub voltage:     187 (0xBB)                                2B × 0.01 → V
 *
 * RAK9154 Solar Battery (when connected to hub):
 *   Battery capacity   type 0xB8 (184) × 1    → %
 *   Battery current    type 0xB9 (185) × 0.01 → A (signed; positive = charging)
 *   Battery voltage    type 0xBA (186) × 0.01 → V
 *
 * Installation north offset: sensor north arrow points 230° true (SW).
 * All wind_direction values are corrected: true = (raw + 230) % 360.
 *
 * For TTN/TTS: Application payload formatter → Uplink → JavaScript.
 */
"use strict";

var WIND_DIR_OFFSET = 230;

var CHANNEL_NAMES = {
  // RK900-09 weather station
  "temperature_3": "wx_temperature",
  "barometer_5": "wx_barometer",
  "wind_direction_2": "wx_wind_direction",
  "wind_speed_1": "wx_wind_speed",
  "humidity_prec_4": "wx_humidity",
  // RAK9154 Solar Battery Lite
  "temperature_24": "batt_temperature",
  "capacity_batt_23": "batt_capacity",
  "dc_current_batt_22": "batt_current",
  "dc_voltage_batt_21": "batt_voltage",
  // Sensor Hub
  "hub_voltage_77": "hub_voltage",
  "rak_serial_0": "rak_serial"
};

function decodeUplink(input) {
  var warnings = [];
  var errors = [];
  var bytes = ensureByteArray(input.bytes);
  if (bytes.length === 0) {
    return { data: {}, warnings: warnings, errors: ["Empty or invalid payload"] };
  }
  try {
    var data = lppDecodeToFlat(bytes);
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
      result.push((typeof b === "number" && !isNaN(b)) ? (b & 0xff) : 0);
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
    var channel = bytes[i++];
    var typeId = bytes[i++];
    var type = WX_TYPES[typeId];
    if (!type) {
      throw new Error("Unknown sensor type: " + typeId);
    }
    var raw = bytes.slice(i, i + type.size);
    i += type.size;
    var value = arrayToDecimal(raw, type.signed, type.divisor);
    sensors.push({ channel: channel, name: type.name, value: value });
  }
  return sensors;
}

function lppDecodeToFlat(bytes) {
  var sensors = lppDecode(bytes);
  var data = {};
  var windSpeed = null;
  for (var idx = 0; idx < sensors.length; idx++) {
    var s = sensors[idx];
    var val = s.value;
    if (s.name === "wind_direction" && WIND_DIR_OFFSET !== 0) {
      val = (val + WIND_DIR_OFFSET) % 360;
    }
    var key = s.name + "_" + s.channel;
    data[CHANNEL_NAMES[key] || key] = val;
    if (s.name === "wind_speed") windSpeed = val;
  }
  if (windSpeed === 0) {
    var dirKey = CHANNEL_NAMES["wind_direction_2"] || "wind_direction_2";
    if (data.hasOwnProperty(dirKey)) data[dirKey] = null;
  }
  return data;
}
