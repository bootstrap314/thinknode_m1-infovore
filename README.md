# ThinkNode M1 LoRaWAN Tracker

[![Native firmware verification](https://github.com/bootstrap314/thinknode_m1-infovore/actions/workflows/ci.yml/badge.svg)](https://github.com/bootstrap314/thinknode_m1-infovore/actions/workflows/ci.yml)

Native firmware that converts the **Elecrow ThinkNode M1**, originally sold as a Meshtastic device, into a dedicated **LoRaWAN tracker and messenger**. Use it as a standalone GPS tracker, control it from a phone over Bluetooth, or connect it to a computer as a USB LoRaWAN dongle.

The firmware targets **AU915 sub-band 2** and a private **ChirpStack** deployment. It replaces Meshtastic functionality; it does not use the Meshtastic mesh or depend on The Things Network (TTN).

Choose **Zephyr** or **FreeRTOS** at build time. Both are native C/C++ implementations without Arduino, sketches, PlatformIO, or the Adafruit Bluefruit firmware library. The **Bluefruit Connect phone app** works with the native Nordic UART Service.

## Project status

This is preview firmware. The tested hardware and remaining validation are documented in [Hardware testing](docs/HARDWARE_TESTING.md).

| Target | Build and host tests | Device validation |
|---|---|---|
| Zephyr | Passing | USB dongle commands, streaming, serial reconnection, authenticated iPhone Bluetooth, battery reporting, backlight controls and two-button screen blanking/waking |
| FreeRTOS | Passing | USB startup, dongle commands, streaming, serial reconnection, battery reporting and USB backlight commands; Bluetooth advertising observed |

**FreeRTOS release caveat:** the FreeRTOS image published in `v0.1.0-rc.1` failed startup on the M1. The source now includes startup and battery-reading fixes, tested locally on 2026-10-19. Those fixes are not in the existing release assets. Build the corrected source for FreeRTOS; see [hardware validation and limitations](docs/HARDWARE_TESTING.md#freertos).

Actual LoRaWAN join/uplink/downlink delivery, outdoor GPS acquisition, long-duration operation and current consumption remain to be validated. Release manifests retain `hardware_validated: false`. Passing a build or host simulation does not establish field readiness.

## Features

- Tracker and USB dongle modes, selectable with a button, Bluetooth command or USB command.
- GPS position, battery estimate and device status, with independent local USB and Bluetooth streams at 2–60-second intervals.
- AU915 sub-band 2, LoRaWAN 1.0.x OTAA and Class A operation.
- Position uplinks on FPort 1; application messages on FPorts 2–223, with received downlink reporting.
- Large, 16-pixel screen text, three information pages and a 24-pixel Bluetooth pairing code.
- Partial e-paper updates restricted to changed regions; identical frames do not refresh. A full update runs at startup or on `display clean;`.
- Hold both buttons to blank the display before switching off with the rotary knob.
- Automatic GPS sampling, standby between samples, radio/display sleep, idle CPU sleep and a 60-second backlight timeout.
- Persistent LoRaWAN credentials and state shared by the two native firmware targets; separate Bluetooth bond storage for each RTOS.
- Pre-flash tests, native cross-builds, application-only image checks and manual GitHub release automation.

## Hardware and network requirements

The board target is the Elecrow ThinkNode M1 with its installed Elecrow/Adafruit bootloader and Nordic S140 6.1.1. Application images use the memory layout described in [Architecture](docs/ARCHITECTURE.md). Check [Flashing and migration](docs/FLASHING.md) before replacing existing firmware or using a different bootloader layout.

Local Bluetooth and USB commands work without a LoRaWAN network. Radio messaging requires:

- An AU915 LoRaWAN gateway configured for the same channel group as the M1.
- A ChirpStack server and provisioned OTAA device credentials.
- An application service to interpret messages and route requests to other sensors or devices.

```text
Phone -- Bluetooth --+
                     |
Computer --- USB --- M1 -- LoRaWAN -- Gateway -- ChirpStack -- Application
```

The M1 is a LoRaWAN **end device**, not a gateway. It cannot directly receive other sensors' uplinks or connect to ChirpStack over IP. Class A downlinks arrive after an uplink; continuous remote sensor streaming requires an application design suited to LoRaWAN's small, intermittent messages. Local `stream` commands do not generate radio traffic.

## Buttons and operating modes

Button 1 is the backlight button (P1.10); button 2 is the page button (P1.07).

| Control | Action |
|---|---|
| Tap button 1 | Toggle the backlight when released |
| Hold button 1 for at least 1 second | Switch between tracker and USB dongle mode, turn on the light and show the radio/mode page |
| Tap button 2 | Cycle overview → GPS → radio/mode |
| Hold button 2 for about 1.2 seconds | Request a fresh LoRaWAN join using stored credentials |
| Hold both buttons together for at least 2 seconds | Clear the screen and turn off its light; release both buttons afterward |
| Press either button while the screen is blank | Restore the current page and turn on the light; this first press only wakes the screen |
| Rotary power/brightness knob | Turn clockwise to power on and adjust brightness; turn fully counterclockwise to power off |
| GPS switch | Permit GPS operation when on; force GPS off when off |

Keep button 1 pressed for at least one second, then release it; the e-paper display may take a moment to show the new mode. A hold acts once until the button is released. Turning the brightness knob alone does not enable the light. The light switches off after 60 seconds; tap button 1 to enable it again. Turning off the backlight does not shut down the M1. Use the rotary power switch to turn the device off; the button controls above are specific to this firmware. See the [manufacturer’s power-switch instructions](https://www.elecrow.com/download/product/CIL12901M/ThinkNode-M1_User_Manual.pdf).

### Turn the screen off before powering down

1. Press **both buttons together for at least two seconds** to blank the screen and turn off its light.
2. Wait for the display to finish clearing, then release both buttons.
3. To turn the device off, rotate the knob fully counterclockwise to its **off click**.

The erase uses partial updates; faint e-paper ghosting may remain. Pressing both buttons together suppresses their individual mode/join/tap actions until both are released. A shorter two-button press does nothing.

Blanking only affects the screen and light: tracking, Bluetooth and USB keep running until you switch off the power. Background updates and Bluetooth connection events leave the screen blank. If you keep using the device, press either button to restore the current page and light; release it before using a normal button gesture. `page 0;`, `page 1;`, `page 2;` or `display clean;` also restore the display. If a new Bluetooth pairing needs a code while the screen is blank, press either button to reveal it. Powering on again restores the normal screen.

### Tracker and dongle modes

**Tracker mode** enables automatic GPS sampling and scheduled location uplinks when joined. **USB dongle mode** pauses automatic location uplinks and initially puts GPS in standby. Bluetooth, USB commands and the display work in either mode. `gps now;` can acquire a local position while in dongle mode.

Mode changes take effect immediately. The overview and radio screens show `Mode: Track` or `Mode: Dongle` on one line; `status;` reports the full operating mode. Disconnecting USB retains the selected mode. Rebooting returns to tracker mode; mode selection is not saved to flash. The physical GPS switch takes precedence over software commands.

## Phone setup

1. Install [Adafruit Bluefruit Connect](https://learn.adafruit.com/bluefruit-le-connect).
2. Scan for **ThinkNode-M1** and connect. Enter the six-digit code shown on the M1 if prompted.
3. Open **UART**, select **Text** and **ASCII**, and send commands ending in `;` or a newline.

```text
status;
stream 5;
stream off;
mode tracker;
mode dongle;
gps now;
```

`status;` includes the firmware/RTOS version as well as measurements and operating mode. The on-device overview focuses on mode, battery voltage/percentage and status, without a device-name or RTOS heading. `Join: no` means the M1 has not joined a LoRaWAN network; `Join: yes` means it has an active LoRaWAN session. Bluetooth and USB work independently of this state. The radio page shows the numeric result of the last join attempt.

Bluetooth replies use short, labelled lines. Timestamp view shows individual BLE packets and can split words. If a stale bond prevents connection after changing RTOS, forget ThinkNode-M1 in the phone's Bluetooth settings and reconnect through Bluefruit Connect.

See [Bluetooth commands and provisioning](docs/BLUEFRUIT.md) for the full command table, pairing details and output examples.

## USB dongle setup

Connect the M1 by USB and use the Python client from the repository root. With the project virtual environment active:

```sh
python scripts/m1.py ports
python scripts/m1.py --port /dev/cu.usbmodemXXXX status
python scripts/m1.py --port /dev/cu.usbmodemXXXX mode dongle
python scripts/m1.py --port /dev/cu.usbmodemXXXX monitor --interval 5
```

Replace the example port with the one listed by `ports`. Linux commonly uses `/dev/ttyACM0`; Windows uses a COM port. A client-only installation requires Python 3.10+ and `pyserial==3.5`, without firmware toolchains.

The client emits JSON lines for scripting. The underlying USB protocol retains single-line `STATUS protocol=1 ...` records, independently of the readable Bluetooth layout. Sending radio messages requires credentials and a joined network. See [USB dongle protocol](docs/USB_DONGLE.md) for sending, receiving and provisioning.

## Build and verify

Supported bootstrap hosts are Apple Silicon macOS and x86_64 Linux. Install Git, curl, Python 3.12, a host C++ compiler and Node.js 20+. Linux also needs `device-tree-compiler`.

Run from the repository root:

```sh
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements-dev.txt
python scripts/bootstrap.py --rtos all

# Host tests and simulations; no device required.
python scripts/verify.py

# Build, test and package either target, or both.
python scripts/verify.py --build --rtos zephyr
python scripts/verify.py --build --rtos freertos
python scripts/verify.py --build --rtos all
```

| Target | Native platform |
|---|---|
| `zephyr` | Zephyr 4.4.2, Zephyr SDK 1.0.1 |
| `freertos` | FreeRTOS Kernel 11.2.0, Nordic nRF5 SDK 15.3.0, existing S140 6.1.1, Arm GCC 14.2 |

The FreeRTOS target uses the Nordic SDK version matching the installed SoftDevice. RTOS selection is a build choice, not an on-device toggle or dual boot. Dependencies are pinned in [dependencies.lock.json](dependencies.lock.json).

Compile-only iteration uses `python scripts/build.py --rtos zephyr` or `--rtos freertos`. Use `verify.py --build` before flashing or releasing. Tests cover command parsing, power schedules, button gestures, screen bounds/refreshes, transport formatting, persistent state and image packaging. See [Verification](docs/VERIFY.md) for scope and limitations.

[GitHub Actions checks](https://github.com/bootstrap314/thinknode_m1-infovore/actions/workflows/ci.yml) run on branch pushes and pull requests: host tests and application-only firmware builds for **Zephyr and FreeRTOS**, plus a **Gitleaks secret scan of Git history**. They require no attached hardware or LoRaWAN credentials. Physical USB/Bluetooth/radio testing remains separate.

Downloads stay in `.deps/`, tools in `.tools/`, build files in `build/` and packaged firmware in `dist/<rtos>/`. These generated directories are ignored by Git.

### Firmware files and flashing

Each verified target produces:

- `.uf2` and `-dfu.zip`: application-only update images.
- `.hex` and `.elf`: application image and debugging symbols.
- `-support.zip`: host client, documentation and third-party notices.
- `-manifest.json`, `verification-<rtos>.json` and `SHA256SUMS.txt`: dependencies, hashes and verification evidence.

Follow [Flashing and migration](docs/FLASHING.md) to install the serial DFU helper, select the device and preserve the existing bootloader. The supported flash command runs the verification gate before accessing USB:

```sh
python scripts/flash.py --rtos zephyr --port /dev/cu.usbmodemXXXX
```

## LoRaWAN credentials

Release images contain no private LoRaWAN keys. Provision over authenticated Bluetooth UART or local USB:

```text
provision <JoinEUI> <DevEUI> <AppKey>;
```

Use canonical hexadecimal strings without `0x`: 16 characters each for JoinEUI and DevEUI, and 32 for AppKey. Keys are not echoed. Native credentials and LoRaWAN state survive application-only updates, including switching between the native RTOS builds. Migration from other firmware requires the procedure in the flashing guide.

Keep local credential files in the Git-ignored `private/` directory. `include/secrets.h`, local environments, downloaded dependencies, build logs and generated artifacts are also ignored. Do not commit credentials or attach them to releases. CI and release jobs run a pinned Gitleaks scan with redacted findings; review staged changes before pushing because automated scanners cannot recognize every secret.

## Manual GitHub releases

The workflow builds both firmware variants from an existing pushed version tag:

1. Ensure the workflows are present on the repository's default branch.
2. Create and push a tag such as `v0.1.0-rc.1`.
3. Open **Actions → Manual firmware release → Run workflow**, choose the default branch and enter that tag.
4. Both targets and the secret scan must pass before the workflow creates a **draft release** with their firmware and verification files.
5. Review and publish the draft manually.

A tag push alone does not publish a release. Existing releases are not overwritten. See [Release procedure](docs/RELEASING.md).

## Source layout

```text
firmware/common/     Shared application, radio HAL, output and screen drivers
firmware/zephyr/     Zephyr configuration, board definition and native adapter
firmware/freertos/   FreeRTOS/Nordic configuration, linker script and adapter
include/            Portable command, button, GPS, power and storage policies
scripts/            Bootstrap, builds, verification, packaging and USB tools
tests/              Host tests and simulations
chirpstack/         Application payload decoder
.github/workflows/  CI and manual tag-based release workflow
licenses/           Third-party notices
```

## Credits and third-party licenses

This project uses RadioLib, Zephyr or FreeRTOS/Nordic components, adapted GxEPD2 display sequencing and the font8x8 bitmap font. Third-party components retain their respective licenses and attributions; see [Third-party notices](licenses/README.md).
