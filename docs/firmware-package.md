# Firmware package: Platforms/OnePlus/instantnoodlepPkg

The UEFI firmware comes from Mu-Silicium. Upstream ships OnePlus packages for
kebab (8T), lemonade (9) and others on SM8250, but none for instantnoodlep, so
this port adds one. The requirements are in
[UEFI firmware](https://unixnu.com/porting/uefi-firmware).

## Files to create

| Path | Content |
| --- | --- |
| `instantnoodlep.dsc` | Platform defines, PCDs, library classes, components |
| `instantnoodlep.fdf` | FD/FV layout and the INF list |
| `DeviceBuild.py` | EDK II build entry: supported packages, scopes, packages path |
| `Include/APRIORI.inc`, `Include/DXE.inc`, `Include/RAW.inc` | DXE driver order for the platform |
| `Library/ConfigurationMapLib/` | Board configuration consumed by the platform PEI/DXE code |
| `Library/MemoryMapLib/` | Memory map handed to DXE |
| `Resources/Configs/instantnoodlep.toml` | `uefi_fd` base/size and boot image parameters |

## Required UniXNU integration

`docs/porting/uefi-firmware.md` requires these modules in the DSC `[Components]`
and as `INF` lines in the FDF; without them the payload cannot be discovered and
the menu never lists an entry:

```
UniXNUPkg/Application/UniXNU/UniXNU.inf
UniXNUPkg/Driver/UniXNUDxe/UniXNUDxe.inf
UniXNUPkg/Driver/UniXNURamDiskDxe/UniXNURamDiskDxe.inf
DarwinBootPkg/Driver/DarwinBoot.inf
MdeModulePkg/Universal/Disk/DiskIoDxe/DiskIoDxe.inf
FatPkg/EnhancedFatDxe/Fat.inf
```

and the shared libraries in `[LibraryClasses]`:

```ini
HobLib|MdePkg/Library/DxeHobLib/DxeHobLib.inf
FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf
```

`PartitionDxe` is only needed for a payload with a partition table. The FAT driver
also needs the platform's Unicode collation service.

## Board values, and which are measured

| Value | Where | State |
| --- | --- | --- |
| SMBIOS manufacturer, model, retail name | `instantnoodlep.dsc` | OnePlus / 8 Pro / instantnoodlep |
| Framebuffer width x height | `instantnoodlep.dsc` | 1440 x 3168 from the panel |
| `PcdSystemMemoryBase` | `instantnoodlep.dsc` | 0x80000000 (SM8250) |
| CPU count, cluster count, GIC bases | `KonaPkg/KonaPkg.dsc.inc` | 8, 3, 0x17a00000, 0x17a60000 |
| `[uefi_fd] base` and `size` | `Resources/Configs/instantnoodlep.toml` | **REQUIRED**: the range the ABL leaves free |
| `[boot_image] header_version` | `Resources/Configs/instantnoodlep.toml` | **REQUIRED**: the ABL's boot image header version |
| Memory map | `Library/MemoryMapLib/MemoryMapLib.c` | **REQUIRED**: the ABL's own map for this board |
| DXE driver list | `Include/*.inc` | starts from the SM8250 OnePlus package, then measured |

The firmware volume base and size are compiled into BootShim and checked again
when the boot image is packed, so a wrong pair produces an image the ABL rejects.

## Procedure once the Mu-Silicium tree is present

1. Copy the SM8250 OnePlus package as the structural starting point and rename
   every `kebab` identifier to `instantnoodlep`.
2. Fill the measured values in the table above; keep **REQUIRED** markers until
   each one comes from this board.
3. Add the six UniXNU modules and the two library classes.
4. `./unixnu build firmware --uefi ../sources/Mu-Silicium` from the UniXNU checkout
   with `unixnu.local.toml` pointing at the tree.
5. Confirm the FD path from `[firmware].fd` and its size against `uefi_fd.size`.

## Why the memory map is not copied

The ABL reserves different ranges per model: hyp, ADSP, MPSS, TZ, splash, and the
region it leaves for the OS. A map taken from another OnePlus board can have the
firmware write over a secure carveout before any kernel code runs, which presents
as an unexplained hang or reset rather than a build error.
