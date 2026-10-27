# Port status

Last updated: 2026-10-28. Everything below was measured on the target unit
(IN2020, serial `fa8620b3`) unless stated otherwise.

## Where the port stands

| Layer | State |
| --- | --- |
| Repository configuration | resolves; `unixnu defconfig` / `status` / `tree` run offline against local sources |
| Hardware inventory | complete for SoC, CPU, memory, reserved memory, storage geometry, panel, touch, WLAN and GPU |
| Device tree | board DTS compiles with dtc and carries the measured memory span and splash region |
| UEFI platform package | **not written**: no vendor DXE driver set exists for this board |
| Kernel, payload, boot image | not built |

## What was measured (evidence files)

| Input | Value | Evidence |
| --- | --- | --- |
| DRAM | 0x80000000+0x3bb00000, 0xc0000000+0xc0000000, 0x180000000+0x100000000 | `docs/evidence/device-fdt.dts` |
| Splash | `cont_splash_region` 0x9c000000 + 0x2300000 | `docs/evidence/device-fdt.dts` |
| userdata | LBA 4133800..30513146, 4096-byte sectors, 100.63 GiB | `docs/evidence/device-gpt.json` |
| UFS | host 0x1d84000/0x3000 + ICE 0x1d90000/0x8000, PHY 0x1d87000, SPI 265 | `docs/evidence/device-fdt.dts` |
| Panel | `qcom,mdss_dsi_oppo19811_samsung_1440_3168_dsc_cmd`, 1440x3168 | bootargs + `docs/evidence/device-fdt.dts` |
| Touch | Synaptics S3908 at I2C 0x4B on `i2c@a94000`, irq-gpio TLMM 39, reset-gpio TLMM 38, 1080x2400, 16 TX / 36 RX, 10 contacts | `docs/evidence/device-fdt.dts` + `in2020-permissive-delta.txt` |
| WLAN | `qcom,cnss-qca6390` at 0xb0000000, `wlan-en` TLMM GPIO 20 | `docs/evidence/device-fdt.dts` |
| GPU | `kgsl-3d0` 0x3d00000, `kgsl-smmu` 0x3da0000 | `docs/evidence/device-fdt.dts` |
| Bootloader | unlocked (`verifiedbootstate=orange`), verity enforcing | `docs/evidence/device-fastboot-inventory.log` |

Raw captures, first pass with SELinux enforcing:

| File | Lines | Content |
| --- | --- | --- |
| `docs/evidence/in2020-raw.txt` | 2494 | shell inventory |
| `docs/evidence/in2020-root-raw.txt` | 4219 | root inventory |

Second pass after `setenforce 0`, which is what makes `/proc/device-tree` and
`/proc/bus/input/devices` readable as `shell` on this build:

| File | Lines | Content |
| --- | --- | --- |
| `docs/evidence/in2020-raw-permissive.txt` | 4537 | shell inventory |
| `docs/evidence/in2020-root-raw-permissive.txt` | 4217 | root inventory |
| `docs/evidence/in2020-permissive-delta.txt` | 909 | inputs, I2C/SPI bus nodes, display, power |

Binary captures: `docs/evidence/device-fdt.bin` (live FDT, 900355 bytes),
`docs/evidence/device-gpt.bin` + `device-gpt.json`, and `docs/evidence/dtbo/`
(19 overlays from `dtbo_a`, split with `scripts/extract-dtbo.py`; the raw `.dtb`
files are not committed because the partition dump regenerates them).

## Device state at archive time

| Item | Value |
| --- | --- |
| Active slot | `_a`, stock boot image |
| Bootloader | unlocked (`verifiedbootstate=orange`) |
| SELinux | `Permissive` (session only; a reboot restores `Enforcing`) |
| Root | only while running `fastboot boot boot_patched.img` with the Magisk app in the foreground; `su` is denied when the app is not open |
| Slot B | `boot`, `dtbo`, `vbmeta`, `vbmeta_system`, `modem`, `recovery`, `multiimgqti` written with slot A's bytes; the other 22 critical partitions were refused by the bootloader and were never modified |

## The blocker for the firmware

A Mu-Silicium platform package needs the device's DXE driver set, which upstream
ships only as `Binaries/<device>/QcomPkg/**` prebuilt modules. `Device-Binaries` has
no `instantnoodlep`, and this board's `abl_a` dump carries no extractable PE images
(OnePlus signs and compresses it), so a firmware built from generic sources alone
cannot bring up display, touch or UFS on this unit.

Measured address facts that rule out reusing another OnePlus SM8250 package:

| Item | Value on this board |
| --- | --- |
| Free window for a firmware volume | 0x9E400000 .. 0x9FE00000 (26 MiB) |
| `kboot_uboot_logmem` | 0x9FE00000 + 0x200000 |

A package whose FD sits at 0x9FC00000 with size 0x300000 overlaps
`kboot_uboot_logmem` by 1 MiB, and a stack base of 0x9FF90000 lands inside it.

## Next steps, in order

1. Decide the driver source: obtain a `Binaries/instantnoodlep` set, or extract the
   vendor DXE modules from a decompressed OnePlus update package.
2. Write `Platforms/OnePlus/instantnoodlepPkg` (dsc, fdf, `ConfigurationMapLib`,
   `MemoryMapLib` from the measured map, FD at 0x9E400000/0x1000000, stack 0x9F800000).
3. `unixnu build firmware --uefi ../sources/Mu-Silicium`, then `payload`, then `bootimg`.

## Local layout used for this port

| Path | Content |
| --- | --- |
| `../sources/` | pinned source trees (xnu, edk2, Mu-Silicium, corecrypto-2023, ...) |
| `../ab-sync/ab-sync/` | slot A partition dumps (29 images, 1.15 GiB) |
| `../unixnu/unixnu.local.toml` | `[build] uefi` and device paths |
| `local.toml` | local source overrides (ignored by Git) |

`board.example` stays `true` until the firmware exists: `build` and `bootimg` refuse
an example repository, which is the guard against producing an image from an
unverified configuration.
