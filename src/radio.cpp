#include "radio.h"

#include "build_features.h"
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

// The Things Network listens on US915 sub-band 2 — the eight 125 kHz channels 8 to 15,
// plus the 500 kHz channel 65. The region defines 72 channels in total, so leaving the MAC
// to use all of them means most transmissions land where nothing is listening.
constexpr uint8_t kSubBand = 2;

// Bounded join attempt. If the gateway is not there, the answer is to sleep and try later,
// not to keep the radio awake hunting for it.
constexpr uint32_t kJoinTimeoutMs = 30000;

// Margin added after the second receive window closes, covering clock drift between the
// node and the gateway plus the time the MAC needs to finish handling anything received.
constexpr uint32_t kRxWindowMarginMs = 1500;

// Used only if the MAC cannot report its receive delays. Chosen to cover the 5 s delay the
// network assigns rather than the 1 s specification default: waiting too long costs a
// little current, while waking too early misses every downlink the node will ever get.
constexpr uint32_t kRxWindowFallbackMs = 7000;

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

// Consecutive send failures before the session is treated as dead. Rejoining is expensive
// and only helps when the network has genuinely forgotten the node, so it should follow a
// pattern rather than a single event.
constexpr uint32_t kFailuresBeforeRejoin = 3;

// Most-significant byte first, matching what the network server displays. This library
// requires that order and reverses the bytes itself when it builds the join request; its own
// example is emphatic about it ("OTAA keys !!!! KEYS ARE MSB !!!!" in
// examples/LoRaWAN_OTAA_ABP_RAK3401/src/main.cpp). Supplying them reversed — the convention a
// different Arduino LoRaWAN library uses — produces a device that transmits correctly forever
// and is never once recognised. [CIT-SX126X-ARDUINO]
uint8_t s_dev_eui[8]  = OTAA_DEVEUI;
uint8_t s_app_eui[8]  = OTAA_APPEUI;
uint8_t s_app_key[16] = OTAA_APPKEY;

char s_dev_eui_text[17] = {0};
char s_app_eui_text[17] = {0};

void eui_to_hex(const uint8_t *eui, char *out)
{
    static const char kHex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < 8; i++) {
        out[i * 2]     = kHex[(eui[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kHex[eui[i] & 0x0F];
    }
    out[16] = '\0';
}

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

const char *Radio::deveui_hex() const
{
    eui_to_hex(s_dev_eui, s_dev_eui_text);
    return s_dev_eui_text;
}

const char *Radio::appeui_hex() const
{
    eui_to_hex(s_app_eui, s_app_eui_text);
    return s_app_eui_text;
}

uint8_t Radio::sub_band() const
{
    return kSubBand;
}

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

    // US915 defines 72 channels, and the network listens on eight of them. Without this
    // the MAC picks from all 72, so most transmissions go out on frequencies no gateway is
    // tuned to — joins fail for no visible reason, and after a session restore the node
    // would report every uplink as sent while roughly seven in eight reached nobody.
    //
    // Set on every boot rather than persisted, because it is a property of the network
    // this node talks to, not of the session it happens to hold.
    if (!lmh_setSubBandChannels(kSubBand)) {
        LOGF("   radio   : could not select sub-band %u\n", kSubBand);
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

        // The backoff is the sleep between cycles, not the wait until the next attempt. While
        // both sensors are silent main.cpp reaches ensure_joined() only on some cycles, so the
        // real wait is the backoff multiplied by however many cycles that is — up to the
        // heartbeat cadence. Reporting the backoff alone understated it by hours and cost
        // bring-up debugging time, which is the whole of #24.
        //
        // Stated as an upper bound because a sensor recovering brings the next attempt forward
        // to the very next cycle. A bound that is honest about its direction is worth more than
        // a precise-looking number that is wrong in the direction of optimism. Refs #24.
        const uint32_t next_attempt_seconds = backoff_seconds() * m_cycles_until_next_call;
        LOGF("   radio   : join failed (attempt %lu, next attempt within %lu s — %lu cycle(s) "
             "at %lu s)\n",
             (unsigned long)m_failures, (unsigned long)next_attempt_seconds,
             (unsigned long)m_cycles_until_next_call, (unsigned long)backoff_seconds());
    }

    return m_joined;
}

size_t Radio::max_payload() const
{
    LoRaMacTxInfo_t info = {0, 0};

    // Asking about a zero-length frame returns the allowance without judging the size.
    // The call reports an error when nothing can currently be sent at all, but it still
    // fills in the sizes, so the values are usable either way.
    (void)LoRaMacQueryTxPossible(0, &info);

    const size_t allowed = info.MaxPossiblePayload;

    // A zero here means the MAC had nothing to say — most likely it is not joined yet.
    // Assume the worst rate rather than the best: too small drops a few fields, too large
    // gets the whole uplink refused.
    if (allowed == 0) {
        return kMinDataRatePayloadBytes;
    }
    return (allowed > kMaxPayloadBytes) ? kMaxPayloadBytes : allowed;
}

bool Radio::send(const Payload &p)
{
    // An empty payload is permitted here and is not a mistake. When both sensors are silent
    // the node still owes the network proof that it is alive, and a zero-length uplink is the
    // cheapest way to say so — see the total-silence policy in main.cpp. Refusing it here was
    // dropping that uplink without a word, which left the node looking dead exactly when
    // knowing otherwise mattered most.
    if (!m_joined) {
        return false;
    }

    // Discard anything left over from a previous cycle before opening new RX windows.
    // take_downlink() clears the flag when it consumes a command, but nothing guarantees it is
    // called: a cycle that returns early leaves the flag set, and the next cycle then applies a
    // command the network sent for the last one. For a set-interval command that means the node
    // silently adopts a stale interval nobody sent it. This restores what radio.h promises —
    // that a downlink belongs to the windows after *this* send and no other.
    s_have_downlink = false;
    s_rx_len        = 0;
    s_rx_port       = 0;

    // Checked before the frame is handed to the MAC, because lmh_send() consumes the counter
    // and there is no putting it back. A save withheld by the brownout gate leaves the stored
    // counter behind the wire, and every uplink past that point is one a reset replays — which
    // the network discards without telling anybody. Refs #51.
    //
    // Deliberately not counted as a failure. m_failures drives the rejoin escape, and a rejoin
    // is both the most expensive thing this node can do and useless here: the session is fine,
    // it is the flash write that is unavailable, and only the pack recovering fixes that.
    if (!session::counter_headroom_ok()) {
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

        // Deliberately NOT rejoining on a single failure. The library reports one generic
        // error for every cause — a busy MAC, a payload too large for the current data
        // rate, a momentary refusal — so a failure says nothing about whether the session
        // is still good. Rejoining on the first one turns a recoverable hiccup into a
        // rejoin on every cycle, which is the most expensive loop this node can enter and
        // does not fix any of those causes anyway.
        //
        // Several in a row is different: that pattern does suggest the network has
        // forgotten the session, and a rejoin is the only way out.
        if (m_failures >= kFailuresBeforeRejoin) {
            LOGF("   radio   : %lu failures in a row — dropping the session and rejoining\n",
                 (unsigned long)m_failures);
            m_joined = false;
            session::forget();

            // Reset the MAC as well as the session. Dropping the session leaves the MAC in
            // whatever state produced three failures in a row, and RAK's own framework ships
            // this call specifically because rejoining on top of a wedged MAC was not enough:
            // re_init_lorawan() is titled "Workaround for bug after NAK" and its whole body is
            // lmh_reset_mac(). Without it the rejoin can loop against a MAC that cannot
            // succeed until the watchdog happens to catch it — cheap to avoid, so avoid it.
            //
            // CITE(prior-art): beegee-tokyo/WisBlock-API-V2 src/lorawan.cpp:210-216 —
            //   `re_init_lorawan()` = `lmh_reset_mac()`, documented as the post-NAK MAC bug
            //   workaround. [CIT-WISBLOCK-API2] — docs/CITATIONS.md
            // CITE(prior-art): [CIT-SX126X-ARDUINO] — `lmh_reset_mac()` is the library's own
            //   MAC re-initialisation entry point, so this is its intended use rather than a
            //   reach into internals.
            // CITE(bench): docs/reviews/2026-08-12_rak_reference_benchmark.md §2 — the gap
            //   analysis that found this call absent from the whole tree.
            lmh_reset_mac();
        }
        return false;
    }

    m_failures = 0;
    LOGF("   radio   : sent %u bytes on port %u\n", (unsigned)len, kUplinkPort);

    // Class A opens two receive windows after each uplink, and those windows are the only
    // downlink opportunity this node ever gets. Sleeping before the second one closes
    // means the transceiver is powered down while the answer is being sent — the setting
    // never arrives, and from a distance the downlink simply looks ignored.
    //
    // The delay is read from the MAC rather than assumed. The specification default is one
    // second, but the network assigns five in the join accept, so a fixed short wait would
    // silently miss every downlink on a real network while looking correct against the
    // specification.
    delay(rx_window_ms());
    return true;
}

uint32_t Radio::rx_window_ms() const
{
    MibRequestConfirm_t req;
    memset(&req, 0, sizeof(req));
    req.Type = MIB_RECEIVE_DELAY_2;

    if (LoRaMacMibGetRequestConfirm(&req) != LORAMAC_STATUS_OK ||
        req.Param.ReceiveDelay2 == 0) {
        return kRxWindowFallbackMs;
    }

    return req.Param.ReceiveDelay2 + kRxWindowMarginMs;
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

    // Exact lengths, not minimums. A command that changes how often the node reports
    // should be ignored when it does not look exactly as expected — a message of the wrong
    // length is not a command with extra bytes, it is a message we have misunderstood, and
    // acting on half of it is worse than ignoring all of it.
    if (opcode == kCmdSetInterval && s_rx_len == 5) {
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
