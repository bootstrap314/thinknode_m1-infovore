# Hardware validation — updated 2026-10-19

Paths in this document are relative to the repository root. Hardware logs under `build/` are local evidence and are ignored by Git.

## Zephyr

Flashed through the installed serial DFU bootloader with `scripts/flash.py --rtos zephyr`, after passing host tests, cross-build and image validation. Application-only updates preserved the installed bootloader. These results describe the Zephyr runs before the FreeRTOS checks below.

Verified on the attached M1:

- Native USB product/serial identity and protocol=1 status handshake.
- Automatic USB enumeration immediately following the corrected firmware upload; no cable replug needed.
- Dongle mode and GPS standby, plus mode retention across a serial close/reopen.
- Local status streaming over USB, and stream shutdown when the serial connection closes.
- Fragmented commands, oversized-command rejection and parser recovery.
- Unjoined send rejection without transmitting an application uplink.
- Backlight on/off commands, with the physical button and brightness knob confirmed by the user.
- Plausible battery reporting at approximately 4.22 V while plugged in.
- Bluetooth advertising with the Nordic UART Service and authenticated iPhone pairing.
- Bluefruit Connect UART `status;`, five-second readings with `stream 5;`, and radio-page selection with `page 2;`, confirmed by the user.
- Concurrent USB status streaming and authenticated BLE in dongle mode. USB observations recorded the page changing from 0 to 2 in response to the phone command.

These are initial functional checks. Authenticated BLE reconnection after application updates was subsequently observed; stale-bond recovery has not been separately tested. Actual AU915 join, uplink/downlink delivery, session restore with a server, outdoor GPS fixes, long-duration stability and current consumption are not validated. No gateway or ChirpStack server is configured yet. Release manifests retain `hardware_validated: false`; passing this subset does not mean full field acceptance.

Two issues found during the first hardware run were corrected:

1. The bootloader can consume the USB cable-insertion event before starting Zephyr. Startup now checks the current VBUS level, as well as listening for later events, so USB starts without replugging.
2. Zephyr's ADC interface uses zero-based analog channel identifiers. The battery input now uses the named `NRF_SAADC_AIN2` constant for P0.04 rather than a hardware-register selector value.

## Two-button screen blanking — confirmed on Zephyr

The new shared implementation holds both buttons for two seconds to erase the screen using partial updates and disable the light. Host tests cover gesture timing/conflicts, wake/release behavior, persistent refresh suppression and the display erase/retry paths. The updated Zephyr image was flashed on 2026-10-18 after passing the verification gate; the serial DFU tool explicitly acknowledged successful programming. All 12 USB acceptance checks passed. The device was returned to tracker mode, overview page 0, with the light enabled; USB reported 4.22 V, healthy storage and Bluetooth advertising. The operator confirmed that holding both buttons clears the screen, it remains blank for about 20 seconds after release, and pressing a button restores the screen without changing mode. Phone pairing has not been retested on this image.

Evidence: `build/screen-blank-flash.log`, `build/screen-blank-usb-test.log`, `build/hardware/screen-blank-usb-report.json` and `build/hardware/screen-blank-acceptance.json`. The acceptance record identifies the flashed package hash and separately records operator confirmation when available. Other hardware results below describe earlier firmware.

Remaining physical checks: blank-state persistence through a Bluetooth reconnect, ordinary individual gestures after waking, an off/on cycle after blanking, and acceptable partial-update ghosting. By design, the blank state is held only in RAM: every firmware startup initializes it to false and draws the normal screen; it is not persisted in flash.

## Readable Bluetooth output follow-up

Zephyr was updated again on 2026-10-18 with short, labelled Bluetooth replies. Status and streaming share the same formatter; phone help, diagnostics and downlinks are also wrapped. USB keeps the original protocol. The phone owner confirmed the new status/stream layout was readable and requested a shorter position label; the final firmware uses `Position: no fix` (16 characters).

The final flashed image passed all 12 USB hardware checks again, with authenticated Bluetooth connected concurrently. Formatter regression tests verify the exact previous USB status/response bytes, phone line widths, stale/unavailable GPS states, long payloads and output failures. Both native targets pass the host/build/image verification gate. FreeRTOS is still build-tested only; this update does not establish radio or power acceptance.

The follow-up logs are `build/readable-output-flash.log`, `build/readable-output-verification.log` and `build/readable-output-usb-test.log`. Firmware hashes and hardware results are recorded in `build/hardware/readable-output-usb-report.json` and `build/hardware/readable-output-acceptance.json`. In Bluefruit UART, use **Text / ASCII**, send `status;` and `stream 5;`, and check that the labelled fields fit on the phone. Timestamp view exposes BLE packet boundaries.

## Button mode switch and larger screen text

The Zephyr update adds a one-second button-1 hold to toggle tracker/dongle mode. Short presses toggle the light on release, while button 2 retains page cycling and its join hold. Mode commands and the physical shortcut use the same GPS transition. Mode switching enables the light and selects the radio page.

All on-device labels/values now use 16-pixel text, with a 24-pixel pairing code. The overview starts with mode, shows battery voltage and charge percentage, and omits the device-name/RTOS headings. Key/value labels have colons. Mode is shown on one line as `Mode: Track` or `Mode: Dongle`, using mixed case. Firmware identity is still available through Bluetooth and USB `status;`.

Host checks cover button bounce, hold/tap exclusivity, repeated holds, delayed polls and timer rollover. Screen-layout tests check bounds and overlaps at extreme coordinates, counters and error values; framebuffer previews were rendered through the actual display driver. After flashing, all 12 USB checks passed on the final layout and authenticated Bluetooth remained connected.

An earlier streaming check timed out while pages were being changed; the repeat with the screen idle passed. Stream intervals are scheduling targets, since screen refreshes and radio operations can block the application loop. The upload helper also now handles the observed macOS serial disconnect during bootloader entry, with regression tests; it still requires the matching bootloader identity before uploading.

The initial two-second gesture needed a longer hold in operator testing. A four-second hold was confirmed by the operator and independently observed over USB changing dongle to tracker and selecting page 2. The follow-up uses a one-second threshold measured from the accepted press edge, without adding debounce time. Its build, flash and USB logs use the `build/button-hold-` prefix; physical confirmation is recorded separately in `build/hardware/button-hold-acceptance.json`. A USB disconnect interrupted the first transfer; the uploader now rejects a zero exit code unless it also receives the explicit programming-success acknowledgement. The interrupted transcript is retained in `build/button-hold-flash-interrupted.log`.

The device subsequently enumerated as `ELECROW PCA10056` (`239A:00DA`) with an `ELECROWBOOT` volume. The verified one-second-hold Zephyr image was installed through serial DFU, and the tool explicitly acknowledged successful programming. All 12 USB checks passed after recovery; battery reporting was 4.22 V, and Bluetooth was advertising. The operator confirmed that the normal screen returned and mode switching works. No new iPhone pairing/streaming test was performed on this recovery image. Recovery logs are `build/button-hold-recovery-flash.log` and `build/button-hold-recovery-usb-test.log`; the exact firmware hash and operator confirmations are in `build/hardware/button-hold-acceptance.json`.

Evidence: `build/button-screen-flash.log`, `build/button-screen-usb-test.log`, `build/button-screen-verification.log`, `build/hardware/button-screen-usb-report.json`, `build/hardware/button-screen-final-observations.json` and `build/hardware/button-screen-acceptance.json`. The last file records operator confirmation of the physical buttons and final layout. Radio and field-acceptance limits above still apply.

## Repeat the USB check

With the selected firmware already running and the port printed by `scripts/m1.py ports`:

```sh
source .venv/bin/activate
python scripts/hardware_usb.py --port /dev/cu.usbmodemXXXX --rtos zephyr
# For FreeRTOS, use --rtos freertos. To check a particular build's version too:
python scripts/hardware_usb.py --port /dev/cu.usbmodemXXXX --rtos freertos \
  --manifest dist/freertos/thinknode-m1-freertos-manifest.json \
  --report build/hardware/freertos-usb-report.json
```

This is an explicit hardware test, separate from the pre-flash gate. It enables dongle mode, exercises the backlight commands, and leaves dongle mode selected. It does not provision credentials or initiate a LoRaWAN transmission. JSON evidence is saved to `build/hardware/usb-report.json`; the run transcript is `build/zephyr-usb-test.log` when redirected by the operator. Flash transcript: `build/zephyr-flash.log`. Build reports and firmware manifests identify the tested image hashes. Bluetooth coexistence observations are in `build/hardware/usb-ble-coexistence.json`; the combined acceptance record, including user-confirmed iPhone results, is `build/hardware/acceptance.json`. These local logs are ignored by Git.

The test rejects an unexpected RTOS before changing device settings. `--manifest` also checks the reported version and records the supplied manifest in the report. Supply the manifest from the image actually uploaded; a version match alone does not prove a binary hash. Without this option, no local build manifest is attached to the result. Regression tests cover mismatched RTOS, manifest target and version.

For iPhone testing, connect in Bluefruit Connect, enter the displayed passkey, open UART, and send `status;`, `stream 5;`, and `page 2;`. Verify readings and the screen change, then disconnect/reconnect and repeat `status;`. Both the secure BLE subscription and USB can operate at once. After changing between old and new Bluetooth stacks, forget a stale phone bond if pairing fails.

## FreeRTOS

On 2026-10-19, the published `v0.1.0-rc.1` FreeRTOS package passed artifact validation and uploaded successfully, but did not appear over USB or Bluetooth. The operator observed the previous e-paper image and a blinking LED. Double-clicking reset restored the existing bootloader; no bootloader, SoftDevice or provisioning region was erased.

The corrected source initializes the Nordic power driver before enabling the SoftDevice. Otherwise `app_usbd_init()` tries to initialize that driver after Bluetooth is enabled and the SDK rejects it. The startup flash-layout check also now uses Nordic's MBR-first/UICR-fallback bootloader-address lookup, matching FDS. The address-check change alone did not restore startup; USB appeared after correcting power-driver initialization.

The first USB run then rejected a battery value of `-1.00V`. The ADC DMA buffer is now aligned and volatile so optimized code reads the hardware-written sample. The next run reported approximately 4.21 V. USB product/manufacturer macros were corrected for SDK 15.3 so the device identifies as `ThinkNode M1 FreeRTOS` instead of the SDK demo.

The final local `dev` image passed all **13 USB checks**, including RTOS/version identity, dongle mode and GPS standby, battery plausibility, backlight commands, unjoined-send rejection, fragmented/oversized command handling, streaming, and serial close/reopen behavior. Native USB bootloader entry also worked for the subsequent application-only update. Bluetooth UART advertising was observed. The device was returned to tracker mode, overview page 0, with the light enabled and healthy storage.

Authenticated iPhone UART, physical buttons, screen blank/wake, and battery-only behavior are awaiting operator confirmation for this FreeRTOS build. The radio/GPS/current-consumption limitations above still apply. These are source fixes and local test results, **not a replacement of the existing `v0.1.0-rc.1` assets**. Release manifests still say `hardware_validated: false`.

Local evidence: `build/freertos-release-flash.log`, `build/freertos-power-init-flash.log`, `build/freertos-battery-flash.log`, `build/freertos-battery-usb-test.log`, `build/hardware/freertos-battery-usb-report.json` and `build/hardware/freertos-acceptance.json`. The acceptance record retains the exact flashed package manifest and separates automated checks from operator confirmation.
