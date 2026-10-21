# Publishing to M5Burner

M5Burner lists firmware per device, so Meshputer is two entries: one in the
**Cardputer** category and one in **Tab5**. Publishing needs an M5Stack account
(sign in inside M5Burner); each later release is added as a new version of the
same entries.

Both entries take the single images from the GitHub Release (they flash at 0x0
and reset saved settings; the browser installer keeps them):

| Entry | Category | File | Cover |
|---|---|---|---|
| Meshputer: Meshtastic messenger | Cardputer | `meshputer-cardputer-<version>.bin` | `docs/screens/chat.png` |
| Meshputer: Meshtastic touch messenger | Tab5 | `meshputer-tab5-<version>.bin` | `docs/screens/tab5-chat-landscape.png` |

GitHub link for both: `https://github.com/Dvorson/meshputer`

## Description: Cardputer

```
Pocket Meshtastic messenger for the Cardputer (v1.1) and Cardputer ADV. No LoRa
cap needed: pair over Bluetooth with the Meshtastic radio you already own
(Heltec, T-Beam, RAK, T-Echo, Station G2... stock firmware) and chat from the
keyboard.

- Finds radios nearby, pairs with the PIN, reconnects by itself
- Direct messages and channels with sent / relayed / delivered ticks
- See who anyone is (name, hardware, hops, signal, encryption); set your own name
- History on the SD card, per radio; emoji and quick replies
- Internet DMs over Nostr when out of LoRa range (optional, off by default)
- Battery first: 80 MHz, Wi-Fi off unless Internet is on, Bluetooth only when needed

Close the radio's phone app first: a radio talks to one Bluetooth client at a time.
Install from the browser (keeps settings): https://dvorson.github.io/meshputer/
```

## Description: Tab5

```
Meshtastic messenger for the Tab5 with a touch UI made for its 5" screen. Pair
over Bluetooth with the Meshtastic radio you already own and chat without a phone.

- Turns with the device: chats, nodes and settings side by side in landscape,
  one page at a time in portrait
- Direct messages and channels with delivery ticks, on-screen keyboard, quick replies
- Nodes with identity, hops and signal; message anyone with one tap
- Scan, pair (PIN keypad) and reconnect to your radio
- Internet DMs over Nostr when out of LoRa range (optional), with a QR code of
  the device's identity
- History on the microSD card, or in internal flash without one

Install from the browser (keeps settings): https://dvorson.github.io/meshputer/
```
