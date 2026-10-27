# UniXNU-instantnoodlep

UniXNU device repository for the OnePlus 8 Pro — codename `instantnoodlep`,
models IN2020, IN2021, IN2023 and IN2025 — on Qualcomm SM8250 (kona).

This repository owns the board device tree, peripheral wiring, component
selection, firmware inputs, partition geometry and packaging.

| Repository | Contents |
| --- | --- |
| [UniXNU-instantnoodlep-10-27](https://github.com/zhangp996/UniXNU-instantnoodlep-10-27) | this board: DTS, defconfig, record, firmware inputs |
| [UniXNU-sm8250-common](https://github.com/zhangp996/UniXNU-sm8250-common) | shared kona support: topology, GICv3, UFS, GPU, QUPv3 |
| [UniXNU](https://github.com/UniXNU/UniXNU) | build tools, OS profiles, component catalog |

## State

| Layer | State |
| --- | --- |
| Configuration | resolves: arch + SM8250 + iPadOS 17.7.11 + device profiles, five components, ordered patch series |
| Hardware inventory | collection plan in [docs/hardware-inventory.md](docs/hardware-inventory.md); values not yet measured |
| UEFI firmware | `Platforms/OnePlus/instantnoodlepPkg` does not exist yet |
| Kernel, payload, boot image | not built; the toolchain needs Linux or macOS |

`board.example = true` until the `REQUIRED` values are measured. `build` and
`bootimg` refuse an example repository on purpose.

## Build pipeline

Two lanes, because the official flow needs both hosts: configuration, tree and the
UEFI firmware build run locally in WSL, and the kernel (which calls Xcode) runs on
a GitHub `macos-26` runner through `.github/workflows/macos-kernel.yml`. See
[docs/build-pipeline.md](docs/build-pipeline.md) for the commands and the current
gates.

## Requirements

The bootloader must be unlocked: this unit reports `unlocked:no`, `secure:yes` and
`Verity mode: true`, and a locked bootloader refuses to boot an unsigned image.
Measured partition sizes: `boot_a`/`boot_b` 96 MiB, `super` 14 GiB, `userdata`
100.63 GiB (see `docs/evidence/device-fastboot-getvar-all.log`).

Follow [Port a device](https://unixnu.com/porting/) in order. The host needs the
[build requirements](https://unixnu.com/guide/requirements); this port was
prepared on Windows, where only the configuration steps run.

## Commands

Run from the UniXNU checkout:

```sh
./unixnu defconfig instantnoodlep_defconfig -D ../UniXNU-instantnoodlep
./unixnu status -D ../UniXNU-instantnoodlep
./unixnu tree   -D ../UniXNU-instantnoodlep
python3 scripts/porting-record.py check --device ../UniXNU-instantnoodlep
```

After the hardware inputs are filled and the firmware package exists:

```sh
./unixnu build kernel
./unixnu build package
./unixnu build firmware --uefi ../uefi-instantnoodlep
./unixnu build payload --unique-chip-id <hex> --trust-cache <bundle> --data-ramdisk <seed>
./unixnu bootimg --uefi ../uefi-instantnoodlep --dtb <firmware.dtb>
fastboot boot out/instantnoodlep/<profile>/boot/boot.img
```

`fastboot boot` is temporary: it does not write a partition.

## Inputs still missing

| Input | Consumed by |
| --- | --- |
| GPT start LBA of `userdata`, LUN block size | `[userdata]` in `board.toml` |
| DRAM size and reserved ranges | `dts/instantnoodlep.dts`, `[payload].dram_size` |
| `cont_splash_region` address and size | `dts/instantnoodlep.dts`, `[bootimg].dtb_node` |
| Panel timing, backlight, touch, WLAN, GPU facts | display, input, network and GPU areas of `porting.toml` |

## Porting record

`porting.toml` is the authoritative record; the documentation site renders it.
Read the [record guide](https://unixnu.com/porting/records) before changing an
area, and update the affected area's findings or evidence with every change.

- [Device repository](https://unixnu.com/porting/device-repository)
- [Device tree](https://unixnu.com/porting/device-tree)
- [UEFI firmware](https://unixnu.com/porting/uefi-firmware)
- [Component catalog](https://unixnu.com/reference/components)
