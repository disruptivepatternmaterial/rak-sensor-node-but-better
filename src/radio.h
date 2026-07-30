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

  private:
    bool m_joined   = false;
    bool m_started  = false;
    uint32_t m_failures = 0;
};
