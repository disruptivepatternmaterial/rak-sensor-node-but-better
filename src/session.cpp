#include "session.h"

#include "build_features.h"

#if FEATURE_RADIO

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <LoRaWan-Arduino.h>
#include <string.h>

using namespace Adafruit_LittleFS_Namespace;

namespace {

constexpr char kPath[] = "/session.bin";

// Bumping the version invalidates every stored session. Do that whenever the struct or the
// key derivation changes — reading an old layout as a new one would produce a session that
// looks valid and cannot possibly work.
constexpr uint32_t kMagic   = 0x4C575353; // "LWSS"
constexpr uint16_t kVersion = 2;

struct Stored {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t dev_addr;
    uint8_t  nwk_skey[16];
    uint8_t  app_skey[16];
    uint32_t uplink_counter;
    uint32_t downlink_counter;

    // The receive-window delays the network assigned during the join. These arrive in the
    // join accept, so a restored session has never seen them and the MAC would fall back
    // to the specification default of one second — while the network answers at five.
    // Every downlink would miss, silently, for as long as the restored session lasts.
    uint32_t rx_delay1_ms;
    uint32_t rx_delay2_ms;
};

// Highest counter value written to flash so far. Held in RAM so the periodic save knows
// when the next write is due without reading the file back.
//
// Only ever assigned after write_file() has actually succeeded. Advancing it on the way to a
// write that then does not happen is the whole of the H3 regression: the ceiling would claim
// headroom the stored file does not have, and every uplink taken against that phantom headroom
// is one a reset would replay.
uint32_t s_saved_counter_ceiling = 0;

// Whether a stored session exists that a reset would actually resume from.
//
// This is what makes the ceiling meaningful. With nothing stored, a reset rejoins and the
// network issues a fresh address and a fresh counter, so no value this node transmits can
// collide with anything — the ceiling constrains a node that resumes, and only that node.
// Without this distinction the headroom check below would refuse the first uplink after a join
// whose save was withheld, which is precisely the healthy case.
bool s_have_stored_session = false;

// One-shot authorization to write the counter checkpoint despite a withheld flash-write gate.
// Set by main.cpp immediately before a keepalive uplink and consumed by the next headroom check.
// See session::permit_counter_checkpoint() for why the reserve alone is not enough.
bool s_checkpoint_permit = false;

// Null means "allowed". See set_flash_write_gate() — an un-wired gate must not disable
// persistence, because that failure would be silent and would cost a rejoin every reset.
session::FlashWriteGateFn s_flash_write_gate = nullptr;

bool mib_get(Mib_t type, MibRequestConfirm_t &req)
{
    memset(&req, 0, sizeof(req));
    req.Type = type;
    return LoRaMacMibGetRequestConfirm(&req) == LORAMAC_STATUS_OK;
}

bool write_file(const Stored &s)
{
    InternalFS.remove(kPath);

    File f(InternalFS);
    if (!f.open(kPath, FILE_O_WRITE)) {
        return false;
    }
    const size_t n = f.write((const uint8_t *)&s, sizeof(s));
    f.close();
    return n == sizeof(s);
}

bool read_file(Stored &s)
{
    File f(InternalFS);
    if (!f.open(kPath, FILE_O_READ)) {
        return false;
    }
    const int n = f.read((uint8_t *)&s, sizeof(s));
    f.close();

    return n == (int)sizeof(s) && s.magic == kMagic && s.version == kVersion;
}

bool collect(Stored &s)
{
    MibRequestConfirm_t req;

    if (!mib_get(MIB_DEV_ADDR, req)) {
        return false;
    }
    s.dev_addr = req.Param.DevAddr;

    if (!mib_get(MIB_NWK_SKEY, req) || req.Param.NwkSKey == nullptr) {
        return false;
    }
    memcpy(s.nwk_skey, req.Param.NwkSKey, sizeof(s.nwk_skey));

    if (!mib_get(MIB_APP_SKEY, req) || req.Param.AppSKey == nullptr) {
        return false;
    }
    memcpy(s.app_skey, req.Param.AppSKey, sizeof(s.app_skey));

    if (!mib_get(MIB_UPLINK_COUNTER, req)) {
        return false;
    }
    s.uplink_counter = req.Param.UpLinkCounter;

    if (mib_get(MIB_DOWNLINK_COUNTER, req)) {
        s.downlink_counter = req.Param.DownLinkCounter;
    }

    // Zero is stored when the MAC cannot report a delay, and restore treats zero as
    // "leave whatever the MAC already has" rather than forcing an impossible value.
    s.rx_delay1_ms = mib_get(MIB_RECEIVE_DELAY_1, req) ? req.Param.ReceiveDelay1 : 0;
    s.rx_delay2_ms = mib_get(MIB_RECEIVE_DELAY_2, req) ? req.Param.ReceiveDelay2 : 0;

    s.magic   = kMagic;
    s.version = kVersion;
    return true;
}

bool mib_set_u32(Mib_t type, uint32_t value)
{
    MibRequestConfirm_t req;
    memset(&req, 0, sizeof(req));
    req.Type = type;

    switch (type) {
    case MIB_DEV_ADDR:        req.Param.DevAddr = value; break;
    case MIB_UPLINK_COUNTER:  req.Param.UpLinkCounter = value; break;
    case MIB_DOWNLINK_COUNTER:req.Param.DownLinkCounter = value; break;
    case MIB_RECEIVE_DELAY_1: req.Param.ReceiveDelay1 = value; break;
    case MIB_RECEIVE_DELAY_2: req.Param.ReceiveDelay2 = value; break;
    default: return false;
    }
    return LoRaMacMibSetRequestConfirm(&req) == LORAMAC_STATUS_OK;
}

} // namespace

namespace session {

bool restore()
{
    Stored s = {};
    if (!read_file(s)) {
        return false;
    }

    // An all-zero address means the stored file predates a successful join. Treat it as
    // absent rather than pushing a meaningless session into the MAC.
    if (s.dev_addr == 0) {
        return false;
    }

    if (!mib_set_u32(MIB_DEV_ADDR, s.dev_addr)) {
        return false;
    }

    MibRequestConfirm_t req;

    memset(&req, 0, sizeof(req));
    req.Type          = MIB_NWK_SKEY;
    req.Param.NwkSKey = s.nwk_skey;
    if (LoRaMacMibSetRequestConfirm(&req) != LORAMAC_STATUS_OK) {
        return false;
    }

    memset(&req, 0, sizeof(req));
    req.Type          = MIB_APP_SKEY;
    req.Param.AppSKey = s.app_skey;
    if (LoRaMacMibSetRequestConfirm(&req) != LORAMAC_STATUS_OK) {
        return false;
    }

    // The stored counter is already ahead of anything transmitted, which is the whole
    // point — resuming at or below a used value would have the network discard the uplinks
    // silently while the node reported success. That silent-discard behavior is exactly
    // why a failure here cannot be shrugged off: the node would look healthy, report
    // success on every send, and reach nobody. Better to throw the session away and join
    // fresh, which costs one handshake and is guaranteed to work.
    if (!mib_set_u32(MIB_UPLINK_COUNTER, s.uplink_counter)) {
        LOGLN(F("   session : frame counter rejected — discarding, will join fresh"));
        forget();
        return false;
    }

    // The downlink counter only affects receiving. Losing it costs at most one ignored
    // downlink, so it is not worth abandoning an otherwise good session over.
    (void)mib_set_u32(MIB_DOWNLINK_COUNTER, s.downlink_counter);

    // Restoring these is what keeps the downlink path alive across a reset. Without them
    // the MAC listens one second after each uplink while the network answers at five, so
    // the node would transmit normally and never hear a reply again.
    if (s.rx_delay1_ms != 0) {
        (void)mib_set_u32(MIB_RECEIVE_DELAY_1, s.rx_delay1_ms);
    }
    if (s.rx_delay2_ms != 0) {
        (void)mib_set_u32(MIB_RECEIVE_DELAY_2, s.rx_delay2_ms);
    }

    memset(&req, 0, sizeof(req));
    req.Type                  = MIB_NETWORK_JOINED;
    req.Param.IsNetworkJoined = JOIN_OK;
    // Setting the address and keys is what actually makes the session usable; this only
    // stops the MAC from insisting on a handshake first, so a failure here is not fatal.
    (void)LoRaMacMibSetRequestConfirm(&req);

    s_saved_counter_ceiling = s.uplink_counter;
    s_have_stored_session   = true;

    LOGF("   session : restored 0x%08lX, counter %lu\n", (unsigned long)s.dev_addr,
         (unsigned long)s.uplink_counter);
    return true;
}

// The whole of save(), plus the one way past the H3 gate. File-local: nothing outside this
// translation unit may choose to bypass the gate.
//
// checkpoint_permitted is granted only for an authorized keepalive, and only for the write that
// keeps the frame counter ahead of the wire. Everything else still obeys the gate.
static bool write_session(bool checkpoint_permitted)
{
    // H3: no flash writes while the pack is held below cutoff. Both paths into this function
    // run during a brownout hold — radio.cpp after a join, and counter_headroom_ok() below
    // before an uplink — and the keepalive is by definition a transmit that happens *while*
    // holding, so without this the node keeps paying flash-write current on a pack that is
    // already too low to be spending it. Checked here rather than at the two call sites so a
    // third caller cannot reopen the hole.
    //
    // The cost of skipping is a rejoin after the next reset, and that is the right trade: a
    // page write interrupted by a supply that sags mid-write corrupts the record we would be
    // rejoining from anyway. This is the same reasoning power.cpp already applies when it
    // declines to persist a hold taken on unknown voltage.
    //
    // The one exception is the counter checkpoint behind an authorized keepalive. Withholding
    // that write does not protect the node, it silences it permanently — the reserve is finite,
    // the hold need not end, and a mute Class A node cannot be told anything. The write is safe
    // to take here for three reasons: it happens at most once per kCounterMargin keepalives, so
    // roughly monthly rather than per cycle; it rides on a cycle that is already transmitting,
    // and a LoRa transmit burst is the largest current this node ever draws, so the marginal
    // charge of one page write is small against a decision already made; and a supply that
    // collapses mid-write cannot leave a plausible-looking wrong record, because LittleFS
    // commits atomically and the RAM ceiling below advances only after the write returned.
    if (!checkpoint_permitted && s_flash_write_gate != nullptr && !s_flash_write_gate()) {
        LOGLN(F("   session : skipping save — brownout hold, flash writes withheld"));
        return false;
    }

    Stored s = {};
    if (!collect(s)) {
        LOGLN(F("   session : could not read session from the MAC"));
        return false;
    }

    // Store a counter deliberately ahead of the live one. After a reset the node resumes
    // from this value, which is guaranteed to be higher than anything it actually sent.
    s.uplink_counter += session::kCounterMargin;

    if (!write_file(s)) {
        LOGLN(F("   session : write failed"));
        return false;
    }

    // Only now. Both early returns above — the brownout gate and a failed write — leave the
    // ceiling exactly where the stored file leaves it, so the two can never disagree.
    s_saved_counter_ceiling = s.uplink_counter;
    s_have_stored_session   = true;

    LOGF("   session : saved 0x%08lX, resume at %lu\n", (unsigned long)s.dev_addr,
         (unsigned long)s.uplink_counter);
    return true;
}

bool save()
{
    return write_session(false);
}

void permit_counter_checkpoint()
{
    s_checkpoint_permit = true;
}

bool counter_headroom_ok()
{
    // Consumed on every check, used or not. A permit that outlived the send it was granted for
    // would sooner or later let an ordinary uplink write flash during a hold, which is the thing
    // H3 exists to stop.
    const bool permitted = s_checkpoint_permit;
    s_checkpoint_permit  = false;

    // Nothing stored means a reset joins fresh and the network hands out a new address and
    // counter. There is no stored value to run past, so there is nothing to protect.
    if (!s_have_stored_session) {
        return true;
    }

    MibRequestConfirm_t req;
    if (!mib_get(MIB_UPLINK_COUNTER, req)) {
        // Where the counter actually is cannot be established. Allow the uplink: a MIB read
        // failing is not evidence that a replay is about to happen, and treating it as one
        // would silence a healthy node on a transient.
        return true;
    }

    // Strictly below, not at. The stored value is the counter a reset resumes *from*, so
    // transmitting it and then resetting sends it a second time — the same replay, one frame
    // earlier than the off-by-one version of this test would catch.
    if (req.Param.UpLinkCounter < s_saved_counter_ceiling) {
        return true;
    }

    // The wire has caught up with flash. save() is the only thing that moves the ceiling, and
    // it moves it only when the write landed — so this answers false exactly when the stored
    // value cannot be advanced, which during a brownout hold is the case issue #51 created.
    if (write_session(permitted)) {
        return true;
    }

    // Refusing costs one interval of data. Not refusing costs every uplink until somebody
    // walks out there: unconfirmed sends report success (radio.cpp), so m_failures never
    // increments, the rejoin escape never fires, and the network discards each frame as a
    // replay in silence. Recovery would be one frame per kCounterMargin resets.
    LOGF("   session : uplink withheld — counter %lu has reached the stored ceiling %lu and "
         "the ceiling cannot be advanced\n",
         (unsigned long)req.Param.UpLinkCounter, (unsigned long)s_saved_counter_ceiling);
    return false;
}

void set_flash_write_gate(FlashWriteGateFn gate)
{
    s_flash_write_gate = gate;
}

void forget()
{
    InternalFS.remove(kPath);
    s_saved_counter_ceiling = 0;
    s_have_stored_session   = false;
    LOGLN(F("   session : discarded — next boot will join"));
}

} // namespace session

#else // !FEATURE_RADIO

namespace session {
bool restore() { return false; }
bool save() { return false; }
bool counter_headroom_ok() { return true; }
void permit_counter_checkpoint() {}
void set_flash_write_gate(FlashWriteGateFn) {}
void forget() {}
} // namespace session

#endif
