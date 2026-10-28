# UEFI platform package

The Mu-Silicium platform package for this board lives here so it is versioned with the
device configuration, and is copied into a Mu-Silicium checkout to build:

```sh
cp -R uefi/Platforms/OnePlus/instantnoodlepPkg  $UEFI/Platforms/OnePlus/
cp    uefi/Resources/Configs/instantnoodlep.toml $UEFI/Resources/Configs/
./unixnu build firmware --uefi $UEFI
```

| File | Purpose |
| --- | --- |
| `instantnoodlep.dsc` | Platform defines, board PCDs, library classes, components |
| `instantnoodlep.fdf` | FD at 0x9E400000 (4 MiB), one volume, all INF entries, packaging rules |
| `Library/MemoryMapLib` | Memory map measured from this board's live device tree |
| `Library/ConfigurationMapLib` | Board configuration entries |
| `Resources/Configs/instantnoodlep.toml` | `uefi_fd` base/size and boot image parameters |

Deliberate limits at this stage:

- No vendor DXE driver is selected, because upstream publishes those only per device
  (`Binaries/<device>/QcomPkg/**`) and there is no `instantnoodlep` set. Display comes
  from `SimpleFbDxe` over the framebuffer the bootloader leaves at 0x9C000000, so
  there is no touch input and no UEFI-side UFS driver yet.
- The firmware volume sits in the window between `dfps_data_region` (ends 0x9E400000)
  and `kboot_uboot_logmem` (starts 0x9FE00000). A package built for another OnePlus
  SM8250 board overlaps that log buffer, which is one reason such a package cannot
  boot here.
