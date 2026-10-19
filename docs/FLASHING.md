# Flashing, migration, and recovery

Zephyr was flashed and tested on the physical M1 on 2026-10-18. USB command/stream tests and iPhone Bluefruit pairing, readings and screen-control checks passed. FreeRTOS has not been flashed; LoRaWAN traffic still needs a gateway/server. See [hardware validation](HARDWARE_TESTING.md) for the tested scope.

Both targets use the inspected Elecrow bootloader, S140 6.1.1 (FWID `0xB6`), application start `0x26000`, and bootloader start `0xF4000`. Images deliberately stop before native data storage at `0xE5000`. Do not use mass erase or a generic nRF52840 DK full-flash image: those can erase the installed bootloader and provisioning data.

## Verified local flashing

```sh
# From the repository root
source .venv/bin/activate
python scripts/flash.py --rtos zephyr --check-only
# Or: --rtos freertos
```

Check-only runs tests, a fresh native build, and image validation without accessing USB.

The optional [Adafruit nrfutil](https://github.com/adafruit/Adafruit_nRF52_nrfutil) command-line tool speaks the installed legacy serial bootloader protocol. It is a host update utility, not an Arduino firmware dependency. The tested host utility is `adafruit-nrfutil==0.5.3.post16`. Install it in a separate environment and put it on PATH:

```sh
python -m venv .tools/dfu-venv
.tools/dfu-venv/bin/python -m pip install adafruit-nrfutil==0.5.3.post16
export PATH="$PWD/.tools/dfu-venv/bin:$PATH"
```

Then explicitly select the device:

```sh
python scripts/flash.py --rtos zephyr --port /dev/cu.usbmodemXXXX
```

The script verifies everything before opening a port, checks USB identity, asks native firmware to enter the bootloader, and uploads only to the matching device serial number. The previous firmware also supports a 1200-baud touch. `bootloader;` is available from native USB or authenticated BLE UART. The script requires an explicit port; it does not pick an arbitrary serial device. Upload success requires the tool’s `Device programmed.` acknowledgement and a clean exit: an interrupted transfer may otherwise misleadingly return exit code zero. Keep USB connected until startup checks finish.

If the installed bootloader exposes the `ELECROWBOOT` mass-storage volume, the matching `.uf2` is an alternative. Both this volume and serial DFU have been observed on the tested M1. An interrupted upload can leave the device in the bootloader with no normal screen or Bluetooth service. If it appears as `ELECROW PCA10056` with USB VID/PID `239A:00DA`, it is powered and the bootloader is available: rerun the verified serial-flash command above using that port, then check application startup before disconnecting. Power cycling alone does not complete an interrupted upload.

If neither the application nor bootloader enumerates, check power and the USB data cable. The [manufacturer’s recovery instructions](https://www.elecrow.com/download/product/CIL12901M/ThinkNode-M1_User_Manual.pdf) describe connecting USB and double-clicking the reset button to enter programming mode. Hardware recovery may require an SWD probe if the bootloader itself is damaged; normal application-only updates do not overwrite it.

## Provisioning and changing RTOS

Release firmware is unprovisioned. Existing private credentials are kept in `private/`; the build does not read them. Provision through authenticated BLE or local USB as described in [BLUEFRUIT.md](BLUEFRUIT.md).

Application-only updates preserve the shared native flash journal, so Zephyr ↔ FreeRTOS switching retains the native credentials and RadioLib state. BLE bonds are stored separately and the phone may need to forget/re-pair when the Bluetooth stack changes.

The former Arduino implementation used another storage format. Its nonce/session records are **not imported**. For a device already registered and joined to a server, migrate using a fresh key/device assignment and validate counters; do not assume native state continues the previous format. Returning to old firmware also returns to its old session database.

Keep a backup of the previous working firmware and any required private configuration outside the public repository. Local backups and credentials are not release assets.

After an actual upload, confirm startup, phone communication, and recovery before disconnecting. Building or running host verification does not require the M1 to be connected.
