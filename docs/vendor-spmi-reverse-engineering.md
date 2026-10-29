# Vendor SPMI/PMIC drivers: what the binaries expose

Source: `Project-Silicium/Device-Binaries` at `kebab/QcomPkg/Drivers` (kebab is SM8250, the same SoC
as this board, so the SoC-level drivers are the same). Files inspected: `SPMIDxe.efi` (45 KiB),
`PmicDxeLa.efi` (196 KiB), `ButtonsDxe.efi` / `ButtonsDxe.patched.efi`.

## SPMIDxe protocol

`SPMIDxe.efi` is a PE32+ with `.text` at 0x1000/0x7000 and `.data` at 0x8000/0x2000. Its `.data`
begins with the GUIDs it uses:

| Offset | GUID | Meaning |
| --- | --- | --- |
| 0x8008 | `B898D8DC-080A-40F7-99E3-31627B806A5A` | protocol SPMIDxe installs |
| 0x8018 | `2A7B4BEF-80CD-49E1-B473-374BA4D673FC` | the module's own FILE_GUID (matches its INF) |
| 0x8028 | `BEDAEABC-5E70-4D66-9733-213D072B9D04` | Qualcomm SPMI protocol it consumes |
| 0x8038 | `7739F24C-93D7-11D4-9A3A-0090273FC14D` | PMIC protocol |
| 0x8048 | `FA5F306B-F47D-4AC4-A47D-882F8204EC30` | dependency |
| 0x8058 | `D2B2B828-0826-48A7-B3DF-983C006024F0` | dependency |
| 0x8068 | `B5062BE7-170B-4A32-BE21-689262FF4399` | dependency |
| 0x8078 | `5B1B31A1-9562-11D2-8E3F-00A0C969723B` | EFI_LOADED_IMAGE_PROTOCOL |

The interface struct follows at 0x8088: revision `0x00010001`, then five function pointers
(`0x13dc`, `0x1494`, `0x152c`, `0x15cc`, `0x164c`). The first one was disassembled far enough to
recover its shape:

```c
u32 SpmiTransaction (void *this, u32 a, u32 b, u32 c, void *buffer, u32 length, u32 *out);
// returns 0xC (EFI_INVALID_PARAMETER) when `this` is NULL, and stores a local value through `out`
```

## ButtonsDxe

The upstream binary set already ships `ButtonsDxe.patched.efi`, and its INF points at that file, so
the OplusProject NULL-call workaround does not need to be reproduced here.

## Why they are not in the image

Both `PmicDxeLa.efi` and, on its own, `SPMIDxe.efi` abort on this firmware: a data abort with
`FAR` 0x20 / 0x28 and a PC inside the firmware volume, which is the shape of a NULL function-
pointer call into a protocol the vendor platform normally provides (SMEM, clock, chip info). They
are built for the vendor's full DXE set, not for a minimal Mu platform.

The binaries are kept under `uefi/Platforms/OnePlus/instantnoodlepPkg/Binaries/` with their INFs,
and both are commented out of the DSC and FDF with the reason recorded next to them.
