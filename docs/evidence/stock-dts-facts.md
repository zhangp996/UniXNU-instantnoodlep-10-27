# IN2020 measured facts

Sources, in order of authority: the phone's own stock device tree (`fdt.dtb`
dumped from slot b, decompiled with dtc), the bootloader's `getvar all`, the
kernel command line and `/proc/iomem` captured on the device, and ADB
observations. Addresses are hex; sizes are bytes unless a unit is given.

## Identity

| Field | Value | Source |
| --- | --- | --- |
| Model | `IN2020` (OnePlus 8 Pro, China) | `ro.product.model` |
| Device | `OnePlus8Pro`, platform `kona` | `ro.product.device`, `ro.board.platform` |
| SoC | `SM8250`, `ro.soc.model` | `getprop` |
| Project | `19811` (`oplus,dtsi_no = 0x4d63`) | `ro.boot.prjname`, stock DT |
| Build | `IN2020_13.1.0.185(CN01)`, Android 13, patch 2023-12-05 | `getprop` |
| Bootloader | unlocked, `ro.boot.verifiedbootstate = orange` | `getprop`, `fastboot oem device-info` |
| Slot | `_b` current, 2 slots | `ro.boot.slot_suffix` |
| hw-revision | `20001` | `getvar` |
| Serial | `fa8620b3` | `getvar` |
| Build variant | `userdebug` in the kernel command line, but `ro.debuggable=0` and `adb root` refuses (`adbd cannot run as root in production builds`) | `getprop`, `adb root` |

## Memory

Stock DT memory node, three banks:

| Bank | Base | Size |
| --- | --- | --- |
| 1 | 0x80000000 | 0x3bb00000 |
| 2 | 0xc0000000 | 0xc0000000 |
| 3 | 0x180000000 | 0x100000000 |

`/proc/meminfo` reports `MemTotal: 7708100 kB` (7.35 GiB) on an 8 GiB board.

`/proc/iomem` shows RAM is not contiguous: the largest observed System RAM
windows are `0xa0000000-0xbbafffff`, `0xc0000000-0xffbfffff` and
`0xffc20000-0x27fffffff`. The ADT generator requires exactly one contiguous DRAM
range (`scripts/unixnu/devicetree.py:303`), so the board DTS hands off
`0x80000000 + 0x200000000` and `payload.dram_size` must equal that size.

## Reserved memory (stock DT, selected)

| Region | Base | Size |
| --- | --- | --- |
| hyp_region | 0x80000000 | 0x600000 |
| xbl_aop_region | 0x80700000 | 0x160000 |
| cmd-db | 0x80860000 | 0x20000 |
| smem_region | 0x80900000 | 0x200000 |
| removed_region | 0x80b00000 | 0xcd00000 |
| pil_camera_region | 0x8dc00000 | 0x500000 |
| pil_wlan_fw_region | 0x8e100000 | 0x100000 |
| pil_gpu_region | 0x8e21a000 | 0x2000 |
| pil_npu_region | 0x8e300000 | 0x500000 |
| pil_video_region | 0x8e800000 | 0x500000 |
| pil_cvp_region | 0x8ed00000 | 0x500000 |
| pil_cdsp_region | 0x8f200000 | 0x1400000 |
| pil_slpi_region | 0x90600000 | 0x1500000 |
| pil_adsp_region | 0x91b00000 | 0x2500000 |
| pil_spss_region | 0x94000000 | 0x100000 |
| cdsp_secure_heap | 0x94100000 | 0x4600000 |
| **cont_splash_region** | **0x9c000000** | **0x2300000** |
| disp_rdump_region | 0x9c000000 | 0x800000 |
| dfps_data_region | 0x9e300000 | 0x100000 |
| cnss_wlan_region | 0xb0000000 | 0x400000 |

## Interrupts, CPUs, timers

| Item | Value |
| --- | --- |
| GICv3 distributor | 0x17a00000, 0x10000 |
| GICv3 redistributors | 0x17a60000, 0x100000, stride 0x20000 |
| CPUs | 8 Kryo cores, `reg` 0x0, 0x100 ... 0x700, `enable-method = psci` |
| PSCI | `cpu-release-addr = <0 0x90000000>` |
| Timer | 19.2 MHz architectural (SM8250 platform) |

## Storage

| Item | Value |
| --- | --- |
| UFS host | `ufshc@1d84000`, reg 0x1d84000/0x3000 (`ufs_mem`) and 0x1d90000/0x8000 (`ufs_ice`) |
| UFS interrupt | `<0 0x109 4>` = SPI 265 |
| UFS PHY | `ufsphy_mem@1d87000`, `phy_mem` 0x1d87000/0xdff |
| Boot device | `androidboot.bootdevice=1d84000.ufshc` |
| LUN table | `sda` 122052608 KiB (userdata LUN), `sdb`/`sdc` 8 MiB, `sdd` 128 MiB, `sde` 2.5 GiB, `sdf` 128 MiB |
| userdata | `sda23`, 105517388 KiB = 0x1928453000 bytes = 26379347 x 4096-byte sectors |
| boot_a / boot_b | 96 MiB each (0x6000000) |
| dtbo_a / dtbo_b | 24 MiB each (0x1800000) |
| super | 14 GiB (0x380000000) |
| Logical block size | 4096 (`logical-block-size: 0x1000`) |
| **userdata first LBA** | **not measured**: reading the GPT needs root, which this build refuses |

## Display

| Item | Value |
| --- | --- |
| Panel selected by the bootloader | `qcom,mdss_dsi_oppo19811_samsung_1440_3168_dsc_cmd` |
| Panel | Samsung AMOLED, `oplus,mdss-dsi-vendor-name = "ANA6706"`, manufacture `samsung1024` |
| Physical resolution | 1440 x 3168, DSC command mode |
| Reported by Android | `wm size` 1080x2376, `wm density` 480 (runtime FHD+ resolution) |
| Backlight | `/sys/class/backlight/panel0-backlight`, `panel1-backlight` |
| Framebuffer handoff | `cont_splash_region` 0x9c000000, 0x2300000 |

## Input

| Item | Value |
| --- | --- |
| Touch | `synaptics_20828@4B`, `compatible = "synaptics-s3908"`, on a QUPv3 GENI SPI engine |
| Touch pinctrl | `pmx_ts_int_active_oplus`, `pmx_ts_reset_active_oplus` and their suspend variants |
| Fingerprint | `goodix_fp`, `oplus_fp_common` |
| Buttons | volume and power through the PMIC key driver |
| `/proc/bus/input/devices` | **not readable as shell** (SELinux) |

## Network

| Item | Value |
| --- | --- |
| WLAN | `qcom,cnss-qca6390@b0000000`, `wlan-en-gpio = <&tlmm 20 0>` |
| WLAN memory | `cnss_wlan_region` 0xb0000000, 0x400000; `pil_wlan_fw_region` 0x8e100000, 0x100000 |
| PCIe | `pci-domain0 = /soc/qcom,pcie@1c00000` |

## GPU

| Item | Value |
| --- | --- |
| GPU | `kgsl-3d0` at 0x3d00000, 0x40000 |
| GPU SMMU | `kgsl-smmu@3da0000` |
| GPU carveouts | `pil_gpu_region` 0x8e21a000, 0x2000; `kgsl-3d0` maps 0x3d00000-0x3d3ffff |

## What still needs root or a rooted image

| Input | Why |
| --- | --- |
| userdata first LBA | the GPT lives in block 1-33 of `/dev/block/sda` |
| Full `/proc/device-tree` property values | SELinux denies `/proc/device-tree/soc` and property files |
| `/proc/bus/input/devices` | SELinux denies it to shell |

`adb root` is refused (`adbd cannot run as root in production builds`) even though
the kernel command line says `buildvariant=userdebug` and `ro.debuggable=0`.
Booting a rooted image, or reading the GPT from a `fastboot`-accessible path, is
the next step for those three rows.
