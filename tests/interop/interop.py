# /// script
# requires-python = ">=3.9"
# dependencies = ["nostr-sdk==0.45.1"]
# ///
"""Cross-checks src/core/nostr against rust-nostr (nostr-sdk) in both directions:
NIP-17 gift wraps made by rust-nostr open in our code, and ours open in rust-nostr.
Run via `make interop` (builds build/tests/nostr_cli first)."""

import subprocess
import sys

import nostr_sdk as n

CLI = "build/tests/nostr_cli"


def cli(*args, stdin=None):
    r = subprocess.run([CLI, *args], input=stdin, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"nostr_cli {args[0]} failed with code {r.returncode}")
    return r.stdout.strip()


def main():
    ours = n.Keys.generate()     # stands in for the Cardputer
    theirs = n.Keys.generate()   # stands in for a phone app
    ours_sec = ours.secret_key().to_hex()

    assert cli("pub", ours_sec) == ours.public_key().to_hex(), "public key derivation differs"
    print("ok   same public key from the same secret")

    text_in = "hello from rust-nostr ⛰"
    wrap = n.nip17_make_private_msg(theirs, ours.public_key(), text_in)
    sender, text = cli("unwrap", ours_sec, stdin=wrap.as_json()).split("\t", 1)
    assert sender == theirs.public_key().to_hex(), "wrong sender after unwrap"
    assert text == text_in, f"wrong text: {text!r}"
    print("ok   rust-nostr gift wrap opens in meshputer")

    text_out = "hello from meshputer \U0001F44D"
    ours_wrap = n.Event.from_json(cli("wrap", ours_sec, theirs.public_key().to_hex(), text_out))
    gift = n.UnwrappedGift.from_gift_wrap(theirs, ours_wrap)
    assert gift.sender().to_hex() == ours.public_key().to_hex(), "rust-nostr sees a different sender"
    assert gift.rumor().content() == text_out, f"wrong text: {gift.rumor().content()!r}"
    print("ok   meshputer gift wrap opens in rust-nostr")


if __name__ == "__main__":
    main()
