# PaperMono User Demo

### SKU: C153, C153-LITE

## Related Link

- [C153 Document & Datasheet](https://docs.m5stack.com/en/products/sku/C153).
- [C153-LITE Document & Datasheet](https://docs.m5stack.com/en/products/sku/C153-LITE).

This project is an ESP-IDF application for ESP32-S3R8 based PaperMono devices. It includes basic demos for display refresh, clock, battery status, brightness, IMU, microphone waveform, RGB LED, buzzer, Wi-Fi scan, NFC scan, LoRa, TF card, sleep/wake, and shutdown behavior.

## IDF Build

### Tool Chains

[ESP-IDF v5.5.1](https://docs.espressif.com/projects/esp-idf/en/v5.5.1/esp32s3/index.html)

### Fetch Components

Component repositories are pinned to full commit hashes in `repos.json`. Project-specific ESP-IDF and Arduino adaptations are stored in `patches/`.

Check the currently installed components without changing them:

```bash
python fetch_repos.py check
```

Fetch components into an empty checkout:

```bash
python fetch_repos.py
```

Existing component directories are never overwritten by default. To rebuild them, the replacement must be explicit; the script moves the existing directories into `_component_backups/` before installing the fetched copies:

```bash
python fetch_repos.py fetch --replace
```

Use `--repo NAME` to process one component and `--output-root PATH` to test in an isolated directory. `--source-root PATH` can use previously cloned `PATH/NAME` repositories as an offline mirror while retaining the locked commits and patches from `repos.json`.

### Configure Target

```bash
idf.py set-target esp32s3
```

### Build

```bash
idf.py build
```

### Flash

```bash
idf.py flash
```

### Monitor

```bash
idf.py monitor
```

## Project Layout

- `main/` - Application source, HAL, app demos, and embedded assets.
- `main/apps/` - Individual demo applications.
- `main/hal/` - Board-level hardware abstraction.
- `components/` - Third-party and M5Stack components.
- `patches/` - Reproducible ESP-IDF and Arduino component adaptations.
- `fetch_repos.py` / `repos.json` - Fixed-version component fetch tooling.
- `partitions.csv` - Custom partition table.
- `sdkconfig.defaults` - Minimal project configuration defaults.

## Acknowledgments

This project references the following open-source libraries and resources:

- https://github.com/m5stack/M5GFX
- https://github.com/m5stack/M5HAL
- https://github.com/m5stack/M5Unified
- https://github.com/m5stack/M5PM1
- https://github.com/m5stack/M5IOE1
- https://github.com/m5stack/M5Unit-NFC
- https://github.com/m5stack/M5UnitUnified
- https://github.com/m5stack/M5Utility
- https://github.com/tobozo/LGFX_Fonts
- https://github.com/arduino-libraries/Arduino_BMI270_BMM150
- https://github.com/Forairaaaaa/mooncake
- https://github.com/jgromes/RadioLib
- https://components.espressif.com/components/espressif/arduino-esp32

## License

- [MIT](LICENSE)
