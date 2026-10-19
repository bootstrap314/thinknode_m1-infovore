# Native firmware architecture

Both builds compile the shared application, output formatter, screen layout and display driver against `platform.h`. RadioLib's generic `RadioLibHal` adapts native GPIO/SPI/time/interrupt operations. The portable command framing, power policy, framebuffer diff, GPS validation, and flash journal also run in host tests.

- **Zephyr 4.4.2:** custom `thinknode_m1/nrf52840` board, native GPIO/SPI/UART/ADC/flash drivers, Zephyr BLE/NUS GATT service, authenticated UART, settings/NVS for bonds, USB device stack Next with CDC ACM, and RTOS idle sleep.
- **FreeRTOS 11.2.0:** native nRF5 SDK 15.3.0/SoftDevice S140 6.1.1 APIs, native Nordic USB CDC, peer manager/FDS bonds, RTC1 tickless kernel port, direct GPIO/SPI/GPS UART/ADC access. Nordic's port is adapted in `port_cmsis.c` to validate priorities without probing a SoftDevice-owned interrupt. BLE, USB, and the application run in separate tasks.

The installed S140/MBR and UF2/serial bootloader remain in flash. FreeRTOS enables S140; Zephyr runs its own controller and leaves S140 inactive. No release package includes a replacement bootloader or SoftDevice.

## Fixed memory map

| Range (end exclusive) | Use |
|---|---|
| `0x00000–0x26000` | Existing MBR + S140 6.1.1; never packaged |
| `0x26000–0xE5000` | Native application, 764 KiB maximum |
| `0xE5000–0xE7000` | Shared credentials/RadioLib journal, two 4 KiB pages |
| `0xE7000–0xEA000` | Zephyr Bluetooth settings/NVS |
| `0xEA000–0xED000` | FreeRTOS peer-manager/FDS bond storage |
| `0xED000–0xF4000` | Previous firmware's reserved storage; left untouched |
| `0xF4000–0x100000` | Existing bootloader/settings; left untouched |

FreeRTOS RAM begins at `0x20006000`, leaving space for S140. Zephyr uses the full nRF52840 RAM while S140 is disabled. FDS uses three virtual pages with seven pages reserved below the bootloader boundary. This layout assumes the inspected Elecrow bootloader at `0xF4000` and S140 FWID `0xB6`; other bootloader layouts require a different board target and packaging policy.

The shared journal writes the inactive page, verifies it, and commits the magic word last; the previous complete snapshot remains valid during an interrupted write. It contains a schema version, credentials, and the pinned RadioLib nonce/session buffers. Before a join, a future DevNonce is committed. Before data transmission, a future uplink counter is reserved. Successful exchanges then save actual state. Persistence failure disables further radio use. BLE bonds have their own OS-specific storage and are not migrated between stacks.

This is a new native storage format. The previous firmware's LittleFS records are not imported. A migration of an already joined deployment needs fresh server credentials/session handling; simply resetting server replay checks is not the intended migration procedure.

## M1 pins

Numbers below are nRF GPIO numbers: 0–31 = P0, 32–47 = P1.

| Function | GPIOs |
|---|---|
| Buttons | 42 / P1.10 and 39 / P1.07, active low with pull-up |
| Light enable / physical knob | 43 / P1.11, high = enabled |
| Shared power / radio supply | 12 / P0.12 and 21 / P0.21 |
| SX1262 SPI | SCK 19, MOSI 22, MISO 23, CS 24 |
| SX1262 reset / DIO1 / busy | 25 / 20 / 17 |
| E-paper SPI | SCK 31, MOSI 29, CS 30; no MISO assigned |
| E-paper DC / reset / busy | 28 / 2 / 3 |
| L76K GPS RX / TX | 41 / P1.09 and 40 / P1.08, 9600 baud |
| GPS wake / reset / switch | 34 high = awake; 37 active-low reset; 33 input only |
| Battery ADC | 4 / P0.04 / AIN2; 3.0 V full scale, divider multiplier 2.02 |
| Notification LED | 13 / P0.13 |

The native display bus does not assign dummy MISO to button 2, avoiding the previous shared-pin workaround. External QSPI flash, I2C, buzzer, and unused peripherals are not enabled by this application.

## Local controls and screen

Button 1 taps toggle the light on release; a one-second hold switches tracker/dongle mode once until released. Button 2 taps cycle pages; its 1.2-second hold requests a join. The shared, portable button policy debounces for 35 ms, measures holds from the accepted press edge, and suppresses tap actions after a hold. Physical and serial/BLE mode changes use the same GPS policy transition. A physical mode change selects the radio page and enables the light for feedback.

Overlapping button presses take priority over individual actions. Holding both for two seconds emits one blank gesture; individual gestures remain suppressed until both inputs have been released for 35 ms. A shorter chord is consumed without an action. On a blank screen, a single-button press wakes the display and light, consuming that entire press, including its hold/release. Existing single-button actions resume after release.

The display driver erases changed regions to white and latches a blank state that suppresses all subsequent refreshes, including background and BLE connection/passkey events. The application turns the light off and preserves mode, page, GPS, radio and transport activity. Explicit page/clean commands or a physical wake clear the latch; reboot starts normally. Erase timeouts are reported over USB and another both-button hold can retry. This is screen blanking, not system shutdown: the user must wait for erasure to finish before cutting power with the rotary switch.

Screen text uses the existing 8 × 8 font at 2× scale (16 pixels per character), with a 3× pairing code. Up to nine rows at 22-pixel spacing fit the 200 × 200 panel. The layout abbreviates labels and separates long coordinates/counters onto their own rows. Mode fits on one line as `Mode: Track` or `Mode: Dongle`. Hardware refresh behavior remains in the display driver.

## Radio and power behavior

AU915 sub-band 2 is RadioLib's one-based numbering: channels 8–15 and 65, corresponding to ChirpStack `au915_1`. LoRaWAN Class A opens downlink receive windows after each uplink. The application uses OTAA 1.0.x, disables ADR for mobile use, and starts joined operation at DR3.

The e-paper driver uses SSD1681 RAM windows and the built-in waveforms. It refreshes fully at startup or explicit cleanup, otherwise computes changed byte-aligned regions, updates only those, synchronizes the controller baseline, and powers the panel down. It does not automatically perform periodic full flashes. Long-term ghosting and temperature behavior need hardware assessment.

GPS automatically samples, sleeps for 15 minutes after a completed sample/timeout, and obeys the physical switch. The light times out after 60 seconds. Battery ADC values are cached for a minute. These policies are tested as schedules; actual energy use depends on hardware and must be measured separately.

## USB dongle operation

USB CDC ACM and authenticated BLE feed separate bounded command parsers into the shared application loop. Each transport has an independent status subscription. Commands run serially; downlinks are reported to both ready transports. `mode dongle` pauses automatic position uplinks and initially sleeps GPS; it does not disable Bluetooth or change Class A radio timing. Mode is volatile and resets to tracker on boot. See [USB protocol](USB_DONGLE.md).

The FreeRTOS USB descriptor and Zephyr hardware-info descriptor both derive the USB serial from FICR DEVICEID, matching the installed bootloader. The flash tool compares this identity without case sensitivity. Native `bootloader` selects the installed serial DFU mode. FreeRTOS verifies the expected bootloader address before initializing bond storage and fails closed on an asynchronous flash timeout.
