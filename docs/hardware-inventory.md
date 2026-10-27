# Hardware inventory for IN2020

Every value this board still needs, where it lands, and the command that reads
it. `scripts/collect-in2020.sh` runs the whole list in one pass and only reads
the device: no partition is written, no property is set.

Connect the phone, enable **Developer options -> USB debugging**, authorise the
host, then:

```sh
adb push scripts/collect-in2020.sh /data/local/tmp/
adb exec-out sh /data/local/tmp/collect-in2020.sh > docs/evidence/in2020-raw.txt
```

Sections marked **(root)** need `su`; run `sh /data/local/tmp/collect-in2020.sh --root`
on a rooted device. Everything else works on the stock shell user.

| # | Input | Lands in | Source on the device |
| --- | --- | --- | --- |
| 1 | Board identity and firmware revision | `porting.toml` firmware area | `getprop ro.product.device`, `ro.build.display.id`, `ro.build.fingerprint`, `ro.boot.prj_version` |
| 2 | Unlock and verified-boot state | bring-up prerequisites | `getprop ro.boot.verifiedbootstate`, `ro.boot.flashing` |
| 3 | RAM size and banks | `dts` `memory@80000000`, `[payload].dram_size` | `/proc/meminfo`, `dmesg`, `/proc/device-tree/memory@*` **(root)** |
| 4 | `cont_splash_region` address and size | `dts` `reserved-memory`, `[bootimg].dtb_node` | `/proc/device-tree/reserved-memory/cont_splash_region/reg` **(root)** |
| 5 | All other reserved ranges | `porting.toml` memory area | `/proc/iomem`, reserved-memory children **(root)** |
| 6 | CPU topology | `dts` CPU nodes, `porting.toml` CPU area | `/sys/devices/system/cpu/present`, `cpu*/topology/*`, `/proc/cpuinfo` |
| 7 | Per-cluster frequencies | `porting.toml` CPU area | `cpufreq/policy*/related_cpus`, `cpuinfo_max_freq` |
| 8 | LUN and partition table | `[userdata]`, `[payload].size_mib` | `/proc/partitions`, `/sys/block/sd*/sd*/{start,size}`, `/dev/block/by-name` |
| 9 | UFS link and host state | `porting.toml` storage area | `/sys/devices/platform/soc/*.ufshc/*`, `/sys/class/scsi_host/*` |
| 10 | Panel resolution and refresh | `porting.toml` display area | `wm size`, `wm density`, `dumpsys SurfaceFlinger` |
| 11 | Panel timing and backlight | `porting.toml` display area | `/sys/class/backlight/*`, `dmesg | grep -i dsi` |
| 12 | Touch controller and bus | `porting.toml` input area | `/proc/bus/input/devices`, `getevent -pl`, `/sys/bus/{i2c,spi}/devices` |
| 13 | WLAN device and firmware | `porting.toml` network area | `/sys/class/net`, `getprop | grep -i wlan`, `dmesg | grep -i wcn` |
| 14 | GPU speed bin and firmware | `porting.toml` GPU area | `dmesg | grep -i adreno`, `/vendor/firmware_mnt/image` |
| 15 | PMIC and battery | `porting.toml` power area | `/sys/class/power_supply/*`, `dmesg | grep -i pm8150` |
| 16 | Boot partition size | `[payload].size_mib` | row 8, the `boot_a` row |

## Reading the result

- `reg` properties print as raw little-endian hex. A two-cell address and size
  pair is `<hi32><lo32><hi32><lo32>`, so `00000000 9c000000 00000000 02300000`
  is address `0x9c000000` with size `0x2300000`.
- `start` and `size` in `/sys/block` are in 512-byte sectors even when the LUN
  reports 4096-byte logical blocks; `[userdata].lba_bytes` takes the LUN's
  logical block size, not the sysfs unit.
- The last LBA of a partition is `start + size - 1`.
- `ro.boot.verifiedbootstate` must be `orange` or `yellow`; with `green` the
  bootloader refuses an unsigned image before UniXNU runs.

## After collection

1. Replace the `REQUIRED` values in `board.toml` and the zeroed `reg` values in
   `dts/instantnoodlep.dts`.
2. Update the configuration, memory and storage areas of `porting.toml` with
   the measured values and the command that produced them.
3. Delete the raw capture or commit it under `docs/evidence/` as the evidence
   file the record cites.
4. Set `board.example = false` only when nothing is left pending.
