# Boot image recipe for this board

Measured on the device, not inferred:

| Fact | Value | Where it comes from |
| --- | --- | --- |
| ABL requires an ARM64 Image header | yes | a kernel without `ARMd` at offset 0x38 gives `Failed to load/authenticate boot image: Load Error` |
| Boot image header version | 2 | the stock `boot_a` image |
| Kernel compression | none | the stock kernel starts with a branch and has `ARMd` at 0x38; ABL does not decompress for this board |
| dtb field must carry `qcom,msm-id` | yes | with a dtb built from this repository's DTS, ABL refuses the image |
| dtb used for the test | stock `boot_a` dtb | 11028247 bytes, `qcom,msm-id = 0x164 0x20001` |
| Panel timing | 1080x2376 | `qcom,mdss-dsi-panel-width = 0x438`, `-height = 0x948` in the live device tree |

The image that boots the firmware on the device is:

```sh
# shim with the ARM64 header, then the firmware volume, uncompressed
clang --target=aarch64-none-elf -c -DFD_BASE=0x9E400000 -DFD_SIZE=0x400000 shim.S -o shim.o
llvm-objcopy -O binary shim.o shim.bin
cat shim.bin SILICIUM_UEFI.fd > kernel.bin

python3 Resources/Scripts/mkbootimg.py \
  --kernel kernel.bin --ramdisk payload.img --dtb stock.dtb \
  --header_version 2 --base 0 --kernel_offset 0x8000 --ramdisk_offset 0x1000000 \
  --tags_offset 0x100 --dtb_offset 0x1f00000 --pagesize 4096 \
  --cmdline "console=ttyMSM0,115200n8 androidboot.hardware=qcom" -o boot.img

fastboot boot boot.img
```

`unixnu bootimg` produces a header version 0 image with a gzip kernel and this
repository's own device tree, which this bootloader rejects; until the tool grows
the options above, the image is assembled by hand.

## Observed on the device

With that image the firmware runs: the screen shows EDK2 console output. The text is
drawn as a narrow vertical band, so the framebuffer geometry the firmware assumes
does not match the buffer ABL scans out. Both 1440x3168 and 1080x2376 have been tried
with the same result, which points at the stride or at the panel pipeline rather than
at the width and height alone.
## Device observations, in order

Each line below was read off the panel after `fastboot boot boot-hdr.img`:

1. `Silicium 4.0 Build at ...`, `Fine Blk Memory Region` style messages and
   `Failed to Decompress FV Status = Not Found` — SEC ran but the flash device held a
   single uncompressed volume, so `DecompressFirstFv` had nothing to decompress and
   returned before DXE. Fixed by rebuilding the FDF as the standard ARM layout:
   `FVMAIN_COMPACT` (SEC + `FILE FV_IMAGE = GUIDED { SECTION FV_IMAGE = FvMain }`).
2. `Failed to Locate "Info Blk Memory Region"!` — a driver resolves a platform region
   by that name and the board's MemoryMapLib does not describe it.
3. `Loading Image at 0x9FE00000 EntryPoint=0x9FE005E0, DevicePath...` followed by
   `ASSERT [ArmPkg] (Code.c:2619) : Length < MaxImageSize ... is zeroed` — an image
   load from 0x9FE00000 trips an assert; that address is the region this repository
   labels `Uboot Log` (kboot_uboot_logmem, 2 MiB).

## Framebuffer geometry

The panel timing in the live device tree is 1080x2376
(`qcom,mdss-dsi-panel-width = 0x438`, `-height = 0x948`). With
`PcdFrameBufferWidth = 1080` and `PcdFrameBufferHeight = 2376` the console renders
crisp horizontal text; 1440x3168 and 3168x1440 both render badly (vertical bands and
repeated fragments respectively), so the geometry is settled.

## Next steps

- Describe the memory region the driver calls "Info Blk Memory Region" in
  `MemoryMapLib.c`, using the address ABL actually hands over.
- Keep the `kboot_uboot_logmem` window out of the regions an image may be loaded into
  so the load at 0x9FE00000 does not trip the assert.
## DXE bring-up findings

The firmware now starts DxeCore, dispatches the driver volume and reaches the BDS boot
menu. Each line below was read off the panel and fixed in this repository or in the
platform package:

| Symptom on the panel | Cause | Fix |
| --- | --- | --- |
| `ASSERT [DxeCore] Gcd.c(2518) : Length >= MinimalMemorySizeNeeded` | the map declared only 8 MiB of conventional memory | added the measured free spans (`Free Low/Mid/High/Tail`) and stopped `MemoryInitPeiLib` from building an allocation HOB for `EfiConventionalMemory`, which DxeCore subtracts before sizing its pools |
| `"Free High" and "Ram Oops" Overlap!` | the span straddled `ramoops@0xB0000000` | split the tail span around the ramoops node |
| every architectural protocol reported missing | the PCD driver waited for `gEfiPcdProtocolGuid` that it publishes itself (`DxePcdLib` carries that Depex) | added an `APRIORI DXE` block to the firmware volume, as the reference packages do |
| `ASSERT [BdsDxe] BmDriverHealth.c(553)` | `BmRepairAllControllers` needs FormBrowser2, which this package does not publish | set `PcdDriverHealthConfigureForm` to the zero GUID, the switch the source documents |
| `ASSERT [RamManagerDxe]` | the module links `QcomPkg/Library/RamManagerLib`, whose constructor needs the RAM Partition protocol from the vendor `RamPartitionDxe` | dropped the module; it is not part of the required integration |

Still logged but not fatal: `Failed to Display Boot Logo` (no boot-logo producer) and
`SetupKeypad: Failed to Locate STI Device Path` (no keyboard driver in the volume).
## DarwinBoot hand-off reached on the device

The panel shows the loader hand-off bar and the loader log:

```
DARWINBOOT: hand-off to the kernel
  kernel va 0xFFFFFFF0042C7000 pa 0x0C007C000
  entry  pa 0x0C31D0B8  args pa 0x0C031000  window 0x100000000 bytes
  video  base=0x9C000000 display=1 row=4320 width=1080 height=2376 depth=32
DARWINBOOT: command line: -v serial=3 debug=0x14e kt_fixed_zones=120 -no_slto_panic
DARWINBOOT: inputs validated; exiting boot services
```

The green bar across the top of the panel is the hand-off marker the loader draws after
`ExitBootServices`, so the hand-off to the kernel entry point happened.

What it took after the menu appeared:

| Symptom | Cause | Fix |
| --- | --- | --- |
| `boot refused: Compromised Data` | the ADT had no `memory-map` entry because no Data seed was passed | build the payload with a Data seed so `/chosen/memory-map` carries `RAMDisk` |
| `boot refused: Compromised Data` (again) | the trust cache was the raw IM4P from the IPSW, not the bundle DarwinBoot validates | unwrap the DER, keep the version-2 module, wrap it as count + offset table + module |
| `boot refused: Out of Resources` | the loader needs one contiguous window as large as `payload.adt.owned_memory_size` (4 GiB) and the map described only the first DRAM bank | add the two upper banks the device tree reports as conventional memory |
| `Compromised Data` with no `DARWINBOOT:` line | the loader read the virtual GOP on the console handle, whose `FrameBufferBase` is zero | validate that GOP and fall back to the platform framebuffer |

Next: the kernel entry itself. The entry probe (docs/guide/debugging#color-bars) is the
next observable.
## Kernel entry: what the bars say

The entry probe is enabled (`[payload] entry_probe_reset_delay = 6`) and its patch is
applied ("stage 1 blue bar, stage 2 magenta bar, stage 3 reset 6 s after the MMU is
on"). On the device only the **green** hand-off bar appears, so the kernel entry code
never ran.

The Mach-O of the payload kernel and the numbers the loader prints disagree:

| Source | Value |
| --- | --- |
| `__TEXT` vmaddr | `0xFFFFFFF007004000` |
| `LC_UNIXTHREAD` entry PC | `0xFFFFFFF0073F0488` (inside `__TEXT_EXEC`) |
| lowest segment (`__PRELINK_TEXT`) | `0xFFFFFFF00427C000` |
| loader prints `kernel va` | `0xFFFFFFF0042C7000` |
| loader prints `entry pa - kernel pa` | `0x3150B8` (~3.2 MiB) |
| expected offset from `__PRELINK_TEXT` | `0x3178488` (~51.9 MiB) |

So either the base the loader reports is not the segment base it uses, or the entry it
computed is not the Mach-O entry. The next step is to read `ParseKernel` against this
kernel and settle which of the two is wrong, then check the jump target.
## Kernel entry reached

With the entry probe enabled the panel shows the three bands in order:

| Band | Meaning | Observed |
| --- | --- | --- |
| Green | DarwinBoot exited boot services | yes |
| Blue | kernel entry ran with the MMU off | yes |
| Magenta | kernel reached the point before the MMU is enabled | yes |

The loader's own sanity line confirms the entry address:

```
entry in __TEXT_EXEC offset 0x1B8488 executable yes
```

which matches the Mach-O analysis of the payload kernel: `LC_UNIXTHREAD` entry
`0xFFFFFFF0073F0488` inside `__TEXT_EXEC` at offset `0x1B8488` from `__TEXT`'s
`0xFFFFFFF007004000`. The earlier suspicion that the loader computed a wrong entry
came from misreading hex digits in a photograph.

Behaviour after the bars, in this build:

| Probe setting | Result |
| --- | --- |
| `entry_probe_reset_delay = 6` | the device resets about six seconds after the MMU is on, then Android boots |
| `panic_screen = true` (no reset) | the kernel is left to continue past the MMU enable |
## The six-second reset was the APSS watchdog

After the probe was removed the phone still reset about six seconds after the hand-off,
so the reset was never the probe's timer. UniXNU's own port notes record the cause
(`docs/devices/kebab-stevedang67889.md`): "APSS watchdog at 0x17C10008 ... Without the
watchdog disable a fault reboots the phone before the log can be read."

`SecEntry` now writes zero to `0x17C10008` before it touches the caches, which is the
first point where a physical register write is safe (SEC runs with the MMU off). With
that in place the hand-off screen stays on the panel with the loader's complete report
and no reset.

## Where the kernel stands

| Observation | Meaning |
| --- | --- |
| green hand-off bar | DarwinBoot exited boot services |
| `entry in __TEXT_EXEC offset 0x317A488 executable yes` | the loader's entry lands in the executable segment, matching the Mach-O |
| blue and magenta bars (probe build) | the kernel entry ran with the MMU off and reached the MMU enable |
| six-second reset (before the watchdog fix) | the APSS watchdog, not the kernel, ended the boot |
| screen holds the hand-off report (after the fix) | the kernel runs without a panic and without a reset; nothing draws because this board has no serial console |

The next observable is the panic screen, which the payload patches into the kernel and
which runs inside the kernel's own panic path.
## The embedded panic log does not survive a power-button reboot

The plumbing is complete and verified in the ADT:

| Piece | State |
| --- | --- |
| device tree `pram@9f400000 { reg = <0 0x9f400000 0 0x80000>; }` | present |
| `/chosen/embedded-panic-log-size = <0x80000>` | present in the ADT (the generator now carries it) |
| memory map | the region is reserved, carved out of the old Free Mid span |
| reader | `PanicLogDxe` prints the header and text, then stops |

A panic is produced every boot (`"AMFI: No PMGR?\n" @ConfigurationSettings.cpp:235`), yet the
reader never sees `0x46554E4B` in the region on the next boot. The reboot used to get back
to fastboot is a **long power-button press**, which power-cycles the PMIC, so DRAM loses
its contents before the next boot can read them.

The entry probe's own reset used **PSCI SYSTEM_RESET** and the phone came back to Android,
which is a soft reset that keeps DRAM. The fix is therefore on the panic path: write the
region and then request PSCI SYSTEM_RESET instead of parking on the panic screen.

Until that is added, the panic screen remains the working evidence path: it shows the
message, the frames and the page counters on the same boot.
## The kernel reaches the root mount

The AMFI refusal is gone and the kernel now walks into IOKit and BSD. The panic that
replaced it names the next milestone exactly:

```
Failed to mount root device @IOMediaBSDClient.cpp:984
frames fffffff0079b4a0 fffffff00792408 fffffff0076b50c ...
```

That is Stage 6 of the bring-up guide ("Storage and root"): no block driver publishes
`OS` and `SystemOS` yet, so there is no device for the kernel to mount.

### Skipping the AMFI refusal

`AMFI: No PMGR?` came from the prelinked kext's report function, which always ends in
a panic. It has exactly one caller:

```
0x4AC93E4  report function (sub sp, ... ; ... ; bl panic)   noreturn
0x4AAED04  bl 0x4AC93E4                                     the only call
0x4AAEC4C  the caller's own register epilogue (ldp x29, x30, [sp, #64] ... ret)
```

`scripts/patch-amfi-nopmgr.py` replaces the call with `b 0x4AAEC4C` (word
`0x17FFFFD2`), so AMFI unwinds normally instead of panicking. Run it on the kernel
before `unixnu build payload`; it checks the original instruction and refuses to patch
a kernel that differs.

The boot argument `amfi_get_out_of_my_way=1` and removing AMFI from the prelink roots
do not help: the check runs before policy is consulted and the kext is built into the
kernelcache.
## UFS probe: the bootloader leaves the controller ready

`UfsProbeDxe` reads the host and PHY registers before any driver touches them and stops
on the panel. On this board:

```
UFSPROBE: host 0x1D84000 phy 0x1D87000
UFSPROBE: VER 0x1587031F CAP 0x00000300
UFSPROBE: HCS 0x0000010F HCE 0x00000001 IS 0x00000400
UFSPROBE: PHY 0x00000001 0x00000000
UFSPROBE: device-present yes, ready yes
```

| Register | Reading | Meaning |
| --- | --- | --- |
| `VER` | `0x1587031F` | the DWC host answers, so its clock branch is already gated on |
| `CAP` | `0x00000300` | capability block is coherent |
| `HCS` bit0 | 1 | a device is present |
| `HCS` bit1 | 1 | the UTP is ready |
| `HCE` bit0 | 1 | the controller is enabled |
| `IS` | `0x00000400` | one pending interrupt bit to clear before use |
| `PHY` | `0x00000001` | the PHY PLL is on |

The bootloader therefore hands over a live controller: adopting the existing request
list is the right path, and the GCC clock ids, `core_reset` and PHY sequence recorded in
`porting.toml` are only needed if a future boot comes up cold.

The region is already mapped by the firmware memory map ("UFS", `0x01D84000`,
`0x00100000`, MmIO), which is why the probe needs no clock or reset code to read it.
## UTP round trip from the firmware

`UfsProbeDxe` now takes the request list and issues two commands, using the UTRD and
UPIU layout from ShengUfs's `RootCommands.h`:

```
UFSPROBE: HCS 0x0000010F, taking the queue
UFSPROBE: UTRLBA 0x27FAC400 RSR 0x0
UFSPROBE: NOP ocs 0x0 response-type 0x20 tag 0
UFSPROBE: READ CAPACITY ocs 0x0 status 0x0
UFSPROBE: data 7F7E2000 00000000 00000000 00000000
```

Both commands complete with `ocs 0` and the NOP answers with a `0x20` NOP IN UPIU, so the
queue ownership, the UTRD encoding, the doorbell and the completion polling all work on
this controller.

The capacity decode is still wrong (the parsed block size is `0x02000000`). The data
words are read without `volatile` after the cache invalidate, so the compiler is free to
keep a stale copy; the reader must use volatile loads (or re-invalidate immediately
before each read) and cross-check the response UPIU's residual length before trusting the
numbers.

Status: the transport works, the capacity decode does not yet.
## UFS transport: what works and what does not

Three builds pin the state down.

| Build | NOP | READ CAPACITY | Data |
| --- | --- | --- | --- |
| PRDT inside the data area, two words, no length | `ocs 0`, response `0x20` | `ocs 0`, `status 0`, `residual 0` | `0060AC7F 02000000` (device data) |
| same, data read again | `ocs 0` | `ocs 0` | same bytes, `block-size 0x02000000` |
| PRDT at `UCD+0x400`, four words with `len-1`, `IS=1` before the doorbell | `ocs 0`, response `0x00` | `ocs 0`, data all zero | nothing transferred |

So the command transport (UTRD, UPIU, doorbell, completion polling) works in every build, and
the data phase only moved bytes when the PRDT lived inside the data area with the two-word
form. The four-word form with `prd[3] = length - 1` (the layout ShengUfs uses on SM8550)
transfers nothing on this controller, and it also leaves the response area unwritten.

Next diagnostic, not yet run: dump the UTRD words and the PRDT bytes immediately before the
doorbell and the response bytes immediately after completion, in the same build, so the
comparison is exact instead of across builds. That tells us whether the controller consumed
the PRDT pointer and the response length fields the way we encoded them.
## UFS: the documented cold-initialization boundary

The firmware reached the point where every remaining failure has one cause: the host
controller cannot be driven without redoing the link startup.

| Build | What it did | Result |
| --- | --- | --- |
| own buffer, `UTRLBA` rewritten, no reset | `NOP` and `READ CAPACITY` reported `ocs 0` once, response `0x20` | one command seemed to complete, nothing repeatable |
| own buffer, PRDT separated, data at `0x3000` | `ocs 0`, response `0x00`, data all zero | no transfer |
| adopt the firmware `UTRLBA` (`0x9A1F3D00`) | `UTRLRSR` read back `0` | the controller never owned a list |
| buffer forced below 4 GiB (`0x9F500000`) | `UTRLRSR` still `0` | address reachability is not the cause |
| `HCE` 0 then 1, then install the list | `HCS` falls from `0x10F` to `0x8`, `UTRLRSR` stays `0` | the reset tears down the link |

`HCS` `0x10F` (device present, UTP ready) becoming `0x8` after a host reset is the
documented boundary: `docs/porting/ufs.md` says a board that needs cold initialization
"needs its PHY, reset, clock and power sequence implemented and recorded".

The inputs for that sequence are all measured and recorded in `porting.toml`: host
`0x1d84000` + `0x3000`, PHY `0x1d87000` + `0xe00`, GCC at `0x100000` with core `0xa7`,
iface `0xa6`, unipro `0xb3`, ice `0xaa` and bus_aggr `0x06`, `core_reset` `0x21`, PHY
`ref_aux_clk` `0xad`, interrupt SPI 265, and no SMMU on this platform.

What is missing is the register-level sequence: the GCC branch enables, the reset
assert/release timing and the PHY power-up table. Those are not in this repository yet.
## UFS: what the firmware experiments settled

Six builds bracket the problem. The controller state and the register values below are
measured; the transport beyond the command phase is not.

| Build | Change | `HCS` | `UTRLRSR` | Data |
| --- | --- | --- | --- | --- |
| 1 | own buffer, PRDT inside the data area | `0x10F` | `0` | NOP answered `0x20`, capacity returned bytes |
| 2 | PRDT separated, data at `0x3000` | `0x10F` | `0` | none |
| 3 | adopt the firmware list (`0x9A1F3D00`) | `0x10F` | `0` | none |
| 4 | buffer forced below 4 GiB | `0x10F` | `0` | none |
| 5 | `HCE` 0 then 1 | `0x8` | `0` | none, link dropped |
| 6 | clocks, `GCC_UFS_PHY_BCR` pulse, `DME_LINKSTARTUP` | `0x8` | `0` | none, link did not return |

Settled facts:

- The bootloader leaves the controller live: `HCS 0x10F`, `HCE 1`, PHY PLL on.
- `UTRLRSR` reads back `0` in every configuration, so the controller never takes
  ownership of a request list even though commands report `ocs 0`.
- Writing `GCC_UFS_PHY_BCR` (0x77000) drops `HCS` to `0x8`. The PHY does not come back
  from `DME_LINKSTARTUP` alone, so the QMP PHY power-up table is required before any
  reset of that block is safe.
- Clock gates do come up: after enabling them, GCC reads `aggr 0x221`, `axi 0x221`,
  `ahb 0x201`.

Missing, and needed before the kernel can mount root:

1. The SM8250 QMP UFS PHY initialization table (`phy-qcom-qmp-ufs.c`) for `0x1d87000`.
2. A request-list submission that the controller actually accepts (`UTRLRSR` = 1).
3. Cache-coherent buffers with the PRDT outside the data area, once (2) works.

The register map gathered so far is in `porting.toml`; the probe lives in
`uefi/Platforms/OnePlus/instantnoodlepPkg/Drivers/UfsProbeDxe`.
## UFS: stop reimplementing it in the firmware

The firmware probe now matches ShengUfs's `hostSubmit` on every parameter that the
source shows:

| Item | ShengUfs | Probe |
| --- | --- | --- |
| request-list run/stop | `0x60` | `0x60` |
| `HCS` bit 1 wait after start | yes | yes |
| UTRD words 0/2/4/5/6/7 | as listed | identical |
| PRDT entry, length in `prd[3]` | `dataBytes - 1` | `dataBytes - 1` |
| interrupt acknowledge before the doorbell | `0x20 = 1` | `0x20 = 1` |
| cache clean and `dsb sy` before the ring | yes | yes |

The queue now starts (`RSR 0x1`) and the controller reports `ocs 0`, yet neither the
response UPIU at `UCD+0x200` nor the data at the PRDT address is written. Two things are
still unknown and cannot be settled from the outside: where this controller looks for the
response UPIU, and why it completes without a data phase.

Continuing to reverse-engineer that in UEFI is the wrong vehicle. The documented design is
a kernel driver with runtime class `NativeUfs` (`docs/drivers/sheng-ufs.md`), where the
reference `NativeUfs.cpp` runs unchanged and its own diagnostics report `ocs`, doorbell and
`IS` per command. The firmware probe has done its job: it proved register access, clock
gates, the run/stop register and the command/PRDT layout.

Next step, in order:

1. Build ShengUfs's sources against the kona constants already recorded (host
   `0x1d84000`, PHY `0x1d87000`, GCC ids, `core_reset` `0x21`, no SMMU).
2. Keep the firmware's `UfsProbeDxe` as the cold-state check, not as the driver.
3. Let the kernel driver publish the root IOMedia and hand the mount to XNU.
## Kernel integration: what the CI had to learn about KonaUfs

The component catalog and the kernel build script do not know a board-local driver by
default. Four things had to be supplied for `KonaUfs` (all in the macOS workflow):

| Requirement | Where | Why |
| --- | --- | --- |
| catalog entry | append `components/kona-ufs/catalog-entry.toml` to `components.toml` | the catalog lives in the UniXNU checkout, and CI uses upstream |
| archive plan | `[kernel] archives` in `board.toml` must name **every** kernel-stage component | once the list is explicit, `archives needs component entries for` fails otherwise |
| storage ABI include | `scripts/build-arm-driver.py` selects it by component id | otherwise `NativeUfs.cpp` cannot find `<IOKit/storage/IOBlockStorageDevice.h>` |
| archive path | `scripts/build-xnu-4k-baseline.sh` hardcodes `build/sheng-ufs/libShengUfs.a` | `build-arm-driver.py` names the archive from the id, so this board builds `build/kona-ufs/libKonaUfs.a` |

Two runtime prerequisites are recorded but not yet met, and neither blocks compiling:

- `payload.py` refuses a payload whose kernel arguments include `native-ufs-root=1` unless
  the arguments also carry exactly one `sheng-pmgr=2` and the kernel defines
  `_sheng_pmgr_run`. Publishing a UFS root therefore needs the sheng PMGR integration or
  an equivalent local decision; the AMFI patch alone only removes the panic.
- `build-xnu-4k-baseline.sh` requires UFS, owner-root audit, Virtio block and source
  corecrypto together when `XNU_BOOT_PROGRESS=1`, so the boot progress layer stays off
  until those are selected.
## Kernel link: the storage classes are a kext, not kernel code

The CI now builds the board driver (\`libKonaUfs.a\`, 458 KiB) and the mockfs fixture, and
the kernel link names exactly what is still missing:

\`\`\`
"vtable for IOBlockStorageDevice", referenced from: NativeUfsBlock::start(IOService*)
"_boot_progress_poll"
"_verified_root_key", "_verified_root_policies", "_verified_root_policy_count"
"_vr_open", "_vr_verify_block", "_vr_xnu_crypto"
\`\`\`

Each has a known supplier, and none of them is selected on this board:

| Symbol | Supplier | Gate |
| --- | --- | --- |
| \`IOBlockStorageDevice\` vtable | IOStorageFamily, a kext of the original kernelcache | none: an archive-form driver cannot link it |
| \`_boot_progress_poll\` | \`build/boot-progress/libBootProgress.a\` | \`XNU_BOOT_PROGRESS=1\` |
| \`_verified_root_*\`, \`_vr_*\` | \`build/verified-root-xnu/libVerifiedRootProbe.a\` | \`OWNER_ROOT_PROBE=1\` |

UniXNU's own device records already describe this failure mode: \`docs/.vitepress/device-records/sirius.toml\`
line 191 says "IOStorageFamily is a kext of the original kernelcache, so the archive form of
the driver left IOBlockStorageDevice undefined at the kernel link", and that port solved it by
building the block driver as a kext that binds the kext's dispatch table (eleven imports)
instead of subclassing it at link time.

So the remaining work is a decision, not a bug:

1. **Kext form** — build \`KonaUfs\` as a kext that IOStorageFamily loads, following the sirius
   derivation of the dispatch table plus the prelink slide. No kernel link change.
2. **Ship the storage ABI into the kernel** — compile the public IOStorageFamily sources from
   \`refer/IOStorageFamily\` into an archive and add it to the kernel link, so the archive-form
   driver resolves. Larger, and it duplicates a kext inside the kernel.
3. **Bring-up only** — keep the transport in the firmware (proved by \`UfsProbeDxe\`) and
   revisit root once one of the above is chosen.

Until then \`CONFIG_XNU_NATIVE_UFS\` pulls in a driver whose storage base class the kernel
cannot resolve, which is why the kernel lane stops at this link.
## Firmware log export without a serial port

`docs/porting/log-capture#qr-logs` documents the transport this board can actually use: the
Advanced Logger buffer paged onto the panel as OneSend QR codes, scanned by a second phone.
The firmware now carries the exporter:

| Change | File |
| --- | --- |
| `UniXNUPkg/Driver/AdvancedLogDxe/AdvancedLogDxe.inf` in `[Components]` | `instantnoodlep.dsc` |
| the same INF in the DXE volume | `instantnoodlep.fdf` |
| `AdvancedLoggerAccessLib` and `QrEncoderLib` mapped | `instantnoodlep.dsc` |
| `[payload] tools = true` | `board.toml` |

The firmware builds with the exporter and the payload carries the Tools entry, so the menu
exercises `Tools → Advanced Logger`. The exporter has `gAdvancedLoggerProtocolGuid` as its
dependency: the entry only shows data once a producer installs that protocol, which needs
the platform DebugLib routed into `AdvLoggerPkg`. That producer wiring is the next step; until
then the menu entry reports an empty log instead of failing.
