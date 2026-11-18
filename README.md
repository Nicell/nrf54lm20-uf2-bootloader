# nRF54LM20 UF2 Bootloader

A small UF2 bootloader for the nRF54LM20A/B. In DFU mode it enumerates as a
USB mass-storage drive (high-speed USB when the host supports it); drop a
`.uf2` on it and the app is written, committed, and booted.

Fork of [carrefinho/nrf54lm20-uf2-bootloader](https://github.com/carrefinho/nrf54lm20-uf2-bootloader)
with Lily Ultra board support and the changes listed under
[Differences from upstream](#differences-from-upstream).

## Boards

| Target | Notes |
|---|---|
| `nrf54lm20dk/nrf54lm20a/cpuapp` | Button 0 held at reset, or double-tap reset |
| `lily_ultra_left/nrf54lm20a/cpuapp` | Lily Ultra left half |
| `lily_ultra_right/nrf54lm20a/cpuapp` | Lily Ultra right half |

The Lily Ultra board files (`bootloader/boards/lily/lily_ultra/`) are
written to be reused as-is by the ZMK build.

## Quick start

```sh
git clone https://github.com/Nicell/nrf54lm20-uf2-bootloader.git
cd nrf54lm20-uf2-bootloader
python3 -m venv .venv && source .venv/bin/activate
pip install west
west init -l bootloader
west update
west patch -sm bootloader apply   # see bootloader/zephyr/patches.yml
pip install -r zephyr/scripts/requirements-base.txt
west build -b lily_ultra_left/nrf54lm20a/cpuapp -d build/left bootloader
west flash -d build/left
```

Needs Zephyr SDK 1.0.1 (`ZEPHYR_SDK_INSTALL_DIR` if not in a default path).

## Partition layout (2036 KiB RRAM)

| Partition | Offset | Size |
|---|---|---|
| `boot_partition` | `0x000000` | 128 KiB |
| `slot0_partition` | `0x020000` | 1808 KiB |
| `storage_partition` | `0x1e4000` | 100 KiB |

Applications are linked with `zephyr,code-partition = &slot0_partition;`
(the Lily Ultra board does this by default). The FLPR core's default 96 KiB
reservation is not used.

## Entering DFU

Checked in this order on every boot:

1. **Boot-mode retention.** Zephyr's standard boot-mode API (one
   `gpregret1` byte, `zephyr,boot-mode` chosen node). ZMK's `&bootloader`
   behavior with `CONFIG_RETENTION_BOOT_MODE=y` sets it and reboots.
2. **DFU key** (`compatible = "uf2,matrix-key"`). One matrix key held at
   power-up. On Lily Ultra it is the outermost top-row key (row0/col0) of
   each half. The key only counts if the row follows the column, so a
   shorted line cannot trap the board in DFU.
3. **DFU button** (`dfu-button` alias), on the DK.
4. **No valid app**: slot0's vector table fails sanity checks.
5. **Double tap** (`CONFIG_UF2_DOUBLE_TAP`, on by default, off on Lily
   Ultra which has no reset button). Costs the window on every boot.

USB is never started on the normal boot path.

## Update flow and crash safety

* Each UF2 block is checked: magic, family ID, `payloadSize <= 476`,
  `blockNo < numBlocks`, target fully inside slot0, 16-byte RRAM alignment.
  Blocks from other families and non-UF2 sectors (FAT metadata, macOS `._`
  files) are ignored.
* When a new transfer starts, the old app's vector table is wiped first.
  The block that holds the new vector table is kept in RAM and written
  only after every other block has landed, then read back and validated.
  Unplugging mid-copy therefore leaves either the old image intact or no
  bootable image (stays in DFU), never half an image.
* After the commit the bootloader **reboots** rather than jumping from a
  live USB session, so the app always starts on a clean chip.
* Before rebooting it **ejects itself**: it waits for the host to stop
  flushing (200 ms idle), then reports "medium not present". The host
  unmounts on its next poll (about 1 s on macOS), and the bootloader
  reboots 200 ms later, or after 3 s if the host never polls. No "Disk Not
  Ejected Properly" on macOS. This needs the medium lock refused
  (`CONFIG_USBD_MSC_MEDIUM_LOCK=n`, from `bootloader/zephyr/patches/`);
  otherwise macOS stops polling a mounted drive.
* After a failed transfer, dropping the same file again restarts it.

## Speed

Measured on Lily Ultra over high-speed USB from macOS: a 1.64 MB image
(3.28 MB UF2) commits in about 2.0 s, i.e. about 830 KB/s of firmware.
RRAM writes account for about 0.32 s of that, so the rest is USB
mass-storage and host overhead, not flash.

* 16 KiB double-buffered SCSI buffers (`CONFIG_USBD_MSC_SCSI_BUFFER_SIZE`),
  so a high-speed host can stream multi-sector writes.
* Full 32-word RRAMC write buffer (`CONFIG_NRF_RRAM_WRITE_BUFFER_SIZE=32`)
  instead of committing each 128-bit word.

## VREGUSB across soft resets

The nRF54L USB regulator (VREGUSB) survives a soft reset. If the previous
firmware left it running, Zephyr's driver never sees a new "VBUS detected"
event and USB never enumerates. `src/vregusb_reset.c` stops it at
PRE_KERNEL_1 on every boot. Any application that enables USB itself and
is entered by a soft reset (ZMK after `&bootloader`, or after a UF2
update) gets a stopped regulator from the bootloader.

## Status and progress

`src/uf2_status.h` is a small status bus: state (waiting, connected,
writing, done, error), entry reason, error code and block/byte progress.
Backends register with `UF2_STATUS_LISTENER_DEFINE()`. The PWM LED
(`pwm-led0` alias) is one listener; a display is meant to be another.

## Test app

`samples/bootmode_roundtrip` boots from slot0, prints over RTT, then sets
boot mode BOOTLOADER and reboots, as ZMK's `&bootloader` does. It writes
`zephyr.uf2` next to `zephyr.hex`:

```sh
west build -b lily_ultra_left/nrf54lm20a/cpuapp -d build/rt samples/bootmode_roundtrip
cp build/rt/zephyr/zephyr.uf2 /Volumes/LILYULTRA/
```

## Tests

Host tests for the UF2 writer state machine (normal, out-of-order,
interrupted, malformed, out-of-range, flash-error-then-retry):

```sh
make -C tests/host
```

## USB VID/PID and family ID

Nordic's VID (`0x1915`) with a placeholder PID, and family ID
`0x4ED75C3F`, as upstream. Replace the VID/PID before shipping
(`UF2_USBD_VID` / `UF2_USBD_PID`).

## Differences from upstream

* Lily Ultra left/right boards; 128 KiB bootloader partition.
* Boot-mode retention instead of a magic word in no-init RAM.
* Matrix-key entry; double tap optional.
* Per-block validation, deferred vector-table commit, reboot instead of
  chain-loading out of a live USB session.
* Stricter vector-table check (SP inside SRAM, reset handler inside slot0)
  and a fuller CPU reset before jumping (NVIC pending, stack limits,
  CONTROL, caches).
* Fixed CURRENT.UF2 being truncated: the bound compared against the slot
  size instead of slot end.
* Status bus replacing the LED-only indicator.
* Larger MSC buffers and RRAMC write buffer.

## Credits

The UF2 / ghost-FAT engine (`src/ghostfat.c`, `src/uf2.h`, `src/compile_date.h`)
is ported from [tinyuf2](https://github.com/adafruit/tinyuf2) — itself descended
from Microsoft's [uf2-samdx1](https://github.com/microsoft/uf2-samdx1) and
Adafruit's work, with the on-the-fly FAT16 synthesis by Henry Gabryjelski..

## License

MIT. See [LICENSE](LICENSE).
