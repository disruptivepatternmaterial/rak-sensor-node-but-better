#!/usr/bin/env python3
"""Did TTN's Network Server ever see a join request from puma-concolor-001?

A pending or active session means the join request arrived and TTN answered -- so any
remaining failure is on the device side (accept not received, keys, timing). Nothing at all
means the request never reached the network, which points at the gateway or the radio.
"""
import json
import os
import urllib.error
import urllib.request

KEY = os.environ["TTN_KEY"]
APP = "my-app-tobi"
DEV = "puma-concolor-001"


def get(base, path, fields):
    url = f"https://{base}.cloud.thethings.network/api/v3/{path}?field_mask={fields}"
    req = urllib.request.Request(url, headers={"Authorization": f"Bearer {KEY}"})
    try:
        with urllib.request.urlopen(req, timeout=30) as r:
            return json.load(r)
    except urllib.error.HTTPError as e:
        return {"_http_error": e.code, "_body": e.read().decode()[:300]}


ns = get("nam1", f"ns/applications/{APP}/devices/{DEV}",
         "mac_state,session,pending_session,frequency_plan_id,lorawan_version,supports_join")
print("== Network Server (nam1) ==")
if "_http_error" in ns:
    print(json.dumps(ns, indent=2))
else:
    print("frequency plan   :", ns.get("frequency_plan_id"))
    print("lorawan version  :", ns.get("lorawan_version"))
    print("supports join    :", ns.get("supports_join"))
    print("has session      :", "session" in ns)
    print("has pending      :", "pending_session" in ns)
    ms = ns.get("mac_state") or {}
    if ms:
        print("device class     :", ms.get("device_class"))
        print("current params   :", json.dumps(ms.get("current_parameters", {}))[:200])

js = get("nam1", f"js/applications/{APP}/devices/{DEV}", "used_dev_nonces,last_dev_nonce,last_join_nonce")
print("\n== Join Server (nam1) ==")
print(json.dumps(js, indent=2)[:600])

iss = get("eu1", f"applications/{APP}/devices/{DEV}", "ids,created_at,updated_at")
print("\n== Identity Server (eu1) ==")
if "_http_error" not in iss:
    print("dev_eui          :", iss.get("ids", {}).get("dev_eui"))
    print("join_eui         :", iss.get("ids", {}).get("join_eui"))
    print("updated_at       :", iss.get("updated_at"))
else:
    print(json.dumps(iss, indent=2))
