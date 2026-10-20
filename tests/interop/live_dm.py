# /// script
# requires-python = ">=3.9"
# dependencies = ["nostr-sdk==0.45.1", "websockets==15.0.1"]
# ///
"""Live end-to-end check over public relays, with an independent client:
sends a NIP-17 DM to a Meshputer's npub and waits for a DM back from it.

    uv run tests/interop/live_dm.py <device-npub> [--timeout 180]

Uses a fresh throwaway identity each run; prints its npub so the device can
reply (console: `nostr dm <that npub> <text>`)."""

import argparse
import json
import sys
import time

import nostr_sdk as n
from websockets.sync.client import connect

RELAYS = ["wss://nos.lol", "wss://relay.primal.net"]  # same as the device


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("device_npub")
    ap.add_argument("--timeout", type=float, default=180)
    ap.add_argument("--text", default="hello from an independent Nostr client ⛰")
    args = ap.parse_args()

    device = n.PublicKey.parse(args.device_npub)
    me = n.Keys.generate()
    print(f"tester npub: {me.public_key().to_bech32()}", flush=True)

    wrap = n.nip17_make_private_msg(me, device, args.text)
    socks = []
    for url in RELAYS:
        try:
            ws = connect(url, open_timeout=15)
        except Exception as e:  # noqa: BLE001
            print(f"skip {url}: {e}", flush=True)
            continue
        ws.send(json.dumps(["EVENT", json.loads(wrap.as_json())]))
        since = int(time.time()) - 3 * 24 * 3600
        ws.send(json.dumps(["REQ", "inbox", {"kinds": [1059], "#p": [me.public_key().to_hex()], "since": since}]))
        socks.append((url, ws))
    if not socks:
        sys.exit("no relay reachable")
    print(f"sent DM to device via {len(socks)} relays", flush=True)

    deadline = time.time() + args.timeout
    while time.time() < deadline:
        for url, ws in socks:
            try:
                raw = ws.recv(timeout=0.5)
            except TimeoutError:
                continue
            msg = json.loads(raw)
            if msg[0] == "OK":
                print(f"{url}: {'accepted' if msg[2] else 'rejected ' + msg[3]}", flush=True)
            elif msg[0] == "EVENT":
                try:
                    gift = n.UnwrappedGift.from_gift_wrap(me, n.Event.from_json(json.dumps(msg[2])))
                except Exception:  # noqa: BLE001 - not for us / undecryptable
                    continue
                sender = gift.sender().to_bech32()
                print(f"reply from {sender}: {gift.rumor().content()!r}", flush=True)
                if gift.sender().to_hex() == device.to_hex():
                    print("PASS: two-way NIP-17 DM with the device over public relays", flush=True)
                    return
    sys.exit("timed out waiting for the device's reply")


if __name__ == "__main__":
    main()
