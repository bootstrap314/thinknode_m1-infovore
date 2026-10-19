# USB LoRaWAN dongle

Both native firmware choices expose **USB CDC ACM**, a serial port suitable for macOS, Linux and Windows. The computer can read the M1, provision credentials, join the private LoRaWAN network, send application uplinks, and receive downlink bytes. Firmware handles LoRaWAN encryption, counters, and radio timing.

```text
Computer ↔ USB serial ↔ ThinkNode M1 ↔ AU915 gateway ↔ ChirpStack ↔ Your application
```

The M1 remains a Class A LoRaWAN **end device**. Your AU915 gateway and ChirpStack server are still required. It cannot replace an SX1302/SX1303 multi-channel gateway, provide an IP network adapter, or receive every other sensor's uplinks. To request sensor data or control a different device, your server application must interpret the M1's uplink, route the request to the target device, and queue any response for the M1.

[ChirpStack Class A downlinks](https://www.chirpstack.io/docs/chirpstack/features/device-classes.html) arrive in receive windows following this device's uplink. A USB monitor does not open those radio windows. `poll;` opens them by transmitting byte `00` on FPort 10. Normal uplinks also open receive windows. Downlinks are small, intermittent messages; this interface is not a continuous high-bandwidth remote sensor stream.

## Computer client

From the project folder, activate the build environment (it already includes `pyserial`):

```sh
# From the repository root
source .venv/bin/activate
python scripts/m1.py ports
```

After native firmware has been installed and hardware-checked, use the application port shown above. Replace `/dev/cu.usbmodemXXXX` with the actual port; Linux typically uses `/dev/ttyACM0` and Windows `COM3` or similar.

```sh
python scripts/m1.py --port /dev/cu.usbmodemXXXX status
python scripts/m1.py --port /dev/cu.usbmodemXXXX mode dongle
python scripts/m1.py --port /dev/cu.usbmodemXXXX provision --file private/chirpstack-credentials.json
python scripts/m1.py --port /dev/cu.usbmodemXXXX join
python scripts/m1.py --port /dev/cu.usbmodemXXXX send 10 010203
# Wait at least 60 seconds after a transmission before another send/poll.
python scripts/m1.py --port /dev/cu.usbmodemXXXX poll
python scripts/m1.py --port /dev/cu.usbmodemXXXX monitor --interval 5
```

Provisioning is needed once and attempts a join; skip `join` if already joined. No gateway/server is needed for local status or monitoring. The private JSON file uses `join_eui` (16 hex characters), `dev_eui` (16 nonzero hex), and `app_key` (32 nonzero hex); other fields are ignored. Do not put keys directly in shell history. The client never prints the provisioning command or keys.

The client emits one JSON object per line for scripts and logging. Example downlink:

```json
{"bytes":"3","hex":"010203","port":"10","type":"downlink"}
```

Numbers originating in the text protocol remain strings. Monitor prints local status and any downlinks received while it is open. It never transmits periodic polls. Quit with Ctrl-C. Only one computer process should own the serial port; close monitor before running send/poll. You can also send an uplink from Bluefruit while the computer monitors received downlinks.

Use `mode tracker` to resume automatic position reporting, or hold button 1 for about one second to toggle between tracker and dongle modes. A button toggle turns on the light and shows the radio/mode page. Dongle mode disables automatic position uplinks and initially puts GPS in standby. `gps now;` can still acquire a local fix without automatic position uplinks. Bluetooth, buttons and display remain available. Mode is shared by phone/USB, remains selected when USB is unplugged, and returns to **tracker** after reboot. Reapply `mode dongle` on each new boot. Credentials and LoRaWAN state are persistent; operating mode and stream subscriptions are not. On reboot, provisioned firmware can automatically join before a computer selects dongle mode.

The release support ZIP also contains `scripts/m1.py` and this guide. A computer used only as a client needs Python 3.10+ and `python -m pip install pyserial==3.5`; it does not need the firmware toolchains. Zephyr USB behavior has been tested on this Mac. Windows/Linux host use and the FreeRTOS USB adapter still need hardware acceptance testing.

## Serial protocol

A terminal or your own program can use 115200 baud, 8N1, no hardware flow control, and **DTR asserted**. Baud rate is informational for native USB. Disable local terminal echo when provisioning. The full command table is in [BLUEFRUIT.md](BLUEFRUIT.md); the same commands work over USB. `stream` subscriptions are independent for USB and BLE and stop when that connection closes.

- ASCII commands, maximum 95 characters, terminated by `;`, CR or LF. Fragmented writes are assembled until a delimiter.
- Responses and asynchronous events are CRLF-terminated records. Allow up to 1024 bytes per record and assemble partial reads.
- `status;` reports `STATUS protocol=1 ...`. The supplied client requires this handshake before other commands.
- `mode dongle;` and `mode tracker;` return `OK mode=...`.
- `join;` returns `JOIN joined=0|1 result=<RadioLib status>` after the attempt.
- `send <FPort> <hex>;` and `poll;` return `TX starting...`, optional `DOWNLINK ...`, then `TX result=<RadioLib status>`. Negative results are failures. A successful send does not acknowledge remote application execution.
- `ERR ...` rejects a command; `ERR radio busy; retry after seconds=N` indicates when a later attempt may be made. There is no automatic retransmission after an uncertain timeout.
- USB downlinks are `DOWNLINK port=N bytes=N hex=...`. The same payload is also sent to a connected phone using short, labelled Bluetooth lines. They are not queued for later delivery to a disconnected computer or phone.
- Send only one command at a time and wait for its terminal response. Radio operations may take several seconds. The supplied client permits 30 seconds. Status/downlink events can appear between command responses; there are no request IDs.

Application uplinks are limited to FPorts 2–223, 1–32 payload bytes, the current data-rate limit, and at least 60 seconds between transmissions plus RadioLib airtime restrictions. FPort 1 is reserved for position reports. These limits apply equally to phone and USB commands; USB is not a radio-rate bypass.

The physical USB connection permits local configuration without BLE pairing. The client selects an explicit M1 application port, never a bootloader port, and checks the native protocol version. Closing the client does not switch firmware or erase credentials.

## Acceptance check

Host tests exercise split/coalesced input, downlinks interleaved with TX results, malformed records, timeouts, protocol mismatch, secret-file validation and stream independence. Both firmware targets cross-compile. Zephyr now passes on-device USB enumeration after flashing, DTR/serial reopen, dongle-mode control, streaming and command framing checks. A physical cable reconnect was also verified. iPhone Bluefruit pairing, UART readings, streaming and screen control passed while USB dongle mode remained active; simultaneous USB status streaming reflected the phone-selected page. Long-running sessions, Windows/Linux hosts and actual radio traffic remain untested; FreeRTOS remains build-tested only. See [hardware validation](HARDWARE_TESTING.md).

Status stream intervals are targets rather than deadlines: screen refreshes and radio operations can delay a report.
