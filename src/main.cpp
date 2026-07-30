/*
 * Stage 0 bring-up.
 *
 * Proves four things and deliberately nothing else: the toolchain builds, the vendored
 * board definition is correct, the bootloader accepts the image, and the USB serial
 * path works. There is no radio, no Modbus, and no sleep here on purpose — when the
 * first flash of brand-new hardware fails, the suspect list should be four items long,
 * not twenty.
 *
 * Behavior contract for the real firmware: docs/FIRMWARE_SPEC.md
 * Framework choice and rationale:        docs/decisions/ADR-0003-firmware-framework.md
 * Bring-up results get recorded in:      docs/EVIDENCE.md
 */

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

// CITE(datasheet): [CIT-RAK-PIO-BSP] rakwireless/variants/rak4630/variant.h lines 70-79
//   define LED_GREEN = PIN_LED1 and give the active level as LED_STATE_ON.
// The active level is read from the variant rather than assumed, because it is not the
// same on every WisBlock base board.
static const uint8_t kStatusLed = LED_GREEN;

// CITE(datasheet): [CIT-RAK4631] the RAK4631's nRF52840 provides USB device support
//   directly — there is no separate USB-to-serial bridge chip on the module.
// The console therefore runs over USB CDC, where the host ignores the line rate. This
// value only has to match monitor_speed in platformio.ini.
static const unsigned long kConsoleBaud = 115200;

// CITE(prior-art): [CIT-TINYUSB] Adafruit TinyUSB supplies the USB CDC device class.
// CITE(prior-art): [CIT-PIO-RAK4631-USB] it must be linked with lib_archive = no, or the
//   port never enumerates and the board looks dead after a successful flash.
// A deployed node boots with no USB host attached. Blocking forever on the port is the
// standard way to make a working headless board look bricked, so the wait is bounded and
// boot continues either way.
static const unsigned long kConsoleWaitMs = 5000;

static const char kFirmwareVersion[] = "0.0.1+stage0";

static uint32_t heartbeat = 0;

void setup()
{
	pinMode(kStatusLed, OUTPUT);
	digitalWrite(kStatusLed, LED_STATE_ON);

	Serial.begin(kConsoleBaud);
	const unsigned long start = millis();
	while (!Serial && (millis() - start) < kConsoleWaitMs) {
		delay(10);
	}

	Serial.println();
	Serial.println(F("=== rak-sensor-node — stage 0 bring-up ==="));
	Serial.print(F("firmware : "));
	Serial.println(kFirmwareVersion);
	Serial.print(F("built    : "));
	Serial.print(F(__DATE__));
	Serial.print(' ');
	Serial.println(F(__TIME__));
	Serial.println(F("board    : RAK4631 (nRF52840 + SX1262) / RAK19007 base"));
	Serial.println(F("scope    : LED + USB serial only. No radio, no Modbus, no sleep."));
	Serial.println(F("next     : stage 1 — RK900 Modbus read over RAK5802."));
	Serial.println();
}

void loop()
{
	// Asymmetric blink: a short pulse is visually distinct from the bootloader's own
	// slow fade, so "my image is running" is unambiguous at a glance.
	digitalWrite(kStatusLed, LED_STATE_ON);
	delay(80);
	digitalWrite(kStatusLed, !LED_STATE_ON);
	delay(920);

	if (++heartbeat % 5 == 0) {
		Serial.print(F("alive — "));
		Serial.print(heartbeat);
		Serial.println(F("s"));
	}
}
