# Checkpoint 2026-10-28

State of the OnePlus 8 Pro (IN2020, SM8250/kona) port of UniXNU at the end of this
working session. Everything below is either on GitHub or in this archive.

## What boots on the device

| Stage | State | Evidence |
| --- | --- | --- |
| Boot image accepted by ABL | yes | `fastboot boot boot-hdr.img` returns OKAY |
| UEFI firmware (own platform package) | yes | BDS menu, 1080x2376 text, driver volume dispatched |
| UniXNU menu | yes | config `\EFI\UniXNU\unixnu.toml`, payload ramdisk read |
| DarwinBoot hand-off | yes | green bar plus the loader report on the panel |
| Kernel entry and MMU enable | yes | blue and magenta probe bars |
| IOKit and BSD initialisation | yes | panic changes from the AMFI refusal to `Failed to mount root device` |
| Root device | no | no block driver publishes OS/SystemOS yet |

## The two blockers that were solved

- **AMFI: No PMGR?** — `scripts/patch-amfi-nopmgr.py` rewrites the single call that ends
  in that panic (`0x4AAED04`: `bl` -> `b` to the caller's own epilogue). The kernel now
  walks past AMFI into BSD.
- **The six-second reset** — the APSS watchdog at `0x17C10008` is now cleared in `SecEntry`,
  so a fault leaves the panel readable instead of rebooting.

## Where the storage work stands

The firmware proved the transport, the driver has not yet been linked:

| Fact | Value |
| --- | --- |
| host | `0x1d84000` + `0x3000` (ufs_mem), ICE `0x1d90000` + `0x8000`, SPI 265 |
| PHY | `0x1d87000` + `0xe00`, ref_aux_clk GCC `0xad` |
| clocks | core `0xa7`, iface `0xa6`, unipro `0xb3`, ice `0xaa`, bus_aggr `0x06` |
| reset | `core_reset` GCC `0x21`, BCR `0x77000` |
| run/stop register | `0x60` (Qualcomm DWC layout, not the UFSHCI `0x5c`) |
| SMMU | none: the `ufshc` node has no `iommus` |
| keys | volume up/down are PM8150 SPMI GPIO 6/7, so input needs an SPMI driver |

`UfsProbeDxe` starts the request list (`UTRLRSR 0x1`) and issues NOP and READ CAPACITY
with `ocs 0`, but the response UPIU and the data phase never land. Six builds bracket
that in `docs/bring-up.md`. The decision recorded there is to run the reference code as
a kernel driver (`components/kona-ufs`, runtime class `NativeUfs`) instead of
reimplementing the transport in the firmware.

## Repositories

| Repository | Contents |
| --- | --- |
| `zhangp996/UniXNU-instantnoodlep-10-27` (this one) | board DTS, ports record, firmware package, patches, docs |
| `zhangp996/UniXNU` branch `instantnoodlep` | fork of UniXNU with the board component entry, the kona device-tree exports, the non-sheng patch adaptation and the two build-script changes |
| `zhangp996/UniXNU-sm8250-common` | shared kona device tree and SoC profile |

## Build state at the checkpoint

The macOS kernel lane builds from the fork and stops at three compile errors in
`drivers/KonaUfs/NativeUfs.cpp` (line 2288 is one of them: the `?:` over `kIOReturn*`
macros). The diagnostics step prints the component log, so the next run names all three.
