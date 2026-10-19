# Changes

## v0.1.0-rc.1 — initial preview

- Prepare the public repository and first draft prerelease with native Zephyr and FreeRTOS firmware.
- Run both firmware build/test jobs as GitHub checks, with a pinned, redacted secret scan on CI and before draft release creation.
- Hold both buttons for two seconds to blank the screen before powering off; press either button to wake it without changing mode. Blanking is temporary, and every startup draws the normal screen.
- Confirm the blank/wake gesture on the Zephyr M1 and retain the documented limits on FreeRTOS, radio and field testing.

## Native firmware preview — 2026-10-18

- Convert the Meshtastic-oriented ThinkNode M1 into a dedicated private-ChirpStack LoRaWAN tracker/messenger, with a USB dongle mode.
- Select native Zephyr or native FreeRTOS builds. Remove the former Arduino/PlatformIO application, board variants, and build helpers from the active project.
- Share GPS validation, battery/status reporting, AU915 Class A messaging, partial e-paper updates, light control, BLE UART commands, power policies and flash persistence across the two builds.
- Add runtime provisioning, nonce/frame-counter reservations and a shared two-page journal. Keep BLE bonds in separate RTOS storage regions.
- Add USB status streaming, dongle/tracker mode commands, downlink events, and a JSON-lines Python USB client.
- Add host tests/simulations, native cross-builds, application-only image validation, and a verification gate before flashing.
- Add GitHub Actions CI and manually dispatched releases from existing pushed version tags. Both firmware variants must pass before a draft release is created.
- Document build selection, phone and USB usage, ChirpStack topology, migration/recovery, release procedure, and hardware acceptance checks.

Both native ports are build-tested. Zephyr was subsequently flashed and passed the USB and iPhone Bluefruit functional checks documented in `docs/HARDWARE_TESTING.md`; FreeRTOS and actual LoRaWAN traffic remain untested. Pre-migration source and private inputs are retained separately; local private files are excluded from builds and releases.

## Zephyr hardware follow-up — 2026-10-18

- Flash and test native Zephyr through the installed serial bootloader.
- Fix USB startup with an already-connected cable after bootloader handoff.
- Use Zephyr's named, zero-based ADC channel for the M1 battery input.
- Add an opt-in USB hardware acceptance script and local JSON evidence.
- Confirm authenticated iPhone pairing, UART readings, five-second streaming and screen control while USB dongle mode is active.
- Clarify that button 1 enables the light, the knob adjusts brightness, and the 60-second timeout is intentional.

## Readable Bluetooth output — 2026-10-18

- Format Bluetooth status and streams as short, labelled lines, with clear unavailable and last-known position labels.
- Wrap phone help, diagnostics and downlinks at 32 characters; document Bluefruit Text/ASCII mode.
- Preserve the USB protocol and add formatter regression tests for exact USB records and phone output boundaries.

## Physical controls and screen readability — 2026-10-18

- Hold button 1 for one second to toggle tracker/USB dongle mode, with visible mode feedback; taps still toggle the light on release.
- Preserve button 2 page/join gestures and share GPS transitions with serial/Bluetooth mode commands.
- Enlarge on-device labels and values to 16 pixels while retaining partial display updates and a 24-pixel pairing code.
- Use mixed-case, single-line mode labels (`Mode: Track` / `Mode: Dongle`), keep battery voltage and percentage, and remove device-name/RTOS headings from the screen.
- Measure holds from the accepted press edge without adding debounce time; cover release exactly at the hold threshold.
- Add button timing and screen-layout regression tests.
- Rewrite the README for public readers, including requirements, controls, setup, validation limits and releases; remove personal checkout assumptions from linked guides.
- Handle macOS serial-port removal during the native-to-bootloader handoff, while retaining matching-device checks before upload.

- Reject misleading serial uploader success exits after interrupted transfers; require the explicit programming acknowledgement.
- Document power-knob operation and recovery from an interrupted upload using the existing bootloader.
