#include "radio.h"

#include "features.h"
#include "session.h"

#if FEATURE_RADIO

#include <Arduino.h>
#include <LoRaWan-Arduino.h>

#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#warning "secrets.h missing — building with placeholder keys. The node will not join."
#endif

namespace {

// The port downlinks must arrive on. Fixed so an unrelated message on another port cannot
// be mistaken for a configuration change.
constexpr uint8_t kCommandPort = 10;
constexpr uint8_t kUplinkPort  = 2;

// Opcodes. A leading byte makes the format extensible: a firmware that does not recognize
// an opcode ignores it rather than misreading it, so adding one later cannot break a node
// already in the field.
constexpr uint8_t kCmdSetInterval   = 0x01;
constexpr uint8_t kCmdRequestStatus = 0x03;

// Bounded join attempt. If the gateway is not there, the answer is to sleep and try later,
// not to keep the radio awake hunting for it.
constexpr uint32_t kJoinTimeoutMs = 30000;

// How many join requests the MAC sends per lmh_join() call. The library expects the
// application to supply this. One, deliberately: retrying inside the MAC keeps the radio
// powered with no gap, whereas returning after a single attempt lets this class sleep
// between tries. Same number of attempts over a day, a small fraction of the current.
#define JOINREQ_NBTRIALS 1

// Backoff bounds. The cap is deliberately below the default reporting interval so a
// recovering network is noticed promptly, and the growth stops the node spending its whole
// energy budget transmitting into an outage.
constexpr uint32_t kBackoffFirstSeconds = 60;
constexpr uint32_t kBackoffMaxSeconds   = 3600;

uint8_t s_dev_eui[8]  = OTAA_DEVEUI;
uint8_t s_app_eui[8]  = OTAA_APPEUI;
uint8_t s_app_key[16] = OTAA_APPKEY;

volatile bool s_joined       = false;
volatile bool s_join_failed  = false;
volatile bool s_have_downlink = false;

uint8_t s_rx_buf[64];
uint8_t s_rx_len  = 0;
uint8_t s_rx_port = 0;

uint8_t  s_tx_buf[kMaxPayloadBytes];
lmh_app_data_t s_tx_data = {s_tx_buf, 0, kUplinkPort, 0, 0};

void on_joined()
{
    s_joined = true;
    lmh_class_request(CLASS_A);
}

void on_join_failed()
{
    s_join_failed = true;
}

void on_rx(lmh_app_data_t *app_data)
{
    if (app_data == nullptr || app_data->buffsize == 0) {
        return;
    }
    s_rx_len = (app_data->buffsize > sizeof(s_rx_buf)) ? sizeof(s_rx_buf)
                                                       : (uint8_t)app_data->buffsize;
    memcpy(s_rx_buf, app_data->buffer, s_rx_len);
    s_rx_port       = app_data->port;
    s_have_downlink = true;
}

void on_class_confirm(DeviceClass_t /*device_class*/) {}

uint8_t get_battery_level()
{
    // The MAC can report battery level to the network. The real figure comes from the
    // pack over its own link, and wiring that through here would mean the radio layer
    // reaching into the sensor layer for a field nothing acts on. Reported as unknown.
    return 255;
}

void get_unique_id(uint8_t *id)
{
    // Only consulted when joining by ABP, which this node does not use.
    memcpy(id, s_dev_eui, 8);
}

uint32_t get_random_seed()
{
    return (uint32_t)((NRF_FICR->DEVICEID[0]) ^ (NRF_FICR->DEVICEID[1]));
}

lmh_callback_t s_callbacks = {get_battery_level, get_unique_id, get_random_seed,
                              on_rx,             on_joined,     on_class_confirm,
                              on_join_failed};

// Adaptive data rate is left on. It lets the network settle the node onto the fastest rate
// the link supports, which is both less airtime and less transmit current per uplink.
lmh_param_t s_params = {LORAWAN_ADR_ON,        DR_3,
                        LORAWAN_PUBLIC_NETWORK, JOINREQ_NBTRIALS,
                        LORAWAN_DEFAULT_TX_POWER, LORAWAN_DUTYCYCLE_OFF};

} // namespace

bool Radio::begin()
{
    if (m_started) {
        return true;
    }

    if (lora_rak4630_init() != 0) {
        LOGLN(F("   radio   : transceiver init failed"));
        return false;
    }

    lmh_setDevEui(s_dev_eui);
    lmh_setAppEui(s_app_eui);
    lmh_setAppKey(s_app_key);

    if (lmh_init(&s_callbacks, s_params, true, CLASS_A, LORAMAC_REGION_US915) != 0) {
        LOGLN(F("   radio   : MAC init failed"));
        return false;
    }

    m_started = true;

    // A session saved before the last reset makes this boot a non-event: the node is
    // already joined and can send its next reading without needing the gateway to be
    // reachable for a handshake first.
    m_joined = session::restore();

    return true;
}

bool Radio::ensure_joined()
{
    if (!m_started && !begin()) {
        return false;
    }
    if (m_joined) {
        return true;
    }

    s_joined      = false;
    s_join_failed = false;

    lmh_join();

    // Bounded wait. The loop exits on success, on an explicit failure, or on the timeout —
    // never on "keep trying", which is the shape that drains the pack during an outage.
    const uint32_t start = millis();
    while (!s_joined && !s_join_failed && (millis() - start) < kJoinTimeoutMs) {
        delay(100);
    }

    m_joined = s_joined;

    if (m_joined) {
        LOGF("   radio   : joined after %lu ms\n", (unsigned long)(millis() - start));
        m_failures = 0;

        // Store it immediately. A join that is never saved has to be repeated after the
        // next reset, which is the situation this exists to avoid.
        session::save();
    } else {
        m_failures++;
        LOGF("   radio   : join failed (attempt %lu, next try in %lu s)\n",
             (unsigned long)m_failures, (unsigned long)backoff_seconds());
    }

    return m_joined;
}

bool Radio::send(const Payload &p)
{
    if (!m_joined || p.empty()) {
        return false;
    }

    const size_t len = (p.length() > sizeof(s_tx_buf)) ? sizeof(s_tx_buf) : p.length();
    memcpy(s_tx_buf, p.bytes(), len);
    s_tx_data.buffsize = len;
    s_tx_data.port     = kUplinkPort;

    const lmh_error_status status = lmh_send(&s_tx_data, LMH_UNCONFIRMED_MSG);
    if (status != LMH_SUCCESS) {
        m_failures++;
        LOGF("   radio   : send failed (%d), backoff %lu s\n", (int)status,
             (unsigned long)backoff_seconds());

        // A send failing after a successful join usually means the session is no longer
        // good — the network may have forgotten it while the node was unreachable. Drop
        // the joined state so the next cycle rejoins instead of failing the same way
        // forever, and discard the stored copy so a reset in the meantime does not restore
        // the same dead session.
        m_joined = false;
        session::forget();
        return false;
    }

    m_failures = 0;
    LOGF("   radio   : sent %u bytes on port %u\n", (unsigned)len, kUplinkPort);

    // Periodically advance the stored frame counter. Almost always a no-op; it writes
    // roughly once a month at the default interval.
    session::maybe_save_counter();

    // Class A opens its two receive windows immediately after the uplink. Staying awake
    // through them is the only chance to hear anything at all.
    delay(3000);
    return true;
}

bool Radio::take_downlink(DownlinkCommand &out)
{
    if (!s_have_downlink) {
        return false;
    }
    s_have_downlink = false;

    if (s_rx_port != kCommandPort || s_rx_len < 1) {
        LOGF("   radio   : ignoring %u bytes on port %u\n", s_rx_len, s_rx_port);
        return false;
    }

    const uint8_t opcode = s_rx_buf[0];

    if (opcode == kCmdSetInterval && s_rx_len >= 5) {
        out.set_interval = true;
        out.interval_value = ((uint32_t)s_rx_buf[1] << 24) | ((uint32_t)s_rx_buf[2] << 16) |
                             ((uint32_t)s_rx_buf[3] << 8) | (uint32_t)s_rx_buf[4];
        LOGF("   radio   : downlink — set interval %lu s\n",
             (unsigned long)out.interval_value);
        return true;
    }

    if (opcode == kCmdRequestStatus) {
        out.request_status = true;
        LOGLN(F("   radio   : downlink — status requested"));
        return true;
    }

    // An unrecognized opcode is ignored rather than treated as an error. That is what lets
    // a newer command be sent to an older node without breaking it.
    LOGF("   radio   : downlink — unknown opcode 0x%02X, ignored\n", opcode);
    return false;
}

uint32_t Radio::backoff_seconds() const
{
    if (m_failures == 0) {
        return 0;
    }

    uint32_t seconds = kBackoffFirstSeconds;
    for (uint32_t i = 1; i < m_failures && seconds < kBackoffMaxSeconds; i++) {
        seconds *= 2;
    }
    return (seconds > kBackoffMaxSeconds) ? kBackoffMaxSeconds : seconds;
}

#else // !FEATURE_RADIO

bool     Radio::begin() { return false; }
bool     Radio::ensure_joined() { return false; }
bool     Radio::send(const Payload &) { return false; }
bool     Radio::take_downlink(DownlinkCommand &) { return false; }
uint32_t Radio::backoff_seconds() const { return 0; }

#endif
