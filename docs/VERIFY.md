# Verification before flashing

From the repository root, activate `.venv` and run:

```sh
python scripts/verify.py
python scripts/verify.py --build --rtos all
```

The first command runs host tests without touching USB. The second additionally cross-compiles both native targets and validates their update packages. Use `--rtos zephyr` or `--rtos freertos` for one target.

## Automated checks

- GitHub Actions runs separate Zephyr and FreeRTOS verification jobs on pushes and pull requests, plus a pinned Gitleaks scan of the full fetched Git history. Findings are redacted. The manual release workflow requires the same secret scan before creating a draft.
- C++ tests compiled with warnings as errors plus AddressSanitizer/UndefinedBehaviorSanitizer.
- Command framing, delimiters, bounds, hex/port limits, and 10,000 fuzz inputs.
- GPS/light policy modes, physical switch priority, timing, and timer rollover.
- Debounced button taps/holds, one-shot mode switching gestures, release timing, delayed polls and uptime rollover. Two-button blanking checks cover staggered presses in either order, threshold boundaries, early release, individual-action suppression, one-shot behavior, wake consumption and re-arming.
- Screen layouts with text at least 16 pixels high, bounded to 200 × 200 with no overlaps, including extreme coordinates/counters and pairing codes.
- Two-hour virtual GPS schedules for successful acquisition and no-fix cases. The output is awake time, not a battery-life prediction.
- Framebuffer differences, changed edges, erasure, isolated fields, and 2,000 randomized transitions.
- Actual native SSD1681 driver against a fake SPI/GPIO backend: startup full update, unchanged-frame skip, bounded partial writes, text erasure, and explicit cleanup. Screen blanking checks verify white partial writes, no transfers during suppressed redraws, restoration after wake and retry after an erase timeout.
- Credential validation, GPS checksum/field validation, and flash-journal recovery after power cuts at each write stage.
- Independent USB/BLE subscriptions, connection loss, and stream timer rollover.
- Actual shared reply formatter against a fake transport: byte-for-byte USB status/response compatibility, readable BLE fields, unavailable/stale GPS labels, 32-character line bounds, long-token preservation, truncation boundaries and interrupted output.
- USB client framing, interleaved downlinks/TX completion, timeouts without duplicate sends, malformed records, and native protocol handshake.
- Hardware-test identity checks reject an unexpected RTOS or manifest version before sending commands that change device state.
- Counter reservation carries, exhaustion without wraparound, RadioLib checksum format, and corrupt-journal fallback.
- ChirpStack payload decoder checks.
- Package rejection checks for protected flash addresses, wrong UF2 family, duplicate/corrupt blocks, bootloader components, bad DFU initialization data, and mismatched payloads.
- Flash entry-point tests proving failed checks and check-only mode cannot upload.
- Remote tag resolution tests for lightweight/annotated tags; invalid/missing tags and incomplete releases are rejected.
- Native compile commands are checked for accidental framework dependencies.
- ELF architecture, load segments and vector table; HEX boundaries; matching UF2/DFU application payload; SoftDevice requirement `0xB6` and CRC; checksums and source fingerprint.

Passing reports are written under `build/verification/` and copied into `dist/<rtos>/`. A stale report is removed at the start of a new verification attempt. The source fingerprint must remain unchanged through tests/build/packaging.

GitHub workflows are also suitable for static checking with [actionlint](https://github.com/rhysd/actionlint).

## What this cannot prove

Host tests do not emulate the complete nRF52840, S140, Bluetooth radio, SX1262 timing, analog battery circuit, GPS hardware, or physical e-paper waveforms. A successful cross-build establishes compile/link compatibility, not runtime compatibility. Zephyr and corrected FreeRTOS source have passed USB and iPhone Bluefruit functional checks. FreeRTOS hardware testing exposed startup, ADC and Bluetooth negotiation issues missed by the host checks; the corrected build passed 13 USB checks and operator phone tests. See [HARDWARE_TESTING.md](HARDWARE_TESTING.md) for exact versions, evidence and pending checks.

Before field use, test each OS on the M1 for startup and USB recovery; USB dongle mode, streaming, unplug/reconnect and downlink delivery; iPhone pairing/reconnection; `status;`/`stream 5;`; physical buttons and light timeout; partial refresh and ghosting; physical GPS switch and standby; and battery ADC readings. With a working AU915 gateway/server, check join, uplink, downlink, reboot/session restore, and counter continuity. Measure idle/GPS/radio/connected currents before claiming battery improvements.
