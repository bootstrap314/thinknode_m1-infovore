# Bluefruit Connect, UART, and provisioning

Install Adafruit Bluefruit Connect on iPhone. **USB dongle mode keeps Bluetooth enabled; no mode change is needed.** In the app, scan for **ThinkNode-M1** and tap **Connect**. Enter the six-digit passkey shown on the M1 into the iPhone pairing prompt, then open **UART**. Press button 1 to illuminate the screen if needed. The firmware implements Nordic UART Service directly in the selected OS. Adafruit's phone application is a client; no Bluefruit/Arduino firmware library is used.

There is no separate pairing mode or button gesture: an unconnected M1 advertises automatically in tracker and USB dongle modes. Connect from Bluefruit; if the screen is blank when a code is needed, press either button to reveal it.

The RX characteristic and notification subscription require authenticated encryption. Pairing uses a displayed passkey; bond records persist. After changing RTOS, use iPhone **Settings → Bluetooth → Forget This Device** if the phone holds a bond/cache belonging to the other stack, then pair again. Credentials and LoRaWAN state are separate from BLE bonds.

In the UART view, choose **Text** and **ASCII**. Timestamp view shows individual Bluetooth packets and can split words even when the firmware sends deliberate line breaks. Bluetooth status and streaming use short, labelled lines (at most 32 characters), with a blank line between reports. Help, diagnostics and received downlinks also use short lines. Very narrow screens or large accessibility text can still wrap them.

Example excerpt from `status;` or `stream 5;`:

```text
Battery: 4.22 V (100%)
Mode: USB dongle
GPS: standby
LoRaWAN: not joined
Bluetooth: connected
```

USB retains its original single-line `STATUS protocol=1 ...` records and command responses. Commands and stream timing are unchanged. BLE consumers should treat the phone output as display text, not the USB machine protocol.

Commands are case-sensitive. End each with `;`, carriage return, or newline. A command can be split across BLE writes. Maximum command length is 95 characters; malformed/oversized input is rejected rather than executed as a partial command.

| Command | Purpose |
|---|---|
| `status;` | RTOS/version, battery estimate, GPS, position, LoRaWAN state, frame count, light, uptime, provisioning and persistence status |
| `stream 5;` | Status from the local M1 every 5 seconds; allowed range 2–60 seconds |
| `stream off;` | Stop streaming; disconnect also stops it |
| `mode dongle;`, `mode tracker;` | Pause/resume automatic location uplinks; dongle initially disables GPS. Shared by USB/BLE, resets to tracker on reboot |
| `help;` | Command summary |
| `light on;`, `light off;` | Enable/disable the light; brightness remains controlled by the physical knob |
| `page 0;`, `page 1;`, `page 2;` | Overview, position, or radio screen |
| `display clean;` | One full e-paper cleanup refresh |
| `gps auto;` | Automatic sample/standby policy |
| `gps on;` | Keep GPS awake while its physical switch is on |
| `gps off;` | Put GPS in standby |
| `gps now;` | Start a bounded acquisition, then return to automatic policy |
| `provision <JoinEUI> <DevEUI> <AppKey>;` | Store private OTAA credentials and start joining |
| `join;` | Force a new join while retaining nonce history |
| `send 10 010203;` | Send a 3-byte application uplink on FPort 10 |
| `poll;` | Send byte `00` on FPort 10 to open Class A receive windows |
| `bootloader;` | Reboot into the installed USB update bootloader |

`send` allows FPorts 2–223 and 1–32 bytes of even-length hex. FPort 1 is reserved for the position payload. Actual payload length is also checked against the current radio data rate. Uplinks have at least 60 seconds of application spacing plus RadioLib airtime constraints. A successful send is not confirmation that a remote device executed a command.

Provisioning uses a 16-digit JoinEUI, 16-digit nonzero DevEUI, and 32-digit nonzero AppKey, without `0x`, separated by single spaces. A zero JoinEUI is allowed. Keys are not returned by `status`, echoed in responses, or embedded in release firmware. Repeating identical credentials retains nonce history. Replacing credentials starts new state; create a fresh device/key assignment on the server when intentionally replacing an existing deployment.

Received application bytes appear under `Downlink`, with separate `Port:`, `Bytes:` and `Hex:` lines. Long hex payloads continue on subsequent lines; concatenate those hex lines to recover the original bytes. USB retains `DOWNLINK port=... bytes=... hex=...`. ChirpStack MQTT/application logic must route your sensor requests and responses. The M1 is a LoRaWAN end device, not a gateway. There is no direct IP connection from it to ChirpStack.

Streaming reports cached local measurements. It is not a high-rate remote sensor stream; LoRaWAN is designed for small, intermittent messages. GPS position can be last-known: read `Fix`, `GPS` and `Fix age` together. Without an acquired position, the phone shows `Position: no fix`; a stored position without a current fix is labelled `last known`. USB exposes the equivalent `gps_fix`, `gps_power` and `fix_age_s` fields. Battery percentage is a voltage-based estimate, not a calibrated fuel gauge.

On 2026-10-18, the native Zephyr build passed iPhone pairing and Bluefruit UART checks: `status;` returned readings, `stream 5;` streamed readings, and `page 2;` changed the screen. These checks passed while USB dongle mode was active; USB status also reflected the phone-selected page. On 2026-10-19, corrected FreeRTOS source passed the same phone checks plus disconnect/reconnect, with USB checks passing concurrently. The FreeRTOS fixes are newer than the published `v0.1.0-rc.1` assets. See [hardware validation](HARDWARE_TESTING.md) for exact evidence and limits.

The same commands work on [USB](USB_DONGLE.md), including an independent local status stream.

Physical controls: tap and release **button 1** to toggle the light, or hold it for about **one second** to switch tracker/dongle mode. A mode switch enables the light and shows the radio/mode page. Tap button 2 to cycle pages; hold it for about 1.2 seconds to request a join.

Hold **both buttons together for two seconds** to blank the screen and turn off the light. Release both buttons and wait for clearing to finish before using the rotary power switch. Blanking leaves Bluetooth, USB and tracking active. A later press of either button restores the page and light without performing its usual action; release before pressing again. The `page` and `display clean;` commands also restore the screen. Connection events do not wake a blank display; if pairing requires a code, press either button to reveal it.

Backlight operation: tap **button 1** to enable the light before adjusting the rotary brightness knob. The knob does not wake an extinguished light. After the 60-second timeout, press button 1 again. This is expected power-saving behavior.

Status stream intervals are targets rather than deadlines: screen refreshes and radio operations can delay a report.
