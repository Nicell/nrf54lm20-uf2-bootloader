# nRF54LM20 UF2 Bootloader

A small UF2 bootloader for the
[nRF54LM20 DK](https://www.nordicsemi.com/Products/Development-hardware/nRF54LM20-DK).
On reset, holding the user button (or double-tapping reset) drops the
device into DFU mode: it enumerates as a USB mass-storage volume that
accepts a `.uf2` drag-and-drop. The application is flashed in place
and chain-loaded; no extra reboot.

## Quick start

```sh
git clone https://github.com/<you>/nrf54lm20-uf2-bootloader.git
cd nrf54lm20-uf2-bootloader
python3 -m venv .venv
source .venv/bin/activate
pip install west
west init -l bootloader
west update
west zephyr-export
pip install -r zephyr/scripts/requirements-base.txt
west build -b nrf54lm20dk/nrf54lm20a/cpuapp bootloader
west flash
```

## Layout

```
nrf54lm20-uf2-bootloader/
├── bootloader/         ← Zephyr application + west manifest
│   ├── west.yml        (pulls Zephyr v4.4.0)
│   ├── CMakeLists.txt
│   ├── boards/         (DT overlay carving partitions)
│   └── src/
├── zephyr/             ← pulled by west, gitignored
└── modules/            ← pulled by west, gitignored
```

## Partition layout (nRF54LM20A cpuapp_rram, 2 MiB)

| Partition | Offset | Size |
|---|---|---|
| `boot_partition` | `0x000000` | 96 KiB |
| `slot0_partition` | `0x018000` | 1840 KiB |
| `storage_partition` | `0x1e4000` | 100 KiB |

The application must be linked with `zephyr,code-partition = &slot0_partition;`.

## Indicator LED

The `pwm-led0` DT alias (present on the LM20 DK) is driven through four
states:

- **UNPLUGGED** — slow breathe, waiting for USB host
- **PLUGGED** — fast breathe, host enumerated
- **WRITING** — hard fast blink, UF2 block in flight
- **FINISHED** — solid on, transfer complete

## Credits

The UF2 / ghost-FAT engine (`src/ghostfat.c`, `src/uf2.h`, `src/compile_date.h`)
is ported from [tinyuf2](https://github.com/adafruit/tinyuf2) — itself descended
from Microsoft's [uf2-samdx1](https://github.com/microsoft/uf2-samdx1) and
Adafruit's work, with the on-the-fly FAT16 synthesis by Henry Gabryjelski. The
double-tap magic values (`0xf01669ef` / `0xf02669ef`) match those used by tinyuf2
and the
[Adafruit nRF52 bootloader](https://github.com/adafruit/Adafruit_nRF52_Bootloader),
so an application's "reboot into bootloader" path works here unchanged.

## License

MIT. See [LICENSE](LICENSE).
