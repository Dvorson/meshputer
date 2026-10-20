<h1 align="center">Meshputer</h1>

<p align="center">
  <b>A pocket Meshtastic messenger for the M5Stack Cardputer.</b><br>
  Pair it over Bluetooth with the Meshtastic radio you already own and chat over LoRa from a real keyboard.
</p>

<p align="center">
  <a href="https://dvorson.github.io/meshputer/"><b>⚡ Install from your browser</b></a> ·
  <a href="https://github.com/Dvorson/meshputer/releases/latest">Download</a> ·
  <a href="#using-it">Keys</a> ·
  <a href="#development">Hack on it</a>
</p>

<p align="center">
  <a href="https://github.com/Dvorson/meshputer/actions/workflows/ci.yml"><img alt="CI" src="https://github.com/Dvorson/meshputer/actions/workflows/ci.yml/badge.svg"></a>
  <a href="https://github.com/Dvorson/meshputer/releases/latest"><img alt="Release" src="https://img.shields.io/github/v/release/Dvorson/meshputer"></a>
  <a href="LICENSE"><img alt="License: GPL-3.0" src="https://img.shields.io/badge/license-GPL--3.0-blue"></a>
</p>

<p align="center"><img src="docs/screens/chat.png" width="600" alt="Two Cardputers chatting over Meshtastic"></p>

## Why

Meshtastic radios are great, but you normally drive them from a phone. Meshputer
turns a Cardputer into the radio's keyboard and screen instead:

- **Works with the radio you have.** Heltec, T-Beam, RAK, T-Echo, Station G2… any
  Meshtastic node with Bluetooth, on stock firmware. No LoRa add-on needed.
- **Works on both Cardputers.** Original Cardputer (v1.1) and Cardputer ADV.
- **Leaves the phone at home.** A pocket messenger with a physical keyboard.

Already have a Cardputer ADV with the Cap LoRa-1262? You can also run a full node
on it ([official Mesh Kit firmware](https://docs.m5stack.com/en/core/Cardputer_Mesh_Kit),
[Plai](https://github.com/d4rkmen/plai)). Meshputer is for the other case: a
radio on your belt or backpack, and the Cardputer in your hand.

## Features

- 📡 **Find & pair** radios nearby with their PIN; reconnects by itself after reboots and dropouts
- 💬 **Direct messages and channels**, with `sent` → `relayed` → `delivered` status from mesh ACKs
- 🪪 **Identity**: see who anyone is (name, id, hardware, role, hops, signal, battery, PKI encryption) and **edit your own name** — written to your radio and announced to the mesh
- 💾 **History on SD**, kept per radio
- 😀 **Emoji** sent as real emoji (phones see them) plus quick replies
- 🔋 **Battery first**: 80 MHz CPU, Wi-Fi never on, Bluetooth only when needed, slow BLE interval once synced, screen dims then sleeps

<p align="center">
  <img src="docs/screens/home.png" width="49%" alt="Home: radio, channels, nodes, conversations">
  <img src="docs/screens/nodes.png" width="49%" alt="Nodes heard on the mesh">
  <img src="docs/screens/identity.png" width="49%" alt="Identity of another node">
  <img src="docs/screens/settings.png" width="49%" alt="Settings, including your own name">
</p>

## Install

**Browser (recommended):** open **[dvorson.github.io/meshputer](https://dvorson.github.io/meshputer/)**
in desktop Chrome or Edge, plug the Cardputer in, press *Install*. Updates keep your
paired radio and settings.

**Single image:** grab `meshputer-<version>.bin` from
[Releases](https://github.com/Dvorson/meshputer/releases/latest) for M5Burner,
M5Launcher (as full firmware) or esptool:

```bash
esptool.py --chip esp32s3 write_flash 0x0 meshputer-v0.1.0.bin
```

**From source:** see [Development](#development).

Then on the Cardputer: **Enter** on *Connect a radio*, pick yours, type its PIN
(pre-filled with Meshtastic's default `123456`; radios with a screen show a random
one). Close the phone app first: a radio talks to one Bluetooth client at a time.

## Using it

| Where | Keys |
|---|---|
| Lists | `;` `.` up/down, **Enter** open, **Del** or `` ` `` back |
| Home | `r` radio, `n` nodes (start a DM), `c` channels, `s` settings |
| Chat | type, **Enter** send, **Del** on empty line = back, `fn`+`;`/`.` scroll, `fn`+`r` quick replies, `fn`+`e` emoji, `fn`+`i` who is this |
| Nodes | **Enter** chat, `i` identity |

**Two Cardputers** chat like any two Meshtastic users: pair each with its own
radio on the same channel settings, open *Nodes*, pick the other one, type.

**Your name:** Settings → *Name* / *Short name*. It's saved on your radio
(Meshtastic `set_owner`), read back to confirm, and broadcast so others see it.

## How it saves power

| Part | Policy |
|---|---|
| Wi-Fi | never initialised — the Wi-Fi stack is not even linked |
| Bluetooth | on only while scanning, connecting, connected, or waiting to reconnect; off otherwise |
| BLE link | fast interval while syncing the node list, then 120–160 ms |
| CPU | 80 MHz; the main loop sleeps between polls |
| Display | dims after 20 s, sleeps after 60 s (configurable); a new message lights it dimmed |
| Speaker | amplifier powered only while a tone plays |

## Development

Needs [uv](https://docs.astral.sh/uv/).

```bash
make setup      # PlatformIO + tool deps
make test       # native unit tests (protocol, conversations, text) — no device needed
make flash      # build + flash over USB
make logs       # stream device logs
```

The firmware exposes a serial console, so you can drive and inspect a device from a script:

```bash
make console CMD='status nodes'
make console CMD='"send !a1b2c3d4 hello" "owner ANA Ana Cardputer"'
make shot OUT=screen.png        # what's on the display, as PNG
make screenshots                # regenerate docs/screens on demo data (RAM only)
```

`help` lists every command (`scan`, `connect`, `pin`, `nodes`, `node`, `send`,
`owner`, `key` to inject key presses, `shot`, …). Each ends with `## done` or `## error <why>`.

```
src/core/       portable C++ (no Arduino): Meshtastic protobuf framing,
                conversation model + SD format, UTF-8/emoji text — unit tested
src/            device layer: BLE client, chat service, power, board, UI
src/screens/    screens and navigation
tests/          native tests for src/core, against the real nanopb schema
tools/          serial console/screenshot tool, release + installer-site builders
web/            browser-installer page (GitHub Pages)
```

Releases: push a `v*` tag; CI builds, attaches the image to a GitHub Release and
publishes the installer to Pages (`make site` + `make serve` to preview locally).

### Notes

- Uploads use `--no-stub`: esptool's RAM stub is unreliable over the S3's USB-JTAG port.
- Opening the serial port with RTS asserted and DTR released resets the ESP32-S3; the tool keeps DTR asserted.
- History lives in `/meshputer/<node id>/` on the SD card, one text file per conversation.
- If a radio was reflashed and refuses the old pairing, Meshputer drops the bond and pairs again; *Forget radio* does it by hand.

## License

GPL-3.0, like the Meshtastic protobufs it builds on. Meshtastic® is a registered
trademark of Meshtastic LLC; this project is not affiliated with it.
