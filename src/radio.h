/*
 * LoRaWAN — join, send, and receive downlinks.
 *
 * Class A, US915, joined over the air. The design pressure here is not throughput, it is
 * what happens when the gateway is unreachable for days. Two rules follow from that.
 *
 * Joining must never become a busy loop. A node that keeps retrying a join it cannot
 * complete never sleeps, and that turns a gateway outage into a flat battery in a few days
 * — the failure is well documented on this exact board. Every attempt here is bounded, and
 * failure means going back to sleep and trying again later rather than trying harder.
 *
 * Backoff must be capped and must never give up. An outage should cost a handful of
 * transmissions, and the node should rejoin by itself whenever the gateway comes back,
 * with nobody present to restart it.
 *
 * CITE(prior-art): [CIT-RAK-SLEEP] "if you initialize LoRaWAN, but cannot connect to a
 *   LoRaWAN server, the MCU will never sleep, because it is retrying to join" — the
 *   failure mode this class is shaped around.
 * CITE(prior-art): [CIT-SX126X-ARDUINO] the LoRaWAN MAC used underneath.
 * CITE(spec): [CIT-LW-LINK] Class A: the only downlink opportunities are the two short
 *   windows after an uplink, which is why settings can only change just after a send.
 * CITE(policy): [CIT-TTN-FUP] the network's airtime and downlink allowances.
 */

#pragma once

#include "payload.h"

#include <stddef.h>
#include <stdint.h>

// What a downlink asked for. Deliberately tiny: this node is meant to run untouched, so
// the remote surface is one setting plus a read-only request. Every additional command
// would be another way to break a device nobody can reach.
struct DownlinkCommand {
    bool     set_interval    = false;
    uint32_t interval_value  = 0;
    bool     request_status  = false;
};

class Radio {
  public:
    // Brings up the transceiver and the MAC. Does not join.
    bool begin();

    // Attempts to join if not already joined, within a bounded time. Returns the joined
    // state. A false return is a normal outcome, not an error to retry immediately.
    bool ensure_joined();

    bool joined() const { return m_joined; }

    // The identity being presented to the join server, in the same most-significant-byte-first
    // order the network server displays it. The keys are stored least-significant-byte-first as
    // the MAC layer requires, and that reversal is a routine source of a device that transmits
    // perfectly and is never recognised, so these exist to make the comparison possible.
    const char *deveui_hex() const;
    const char *appeui_hex() const;
    uint8_t     sub_band() const;

    // How many application bytes the current data rate allows. Adaptive data rate means
    // the network moves the node between rates, and the allowance at the slowest one is
    // 11 bytes against a full payload of 35. An oversized uplink is not truncated — it is
    // refused — so the payload has to be built to fit whatever this returns.
    //
    // Falls back to the slowest rate's allowance if the MAC cannot answer, because
    // guessing small costs a few fields and guessing large costs the entire uplink.
    size_t max_payload() const;

    // Sends one unconfirmed uplink. Unconfirmed because an acknowledgement costs a
    // downlink from the gateway, and the network's daily downlink allowance is small
    // enough that spending it on routine traffic would exhaust it.
    bool send(const Payload &p);

    // Consumes any downlink received during the windows after the last send.
    bool take_downlink(DownlinkCommand &out);

    // How long to wait before the next attempt, doubling after each consecutive failure
    // and capped. Never returns zero and never gives up.
    uint32_t backoff_seconds() const;

    uint32_t consecutive_failures() const { return m_failures; }

    // Tells this class how many cycles will pass before it is called again, so a failed join
    // can report the real next attempt instead of just the backoff. The backoff sets the sleep
    // between cycles, but main.cpp only reaches ensure_joined() on some of them, so the two
    // numbers differ by up to the heartbeat cadence — and reporting the smaller one understated
    // the wait by hours during bring-up. Refs #24.
    void set_cycles_until_next_call(uint32_t cycles) { m_cycles_until_next_call = cycles; }

  private:
    // How long to stay awake after an uplink so both Class A receive windows can open.
    // Read from the MAC, because the network assigns a longer delay than the
    // specification default and a node that sleeps too early never hears a downlink.
    uint32_t rx_window_ms() const;

    bool m_joined   = false;
    bool m_started  = false;
    uint32_t m_failures = 0;

    // Cycles until the next ensure_joined(). One means "next cycle", which is the honest
    // default for a caller that has not said otherwise.
    uint32_t m_cycles_until_next_call = 1;
};
