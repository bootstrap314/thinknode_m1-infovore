# Third-party notices

Native dependencies are downloaded by `scripts/bootstrap.py` and remain outside tracked source. Their upstream source files retain their own licenses.

- [RadioLib 7.8.1](https://github.com/jgromes/RadioLib/tree/7.8.1): MIT; `RadioLib.txt`.
- [FreeRTOS Kernel 11.2.0](https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.2.0): MIT; `FreeRTOS.txt`.
- [Zephyr 4.4.2](https://github.com/zephyrproject-rtos/zephyr/tree/v4.4.2): Apache-2.0 and component-specific notices; `Zephyr.txt` and upstream module notices.
- [Nordic nRF5 SDK 15.3.0](https://developer.nordicsemi.com/nRF5_SDK/nRF5_SDK_v15.x.x/): Nordic's license and component notices; `Nordic-SDK.txt`. For Nordic hardware. The FreeRTOS config/port/linker setup derives from Nordic SDK examples; copied files retain their notices.
- SSD1681 sequencing is adapted from Jean-Marc Zingg's [GxEPD2_154_D67](https://github.com/ZinggJM/GxEPD2), GPL-3.0; `GxEPD2.txt`. The native application does not link GxEPD2 or its graphics/framework dependencies.
- `firmware/common/font8x8.h`: Daniel Hepper's public-domain [font8x8](https://github.com/dhepper/font8x8), including the retained attribution to Marcel Sondaar/IBM VGA fonts.
- Earlier tracker logic originated in [maur3ra/thinknode-ttnmapper](https://github.com/maur3ra/thinknode-ttnmapper). This project replaces its framework/hardware layer and targets a private ChirpStack deployment.

The installed Elecrow/Adafruit bootloader and Nordic SoftDevice are not bundled in generated releases. The optional Adafruit serial update utility runs on the host computer only.
