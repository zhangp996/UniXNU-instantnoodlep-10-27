# 2026-11-11: the trap PC names its image

## What this answers

The evening handoff left one question open: which image holds the three user PCs that kill
`watchdogd`, `liveactivitiesd` and `SpringBoard`.  Route 1 of that handoff - a kernel hook that
resolves the PC inside the process - is implemented, built and run on the device.

## Change

`patches/xnu/zzz-trap-image.patch`, last in `patches/xnu/series` after `zzz-trap-pc.patch`:

* `bsd/kern/kern_mman.c` - `native_trap_image(pc)` prints two lines per trap:
  * `XNU-TRAP-IMAGE`: `find_region_details()` walks the current map to the entry that holds the
    PC, `vnode_getwithvid()`/`vn_getpath()` turn a file-backed entry into a path, and the offset
    inside the mapping is printed with it.
  * `XNU-DYLD-IMAGE`: the task's dyld image table (`task_get_all_image_info_addr()` plus
    `copyin()` of `dyld_all_image_infos` and `dyld_image_info`) names the image whose load address
    is the closest one below the PC.  This view also works for ranges that are not file backed in
    the process map.
* `osfmk/arm64/sleh.c` - `handle_breakpoint()` calls it once, after the existing `XNU-TRAP` line
  and before `exception_triage()` tears the process down.

Bring-up only: no state changes, the map and object locks are dropped before anything is printed,
and the image table is read with `copyin()` while the thread is still in the trapping task.

## Build

| Step | Value |
| --- | --- |
| `kernel/kernel.macho` | `d5a69b980abff1bacc8278178cc6dafa72cea7ced139c25c7e9050ff43d039e3` |
| AMFI-patched copy | `a498a1ea29cecc0efd4d6c6a46ae4d1d6381d0337aa56fffeb7e362d2127d5a0` |
| `payload/payload.img` | `95c82aa4af07de7b1f087be4a1f463bcb9d6aad8c5028c026d798af6bfb833c3` |
| `bootEU.img` | `56ec803773e2014b66526325a253d17fba0ad8c7d592926f8e96058e01f324dd`, 183,005,184 bytes |

The kernel needs `XNU_EXTRA_LD_ARCHIVES` for `libRamWatchdog.a`; the payload came from the
AMFI-patched kernel (`patch-amfi-nopmgr.py`, `patch-amfi-trustcache-gate.py`) with
`--data-ramdisk owner-data-3g.dbr`, and was packed with the unchanged shim+firmware volume and
`stock_boot.dtb`.

## Result

Three captures (`console-bootEU.log`, `console-bootEU-2.log`, `console-bootEU-3.log`) agree: every
instance of each process traps in the same image at the same offset.

```
XNU-TRAP: pc=0x100c4bb0c lr=0x100c4bb0c esr=0xf2000001 tid=345
XNU-DYLD-IMAGE: watchdogd pc=0x100c4bb0c /usr/libexec/watchdogd+0xab0c load=0x100c41000 images=159
XNU-TRAP-IMAGE: watchdogd pc=0x100c4bb0c file=/usr/libexec/watchdogd off=0xab0c map=0x100c41000+0x10000

XNU-TRAP: pc=0x1c4f3e4f8 lr=0x1c4f3e4cc esr=0xf2000001 tid=515
XNU-DYLD-IMAGE: liveactivitiesd pc=0x1c4f3e4f8 /System/Library/PrivateFrameworks/SessionSyncEngine.framework/SessionSyncEngine+0x1d4f8 load=0x1c4f21000 images=413
XNU-TRAP-IMAGE: liveactivitiesd pc=0x1c4f3e4f8 not in a file-backed range

XNU-TRAP: pc=0x1a2a272ec lr=0x1a2a272ec esr=0xf2000000 tid=256
XNU-DYLD-IMAGE: SpringBoard pc=0x1a2a272ec /System/Library/PrivateFrameworks/FrontBoardServices.framework/FrontBoardServices+0x1e2ec load=0x1a2a09000 images=1235
XNU-TRAP-IMAGE: SpringBoard pc=0x1a2a272ec not in a file-backed range
```

### watchdogd: the watchdog user client is missing

The image is on the root volume, so the PC symbolises directly.  Offset `0xab0c` from the image
base is `0x10000ab0c` in the Mach-O, the `brk #1` behind `__os_crash()`:

```
10000ab00:  adr x0, #9015 ; "connect_to_kext() unexpectedly failed - unable to lookup watchdog KEXT service"
10000ab08:  bl __os_crash
10000ab0c:  brk #1
```

The watchdog trap is not a timeout or a scheduling failure: userland `watchdogd` never reaches a
watchdog user client.  `RamWatchdog` answered the earlier in-kernel panic, but the platform service
`watchdogd` looks for behind `connect_to_kext()` (the `AppleARMWatchdogTimer`-style user client) is
not published on this board.

### liveactivitiesd: a forced unwrap of nil in SessionSyncEngine

The image is a dyld-cache member.  In `SessionSyncEngine` subcache `.21`, the image sits at
`0x1c18f6210` and the PC is `0x1c18f64f8`; the instruction that ran is the target of a `cbz`:

```
1c18f64bc:  bl     ...          ; returns an optional
1c18f64c4:  mov    x0, x22
1c18f64c8:  bl     ...
1c18f64cc:  cbz    x20, #44     ; x20 == 0 -> 0x...64f8
...
1c18f64f4:  ret
1c18f64f8:  brk    #0x1
```

`lr` is the instruction after the earlier call (`0x...64cc`), which is why the trap reports
`lr = pc - 0x2c`: the `cbz` took the null branch into the trap that the compiler emitted for it.
That is a Swift precondition failure on a nil optional inside SessionSyncEngine, not a shared-cache
or code-signing failure.

### SpringBoard: a noreturn call in FrontBoardServices returned

In subcache `.09` the image sits at `0x19f3c1000`; the PC is `0x19f3df2ec`:

```
19f3df2dc:  mov  x0, x21
19f3df2e0:  bl   0x19fcd82e8
19f3df2e4:  bl   0x19f46e740
19f3df2e8:  bl   0x19fcd7f04
19f3df2ec:  brk  #0
19f3df2f0:  brk  #0x1
```

`lr` equals the PC, i.e. the last `bl` returned to `0x...2ec` and execution fell into the `brk #0`
that the compiler placed after a call it treats as noreturn.  The same four-call block with a
different argument repeats every `0x68` bytes, so this is a fatal-error helper of the framework.
The next step is to resolve which symbol `0x19fcd7f04` binds to; that needs the cache's stub and
bind tables, not just the image bytes.

### Why the earlier "private mapping" reading was wrong

Both library ranges are cache members: the image table names them, and neither framework carries a
binary on the root volume (`FrontBoardServices.framework` there holds only `Info.plist` and
`_CodeSignature`).  The iOS 17 split cache in the SystemOS cryptex is
`System/Library/Caches/com.apple.dyld/dyld_shared_cache_arm64` (image table, 491,520 bytes) plus 52
subcache files and a 685 MB `.symbols` database.  The earlier comparison against the extent of the
one main cache mapping cannot see the subcache ranges, which is why the two PCs looked private.

## Next

* Resolve the two cache PCs to symbols.  The pieces are in place: the image table in
  `dyld_shared_cache_arm64` maps a path to its cache address, each subcache header maps that
  address to a file offset (`.21` and `.09` for the two traps here), and `dyld_shared_cache_arm64.symbols`
  holds the names - that file did not extract with 7-Zip 26.04 ("Unsupported Method"), so it needs
  another reader.
* `watchdogd` needs no cache tooling: publish the user client `connect_to_kext()` looks for, or
  make `RamWatchdog` answer it.

## 18:20: the official record has the recipe, and it resolves the three PCs

`docs/devices/sirius-huajifruit.md` (the reference port that booted the same 21H461 userland) records both
the failure signature and the procedure:

> Crash records on the panel symbolize against the 21H461 arm64 shared cache: unslid address = recorded
> address - the record's slide (+...), looked up with `ipsw dyld a2s`; three cache files (.symbols,
> .34 and .52 .dyldlinkedit) are LZBITMAP-compressed in the SystemOS image and need apfs-fuse to extract.
> liveactivitiesd's SIGTRAP is a Swift runtime trap in SessionSyncEngine under SessionCore.Daemon.workloop

That is exactly the wall this document hit earlier: 7-Zip 26.04 refuses those three files
("Unsupported Method"), and with them the address cannot be named.  `apfs-fuse` (built from
sgan81/apfs-fuse) mounts the cryptex image and reads them, and `ipsw` v3.1.745
(`ipsw dyld a2s`) then resolves any unslid cache address.  The cache must live on a local filesystem:
over the 9p mount every ipsw command fails with `invalid argument`.

### What the three PCs are

| Trap | Symbol | Meaning |
| --- | --- | --- |
| SpringBoard `FrontBoardServices+0x1e2ec` | `-[FBSDisplayMonitor _initWithBookendObserver:transformer:] + 1148` | the branch that calls `__bs_set_crash_log_message()` with **"failed to find the main CADisplay"** and then traps |
| liveactivitiesd `SessionSyncEngine+0x1d4f8` | `func_1c18f6274 + 644` (local Swift thunk) | the null branch of `cbz x20, <brk #1>`, i.e. a forced unwrap of nil |
| the call before SpringBoard's trap | `BaseBoard __bs_set_crash_log_message` | a setter that stores the message and returns, by design |

The literal pool behind the SpringBoard branch, dereferenced through the constant CFString:

```
cfstring 0x1e7a9bf68 -> 0x19f453ecc  "failed to find the main CADisplay"
cfstring 0x1e7a9f28 -> 0x19f453e90  "failed to find the symbol for CADisplay"
cfstring 0x1e7a9f55 -> 0x19f453f55  "two CADisplays had the same displayID(%u) -> $@"
```

So SpringBoard is not dying of a mis-bound call: FrontBoardServices finds no main `CADisplay` and
crashes on purpose, with the message the crash reporter would carry.  `CADisplay` comes from
CoreAnimation and needs a display provider - on this board the framebuffer is driven directly, and no
IOMobileFramebuffer/CoreAnimation display exists, which is the SpringBoard blocker.

### watchdogd: no abort_with_payload call happens

The earlier reading - "`brk #1` follows `bl __os_crash`, so the syscall returned" - was tested with
`zzz-os-crash-observe.patch`, which prints process, reason and thread signal state in
`abort_with_payload_internal()`.  Kernel `4f0dea31`, image `d0f26181` (`console-bootEV.log`):
the same eleven `XNU-DYLD-IMAGE` lines and the same traps, and **zero `XNU-OSCRASH` lines** - the
syscall is never entered on this boot, so the trap is not a returned `abort_with_payload`.  The
`__os_crash` import in `watchdogd` must be resolved to the same kind of message-setter the BaseBoard
frame shows; the actionable part stays the string itself:
"connect_to_kext() unexpectedly failed - unable to lookup watchdog KEXT service".

## Next (revised)

* **SpringBoard**: publish a main display for CoreAnimation (an IOMobileFramebuffer-backed
  `CADisplay`), or the FBSDisplayMonitor assertion will keep firing.  This is the wall in front of the
  UI, not a code-signing or shared-cache problem.
* **liveactivitiesd**: the trap is a nil force-unwrap in SessionSyncEngine; the reference port names the
  same site as `SessionCore.Daemon.workloop`.  Compare its record before treating this as a board bug.
* **watchdogd**: publish the watchdog user client `connect_to_kext()` looks for (or make RamWatchdog
  answer it); the probe patch can stay or be dropped, it prints nothing on a boot without abort_with_payload.

## Reproduce the symbol lookup

```sh
# apfs-fuse (current CMake needs the policy flag; LZFSE comes from the submodule)
git clone --depth 1 --recurse-submodules https://github.com/sgan81/apfs-fuse.git
mkdir apfs-fuse/build && cd apfs-fuse/build
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 && make -j8

# the cryptex image is a raw APFS volume; the files live under root/
./apfs-fuse /path/044-76034-038.dmg /root/cryptex
C=/root/cryptex/root/System/Library/Caches/com.apple.dyld
cp $C/dyld_shared_cache_arm64* /root/dsc/          # local disk: ipsw fails on 9p with "invalid argument"

# ipsw v3.1.745 (release asset ipsw_*_linux_x86_64.tar.gz)
ipsw dyld a2s --no-color -i -d /root/dsc/dyld_shared_cache_arm64 0x19f3df2ec
# -> 0x19f3df2ec: -[FBSDisplayMonitor _initWithBookendObserver:transformer:] + 1148
```

Unslid address = the image table address (`ipsw dyld image` or the cache's own table) plus the offset
the kernel printed as `XNU-DYLD-IMAGE: ... +0x...`.

## What the official documentation says about the display wall

Three places in the UniXNU documentation and catalog describe this stage:

* `docs/porting/display.md` - "Display and brightness", step 2 *Adapt the XNU display backend*:
  read `GopFramebuffer` and its `NativeLegacyDisplay.cpp`, `NativeGOPPresenter.cpp`,
  `NativeGOPWire.h`; select the component with its display archive recipe
  (`scripts/build-gop-legacy-display.py`), enable `CONFIG_XNU_NATIVE_LEGACY_DISPLAY=y` and its
  `offline-legacy-*` arguments.  **Expected result:** "XNU publishes the display, presents frames
  and completes swaps."
* `docs/drivers/gop-framebuffer.md` - the implementation: it reads the firmware framebuffer through
  `PE_current_console` and publishes it through the selected MobileFramebuffer ABI.
* `components.toml` `[[component]] id = "GopFramebuffer"` - `verified_devices = ["sheng"]`, i.e. the
  same SM8550 class, with outputs `build/gop-framebuffer/libGopFramebuffer.a` and
  `libGopFramebuffer.late.a`; `docs/guide/build-kernel.md` lists `XNU_NATIVE_MOBILE_DISPLAY` and
  `XNU_NATIVE_LEGACY_DISPLAY` as the two link flags for those late archives.

This board's own `porting.toml` already records the same next action under `[[area]] id = "display"`
(status `planned`, `components = []`): "Check whether the Mu-Silicium framebuffer covers the panel and
add GopFramebuffer to the selection", with the limit "No display driver is selected; the GOP path
depends on the missing firmware platform".

The console agrees with that state: every boot so far ends the display loader with
`XNU-EXIT: IOMFB_FDR_Loader pid=85 code=256`, which is the same signature the `merlinx` record keeps
("IOMFB_FDR_loader fails before GPU display handoff"), and SpringBoard then reports the missing
`CADisplay` in `FBSDisplayMonitor`.

Reference points for the work: `sheng` (the component's verified device), `j716f` ("UEFI GOP
framebuffer handoff and NativeLegacyDisplay / IOMobileFramebuffer desktop presentation"),
`alioth` (`IOMobileFramebufferLegacy` prelinked and bound by absolute address, `SWAP calls=0`) and
`evb10` (`offline-legacy-*` plus `sheng-vsync=60`).

**Work items** (none of them is a kernel patch):

1. `[kernel] archives` in `board.toml`: add `component:GopFramebuffer` and the display archive recipe
   after the complete arm64 list (the array replaces the inherited one).
2. Bring the late-archive recipe (`scripts/build-gop-legacy-display.py` in the reference board
   package) and the ABI inputs it audits.
3. `configs/instantnoodlep_defconfig`: `CONFIG_XNU_NATIVE_LEGACY_DISPLAY=y` (and the matching
   `XNU_NATIVE_...` link flag), plus `offline-legacy-gop=1`/`offline-legacy-phase=<n>` in the payload
   arguments.
4. Board DTS/ADT: the framebuffer handoff region and display policy the component's board hooks read.

## 18:40: the display component, as far as it goes without a board recipe

* `[components] include` now selects `GopFramebuffer`; the tree step fetches `drivers/GopFramebuffer`
  and `drivers/NativeMobileDisplay`.
* A `component:GopFramebuffer` entry in `[kernel] archives` builds nothing:
  `scripts/unixnu/kernel.py:archive_plan()` expands only components whose catalog record has a non-empty
  `build` list, and `GopFramebuffer` has `build = []` with `build_stage = "embedded"`.  The entry was
  removed again; the archives have to come from a board recipe.
* `python3 scripts/build-arm-driver.py GopFramebuffer` fails with eleven
  `-Werror,-Wmissing-designated-field-initializers` errors from the XNU kalloc macro; with
  `CCC_OVERRIDE_OPTIONS='# +-Wno-missing-designated-field-initializers'` it passes the `-Werror` build and
  the arm64-apple-ios17.0 object audit and writes `build/gop-framebuffer/libGopFramebuffer.a`
  (270,784 bytes) from `NativeMobileDisplay.o`.
* The kernel links `libGopFramebuffer.late.a` (`XNU_NATIVE_MOBILE_DISPLAY`) or
  `libNativeLegacyDisplay.late.a` (`XNU_NATIVE_LEGACY_DISPLAY`), and both are produced by board recipes in
  the reference repository:
  * `scripts/build-gop-framebuffer.py` (pinned commit 516a7804): runs `build-arm-driver.py NativeMobileDisplay`,
    then requires the exact candidate `build/xnu-prelink/native-owner-mobile-graphics-candidate.macho`
    (sha256 `aded28d3c61dafaf7b8a7c96b14427a798e9e95470ae6f287bcd038c07626798`), verifies that the original
    constructor and destructor at `0xfffffff00577b348`/`0xfffffff00577b37c` really branch to
    `IOService::IOService(OSMetaClass const*)` and `IOService::~IOService()` through the original veneers,
    globalizes `__GLOBAL__sub_I_NativeMobileDisplay.cpp`, drops `__mod_init_func`, and archives the late
    object with `original-bindings.o` and `MobileDisplayProviderProbe.o`.
  * `scripts/build-gop-legacy-display.py` is iOS 17 only by construction and additionally runs
    `scripts/audit-mobile-graphics-legacy.py`, which is not present in the reference repository's `main`.
* Consequence: the display step is a port of a recipe plus its ABI audit, not a configuration change.
  `porting.toml` `[[area]] display` now records that as its next action.

## 18:50: the patch reference documents the display start, and it is already applied

`docs/xnu/patch-reference.md` (the page the porting guide links) lists both display hooks:

| Patch | Effect |
| --- | --- |
| `offline-legacy-start` | Starts the legacy display probe after `IOKitBSDInit` |
| `offline-component-load-probes` | Loads the APFS, IOGPU and mobile-graphics components and records checkpoints |

Both are already in `profiles/arch/arm64/patches/xnu/series` (lines 34-35), so this kernel carries
them: `bsd_init.c` line 1539 now reads

```c
kern_return_t result = IOKitBSDInit();
#if (DEVELOPMENT || DEBUG) && defined(XNU_NATIVE_LEGACY_DISPLAY) && XNU_NATIVE_LEGACY_DISPLAY
	extern void native_legacy_display_late_probe(void);
	native_legacy_display_late_probe();
#endif
```

and `CONFIG_XNU_NATIVE_LEGACY_DISPLAY` is not in `configs/instantnoodlep_defconfig`, so the call is compiled
out.  No patch work is needed on this board; what remains is the configuration flag, the late archive that
defines `native_legacy_display_late_probe` (the board recipe), and the `offline-legacy-*` arguments.

## 19:00: what the payload page covers, and the argument budget

`/zh/concepts/payload` does not describe the display work, but it documents the delivery path for the third
piece - the `offline-legacy-*` arguments:

* the command line written into `EFI/UniXNU/unixnu.toml` is `[payload] kernel_args` (board.toml) merged
  with `--kernel-arg NAME=VALUE`, and DarwinBoot copies it verbatim into `boot_args`;
* each argument matches `[A-Za-z0-9_.\-]+` with an optional `=value` from `[A-Za-z0-9_.,\-]+`, and the
  merged line must stay within **1000 bytes** (DarwinBoot's buffer is 1024);
* the payload carries only the kernel, ADT, trust cache and Data seed - a display provider is linked into
  the kernel archive, not shipped as a payload file.

Measured on this board: the current line is 10 arguments / 171 bytes; adding the eight reference display
arguments (`offline-legacy-register`, `-gop`, `-unit-scale=2`, `-pm`, `-validate`, `-transfer`,
`-cpu-present`, `-phase=4`) gives 370 bytes, so the budget is not a constraint for the display step - the
reference board's 60-argument line would be, and only a subset is needed here.

## 19:10: the right driver page is xnu-drivers, not kexts

`/zh/drivers/kexts` does not cover this step: it describes staging external `.kext` bundles through
`UNIXNU_LOADER_PROTOCOL` revision 2, and states that the shipped DarwinBoot **rejects** a request carrying
staged kexts in favour of the built-in drivers.

`/zh/drivers/xnu-drivers` is the page that applies:

* a built-in XNU driver is a C++ IOKit class compiled to a static archive and linked with
  `-Wl,-force_load`, switched by a `CONFIG_XNU_<NAME>` defconfig entry that the kernel script turns into
  the force-load block, with its personality in `iokit/KernelConfigTables.cpp`;
* its reference table lists `SoftwareAES` as "the accelerator subclass with the original ABI" - the same
  original-ABI subclassing this port already uses for storage (`scripts/derive-storage-bindings.py` and
  `scripts/data/native-storage-21H461-original-bindings.S`), and what `build-gop-framebuffer.py` does with
  `original-bindings.o`;
* `/zh/porting/drivers` states the rule for private interfaces: record the class layout, vtable slots,
  selectors, argument structures and symbol bindings from the selected OS kernelcache - exactly the audit the
  legacy display builder performs against the 21H461 prelink.

One supporting record from the sheng variants: the legacy display builder "now refuses arm64e/iOS 26 before
reading original ABI addresses", i.e. it is iOS 17-only by construction, which is the OS this board targets.

## 19:20: the legacy display archive builds; the link needs the board probe layer

Continuing the display step on this board, the missing piece turned out to be smaller than expected and
is now precisely bounded.

**What already works**

* The 21H461 OS profile's `[kernel.prelink] roots` already names
  `com.apple.iokit.IOMobileGraphicsFamily`, so the original display family has always been inside
  `kernel.macho`.
* Using that prelink output as the audit's candidate (`kernel/kernel.macho` + `kernel.json` copied to
  `build/xnu-prelink/native-owner-mobile-graphics-candidate.{macho,json}`), `scripts/audit-mobile-graphics-legacy.py`
  **passes**: "original Legacy has 358 slots with nine abstract leaves".  The family `__TEXT_EXEC` hash is
  `2e044ee68c1650397e1435234ca6459206262e4c7ed709cb7131c6f664f9bd9b`, the same constant the audit pins.
  The QEMU observation was pruned from the reference and only its hash is carried, so
  `build/display-analysis/mobile-graphics-legacy-abi.json` was seeded with that recorded hash and the audit
  then wrote the full 48,970-byte ABI table (358 slots, abstract leaves 211/213/245/246/306/307/337/338/345).
* `scripts/build-gop-legacy-display.py` (reference, unmodified except for the toolchain) generates
  `build/native-legacy-display/OriginalLegacyFramebufferABI.h` (22,127 bytes), compiles
  `NativeLegacyDisplay.o` (384,400 bytes) and generates and compiles `original-bindings.S`/`.o`
  (23,130/11,904 bytes).
* The late object needs three manual steps that macOS `llvm-objcopy` does and Linux LLVM 14/15 cannot:
  `--remove-section=__DATA,__mod_init_func` (works), `--globalize-symbol=__GLOBAL__sub_I_NativeLegacyDisplay.cpp`
  (not supported for Mach-O; done by patching the symbol's `n_type |= N_EXT` in Python), then
  `llvm-ar rcs`.  Result: `libNativeLegacyDisplay.late.a` 407,816 bytes, sha256 `56403366...`, with a
  `build-report.json` the kernel consumer validates.
* `CONFIG_XNU_NATIVE_LEGACY_DISPLAY=y` must be applied through `./unixnu defconfig`; editing the defconfig
  alone does not regenerate `.config` and the flag silently stays out of the build.

**What still blocks the link**

With the archive force-loaded, the kernel link fails on twenty symbols the component expects from the board
package - the same role `package/LegacyDisplayProbe.cpp` (49 KB) plays in the reference:

```
_native_legacy_display_probe      _native_legacy_display_late_probe
_native_legacy_backend_guard      _native_legacy_client_enter / _observed
_native_legacy_swap_stage         _native_legacy_swap_validation_observed
_native_legacy_gop_mapping_observed  _native_legacy_pm_observed
_native_legacy_transfer_observed  _native_legacy_retirement_observed
_native_legacy_frame_observed / _frame_colors_observed / _copy_timing_observed
_native_legacy_surface_rejected   _native_legacy_gpu_offer / _prepare_scanout
_native_gpu_trace_enabled / _record  _native_adreno_fences / _fences_detail
```

Two of them are functional (`native_legacy_display_probe`, `native_legacy_display_late_probe`, the entry the
`offline-legacy-start` patch calls after `IOKitBSDInit`); the rest are diagnostics and GPU-handoff hooks that a
CPU-presentation port can stub.

**State on disk**

The defconfig flag and the eight `offline-legacy-*` arguments were reverted after the experiment so the
repository still builds; the tree keeps the generated header, objects and archive for the next step.

## 19:30: the reference section names the display assertion

The `/zh/reference` index is a hub; the page behind it that matters here is the troubleshooting table:

> | 用户态显示断言 | 显示 provider 和启动参数 | 检查显示驱动和 focus 参数 |
> - `reference/troubleshooting.md:66`

That row is exactly the SpringBoard failure ("failed to find the main CADisplay"): the official advice is to
check the display provider and the boot arguments, specifically the focus arguments.

The focus arguments are trace selectors, not a fix: `xnu/boot-arguments.md:89` defines
`boot-ui-focus=<name>`, and `guide/debugging.md:288-291` shows the pair form
(`boot-ui-focus=launchd`, `boot-ui-focus2=SpringBoard`, `boot-ui-focus3=backboardd`,
`boot-ui-focus4=Setup`) used with `boot-ui-trace=1` to capture a process's boot UI stack.  They are the
documented way to record the assertion's userland frames once the provider exists.

The other reference pages that apply:

* `reference/components.md:28` - `legacy_name` is the "official compatible input name", which is why
  `build-arm-driver.py NativeLegacyDisplay` resolves the `GopFramebuffer` catalog entry; line 51 says
  `component:<ID>` positions a component in `[kernel] archives`, which only takes effect for a component whose
  catalog record has a build recipe (GopFramebuffer has `build = []`, hence a board script).
* `reference/output-layout.md:85` - every `[kernel] archives` script logs to `archive-NN-<name>.log`, the
  place to read the failed display link and its inputs.
* `reference/qemu.md:52-53` - `--no-gop` and `--display-size WxH` let the display path be exercised on the
  host before touching the phone.
* `reference/boot-configuration.md`, `configuration.md`, `cli.md`, `glossary.md` cover kext bundles and archive
  semantics only; nothing about the display provider.

## 19:40: what the UniXNU account actually carries for this stage

Surveyed all 32 repositories under `github.com/UniXNU` (trees API):

| Repository | Display material |
| --- | --- |
| `GopFramebuffer` | the component only: `NativeMobileDisplay.cpp`, `NativeLegacyDisplay.cpp`, `NativeGOPPresenter.cpp`, `NativeGOPWire.h`, `NativeLegacyRequest.h`; no board probe |
| `UniXNU-sheng` | the only board-side probe in the organisation: `package/LegacyDisplayProbe.cpp` (49 KB), `OfflineGOPComposition.mm`, `OfflineGOPPresentation.mm`, `OfflineGOPResources.mm`, plus `scripts/build-gop-framebuffer.py`, `build-gop-legacy-display.py`, `build-gop-legacy-display-27.py`, `legacy-display-24A446.json`, `legacy-display-26C5035r.json` |
| `UniXNU` (main) | the qualification tooling for this exact stage: `scripts/check-native-legacy-display.py`, `scripts/probe-native-legacy-display.py`, `scripts/check-gop-handoff-checkpoints.py` |

There is no generic board probe; a new board ports the sheng one.

`check-native-legacy-display.py` states the evidence contract: the build report needs
`generated_abi_sha256`, `compiled_object_sha256`, `late_object_sha256`, `archive_sha256`, `source_sha256`,
`virtual_entries == 358` and `owned_entries == [0,1,7,84,211,213,245,246,288,306,307,337,338,345]`; then seven
bounded QEMU observations (`build/darwinboot-owner-hvf-legacy-display-*`) must show the class initialising with
object size 0x18a0, the GOP mapping and mode getter, and the "original pre-publication boundary".  Those runs use
HVF, i.e. a macOS host, so this leg cannot be reproduced from this Linux/WSL host.

The same checker also records the reference's own conclusion: the untouched original start path refuses with
`start_hardware failed` because of the real IOSurface dependency, so `display_backend_complete=False`,
`presentation=False`, `desktop=False`.  A board must therefore own the backend, which is what
`NativeLegacyDisplay.cpp` plus a board probe provide.

Our own probe (`package/LegacyDisplayProbe.cpp`, 5 KB) now implements the twenty symbols the late object needs:
the two entries (`native_legacy_display_probe`, `native_legacy_display_late_probe`) do the metaclass
registration, validate the `PE_current_console` mode (32-bit, unrotated, sane stride) and allocate the original
class; the diagnostics are no-ops; `native_legacy_backend_guard` panics with the slot number instead of
returning; the GPU trace and Adreno fence hooks report "no GPU".  It compiles with the kernel's own recorded
command line (197,024 bytes) and joins the late archive, which is now 605,832 bytes
(sha256 `c1632378...`).

## 19:50: the display pipeline links, boots and runs on the phone

With the board probe in the archive the kernel links (previous attempt stopped on twenty undefined
symbols, then on three: the two entries needed C linkage and `native_legacy_vsync_observed` was missing).
`bootEU.img` `04c4377c...` (payload `8f79cb36...`) boots and prints:

```
DISPLAY: probe entered (registered=0)
DISPLAY: GOP console 0x9c000001 1080x480 depth=32 stride=4320
DISPLAY: allocated NativeLegacyDisplay size=0x1ae0 legacy=0 mobilefb=0
```

What that says:

* The entry the `offline-legacy-start` patch calls after `IOKitBSDInit` really runs, and the metaclass
  registration (`preModLoad` -> static init -> `postModLoad`) returns zero.
* `PE_current_console` hands over the port's 480-row window (1080x480, stride 4320, base with the tag
  bit set) rather than the full 1440x3168 panel - the same 2 MiB hand-off the boot chain has used since
  2026-11-10.  The reference's QEMU fixture sees 800x600.
* `native_legacy_display_allocate()` returns an object, but it is **not** the original class:
  `metaCast("IOMobileFramebufferLegacy")` and `metaCast("IOMobileFramebuffer")` both return zero and its
  metaclass size is 0x1ae0, where the reference records 0x18a0 with `parent_match` 1.  Nothing is
  published, so the userland side is unchanged: `watchdogd`, `liveactivitiesd` and `SpringBoard` trap at
  exactly the same addresses as before this change.

So the link and the boot-thread entry are proven; the open problem is now concrete: give the owned class
the original IOMobileFramebuffer identity (the parent metaclass from the prelinked
`com.apple.iokit.IOMobileGraphicsFamily`) and publish the service.

```
kernel: linked with XNU_NATIVE_LEGACY_DISPLAY=1, late archive 605,832 bytes sha256 c1632378...
kernel.macho differs from the 4f0dea31 build (the archive is force-loaded)
payload 8f79cb36..., bootEU.img 04c4377c..., 183,005,184 bytes
console: instantnoodlep-artifacts/console-bootEW.log (453 lines, same three traps as before)
```

The defconfig switch and the eight `offline-legacy-*` arguments are reverted after the run so a clean
checkout still builds; the tree keeps the generated header, objects, archive and report.  Re-enabling is:
audit -> builder -> board probe compile -> archive -> `unixnu defconfig` -> `build kernel`.

## 20:00: the family is prelinked but never registers its classes

The first on-device runs of the probe narrowed the display problem to the original kext's registration:

```
DISPLAY: probe entered (registered=0)
DISPLAY: GOP console 0x9c000001 1080x480 depth=32 stride=4320
DISPLAY: allocated NativeLegacyDisplay size=0x1ae0 legacy=0 mobilefb=0
DISPLAY: bound parent meta=0xfffffff006b35940 raw=0 0 0 0
DISPLAY: registered family meta=0 name=- size=0x0
DISPLAY: meta[0] NativeLegacyDisplay size=0x1ae0 super=
DISPLAY: meta[1]  size=0x0 super=-
```

* `original-bindings.S` binds `__ZN25IOMobileFramebufferLegacy10gMetaClassE` to `0xfffffff006b35940`,
  and the kernel really exports that absolute symbol, so the binding is right.
* The object behind it is **all zeros** at runtime, and `OSMetaClass::getMetaClassWithName("IOMobileFramebufferLegacy")`
  returns null even from a kernel thread that retries 5 s after `offline-legacy-start` (`DISPLAY: retry after 5 s`
  runs before the same zeros), so this is not an ordering race with IOKit.
* The prelink report shows the family is completely included: it appears in `components` (30), in
  `codeless_components` (none), in `module_entrypoints` with kmod_info/start/stop addresses, and its four
  extents (`__TEXT` 0xfffffff0045f5780+0x9708, `__TEXT_EXEC` 0xfffffff00576c000+0x36f74, `__DATA_CONST`
  0xfffffff005f33840+0x63c0, `__DATA` 0xfffffff006b2e6c0+0x1f2c8) cover the metaclass address
  `0xfffffff006b35940`.

So the image carries the family's code and data, but the kext's classes are never registered, which means XNU
does not load that prelinked kext - the metaclass is filled by its static initialiser at load time and stays
zero otherwise.  The next measurement is the prelink's `__PRELINK_INFO` entry for the family against a
candidate the reference tooling accepts (`scripts/probe-native-legacy-display.py` loads
`build/xnu-prelink/native-owner-legacy-display-<profile>-candidate`), i.e. whether `emit_prelink.py` writes the
`_PrelinkExecutable*` keys XNU needs for this bundle.

The probe itself is now a working instrument: it prints the GOP mode, the allocation, the bound parent
metaclass, the registered family metaclass and the whole metaclass chain.  Console:
`docs/evidence/logs/2026-11-11-bootEX-console.log`, `...bootEY...`, `...bootEZ...`.

## 20:20: the APFS "class open" line

A live read of the serial port caught one line that earlier captures did not have:

```
apfs_is_valid_class:2395: disk5s1 rejecting class open (class 2) because we're not content protected
```

What the same log says about that disk, in order:

```
SEL-DIAG: disk5s1 seedInfo=1 base=0 bytes=3221225472
apfs_log_op_with_proc:3091: disk5s1 mounting volume OwnerData, requested by: init (pid 1)
handle_mount:817: disk5s1 vol-uuid: E553E004-A6C6-4992-B474-25EB0F160524 block size: 4096
   block count: 786432 (unencrypted; flags: 0x1; features: 1.0.2)
BLK: disk5s1 size=3221225472 uuid=E553E004-A6C6-4992-B474-25EB0F160524
```

So `disk5s1` is the 3 GiB `OwnerData` volume, and APFS itself reports it as **unencrypted, flags 0x1**.
The rejecting function, `apfs_is_valid_class()`, sits next to the other content-protection strings in the
APFS kext ("cannot roll non-content protected volume", "cannot create xattr crypto for non-content protected
volume", "cannot lock inode on non-content protected volumes", and
`((cp_key_class_t)(0x0000001f & (skey_get_class(key)))) == key_out_class`), so "class" here is a
`cp_key_class_t` (five bits, values 0-31) and "class 2" is one concrete content-protection class.

**What it means:** a caller opened a file on Data and asked APFS to apply content-protection class 2; because
the volume carries no content-protection wrapper, APFS refuses instead of serving the file with that class.
Everything else on the volume works: it mounts, reads, writes, and the services that use it keep running.

**Frequency:** one occurrence in a 60 s window, none in the earlier `bootEW`/`bootEX`/`bootEY`/`bootEZ`
captures, so it is a one-shot attempt rather than a loop.  The line carries the function name but not the
pid, and no `apfs_log_op_with_proc` line is adjacent, so the caller cannot be named from the console alone.

**Why this board sees it:** the Data seed (`owner-data-3g.dbr`) is a plain APFS volume we generate; a real
device's Data volume is content-protection wrapped and its class keys come from AppleKeyStore.  A plain volume
therefore rejects any class-scoped open by construction.

**Options, in order of cost:** accept and record it (it does not stop the boot); instrument
`apfs_is_valid_class` in the prelinked kext to print the calling process before deciding whether it matters; or
build a content-protected Data volume with real class keys, which needs the keystore/keybag path this port does
not have yet.

## 20:40: the family just needed to be asked for

The open question from the previous section - why the prelinked `com.apple.iokit.IOMobileGraphicsFamily`
never registers its classes - is answered by the prelink entry itself:

| Bundle | `OSBundleRequired` |
| --- | --- |
| `com.apple.driver.AppleMobileFileIntegrity` | `Root` |
| `com.apple.iokit.IOSurface` | `Console` |
| `com.apple.filesystems.apfs` | `Local-Root` |
| `com.apple.iokit.IOMobileGraphicsFamily` | **absent** |

All the `_Prelink*` keys are present for the family (`_PrelinkExecutableLoadAddr` 0xfffffff0045f5780,
`_PrelinkExecutableSize` 0x9708, `_PrelinkExecutableSourceAddr`, `_PrelinkKmodInfo` 0xfffffff006b2e6c0,
bundle path and relative executable path), so the image is complete; the family simply has no
`OSBundleRequired`, and XNU only loads such kexts when something asks for them.  Nothing in this port did.

The probe now calls `OSKext::loadKextWithIdentifier("com.apple.iokit.IOMobileGraphicsFamily")` before it
registers its own class, and that is all it needed:

```
DISPLAY: load family kext rc=0x0 loaded=1
DISPLAY: allocated NativeLegacyDisplay size=0x1ae0 legacy=1 mobilefb=1
DISPLAY: registerered family meta=0xfffffff006b35940 name=IOMobileFramebufferLegacy size=0x18a0
DISPLAY: meta[0] NativeLegacyDisplay size=0x1ae0 super=IOMobileFramebufferLegacy
DISPLAY: meta[1] IOMobileFramebufferLegacy size=0x18a0 super=IOMobileFramebufferService
DISPLAY: meta[2] IOMobileFramebufferService size=0x90 super=IOMobileFramebuffer
DISPLAY: meta[3] IOMobileFramebuffer size=0x90 super=IOService
DISPLAY: meta[4] IOService size=0x88 super=IORegistryEntry
DISPLAY: meta[5] IORegistryEntry size=0x28 super=OSObject
DISPLAY: meta[6] OSObject size=0x10 super=-
```

* `loadKextWithIdentifier` returns 0 and `isKextWithIdentifierLoaded` reports 1.
* The original class registers with the exact size the reference records (0x18a0) and its full ancestry
  (`IOMobileFramebufferService` -> `IOMobileFramebuffer` -> `IOService`), so the owned class now answers both
  `metaCast("IOMobileFramebufferLegacy")` and `metaCast("IOMobileFramebuffer")` (`legacy=1 mobilefb=1`).
* Our concrete class is 0x1ae0 because `NativeLegacyDisplay` adds its own members on top of the 0x18a0 base;
  the base size is what the reference's check compares.

The userland side is unchanged in this boot: SpringBoard still traps in
`-[FBSDisplayMonitor _initWithBookendObserver:transformer:] + 1148` because no display service is published
yet - the probe stops after allocating.  The next step is `init`/`start`/`registerService` with the GOP
console as the provider, which is what turns the allocated object into the `CADisplay` QuartzCore looks for.
Console: `docs/evidence/logs/2026-11-11-bootFA-console.log`.

## 21:00: the display publishes, and SpringBoard gets past the CADisplay assertion

The probe now runs the whole publication sequence the reference uses at `offline-legacy-phase=4`:

```
DISPLAY: init ok phase=4, retained for publication
DISPLAY: IOSurface kext rc=0x0
DISPLAY: IOSurface root found=1
DISPLAY: nub 0xfffffff9e36876a0 attach=1 start=1 mem=0x9c000001+0x1fa400
DISPLAY: dt attach=1
DISPLAY: display attach=1 start=1
```

That is: the family is loaded on demand, the owned class registers with the original identity (0x18a0 base,
`IOMobileFramebufferLegacy` -> `IOMobileFramebufferService` -> `IOMobileFramebuffer` -> `IOService`), a nub is
built over the firmware framebuffer memory (`0x9c000001 + 0x1fa400`), attached to the resource service and to
`/arm-io` as `gop-display`/`display-subsystem`, and the display attaches to it and starts.

**The effect on SpringBoard is visible in the same boot:** the previous image (`bootFA`) logged eighteen
SpringBoard lines and trapped every instance at `FrontBoardServices+0x1e2ec`
(`-[FBSDisplayMonitor _initWithBookendObserver:transformer:]`, "failed to find the main CADisplay").  This
image logs no `+0x1e2ec` trap at all.  SpringBoard now loads one more image (1236 against 1235) and dies at a
different site:

```
XNU-TRAP: pc=0x1a05a4744 lr=0x1a05a4744 esr=0xf2000000
XNU-DYLD-IMAGE: SpringBoard pc=0x1a05a4744 FrontBoardServices+0xf744 load=0x1a0595000 images=1236
0x19f3d0744: -[FBSDisplayMode _initWithWidth:height:preferredScale:scaleOverride:refreshRate:
             gamut:hdr:rotation:virtual:validityCheck:] + 580
```

So the display is found, and the next wall is the display **mode** the firmware framebuffer implies: our console
window is 1080x480 (the 2 MiB 480-row hand-off), while the panel is 1440x3168, and FBSDisplayMode refuses the
values it is handed.  The next step is to make the published display report a valid mode - real panel geometry,
scale and refresh - instead of the kernel console's 480-row window.

## 21:20: the scale lands, the family calls an abstract slot, and the device drops off USB

Two more boots with the published display.

**bootFC** - with `display-scale` published into `/chosen`:

```
DISPLAY: nub 0xfffffff501809a20 attach=1 start=1 mem=0x9c000001+0x1fa400
DISPLAY: dt attach=1
DISPLAY: chosen display-scale=2 rotation=0 ok=1
DISPLAY: display attach=1 start=1
```

The boot then panicked in our own guard:

```
DISPLAY: unimplemented legacy slot 345 (object=0xfffffff334e75000 caller=0xfffffff00578c1cc)
instantnoodlep legacy display slot 345 is not implemented @LegacyDisplayProbe.cpp:274
```

The caller `0xfffffff00578c1cc` lies in the prelinked family's `__TEXT_EXEC`
(`0xfffffff00576c000 + 0x36f74`), so the original code asked the concrete backend for slot 345.  In the ABI
table slots 337, 338 and 345 all point at `___cxa_pure_virtual` (0xfffffff00c0098ac) and are marked abstract,
i.e. they are leaves only the owned backend can implement - the reference's QEMU observations recorded
`guard_slot = 0`, so this path is new for this board.

**bootFD** - the guard now logs the first eight hits and returns instead of panicking:

```
DISPLAY: chosen display-scale=2 rotation=0 ok=1
DISPLAY: display attach=1 start=1
```

No panic, 461 console lines, six traps, and in that window the only owners are `watchdogd+0xab0c` and
`SessionSyncEngine+0x1d4f8` - no SpringBoard trap at all (`bootFA`, the image from before the display was
published, had eighteen SpringBoard lines).  A later live read could not confirm how long that holds: after
`bootFD` the USB console disappeared (COM14 gone, `fastboot devices` and `adb devices` both empty), so the phone
needs a manual reboot before the next measurement.

**Next:** identify slot 345 - disassemble the family around `0xfffffff00578c1cc` to see which interface it
expects the backend to answer, implement it in the probe instead of returning silently, and re-run.

## 21:40: the panel shows the display stack using our service, then a NULL metaclass teardown

The panic screen from the relaxed-guard build carries four useful lines above the panic block:

```
AFB: default surface: No Default FB Surface found. Return error
IOMFB: swap_wait_gated: CommandMake didn't happen even after 5 seconds.
       Seems system in bad state, cancelling swaps
DISPLAY: unimplemented legacy slot 345 (object=0xfffffff44ac0b000 caller=0xfffffff00578c1cc), returning
panic(cpu 0 caller 0xfffffff00c00b2d0): Kernel data abort. at pc 0xfffffff00be80944, lr 0xfffffff00bfe4684
  x0: 0  x1: 0  esr: 0x96000045  far: 0x0
```

Symbolised against the built kernel:

| Address | Symbol |
| --- | --- |
| `0xfffffff00be80944` (pc) | `OSMetaClass::~OSMetaClass() + 0x1c` |
| `0xfffffff00bfe4684` (lr) | `NativeLegacyDisplay::MetaClass::~MetaClass()` |
| `0xfffffff00c00b2d0` (caller) | `_panic_with_thread_kernel_state` |
| `0xfffffff00578c1cc` (slot-345 caller) | `IOMobileFramebufferLegacy::opaque243() + 0xd7c` |

So the display stack really reached our published service: it asked the family for the **default framebuffer
surface** ("No Default FB Surface found. Return error"), the compositor's swap then waited five seconds for a
command that never came and cancelled, the family asked the backend for slot 345, and after that returned the
teardown ran into a NULL `this` in `OSMetaClass::~OSMetaClass` (`far = 0`, `x0 = 0`) through our own metaclass
destructor.

"Default FB Surface" is exactly the piece `porting/display.md` documents for this class:

> The 24A446 adapter supplies the default surface through the original IOSurfaceRoot factory and validates its
> backing memory and geometry before transferring ownership to the display family. The family's
> `create_default_fb_surface()` slot returns void; its residual return register is not an object pointer.

So slot 345 is the default-framebuffer-surface leaf: returning without a surface is what leaves the family with
a null object and the later metaclass teardown crashes.  The next step is to implement it - create the surface
through the real IOSurfaceRoot factory over the GOP nub's memory, validate its geometry, hand ownership to the
display - and to keep the guard's return from being reachable in that path.

## 21:55: where the default surface is supposed to come from

The component already contains the hook for the missing piece, but no implementation of it exists anywhere in
the open-source tree:

```cpp
/* NativeLegacyDisplay.cpp, inside startHardware() after the GOP mapping check */
#if defined(NATIVE_LEGACY_DEFAULT_SURFACE) && NATIVE_LEGACY_DEFAULT_SURFACE
    // The family calls its void slot 316 after startHardware. Our backend supplies the client's default surface.
    if (valid) valid = installDefaultSurface(width, height);
#endif
```

* `installDefaultSurface(uint32_t, uint32_t)` is only *called*; no definition exists in the component, in the
  reference board package (`package/LegacyDisplayProbe.cpp`), in the reference scripts, or in the UniXNU
  tooling.  The macro `NATIVE_LEGACY_DEFAULT_SURFACE` appears nowhere else either, so the reference builds
  without it - which matches its own records: the desktop was never reached there either.
* The device records of the iOS 26 boards describe what the implementation must do: "The 24A446 adapter creates
  a BGRA IOSurface through the original IOSurfaceRoot factory, checks allocation and geometry, and transfers
  its reference to the family default-surface field. The void `create_default_fb_surface` slot is invoked by
  the original start method."

So the next piece of work is ours to write, in the board probe:

1. compile `NativeLegacyDisplay.cpp` with `-DNATIVE_LEGACY_DEFAULT_SURFACE=1`;
2. implement `installDefaultSurface(width, height)`: create a BGRA surface through the original IOSurfaceRoot
   factory over the nub's framebuffer memory, validate allocation, stride, pixel format and geometry;
3. transfer the reference into the family's default-surface field (the offset has to be derived from the
   family's own `create_default_fb_surface`/`startHardware` code - the generated header only carries the
   dimension and swap-queue offsets);
4. keep the abstract-slot guard unreachable on that path.

The phone is on the panic screen from the previous run (`OSMetaClass::~OSMetaClass` with a null `this`), its
serial console is gone and neither `fastboot` nor `adb` sees it, so no further device measurement is possible
until it is rebooted by hand.

## 22:10: the published display panics about two minutes in, deterministically

Re-running the relaxed-guard image (the one that boots with `display-scale` published) reproduces the same
crash every time, about two minutes after hand-off:

```
AFB: default surface: No Default FB Surface found. Return error
IOMFB: swap_wait_gated: CommandMake didn't happen even after 5 seconds. Seems system in bad state, cancelling swaps
DISPLAY: unimplemented legacy slot 345 (...), returning
panic(cpu 0 caller 0xfffffff00c00b2d0): Kernel data abort.
  at pc 0xfffffff00be80944 (OSMetaClass::~OSMetaClass + 0x1c)
  lr 0xfffffff00bfe4684 (NativeLegacyDisplay::MetaClass::~MetaClass), far 0x0
```

The two capture windows show the same shape: no SpringBoard trap at all before the panic (the crash takes the
machine down first), only `watchdogd+0xab0c` and `SessionSyncEngine+0x1d4f8`.

So the display is real and the compositor uses it - `AFB` asks for the default framebuffer surface, `IOMFB`
waits for a command that never comes and cancels the swap - but because the backend never supplies that surface,
the family ends up tearing down a null object.  The wall is exactly the one from the previous section:

* the component's `NATIVE_LEGACY_DEFAULT_SURFACE` branch calls `installDefaultSurface(width, height)`, which no
  source in the tree implements;
* the device records of the 26-series boards describe the required body: create a BGRA surface through the
  original IOSurfaceRoot factory, check allocation and geometry, transfer the reference into the family's
  default-surface field;
* the kernel's `IOSurface` class is not exposed as headers in the target SDK, so the board has to reach it the
  same way the display family does - through the original bindings - and the family's own
  `create_default_fb_surface` (`startHardware` calls it as a void slot) is the reference for what to build.

Until that body exists, the choices are: keep the display published and accept the panic roughly two minutes in
(useful, because the panel proves the stack is using the service), or revert to the pre-publication image, which
does not panic but leaves SpringBoard crash-looping on the missing CADisplay.

## 22:20: what the official repository says about the default surface

Yes - the official documentation describes this piece, in two places:

* `docs/porting/display.md` (lines 65-69):

  > The 24A446 adapter supplies the default surface through the original IOSurfaceRoot factory and validates its
  > backing memory and geometry before transferring ownership to the display family. The family's
  > `create_default_fb_surface()` slot returns void; its residual return register is not an object pointer.

* The same sentence appears as a finding in about fifteen device records (`fuxi`, `sheng-ad1095445`,
  `sheng-airalex002`, `sheng-bai805`, `sheng-chenjin1494`, `sheng-enlixk`, `sheng-gkxqh`, `sheng-jsongmax`, ...),
  each with the implementation contract: "creates a BGRA IOSurface through the original IOSurfaceRoot factory,
  checks allocation and geometry, and transfers its reference to the family default-surface field.  The void
  `create_default_fb_surface` slot is invoked by the original start method."

But the same records also state the limit (`fuxi.md:320` and the others):

> The 24A446 default-surface adapter still requires a hardware boot result.

and there is no implementation of it anywhere in the open repositories: `installDefaultSurface` has no
definition in the component, in the reference board package (`package/LegacyDisplayProbe.cpp`,
`OfflineGOPComposition.mm`, `OfflineGOPPresentation.mm`, `OfflineGOPResources.mm`), in its scripts, or in the
UniXNU tooling, and `NATIVE_LEGACY_DEFAULT_SURFACE` only ever appears in the component's `#if`.

So the documentation defines what the adapter must do and where it plugs in (BGRA surface from the original
IOSurfaceRoot factory, geometry validation, transfer into the family's default-surface field, invoked from the
original start method, and the warning that the void slot's residual return register is not a pointer), while
this board has to write the implementation.  Our panic is exactly the documented gap: the family asks for the
default surface, none exists, and the swap is cancelled.

Kernel-side string work for the next step: the message "No Default FB Surface found" lives in the family's
`__TEXT` at `0xfffffff0045f6ad7`; it has no `adrp`+`add` cross-reference in the image, so the field offset has
to be found another way (the referencing code loads it through a literal pool, or the check is inlined into the
original start method).

## 22:35: static reverse-engineering of the default-surface field reaches its limit

What the disassembly established, in order:

* slot 288 (`0xfffffff00579278c`) is the original `startHardware` and is a stub:
  `sub sp; str x0; mov w8, #0; and w0, w8, #1; ret` - it returns false, which is exactly the
  "start_hardware failed" the reference records.  Our component overrides it.
* slot 316 (`0xfffffff00579ea44`) is not a surface factory either: it stores `x0` and one byte and returns.
* the code that asked for slot 345 is the family's own `opaque243`, through the ordinary vtable dispatch
  `ldr x8, [x0]; ldr x8, [x8, #2760]` (#2760 = 345 * 8), and the instructions after the call read fields at
  +5844/+5848 - mode geometry, not a surface reference.
* `ldr [x, #2528]` (#2528 = 316 * 8) appears three times, and the one that branches is part of a stub island
  (`adrp x16; ldr x16; br x16` repeated per slot), so it names the slot's implementation, not its caller.
* the family's message "No Default FB Surface found" lives at `0xfffffff0045f6ad7` inside the family's
  `__TEXT`, and has no `adrp`+`add` cross-reference and no literal pointer to it in the image, so the code that
  emits it cannot be reached by those two scans.

So the field offset the documentation calls "the family default-surface field" is not statically obvious: the
official records describe the contract but publish no offset, the reference never implemented the adapter
("still requires a hardware boot result"), and the family's consumer of that field is not reachable with the
scan patterns that worked for the CADisplay assertion.

Next approaches, in order of cost:

1. extend the ABI audit to record field offsets directly (it already derives the vtable, dimensions and queue
   offsets); the audit reads the same prelinked class, so the offset can be taken from the family's own
   `create_default_fb_surface`/`start` code once that function is identified by slot;
2. dynamic: after start, print the qwords of the display object whose pointee's metaclass name is "IOSurface" -
   an empty scan now, but it tells which offsets carry surfaces once any exist, and can be repeated with the
   adapter writing a placeholder;
3. hook the family's error path: the message is printed from a known string; a temporary printf inside
   `NativeLegacyDisplay::start` after the family's start returns can at least show which fields changed.

## 22:45: the organisation publishes the contract, not the adapter

Checked every place under `github.com/UniXNU` that could carry the default-surface implementation:

| Repository / path | Result |
| --- | --- |
| `UniXNU/docs/porting/display.md` (65-69) | the only prose contract: the 24A446 adapter, the original IOSurfaceRoot factory, the void `create_default_fb_surface()` slot, "its residual return register is not an object pointer" |
| `UniXNU/docs/devices/*.md` (~15 records) | the same sentence plus the limit "The 24A446 default-surface adapter still requires a hardware boot result" |
| `GopFramebuffer/NativeLegacyDisplay.cpp` (pinned 897 lines and upstream `main` 1230 lines) | exactly one `installDefaultSurface` occurrence in both: the call inside `#if NATIVE_LEGACY_DEFAULT_SURFACE`; no definition anywhere |
| `UniXNU-sheng/package` (`LegacyDisplayProbe.cpp`, `OfflineGOPComposition.mm`, `OfflineGOPPresentation.mm`, `OfflineGOPResources.mm`) | no mention |
| `UniXNU/scripts` (`check-native-legacy-display.py`, `check-gop-handoff-checkpoints.py`, `check-mobile-display-provider.py`, `check-mobile-graphics-checkpoints.py`, `check-original-mode-validation.py`) | no mention; the mobile-display checker even prints "display publication and presentation remain incomplete" |
| `GopFramebuffer/README.md`, `AGENTS.md`, `Test/` | describe swap formats only |

So the field offset and the adapter body are published nowhere; the two doc places define what it must do and record
that it was never proven on hardware.  Also noted: our catalog pin `e47c1b8` is 897 lines where upstream `main` is
1230, so a later revision of the component exists (still without the adapter) and is worth re-pinning once the
surface work starts.

## 22:55: what the prelink page does and does not cover

`/zh/concepts/prelink` explains the mechanism this port relies on, and nothing about the default surface:

* inputs: the native kernel, the original kernelcache, `[kernel.prelink] roots`, slide and early-security
  initialiser;
* steps: parse both Mach-Os, read `__PRELINK_INFO`, select the roots and their kernel interfaces, copy each
  component's segments into the merged ranges, rebind pointers, apply the slide, rewrite module headers and kmod
  entries, build the sparse index, write `kernel.macho`;
* "输出保留原始组件代码不变" - the invariant that lets this board bind the family's methods by absolute address;
* the report next to the output lists `components`, `kernel_interfaces`, `bindings` (`new_site` ->
  `new_target`), `instruction_inventory`, `module_entrypoints` and `early_security_initializers`.

That report is where our `original-bindings.S` addresses and the family's extents came from, so the page is the
reason the ownership work could be done at all.  It does not list object field offsets, and the private-ABI rule
in `porting/drivers.md` (line 70) confirms the scope: record the class layout, the virtual-method slots, the
selectors, the argument structures and the symbol bindings from the selected kernelcache - object fields are not
part of the contract.  That is why the family's default-surface field has no published offset and has to be
measured on this board.

## 23:05: what the boot-configuration reference offers

`/zh/reference/boot-configuration` describes the payload's `unixnu.toml`: menus ([boot], [ui]), drivers, dt patches,
block slices, ACPI, kits, and per-entry keys (`args`, `initrd`, `confirm`/`confirm_args`, `[[entry.kext]]`,
`[[entry.variable]]`, `[[entry.dt_patch]]`).  The one piece that touches our display work is:

> `[entry.chosen]` - 每个键成为 `/chosen` 属性，在菜单项补丁之后应用

That is the declarative counterpart of what the probe does at runtime when it publishes `display-scale` and
`display-rotation` into `/chosen`.  Two limits apply: the page states that `[[dt_patch]]` applies to EFI and Linux
entries while the Darwin path uses the `.adt` sidecar, so for XNU those properties come from the sidecar
generator (`[payload.adt]`) or, as now, from the board probe; and nothing on the page concerns the family's
default-surface field.

So the page is not an answer to the current wall, but it does point at the tidy way to retire one of our probe's
responsibilities later: declare `display-scale`/`display-rotation` in the entry's `[entry.chosen]` (or in the ADT
generator) instead of writing them from `native_legacy_gop_mapping_observed`.

## 23:15: the arch patches already carry the load-and-register flow

`offline-component-load-probes.patch` (the second row of the patch reference's owner-root table) contains the
mobile-graphics machinery this board re-implemented by hand:

```c
static void native_mobile_graphics_load_probe(void) {
    int enabled = 0;
    if (!PE_parse_boot_argn("offline-mobile-graphics-load", &enabled, sizeof(enabled)) || !enabled) return;
    native_mobile_graphics_load_status = OSKextLoadKextWithIdentifier("com.apple.iokit.IOMobileGraphicsFamily");
#if defined(XNU_NATIVE_LEGACY_DISPLAY) && XNU_NATIVE_LEGACY_DISPLAY
    int legacy_register = 0;
    if (native_mobile_graphics_load_status == 0 &&
        PE_parse_boot_argn("offline-legacy-register", &legacy_register, ...) && legacy_register) {
        extern void native_legacy_display_probe(void);
        native_legacy_display_probe();
    }
#endif
    int client_probe = 0;
    if (native_mobile_graphics_load_status == 0 &&
        PE_parse_boot_argn("offline-mobile-graphics-client", &client_probe, ...) && client_probe) {
        extern void native_mobile_graphics_client_probe(void);
        native_mobile_graphics_client_probe();
    }
}
```

(it runs from `bsd_init()` right after `vfsinit()`, and the same patch's `IOUserClient.cpp` hunk defines
`native_mobile_graphics_client_probe()`: it checks that `IOMobileFramebuffer`, `IOMobileFramebufferService` and
`IOMobileFramebufferLegacy` exist as classes, allocates `IOMobileFramebufferUserClient`, calls `init()` and then
`externalMethod` with selectors 90, 0x80000000 and ~0, and finally calls `native_legacy_display_probe()` again.)

So the official path is boot arguments, not code:

| argument | effect |
| --- | --- |
| `offline-mobile-graphics-load=1` | loads the family at `bsd_init`, after `vfsinit` |
| `offline-legacy-register=1` (with `XNU_NATIVE_LEGACY_DISPLAY`) | calls the board's `native_legacy_display_probe()` right after the load |
| `offline-mobile-graphics-client=1` | runs the client probe (user client allocation and the selector-90 call) and calls the board entry again |

There is still nothing about the default surface: no `IOSurface` call, no `create_default_fb_surface`, no field
transfer anywhere in the patch series.  But this changes two things for us:

1. our probe's hand-rolled `OSKext::loadKextWithIdentifier` and five-second retry thread duplicate
   `offline-mobile-graphics-load`; the intended flow loads the family and calls the entry **once**, synchronously,
   after `vfsinit` (i.e. later than `bsd_autoconf`, which is where `offline-legacy-start` runs it now);
2. the client probe shows the selector the platform uses on the framebuffer user client (90, then 0x80000000, then
   ~0), which is worth watching when the default-surface path is exercised.

Boot arguments for the next run therefore become:
`offline-mobile-graphics-load=1 offline-legacy-register=1 offline-legacy-gop=1 offline-legacy-unit-scale=2
offline-legacy-phase=4` (the rest of the display set unchanged).

## 23:30: the probe now follows the arch patch instead of loading the family itself

Changes for the next boot:

* `package/LegacyDisplayProbe.cpp` no longer calls `OSKext::loadKextWithIdentifier` and no longer spawns the
  five-second retry thread.  `native_legacy_display_probe()` now does the whole sequence when the kernel's
  `offline-mobile-graphics-load` probe calls it (family already loaded, class registered, GOP console read,
  object allocated and started, published on the nub); `native_legacy_display_late_probe()` only records that
  `offline-legacy-start` ran earlier, because `bsd_autoconf` happens before `bsd_init`.
* `board.toml` gains `offline-mobile-graphics-load=1`; the display argument set is now 19 arguments, 401 bytes
  of the 1000-byte budget.
* The image is built and waiting: `instantnoodlep-artifacts/bootEU.img`, sha256
  `6af5801edd3fd7ee6636c5f85f6caf697efa6b66bb57f4d580aad214b2d22162`.

The expectation for this boot, if the display stack behaves as before: the family is loaded by the patch at
`bsd_init` (later than the old `bsd_autoconf` call), the probe publishes the service in the same call, and
`AFB`/`IOMFB` still ask for the default surface, which is not implemented yet - so the same two-minute panic
should be expected unless `offline-mobile-graphics-client=1` changes the surface path.  The run is still worth it:
it removes our hand-rolled load, puts the probe on the documented timing, and shows whether the client probe's
selector-90 call reaches the default-surface code.

The phone is powered down (no `fastboot` entry, no `COM14`), so the image cannot be booted until it is started.

## 23:45: the official load flow works and lands on the same wall

The image with the threaded publication (sha `e44f8753...`) boots through the storage root and the display chain:

```
DISPLAY: probe entered (registered=0)
DISPLAY: allocated NativeLegacyDisplay size=0x1ae0 legacy=1 mobilefb=1
DISPLAY: registered family meta=0xfffffff006b35940 name=IOMobileFramebufferLegacy size=0x18a0
DISPLAY: init ok phase=4
DISPLAY: late probe (load handled by offline-mobile-graphics-load)
DISPLAY: IOSurface kext rc=0x0
DISPLAY: IOSurface root found=1
DISPLAY: nub 0xfffffff38273c7e0 attach=1 start=1 mem=0x9c000001+0x1fa400
DISPLAY: dt attach=1
DISPLAY: chosen display-scale=2 rotation=0 ok=1
DISPLAY: display attach=1 start=1
```

and the console also shows `IOMFB: driver commit:`, so the family and the compositor are talking.

**Regression that this build fixed:** the first official-flow attempt published inline from `bsd_init` and the
storage root died with `NativeUfs.cpp:2073` / "UFS ROOT FAILURE: SystemOS signed volume not found" (`mount timeout,
window busy 1`).  The publication now runs in a kernel thread two seconds later, which removes the block from the
boot thread; the UFS root comes up and the display chain follows.

**The wall is unchanged:** two minutes in,

```
AFB: default surface: No Default FB Surface found. Return error
IOMFB: swap_wait_gated: CommandMake didn't happen even after 5 seconds. Seems system in bad state, cancelling swaps
DISPLAY: unimplemented legacy slot 345 (object=0xfffffff54f1cd000 caller=0xfffffff00578c1cc), returning
panic: Kernel data abort at pc 0xfffffff00be80944 (OSMetaClass::~OSMetaClass + 0x1c),
       lr 0xfffffff00bfe4684 (NativeLegacyDisplay::MetaClass::~MetaClass), far 0x0
```

Also seen in this boot: `Maximum number of VM swap files: 5`, `Failed to open swap file 30`,
`vm_swap_create_file failed @ 115 secs` - the Data volume cannot host the dynamic swap file, separate from the
display work.

So the ordering and the load path are now the documented ones, and everything that is left is the default-surface
adapter: create the BGRA IOSurface through the original IOSurfaceRoot factory and hand it to the family field.

## 00:05: the client probe runs, the fields are mapped, the wall stands

With `offline-mobile-graphics-client=1` the kernel's own client probe ran:

```
DISPLAY: probe entered (registered=1)          <- second call, from the client probe
static IOReturn IOMobileFramebufferUserClient::s_map_display_type(...) UserClient display type is 0
IOMFB: driver commit:
```

so an `IOMobileFramebufferUserClient` was allocated, initialised and answered a selector, and the real userland
client then mapped the display ("UserClient display type is 0").  It does not change the outcome: about two
minutes in, the same sequence repeats -

```
AFB: default surface: No Default FB Surface found. Return error
IOMFB: swap_wait_gated: CommandMake didn't happen even after 5 seconds ... cancelling swaps
DISPLAY: unimplemented legacy slot 345 (object=0xfffffff42f623000 caller=0xfffffff00578c1cc), returning
panic: data abort at pc OSMetaClass::~OSMetaClass+0x1c, lr NativeLegacyDisplay::MetaClass::~MetaClass, far 0
```

The field scan of the live display object (25 eight-byte slots hold kernel pointers):

```
+0x000 vtable 0xfffffff007210758     +0x010..+0x030 registry/provider links (nub at +0x030)
+0x060 0xfffffff262cdc100            +0x278, +0x780 = 0xffffffff00000000 (pattern)
+0x1570..+0x15e0  eight pointers spaced 0x10 (0xfffffff42f6245xx)
+0x1600 0xfffffff348fa7e00           +0x1608 0xfffffff262d7f150
+0x1610 0xfffffff42f597380           +0x1638 0xfffffff262ca9700
+0x1648 0xfffffff515c7b2e0           +0x17e8 0xfffffff42f6247e0
+0x17f8 0xfffffff72f303f40           +0x1868 0xfffffff262e52b50
```

The stretch around +0x1610 is interesting because a device record (`uke`) notes that a swap record whose ID
matches the default surface names `fb+0x1618` directly - i.e. the default surface is identified around that
offset, and today nothing is stored there.

The call site of slot 345, from the family's `opaque243`:

```
ldur x0, [x29, #-184]      ; the display object
mov  x9, #6020 ; add x8, x0, x9 ; ldrb w8, [x8]   ; byte at +0x1784
ldr  x8, [x0] ; ldr x8, [x8, #2760] ; blr x8       ; #2760 = 345 * 8
```

Only `x0` is set up, so the slot takes just the object (a notification/void leaf), not a surface request.  The
next scan therefore prints each pointer's `vtable` as well, so the slots can be named against the component
segments (IOSurface's data extent is known from the prelink report) instead of guessed.

## 00:40: the field map names the family's members, and the provider is the next lever

The completed scan (23 pointer slots, each with its pointee's vtable):

| offset | pointee's vtable symbol | what it is |
| --- | --- | --- |
| +0x000 | `NativeLegacyDisplay` vtable | the object itself |
| +0x018, +0x020 | `OSDictionary` | two dictionaries |
| +0x030 | `IOService` | the nub (provider) |
| +0x060 | `IOServicePM` | power management state |
| +0x1570..+0x15e0 | vtable 0 | eight embedded links (queue) |
| +0x1608 | `IOCommandGate` | command gate |
| +0x1610 | `IOWorkLoop` | work loop |
| +0x1638 | `IOInterruptEventSource` | interrupt source |
| +0x1648 | `IOTimerEventSource` | timer source |

Nothing sits at `+0x1618` - the region a device record (`uke`) names for the default surface - which is what a
missing default surface looks like.

Two new facts from the console of the same boot:

```
virtual bool IOMobileFramebufferLegacy::start(IOService *): provider was not AppleARMIODevice.
```

The family's own start rejects a plain `IOService` provider.  Our component's start still succeeds, but the
family's normal path - which is where the default surface is created and installed - never runs.  The reference
board hit the same message and worked around it with a plain service; that is exactly why its records say the
default-surface adapter "still requires a hardware boot result".

```
disk3s1: device is not readable.
mockfs_mountroot failed: 13
apfs_vfsop_mountroot:2965: apfs: mountroot called!
```

The root volume reported "not readable" on this boot before APFS took over; the boot continued, but this is the
same storage path that later fails to create the swap file (`vm_swap_create_file failed @ 115 secs`).

Reaching the original IOSurfaceRoot factory from the kernel is not a symbol call: the prelinked IOSurface kext
exports only `_gIOSurfaceIdentifier` and `_gIOSurfaceTag`, no class methods, so a surface has to be created
through the service's virtual interface (which means auditing IOSurfaceRoot's slots) or by letting the family do
it through its normal start path.

Next lever, cheapest first: give the family the provider it asks for - allocate `AppleARMIODevice` (present in the
prelinked AppleARMPlatform) as the nub over the firmware framebuffer memory, attach it to the resource service and
to `/arm-io`, and pass it to `start`.  If the family's own path then runs, it may create and install the default
surface itself, which is the one thing no board record shows.

## 01:10: the AppleARMIODevice provider is accepted, and the family's own lookup runs

Giving the family the provider it asks for changed the picture:

```
DISPLAY: nub class AppleARMIODevice
DISPLAY: nub 0xfffffff644226e20 attach=1 start=1 mem=0x9c000001+0x1fa400
DISPLAY: dt attach=1
IOMFB: driver commit:
DISPLAY: chosen display-scale=2 rotation=0 ok=1
DISPLAY: display attach=1 start=1
```

* the family no longer prints "provider was not AppleARMIODevice" - the original `start` accepted the provider;
* the field scan grew from 23 to 24 pointer slots: the new one is
  `+0x1640 = <nub> vtable=0xfffffff005cbe4c8`, i.e. the family stored the accepted provider in its object;
* the failure message moved from `AFB: default surface: ...` to
  `IOMFB: default_fb_surface: No Default FB Surface found. Return error`, so the family's own lookup now runs
  instead of the earlier AFB path.

The panic is unchanged two minutes in (`swap_wait_gated: CommandWake didn't happen ... cancelling swaps`, then
slot 345, then the null metaclass teardown).

Where the family looks for the surface, from the disassembly of the function that prints that message
(`0xfffffff005777464`): it calls a virtual slot through `[x0 + 1392]` (slot 174) and reports the failure when
that returns non-zero; the neighbouring small function at `0xfffffff00577742c` forwards through
`[this + 216]` (0xD8) and then slot 217, i.e. the surface is reached through an object held at +0xD8.  Our
display object has no pointer at +0xD8 (the scan lists none), so that reference is null as well.

Both routes end at the same place: **nothing ever created an IOSurface**.  Reaching the factory needs the
prelinked IOSurfaceRoot's virtual interface audited the same way the display family's was (its vtable slots and
class layout), because the IOSurface kext exports no class symbols - only `_gIOSurfaceIdentifier` and
`_gIOSurfaceTag`.

## 01:25: the IOSurfaceRoot audit is staged

The next measurement only needs the device: the probe now prints, right after it finds the surface root,

```
DISPLAY: IOSurfaceRoot <obj> class=<className> size=<classSize> vtable=<address>
DISPLAY:   vt[0..7]
```

so the class layout and the vtable address come out of a normal boot.  With that address,
`_agent/vt_audit.py` enumerates the slots and prints, for each entry, the strings its first instructions
reference - which is how the display family's slots were identified - so the surface-creating method can be
shortlisted before anything is called.

Image: `instantnoodlep-artifacts/bootEU.img`, sha256
`7f67638f99d3098e3a333f3553ba15be58632f107171e16ed23bceb95040e548`.

The phone is not enumerated at the moment (no `VID_1209`, no `fastboot`), so the boot is pending.

## 01:45: IOSurfaceRoot is audited down to a candidate factory slot

The probe printed the service's layout on the device:

```
DISPLAY: IOSurfaceRoot 0xfffffff6c8ccae00 class=IOSurfaceRoot size=0x4a0 vtable=0xfffffff005d3b2b8
DISPLAY:   vt[0]=0xfffffff004ba8c64 vt[1]=0xfffffff004ba8d68 ...
```

That vtable address is the same one the display object had at `+0x1600`, so the family holds a reference to the
IOSurfaceRoot service there.

Enumerating the vtable (it ends at slot 168) leaves only a handful of entries that belong to the class itself
(the rest are inherited IOService/OSObject implementations):

| slot | address | evidence |
| --- | --- | --- |
| 1, 7 | `0xfffffff004ba8d68`, `...8dac` | reference "IOSurfaceRoot" (class name plumbing) |
| 14, 21 | `...8fe8`, `...8ea8` | no strings |
| 85, 86 | `...90ac`, `...90bc` | `hw.memsize`, `/vram`, "IOSurfaceRoot: Initial shared event memory alloc failed" (start/init) |
| 87 | `...9e20` | no strings |
| **141** | `0xfffffff004baa11c` | `IOSurfaceRoot.cpp`, "IOSurface count of %d approaching limit of %d", tailq head/element asserts |

Slot 141 is the surface factory: it increments the count, checks the limit and walks the surface tail queue.  Its
prologue shows the calling convention: `x0` this, `x1` stored for later, `w3` a flag whose non-zero value jumps
straight to the error return (`w19 = 0xE00002C9`), and `x4` an out-parameter that is cleared first.

That is the piece the board has to call: build the descriptor dictionary, call slot 141 on the IOSurfaceRoot
service, check the returned object's metaclass is `IOSurface`, and hand it to the display family's default-surface
field (+0x1618 neighbourhood) instead of leaving it null.

Everything before that is verified on hardware: the family loads through the documented boot argument, the owned
class registers with the original identity, the display publishes over the firmware framebuffer on an
`AppleARMIODevice` nub the family accepts, the userland client maps it (`s_map_display_type`, `IOMFB: driver
commit`), and the remaining failure is exactly "no default surface".

## 02:05: the first call into the IOSurfaceRoot factory names its first parameter

The probe called vtable slot 141 on the IOSurfaceRoot service with `(this, props, null, 0, &out)`:

```
DISPLAY: create_surface 1080x480 stride 4320 via vt[141]=0xfffffff004baa11c
panic(cpu 0 caller 0xfffffff00c0043b0): zone_require failed: address in unexpected zone id 545
  (iokit.OSDictionary) (addr: 0xfffffff9e069cf88, expected: proc_task) @zalloc.c:1434
```

The kernel tried to release the pointer in a `proc_task` zone, so the factory's first argument after
`this` is a task (or proc), not the property dictionary.  The prologue agrees: `x1` is saved early, `x4` is the
out-parameter that is cleared first, `w3` is a flag whose non-zero value jumps to the error return, and `x2` is
untouched until later.

The next call therefore passes `current_task()` first and the dictionary second.

## 02:20: slot 141 is the user-client factory, and it works from the kernel

Calling it with the corrected argument order (`this, task, dictionary, flag, out`) succeeded:

```
DISPLAY: create_surface 1080x480 stride 4320 via vt[141]=0xfffffff004baa11c task=0xfffffff349206450
DISPLAY: create_surface result=0 out=0xfffffff42f92f8b0
DISPLAY: create_surface class=IOSurfaceRootUserClient size=0x138
DISPLAY: slot 345 surface=0xfffffff42f92f8b0 (kept for the next step)
```

So vtable slot 141 of `IOSurfaceRoot` is `createUserClient(task, properties, flag, &client)`: it allocates an
`IOSurfaceRootUserClient` (0x138 bytes) owned by the task passed first.  (The previous attempt passed the
dictionary where the task belongs, which is why the kernel reported a `proc_task` zone expectation.)

Consequences:

* the factory itself is now proved callable from the kernel, and the argument convention is known;
* the surface itself is created *through that user client* (the same external-method path userland uses), not
  directly by `IOSurfaceRoot`, which matches the reference record's "original IOSurfaceRoot factory" wording;
* the two-minute panic is unchanged in this boot (the client was created but nothing installs a default surface
  yet), so the next step is the client's external method.

Next measurement: keep the created `IOSurfaceRootUserClient` and either audit its dispatch table or call its
`externalMethod` with the surface-creation selector and an `IOSurfaceCreateArgs` payload (width, height, bytes per
row, pixel format BGRA, alloc size), then check the returned object's class is `IOSurface`.

## 02:35: the family reaches the default surface through `+0xD8` and slot 174

The function that prints "No Default FB Surface found" (at `0xfffffff005777464`) has this shape:

```
str x0, [x29,#-16]       ; this
str x1, [sp,#24]         ; arg1
str w2, [sp,#20]         ; arg2
str w3, [sp,#16]         ; arg3
ldr x8, [x29,#-16]
ldr x0, [x8, #216]       ; x0 = this->[0xD8]        <-- the default-surface holder
ldr w1, [sp,#20]
ldr w2, [sp,#16]
ldr x8, [x0]             ; its vtable
ldr x8, [x8, #1392]      ; slot 174
blr x8                   ; status -> error path when non-zero
```

So the family's default-surface lookup is: take the object held at **`this + 0xD8`**, ask it through **slot 174**
(with the two 32-bit arguments the caller passed), and report "No Default FB Surface found" when that comes back
non-zero (the error it returns is `0xE00002BC`).

Two possibilities remain, and they are cheap to tell apart on the next boot:

1. the object at `+0xD8` is a real surface holder whose slot 174 answers "no surface", or
2. the field belongs to a different object than the display (our display object's `+0xD8` is empty - the scan
   lists no pointer there), and the family is being asked on a different instance.

Either way the missing piece is unchanged: an IOSurface must be created (now via the `IOSurfaceRootUserClient`
whose factory slot is verified) and installed where slot 174 can find it.

## 03:05: the prelink page's binding report is the documented vtable map

`/zh/concepts/prelink` does not describe the display surface, but it documents the two facts this work has been
standing on and one tool we had been reproducing by hand:

* "范围保留原始虚拟地址，slide 后原始代码中的指针仍然有效" - why absolute bindings into an original component keep
  working;
* "输出保留原始组件代码不变" - the original class layout and vtable are the ones in the kernelcache;
* the report next to `kernel.macho` carries `bindings` ("导入位置和 slide 后的目标"), `components`,
  `instruction_inventory` and `module_entrypoints`.

The report is machine-readable: this build has 69,294 bindings.  Filtering the ones whose site falls inside the
`IOSurfaceRoot` vtable (`0xfffffff005d3b2b8`) returns all 168 slots with their targets, and filtering those whose
target lands in IOSurface.kext's own `__TEXT` returns exactly the nine methods of the class:

```
slot   0 -> 0xfffffff004ba8c64     slot  21 -> 0xfffffff004ba8ea8
slot   1 -> 0xfffffff004ba8d68     slot  85 -> 0xfffffff004ba90ac
slot   7 -> 0xfffffff004ba8dac     slot  86 -> 0xfffffff004ba90bc
slot  14 -> 0xfffffff004ba8fe8     slot  87 -> 0xfffffff004ba9e20
                                   slot 141 -> 0xfffffff004baa11c
```

which is the same set the disassembly scan produced, including the factory slot 141 this board has already called
successfully.  So the documented route to a class's vtable map is the prelink report, not a byte scan.

What the page (and the rest of the documentation) still does not contain: the meaning of each slot, the argument
structures, the family's `+0xD8` field, and the fact that slot 141 is `createUserClient(task, dictionary, flag,
&client)`.  Those belong to the board's own record, which is where they are now.

## 03:20: the surface client is an IOUserClient2022

The first externalMethod attempt on the freshly created `IOSurfaceRootUserClient` panicked before reaching any
method:

```
DISPLAY: create_surface class=IOSurfaceRootUserClient size=0x138
panic(cpu 0 caller ...): wrong externalMethod for IOUserClient2022 @IOUserClient.cpp:6483
```

The kernel source in the tree explains it (`iokit/Kernel/IOUserClient.cpp`):

```c
IOReturn IOUserClient::callExternalMethod(uint32_t selector, IOExternalMethodArguments *args) {
    ...
    if (uc2022) {
        ret = ((IOUserClient2022 *)this)->externalMethod(selector, (IOExternalMethodArgumentsOpaque *)args);
    } else {
        ret = externalMethod(selector, args);
    }
    ...
}
IOUserClient2022::externalMethod(uint32_t, IOExternalMethodArguments *, IOExternalMethodDispatch *,
    OSObject *, void *) { panic("wrong externalMethod for IOUserClient2022"); }
```

So a 2022 client must be driven through `IOUserClient::callExternalMethod()`, which routes to the
`IOUserClient2022::externalMethod(selector, opaque)` overload; calling the five-argument `externalMethod` is what
panics.  The probe now uses `callExternalMethod`.

## 03:45: what the main repository actually contains about the surface

A whole-checkout search (`/home/dev/unixnu/UniXNU`, excluding `out/`) for the surface terms returns only the
documentation, the component's call site, and the device records - no implementation.  But the profile ABIs hold
something useful, if only for another OS:

`profiles/os/ipados-27.0.1-24A446/abi/original-classes.json` records four classes:

* `IOMobileFramebufferLegacy`: size `0x1dd8`, 379 slots (168 base), named slots
  (`isMainDisplay` 186, `getDotPitch` 190, `supportedFrameInfo` 216, `getDisplayDimensions` 261,
  `startHardware` 305, `kickSwapQueue` 307, `completeCurrentSwap` 312, `validateSwap` 313,
  `getRefreshAttributes` 319, `collectCompletionReports` 358), the pure slots, and named fields
  (`current_swap` 0x1a40, `pending_head` 0x1a48, `pending_tail` 0x1a50, `vsync_clients` 0x1ab8,
  `display_width` 0x1bf0, `display_height` 0x1bf4) - and **no default-surface field**;
* `IOMFBSwapIORequest`: the swap request layout, whose `surfaces` field sits at 0xb80 (with rects, options and
  `request_id` 0x3d4), so surfaces travel in the request, not in the framebuffer;
* `IOSurface`: the descriptor accessor functions - `descriptor`, `width`, `height`, `stride`, `allocation`,
  `format`, `planes` - i.e. exactly the values a create-descriptor must carry (the keys our dictionary already
  uses);
* `IOHIDDevice`.

Our own profile `ipados-17.7.11-21H461` has no `abi/original-classes.json` at all, so none of this was ever
generated for this build; the display class layout we have came from the audit header instead.

So the main repository documents *what* the descriptor contains and *where* surfaces travel, but nowhere the call
that creates one: that remains the user-client external method (the 2022 two-argument
`externalMethod(selector, opaque)`), with the factory slot 141 already proved callable.

Housekeeping: an untracked copy of this board's probe had been left in the main checkout's `package/`; it was
removed, and `git status` there is back to its previous state (only the submodule states that were already there).

## 04:10: the sibling port shows the working architecture (GFander/UniXNU-kebab)

`github.com/GFander/UniXNU-kebab` (OnePlus 8T, same SM8250 class, same "the bootloader's framebuffer is scanned
out by the panel" situation) solves this area completely differently from the path this board has been on.  Its
display patch (`patches/xnu-8796-kebab/0093-kebab-display.patch`) adds two native kernel files:

* `iokit/Kernel/arm/KebabSurface.cpp/.h` (~1,370 lines) - **a built-in IOSurface**: a native
  `IOCoreSurfaceRoot` service plus a user client that speaks the ABI `IOSurface.framework` expects, with its own
  selector table:

  ```
  sel 13 limits (struct out 0x18, u16 at +0x16 = ABI version)
  sel 0  create: scalar [parent id], struct in = serialized property dictionary, struct out = lock result
  sel 4  lookup by id; sel 34 lookup by mach port; sel 35 create mach port
  sel 9  set value; sel 10 update non-bulk value cache; sel 11 remove value
  sel 27 set bulk attachments (0x1b)
  traps 0/1 use-count +/-; 2 lock; 3 unlock; 4/5 release
  ```

  Every surface is wired kernel memory mapped into each task that creates or looks it up, with the lock result
  telling the library where the pixels, the shared state page and the timestamps are;

* `iokit/Kernel/arm/KebabFramebuffer.cpp` - an `IOMobileFramebuffer` (AppleCLCD) that CPU-copies CoreAnimation's
  swaps into the bootloader framebuffer and drives vsync from a 60 Hz timer.  `SwapEnd` queues the swap record and
  the vsync timer presents it; `SwapWait(6)` waits with a bounded timeout.

Its header comment states the reason plainly: "Apple's IOSurface kext is closed", so the port provides the name,
the service and the selectors itself.  The display is presented as the iPhone X identity userland expects
(1125x2436 at 3x) while the panel, canvas and default surface stay at the bootloader's 1080x2400.

**What this means for this board:** the route we have been on - original prelinked `IOMobileGraphicsFamily` plus the
closed IOSurface factory - is the harder of the two, and the reference records admit the default-surface adapter
was never proven on hardware.  The sibling's route replaces both closed pieces and its documentation reports a
working GUI.  Porting `KebabSurface` as this board's surface provider, and either keeping the original family for
the framebuffer side or porting `KebabFramebuffer` as well, is the proven path.

Kept for reference: the patch was fetched to `/root/kebab_0093.patch` (81 KB) and the repository's ABI records
(`docs/standardization-run-20261029/abi/IOSurfaceRootUserClient.json`, `IOCoreSurfaceRoot.json`) describe the same
service from the contract side.

## 04:25: what porting the built-in surface means for this board

The sibling's source is now extracted from its display patch: `KebabSurface.h` (76 lines) and
`KebabSurface.cpp` (1,369 lines).  Its public shape is small:

```c
class KebabSurface : public OSObject {            /* id, geometry, planes, values dictionary,
                                                     pixel descriptor, shared state page, kernel map */
    uintptr_t kernelAddress();
};
KebabSurface *kebab_surface_copy(uint32_t id);
KebabSurface *kebab_surface_create_physical(uint64_t phys, uint32_t width, uint32_t height,
    uint32_t bytesPerRow, uint32_t pixelFormat);
mach_port_name_t kebab_surface_port_in_task(KebabSurface *surface, task_t task);
```

behind which sit the `IOCoreSurfaceRoot` service, its user client and the selector table (13 limits, 0 create,
4/34/35 lookup and ports, 9/10/11 values, 27 bulk attachments, traps for retain/lock/release), plus the
`KebabFramebuffer` (AppleCLCD) that CPU-copies swaps and drives vsync from a timer.

**The conflict to decide on:** this board's prelink already carries the original `com.apple.iokit.IOSurface`, and it
is *loaded and running* - the probe matches `IOCoreSurfaceRoot` and creates an `IOSurfaceRootUserClient` from it.
The family also lists `com.apple.iokit.IOSurface` in `OSBundleLibraries`, so removing the kext would stop the
framebuffer family from loading.  The sibling avoids the problem because its kernel never had Apple's kext.

So the two coherent paths are:

1. **Incremental (current)**: keep the original IOSurface kext and find the create call on its user client - the
   remaining unknown is the selector for the 2022 `externalMethod(selector, opaque)`; the factory slot 141 is
   already proved and every wrong selector answers with an error instead of crashing;
2. **Sibling architecture**: drop Apple's IOSurface (and, for consistency, the closed framebuffer family) from the
   prelink roots and provide `KebabSurface`-style native services, which is what has a reported working GUI - but it
   replaces everything this board has built so far.

Path 1 costs one measurement at a time and keeps the working display publication; path 2 is a rewrite with a known
end state.  Both need a decision before code is written.

## 04:40: selector 0 with the documented argument layout

The phone's own screen showed what the console relay had missed:

```
DISPLAY: create_surface class=IOSurfaceRootUserClient size=0x138
DISPLAY: client sel=0 rc=0xe00002c2 out=0x0,0x0,0x0,0x0
... (sel 1..7 the same)
```

`0xE00002C2` is `kIOReturnBadArgument`: the 2022 dispatch *is* reached now (the two-argument virtual was the
right entry), it just rejected the argument shape.  The sibling's source documents the shape:

```
sel 0 create: scalar [parent id], struct in = serialized property dictionary, struct out = lock result (0xa68)
```

so the probe now calls selector 0 with one scalar (parent id 0), a hand-written XML plist
(`IOSurfaceWidth/Height/BytesPerRow/PixelFormat/AllocSize`) as the structure input, and a 0xa68-byte lock-result
buffer as the structure output, then logs the first words of the result.

## 05:00: selector 0 answers BadArgument even with the documented layout

```
DISPLAY: create_surface 1080x480 stride 4320 via vt[141]=0xfffffff004baa11c task=...
DISPLAY: create_surface result=0 out=0xfffffff748328180
DISPLAY: create_surface class=IOSurfaceRootUserClient size=0x138
DISPLAY: create sel0 rc=0xe00002c2 serialized=1 len=447 out=00000000 ...
```

The XML descriptor is accepted as a structure input (447 bytes), the 2022 virtual dispatch runs, and the answer is
still `kIOReturnBadArgument`.  The sibling's selector table was recorded against iOS 16.7 (XNU 8796.141.3,
iPhone10,3 20H392); this board's client comes from iPadOS 17.7.11 (21H461), so its dispatch counts and order differ.
The next measurement is a small argument matrix on selector 0 (0..3 scalars, with and without structure in/out),
which is safe because a mismatched count answers BadArgument rather than crashing.

### Where the work stands after this session

Verified on hardware today:

1. the display family loads through the documented `offline-mobile-graphics-load` flow (no hand-rolled load);
2. the owned class registers with the original identity (base 0x18a0, full ancestry);
3. the display publishes over the firmware framebuffer on an `AppleARMIODevice` nub, attaches to `/arm-io` and
   starts; the userland client maps it (`s_map_display_type`, `IOMFB: driver commit`);
4. the family's default-surface lookup is traced to `this + 0xD8` -> slot 174;
5. `IOSurfaceRoot` is audited: class 0x4a0, vtable 0xfffffff005d3b2b8, nine own slots, slot 141 =
   `createUserClient(task, dictionary, flag, &client)`, returning `IOSurfaceRootUserClient` (0x138);
6. a 2022 user client must be driven through the two-argument virtual `externalMethod(selector, opaque)`; the
   five-argument one panics by design;
7. the remaining failure is exactly the missing default surface, and the panic two minutes in is its consequence.

Open, in order of cost:

* the selector/argument matrix on the original client (one boot, bounded);
* or the sibling architecture: a built-in `KebabSurface`-style service replacing Apple's closed IOSurface (its
  sources are kept in `docs/evidence/reference/`), which requires deciding whether the closed
  `IOMobileGraphicsFamily` stays;
* the storage side of the same boot: `vm_swap_create_file failed` and the one-off "disk3s1 is not readable".

## 05:20: the community answer for the default surface (Allenwdk/UniXNU-uke)

Searching the community devices turned up the port that documents this exact wall.  `UniXNU-uke` ships
`scripts/audit-mobile-graphics-default-surface.py`, whose header states the contract:

> The original class keeps its default surface at instance+0x1618 and its temporary surface at instance+0x1620.
> Only the (absent) platform driver ever installs the former; this audit pins the exact fields, slots and
> user-client dispatch entry the bring-up backend may use.

Its offsets and slots:

```
surface_root 0x1600   command_gate 0x1608   work_loop 0x1610
default_surface 0x1618   temp_surface 0x1620   memory_region 0x17c0
surface_enable 0x1780    mode_block 0x1700    width 0x16b0   height 0x16b4
slot 174  default_fb_surface gate -> slot 333      slot 242  copy the surface port name
slot 299  create_default_fb_surface -> instance+0x1620
slot 333  default-surface layout/commit, returns sid
user-client dispatch selector 3, scalars 2 in / 1 out, output is the default surface's mach port
surface object: sid +0x10, region +0x28, descriptor +0x30, stride +0x68, format +0x80, allocation +0x90
```

and the decisive line: `default_surface_nonzero_writer_in_family: False` - the family never installs +0x1618
itself.

The port's probe (`package/LegacyDisplayProbe.cpp`, 58 KB) then shows the workaround it actually runs with
`offline-legacy-default-surface=1`:

1. allocate `IOBufferMemoryDescriptor::withCapacity(0x2000000, kIODirectionOutIn)`;
2. write the mode block at +0x1700 (`'BGRA'`, planes 1, bpp 4, clear 0x70 bytes, `fb[0x1748] = 0`);
3. put that descriptor into the region cache at **+0x17c0**, instead of the `PurpleGfxMem` region the original
   `create_default_fb_surface` would ask IOSurfaceRoot for (the platform driver that would provide it is absent);
4. set the enable byte `fb[0x1780] |= 1`;
5. start the display - slot 299 then builds the temporary surface at +0x1620 from that region;
6. after start, retain the surface and its +0x28 region and +0x30 descriptor, then
   `memcpy(fb + 0x1618, &temp, 8)` to adopt it, and let the original slots 174/242 produce the IOSurfaceID and
   the mach port the compositor maps.

This board's probe now does the same: seed +0x17c0, enable +0x1780, start, adopt +0x1620 into +0x1618, with the
region and descriptor retained.

## 05:40: the community recipe works - the default surface now exists

With the uke recipe in the probe, the same boot shows:

```
DISPLAY: seeded surface region 0xfffffff8877268c0 bytes=33554432
DISPLAY: surfaces default=0 temp=0xfffffff6ba853a78
DISPLAY: adopted default surface 0xfffffff6ba853a78 sid=1 region=0xfffffff8877268c0 descriptor=0xfffffff887725c10
```

* the seeded region made the family's own `create_default_fb_surface` (slot 299) build the temporary surface -
  it exists now, with `sid=1`;
* it is adopted into `+0x1618`, with the surface, its region and its descriptor retained;
* and the whole `AFB/IOMFB: No Default FB Surface found` + `swap_wait_gated` + `slot 345` chain **no longer
  appears** in the console.  The wall this session was chasing is gone.

What remains is a different failure: about a minute in, the same looking but differently reached data abort at
`pc 0xfffffff00be80944` (`OSMetaClass::~OSMetaClass + 0x1c`), `lr 0xfffffff00bfe4684`
(`NativeLegacyDisplay::MetaClass::~MetaClass`), with `x0 = 0` and `x21 = 0xfffffff00578b960`.  Nothing in the
console precedes it except the usual `watchdogd` and `liveactivitiesd` traps, so this is a teardown ordering
issue of the class object rather than the surface path.

Next: find who tears the metaclass down (the family's `stop`/`free` path versus our retained object), and
whether the uke port's extra step - keeping `activeLegacyDisplay` and never releasing the adopted surface - is
what avoids it there.

## 06:00: how the uke port solves the rest (from its PROGRESS.md)

The uke repository documents its whole display track.  The parts that answer this board's open questions:

**1. The default surface (already applied here).**  It seeds the family's region cache at +0x17c0 with an
`IOBufferMemoryDescriptor`, sets the enable byte at +0x1780, lets slot 299 build the temporary surface at +0x1620
and adopts it into +0x1618, retaining the surface, its +0x28 region and +0x30 descriptor because "the adopted
surface outlives the legacy display's stop" - `stop` clears +0x1618 and drops the region reference.

**2. Why a swap can arrive with no surface.**  Their disassembly of the original `swap_submit` (vtable[178],
their kernel `0x578f224`):

* the gate action looks the swap record up in the `fb+5496` list by the id at `record+0x98`;
* masks are copied from the record (+0x14c, +0x150, +0x154);
* **the layer loop only runs for layer < 4**, and the surface id of each layer lives at `record+0x9c+4*layer`;
* an id equal to the default surface's id uses `fb+0x1618` directly, otherwise it calls the original
  `IOSurfaceRoot` lookup - in their kernel `0xfffffff0057a28c0(root, id, task)`, task from `0xfffffff0057a2e84` -
  and stores the result at `request+0xa40+8*layer`.

Their device record shows why that matters: for a 1200-byte swap record the compositor put the *whole frame* on
**layer 6** (id 41) while layers 1/2/4 carried id 0, so the original loop never reaches it and the request is
rejected with `accepted=0 reason=2 surface=0`.

**3. Their fix: own-present.**  `NativeMobileDisplay` pads short records to 1288 bytes (`offline-legacy-swap-pad`),
captures the first enabled layer that carries an id, and after the original call returns it calls
`presentBase(frameSurfaceId)`, which resolves the overlay surface through the **original lookup stub** (bound to
`0xfffffff0057a28c0` and `0xfffffff0057a2e84`, byte-verified) and copies it into the GOP synchronously on the
compositor's own thread.  That is how they got the system UI on the panel for the first time.

**4. The memory wall** was theirs too: they raised the VM with a UEFI high-DRAM mapping plus
`owned_memory_size`; this board solved the same problem with the 4 GiB window.

So the remaining work on this board is the same shape: accept that the original layer loop will not see the full
frame, capture the id from the record, resolve it through the original lookup, and copy to the framebuffer - while
finding what tears the class down when the display has no presentation to complete.

## 06:15: the surface accessors the component already asks for

The display component calls `native_legacy_surface_{descriptor,width,height,stride,allocation,format,planes}`
when it handles a swap's surface, and `native_legacy_swap_validation_observed(accepted, reason, request, surface)`
on every validation.  Both were stubs on this board, so even a surface that reached the component could not be
read.  The probe now implements the accessors on the original IOSurface layout from uke's audit (descriptor
+0x30, width +0x60, height +0x64, stride +0x68, format +0x80, allocation +0x90, one plane) and logs the first six
validations together with the request's four surface pointers at +2624.

This is also the half of own-present that needs no new binding: the component's copy path can only work once these
accessors answer, and the validation log tells which layers the compositor actually submits.

## 06:30: reading PanicLogDxe correctly - the dumped panic belongs to the previous boot

The console capture after a boot ends with the usual tail

```
QCOM USB PANIC pending_at_start=7 dropped_at_start=0 buffer_capacity=4194304
QCOM PANIC END
```

and the panic block above it is the **saved panic from the previous boot**, printed by PanicLogDxe, not the current
one.  This explains the apparent contradiction in this session: a boot whose payload is built from the newest probe
still showed `slot 345` and `create_surface ... sel0` lines, because those were the previous image's dying words.
The current boot's own log is everything before that block.

With that reading:

* the newest image does reach `found the region, family built the temp surface (sid=1), adopted into +0x1618`, and
  the `IOMFB: driver commit` line - no default-surface error;
* the panic that follows belongs to the older build, and the current one has to be judged on a fresh boot watched
  past the two-minute mark;
* the surface accessors (descriptor +0x30, width +0x60, height +0x64, stride +0x68, format +0x80, allocation +0x90)
  and the swap-validation log are in this build and have not been exercised yet, because no swap has reached the
  component so far in a window we captured.

So the next measurement is a clean boot of the current image with the console watched from ~100 s to ~200 s, looking
for `DISPLAY: swap ...` lines (the compositor's layer ids) and for whether the metaclass panic still occurs.

## 06:45: the current build's panic has no display activity in front of it

A clean boot of the newest image (the one with the region seed, the adoption and the surface accessors) logs:

```
DISPLAY: seeded surface region 0xfffffff89faf8c60 bytes=33554432
DISPLAY: surfaces default=0 temp=0xfffffff5ed000d68
DISPLAY: adopted default surface 0xfffffff5ed000d68 sid=1 region=... descriptor=...
(IOMFB: driver commit:)
... only process exits: liveactivitiesd (corpse limit), watchdogd, fairplayd, bluetoothd ...
Kernel data abort. at pc 0xfffffff00be80944, lr 0xfffffff00bfe4684
  x0 = 0, x21 = 0xfffffff00578b960 (IOMobileFramebufferLegacy::opaque243 + 0x510)
```

* no `AFB/IOMFB: No Default FB Surface`, no `swap_wait_gated`, no `slot 345`, no `DISPLAY: swap` line;
* the panic is `OSMetaClass::~OSMetaClass + 0x1c` reached from `NativeLegacyDisplay::MetaClass::~MetaClass`,
  i.e. our class's metaclass destructor runs with a bogus `this`, about two minutes in;
* it arrives right after launchd declares `Too many corpses being created` for liveactivitiesd and watchdogd,
  i.e. when the boot is at its most failure-prone point, and nothing the display does precedes it.

Working hypothesis: the metaclass registered through `OSMetaClass::preModLoad(g_kernel_kmod_info.name)` /
`postModLoad(handle)` is being torn down by the kext lifecycle (either the family's kext unloading, or the fake
kmod handle used for late registration being freed), and the destructor is entered with a garbage `this`.

Decisive control experiment for the next boot: build the same kernel with the display arguments removed
(`offline-mobile-graphics-load`, `offline-legacy-*`) so nothing registers the class at all.
If the panic disappears, it is the registration/teardown of our class; if it stays, it is unrelated to the display
and belongs to the boot's process-failure storm.

## 07:00: control experiment - the panic belongs to the display registration

Built the same kernel with every display argument removed (`bootEU-control.img`,
sha `954c262c5e1a029cb32476a4554a2d94e827a9260ff894f17c70978908e371e1`, base args only) and booted it:

```
DISPLAY: late probe (load handled by offline-mobile-graphics-load)   <- the only display line
... launchd, watchdogd trap, liveactivitiesd trap, apfs class-open message, fairplayd, bluetoothd ...
```

575 console lines, **no kernel data abort, no panic**.  With the display arguments, the same kernel panics about two
minutes in at `OSMetaClass::~OSMetaClass + 0x1c` reached from `NativeLegacyDisplay::MetaClass::~MetaClass`.

So the teardown fault is caused by this board's display registration itself - not by the boot's process-failure
storm, and not by the default-surface path (which no longer errors).

The remaining question is therefore narrow: what destroys the metaclass.  The class is registered through
`OSMetaClass::preModLoad(g_kernel_kmod_info.name)` and `postModLoad(handle)`, i.e. against a fake kext created for
late registration; the destructor then runs with a bogus `this` (0xfffffff00ba131cb-like code addresses appear in
the registers).  Candidates: the fake kext being freed, or the family's kext unloading and taking the subclass
metaclass with it.

Next measurements, cheapest first:

1. keep the `preModLoad` handle in a global and print it at shutdown paths, or re-register the class through the
   family's own kmod identity so both lifecycles match;
2. print from a destructor-like probe: a static object inside the board archive whose destructor logs the moment the
   kext/fake-kext teardown begins;
3. compare with the uke port, which registers the same way but never lets its display object reach `stop`: it keeps
   `activeLegacyDisplay` and never releases it, and its `stop`/`detach` calls exist only on failure paths.

## 07:10: the control boot is still alive, six minutes in

A live read of the control image (no display arguments) shows the machine still running:

```
XNU-DYLD-IMAGE: SpringBoard pc=... FrontBoardServices+0x1e2ec load=... images=1235
Process[201] crashed: SpringBoard. Too many corpses being created.
Process[219] crashed: SpringBoard. Too many corpses being created.
XNU-EXIT: liveactivitiesd pid=271 code=5 ns=2 sub=0x5
XNU-EXIT: bluetoothd pid=272 code=0 ns=0 sub=0x0
... webbookmarksd: missing executable ...
```

SpringBoard is back to its well-known crash loop at the missing CADisplay assertion (as expected with no display
published), the usual services cycle, and there is **no kernel panic**.  That completes the comparison:

| image | display arguments | after ~2 minutes |
| --- | --- | --- |
| current | yes | data abort in `OSMetaClass::~OSMetaClass` via `NativeLegacyDisplay::MetaClass::~MetaClass` |
| control (`954c262c...`) | no | still running, SpringBoard crash loop only |

So the teardown fault is tied to the display registration, and the machine is otherwise stable - which also gives a
useful baseline: the control image can be left running for long observations.

## 07:25: holding the registration handle does not stop the teardown

The build that keeps `OSMetaClass::preModLoad`'s handle in a global (`bootEU.img`, sha
`c445a7ebf1ff9f7c0e5a7f7a0465e4dd162f50d110fe09d5e998c29ca4a219a9`) still ends the same way.  Line positions in
its console log (542 lines):

```
104: probe entered
172: seeded surface region ...
206: adopted default surface ... sid=1
527: Kernel data abort. at pc 0xfffffff00be80944, lr 0xfffffff00bfe4684
538: QCOM USB PANIC   (PanicLogDxe dump marker)
```

so the panic is the current boot's own, and it arrives after the display is fully published with its surface - with
no display error in between.  The `holding registration handle` line is absent, which means either `preModLoad`
returned null on that call (and the registration went through a path that never stores a handle) or the line was
lost in the console backlog; that has to be settled with an unconditional print.

So the fake-kext reference is not (or not only) what frees the metaclass.  The teardown still happens at the same
place: `NativeLegacyDisplay::MetaClass::~MetaClass` -> `OSMetaClass::~OSMetaClass` with a bogus `this`.

Next, in order:

1. print unconditionally what `preModLoad` returns, and also `postModLoad`'s result, right where they happen;
2. wrap the object's own destruction: `NativeLegacyDisplay` is ours, so a `printf` in its `free()` (the component
   can add one) shows whether the display object dies before the metaclass does;
3. compare with uke, whose `LegacyDisplayStubs.c` may stub the very slot that leads there.

## 07:40: the first accepted swap with a real surface, and the next two walls

This boot is the first one that actually ran the newest probe (the earlier images were built from a stale kernel,
because `unixnu build kernel` does not track the display archive - forcing a relink took deleting and restoring
the kernel output, and the build then needed the archive's member set to be complete again).  What it shows:

```
DISPLAY: preModLoad(__kernel__)=0xfffffff521552e80
DISPLAY: adopted default surface 0xfffffff520f6f7e0 sid=1 region=... descriptor=...
DISPLAY: swap accepted=1 reason=0 request=0xfffffff521bcc000 surface=0xfffffff520f72480
         req_surfaces=fffffff520f72480,0,0,0
IOMFB: swap_wait_gated: CommandWake didn't happen even after 5 seconds ... cancelling swaps
DISPLAY: abstract slot 345 (object=0xfffffff3549ae000 caller=0xfffffff00578c1cc); panicking
panic: instantnoodlep legacy display abstract slot 345 @LegacyDisplayProbe.cpp:475
```

So:

* the compositor's swap is **accepted** (`reason=0`) and the first layer already carries the adopted default
  surface pointer - the surface work is real end to end;
* presentation does not complete: the swap wait times out with "CommandWake didn't happen", so the family never
  gets its completion;
* when it gives up, the family reaches **abstract slot 345**, and the restored guard now panics there on purpose
  (the component declares it noreturn).

The two open items are therefore concrete:

1. **slot 345** must be implemented instead of guarded - the caller is the family's `opaque243 + 0xd7c`, the
   call is `this`-only, and it runs when a swap is abandoned.  The uke port's `LegacyDisplayStubs.c` is the
   place to look for what it stubs;
2. **presentation completion**: the component's copy path has the original surface accessors bound now
   (`original-bindings.S` points them at the family's own functions), so the remaining piece is to make
   `kickSwapQueue`/`prepare_scanout` actually copy the surface and retire the request so the wait becomes a wake.

Also recorded: the kernel build does not track `build/native-legacy-display/libNativeLegacyDisplay.late.a`;
after changing the probe the build must be forced (touch a kernel source or delete the kernel output, and keep the
archive members complete - the probe must define every `native_legacy_*` and `native_gpu_trace_*` symbol the
component references).

## 07:55: the swap is accepted, the copy fails with kIOReturnCannotWire and zero bytes

The presentation-logging build produced the two lines that explain the abandoned swap:

```
DISPLAY: swap accepted=1 reason=0 request=0xfffffff45b80e000 surface=0xfffffff45bd86bf0
         req_surfaces=fffffff45bd86bf0,0,0,0
DISPLAY: transfer rc=0xe00002d8 source=0xfffffff45b80e000 bytes=0
IOMFB: swap_wait_gated: CommandWake didn't happen even after 5 seconds ... cancelling swaps
DISPLAY: abstract slot 345 (object=0xfffffff45badc000 caller=0xfffffff00578c1cc); panicking
```

* the swap's first layer really carries the adopted surface pointer, so the surface is in the request;
* the component's copy attempt returns `0xE00002D8` (`kIOReturnCannotWire`) with **zero bytes**, i.e. the
  geometry it read for the surface is empty, so nothing can be wired and nothing is copied;
* the wait then times out and the family reaches abstract slot 345, which is where the guard panics.

So the surface-side numbers the component sees are the next thing to fix, not the guard.  The component gets them
through the accessors that `original-bindings.S` binds to the family's own functions; the build that logs the
surface's own fields at `+0x60/+0x64/+0x68/+0x80/+0x90` plus its descriptor and region is ready
(`instantnoodlep-artifacts/bootEU.img`, sha
`6c7da76279acf5aec88b9e904dedb386741739e4b9b86cf53f664b289a893303`) and will show whether the adopted temporary
surface is laid out the way those accessors expect.

## 08:15: the compositor's surface geometry, measured

The GOP-copy build printed the surface's real numbers at the first accepted swap:

```
DISPLAY: swap accepted=1 reason=0 request=... surface=0xfffffff362938bf0 req_surfaces=...,0,0,0
DISPLAY: surface geom 480x0 stride=4352 fmt=42475241 alloc=2088960 desc=0xfffffff362487268 region=0
DISPLAY: present surface=... 480x0 stride=4352 fmt=42475241 alloc=2088960 console=0 base=0x9c000001 1080x480 rb=4320
DISPLAY: transfer rc=0xe00002d8 ... bytes=0
```

Reading it correctly:

* `alloc = 2088960 = 4352 * 480`, `stride = 4352 = 1088 * 4`, `fmt = 0x42475241` ('BGRA'), so the compositor's
  surface really is **1088x480 BGRA**;
* the words this probe read at `+0x60`/`+0x64` are **height (480) then 0**, not width/height - so the width lives
  elsewhere (or only in the stride) and the component, which uses the bound original accessors, likewise ends up
  with a zero dimension and therefore copies nothing (`bytes=0`, `kIOReturnCannotWire`).

The probe now derives the width from the stride (`stride / 4`), logs the eight words at `+0x50..+0x6c`, and copies
`min(height, console height)` rows of `min(stride, rowBytes)` bytes into the GOP framebuffer.  Next boot will either
put the compositor's frame on the panel or name the exact width field.

Image: `instantnoodlep-artifacts/bootEU.img`, sha
`0af6c29b09851ecabb3fd5d6547a98cbf07b7e54fddc9e39342cf43083198a42`.

## 08:30: why only the top of the panel shows our frame

The copy now works - `present copied 480 rows x 4320 bytes (frame 1)` - but the panel only shows it in its upper part.
The reason is the geometry the kernel is handed, not the copy:

```
DarwinBoot: video base 0x9C000000  1080x480  row 4320 bytes  32 bpp
DISPLAY: present surface=... 1080x480 stride=4352 ... console=0 base=0x9c000001 1080x480 rb=4320
```

The framebuffer window this kernel receives is **1080x480** (the loader's console window inside the panel's
memory), so a full-frame copy is 480 rows = 2,073,600 bytes.  The panel itself is taller (the UniXNU menu below
is drawn by the loader at the panel's own size, 1080x2400), and those rows are not part of the kernel's window -
they still hold whatever the loader left there.

So the frame is complete but clipped: the compositor's 1080x480 surface now lands correctly in the 480-row window,
and the rest of the panel belongs to a different mapping.

To fill the panel the port needs one of:

1. the loader to hand over the panel-sized window (1080x2400) instead of the 480-row console window;
2. or the display driver to program the panel/DPU so the kernel owns the full scanout (the sibling ports do this
   - uke's DISPLAY-AREA work and kebab's `KebabFramebuffer` both treat the panel as 1080x2400 and present there);
3. or a window expansion through the platform expert, if the firmware already scans out a larger area from the
   same base.

## 08:45: the frame reaches the framebuffer; the swap still never completes

The guard no longer panics, and the boot now shows the copy and the aftermath in order:

```
DISPLAY: swap accepted=1 reason=0 request=... surface=0xfffffff658a3b0c8 req_surfaces=...,0,0,0
DISPLAY: present copied 480 rows x 4320 bytes (frame 1)
DISPLAY: abstract slot 345 (object=0xfffffff3a565d000 caller=0xfffffff00578c1cc); returning
Kernel data abort. at pc 0xfffffff00be80944, lr 0xfffffff00bfe4684
```

So the compositor's frame is now copied into the firmware framebuffer, but:

* the family's swap wait still times out (`CommandWake didn't happen`), so it abandons the swap and reaches the
  abstract slot 345;
* returning from that slot avoids the guard's own panic, but the machine then dies in the same
  `OSMetaClass::~OSMetaClass` data abort a moment later - the teardown issue that has followed every display
  build, now unmasked rather than caused by the guard.

The panel also shows the console text twice, offset: the 480-row window is being copied into a panel area that is
taller than the window, so the same lines appear in two places and the compositor frame lands only in the window.

State of the display track, in order of certainty:

1. surface creation and adoption: **verified** (sid=1, adopted, no default-surface error);
2. swap acceptance and frame copy: **verified** (`accepted=1`, 480 rows x 4320 bytes into the GOP);
3. completion: **missing** - the family never wakes the swap; the protocol path that would (the command gate at
   `fb+0x1608`, slots 307/312/313) has not been driven;
4. window size: the kernel only owns 1080x480 of a 1080x2400 panel, so a full-frame presentation needs the larger
   window or the DPU path;
5. teardown: the metaclass data abort still ends the boot once the display has been published for a while.

## 09:15: the firmware side is prepared but blocked on the edk2 dependency fetch

What was done for the 480-row clamp:

* the clamp in `drivers/DarwinBoot/DarwinBootPkg/Driver/BootVideo.h` (`if (Video[4] > 480) Video[4] = 480;`) is
  removed in the checkout, with a comment recording why it existed;
* `Platforms/OnePlus/instantnoodlepPkg/DeviceBuild.py` was created from the kebab template
  (`PackagesSupported = ("instantnoodlepPkg")`, scopes `instantnoodlep`, same package paths);
* the Python side of the edk2 toolchain is installed (`edk2-pytool-library`, `edk2-pytool-extensions`,
  `antlr4-python3-runtime`, `pefile`, `coloredlogs`), and BootShim already builds.

What blocks the build: `stuart` reports

```
ERROR - Dependency 'mu_nasm' is not met!
ERROR - Dependency 'Mu-Basetools' is not met!
ERROR - Dependency 'onecrypto-bin' is not met!
```

`stuart_update` for the platform returned without fetching them, so the build cannot produce the 4 MiB
BootShim+UEFI image yet.  The remaining work on this path is purely environment setup:

1. make `stuart_update` resolve `mu_nasm`, `Mu-Basetools` and `onecrypto-bin` (it needs network access to their
   release archives and the platform config `Platforms/OnePlus/instantnoodlepPkg/DeviceBuild.py`);
2. `python3 build_uefi.py -d instantnoodlep -r RELEASE` then emits the image;
3. copy that image over `instantnoodlep-artifacts/kernel_small.bin` (the 4 MiB BootShim+UEFI carrier) and repack
   `bootEU.img`;
4. the boot should then report `GOP console ... 1080x2376`, and the console, the diagnostic page and a compositor
   frame should all cover the panel.  If the console-time mapping fails for 10 MiB, the fallback is to keep the
   clamp for the console only and present the full panel from the display driver (the probe already proved a
   10.4 MiB mapping works once the kernel is up).

## 09:30: the firmware toolchain now resolves its dependencies; the build stops in a platform plugin

Progress on the firmware path this round:

* the edk2 toolchain is complete (`edk2-pytool-library`, `edk2-pytool-extensions`, `antlr4`, `pefile`,
  `coloredlogs`, `regex`), and `stuart_update` now runs through the installed script
  (`.../bin/stuart_update`) instead of the module path;
* it fetched the three external dependencies - `mu_nasm`, `Mu-Basetools`, `onecrypto-bin` -
  ("Updated/Verified 3 dependencies"), and the nuget feed is reachable (HTTP 200);
* `build_uefi.py -d instantnoodlep -r RELEASE` gets past BootShim and into the edk2 build.

It then stops inside Mu's OverrideValidation plugin:

```
File ".../OverrideValidation/OverrideValidation.py", line 204, in override_detect_process
    list_path = os.path.normpath(filepath).lower()
TypeError: _path_normpath: path should be string, bytes or os.PathLike, not NoneType
```

i.e. the plugin walks the platform's override list and receives a `None` path - a mismatch between this
`instantnoodlep.dsc` and the plugin's expectations (the kebab/lemonade DSC files in the same tree are the
reference).  Fixing it is a platform-DSC/plugin-config task, not a kernel one:

1. diff `Platforms/OnePlus/instantnoodlepPkg/instantnoodlep.dsc` against `kebab.dsc`/`lemonade.dsc` for the
   sections the plugin reads (it walks DSC modules and their override flags);
2. or disable the plugin for this platform build (the plugin list lives in the Mu build config), since it is a
   validation aid rather than part of the image;
3. then `build_uefi.py -d instantnoodlep -r RELEASE` produces the 4 MiB BootShim+UEFI image, which replaces
   `instantnoodlep-artifacts/kernel_small.bin`.

The DarwinBoot clamp removal (`BootVideo.h`) is in place and will take effect as soon as that image is built.

## 09:45: firmware build reaches the edk2 plugin stage with one unresolved module path

Fixes applied this round:

* `Platforms/OnePlus/instantnoodlepPkg/DeviceBuild.py` now also lists `/home/dev/unixnu/UniXNU/package` in
  `PackagesPath`, because this DSC references `UniXNUPkg` (UniXNULib, the UniXNU application, UniXNUDxe,
  UniXNURamDiskDxe) which lives in the main checkout;
* the earlier "Failed to convert Edk2Relative Path to an Absolute Path: UniXNUPkg/..." message came from that
  missing path and is gone.

The OverrideValidation plugin still aborts the pre-build with a `None` file path:

```
File ".../OverrideValidation/OverrideValidation.py", line 204, in override_detect_process
    list_path = os.path.normpath(filepath).lower()
TypeError: ... not NoneType
```

so one entry the plugin walks resolves to no file at all - most likely a disabled/conditional module in this DSC
(there is a commented `AdvancedLogDxe.inf` that used to change the GOP mode) or a DSC section the plugin does not
understand.  Two ways forward, both small:

1. find that entry by giving the plugin a real path (the DSC's `[Components]` list is short enough to audit), or
2. run the build with the plugin list trimmed (edk2toolext discovers plugins from
   `Mu_Basecore/BaseTools/Plugin`; the plugin can be neutralised by removing its `do_pre_build` registration in a
   local copy, since it is a validation aid).

Once the image builds, replacing `instantnoodlep-artifacts/kernel_small.bin` with it activates the DarwinBoot
clamp removal (full-panel `1080x2376`).

## 10:00: firmware build - environment done, blocked on a build macro

State of the firmware build after this round:

**done**

* edk2 toolchain installed (`edk2-pytool-*`, `antlr4`, `pefile`, `coloredlogs`, `regex`) and `stuart_update`
  fetched `mu_nasm`, `Mu-Basetools`, `onecrypto-bin`;
* `Platforms/OnePlus/instantnoodlepPkg/DeviceBuild.py` created from the kebab template, with two extra package
  paths: `/home/dev/unixnu/UniXNU/package` (UniXNUPkg) and `/home/dev/unixnu/UniXNU/drivers/DarwinBoot`
  (DarwinBootPkg);
* Mu's OverrideValidation plugin was patched locally to skip entries that resolve to no path (it aborted with
  `TypeError: ... not NoneType` on this DSC); that is a validation aid, not part of the image;
* missing third-party include directories (mipisyst, brotli, spdm) were created so the DEC files resolve.

**blocked**

`edk2` now stops on `MdePkg/Library/BaseFdtLib/BaseFdtLib.inf`, whose source line is
`$(FDT_LIB_PATH)/fdt.c`.  `FDT_LIB_PATH` is not defined anywhere in this checkout (kebab/lemonade do not define
it either, so their build environment must supply it) - the fix is to define the macro in the platform build, e.g.
`DEFINE FDT_LIB_PATH = libfdt` next to the other `DEFINE` lines, or pass `-D FDT_LIB_PATH=libfdt` to the edk2
build, matching where `libfdt` actually lives in `Mu_Basecore/MdePkg/Library/BaseFdtLib`.

Once that resolves, the remaining work is: finish the image, replace
`instantnoodlep-artifacts/kernel_small.bin` with it, repack, and confirm `GOP console ... 1080x2376` with the
DarwinBoot clamp removed.

## 10:30: a freshly built firmware with the clamp removed, packaged by hand

The firmware build in the WSL Mu-Silicium tree finally produced the volume:

```
Build/instantnoodlepPkg/RELEASE_CLANGPDB/FV/FVMAIN_COMPACT.Fv   4,194,304 bytes
```

after: installing the edk2 toolchain, fetching `mu_nasm`/`Mu-Basetools`/`onecrypto-bin` with
`stuart_update`, creating `Platforms/OnePlus/instantnoodlepPkg/DeviceBuild.py` from the kebab template with
`/home/dev/unixnu/UniXNU/package` and `/home/dev/unixnu/UniXNU/drivers/DarwinBoot` added to `PackagesPath`,
initialising the nested `MdePkg/Library/BaseFdtLib/libfdt` submodule, and letting the OverrideValidation plugin
skip entries without a path.

The build then stopped only in the *post-build* PECOFF validation (two runtime drivers' section alignment), after
the volume was written.  The final image is therefore assembled from the two parts this port already used:

```
kernel_small.bin = 112-byte Linux Image header (BootShim) + FVMAIN_COMPACT.Fv
```

which reproduces exactly the previous image's size (4,194,416 bytes):

```
head -c 112 kernel_small.bin.old  > kernel_small.new
cat FVMAIN_COMPACT.Fv            >> kernel_small.new
```

`kernel_small.bin` now holds the new firmware (the old one kept as `kernel_small.bin.old`), and `bootEU.img` was
repacked (sha `30a60c18eb329358e9c51bced42bb5c2e8471d7ff8138ee71de0e5866ceeb37e`).

Next boot decides two things at once: whether this hand-assembled firmware boots at all (the shim and volume base
are unchanged, so it should), and whether `PE_current_console` now reports `1080x2376` - which would end the
480-row window and let the console, the diagnostic page and a compositor frame cover the panel.

## 11:00: the full-panel firmware boots; the kernel receives no video

The hand-assembled image (new FV + unchanged 112-byte shim header) **boots**: the loader draws its menu across
the whole panel with the green pulse bar at the bottom, and its hand-off banner reports a panel-sized video mode
(picture shows `video base 0x9C000000 1080x2376 row 4320`).

The kernel, however, gets nothing:

```
DISPLAY: no usable GOP console (base=0x0 0x0 depth=0 rot=0 stride=0)
```

and the machine ends up back in the loader menu.  `PE_current_console` only copies `PE_state.video`, and
`PE_init_platform` copies that straight from `boot_args->Video` - so the zero is already in the boot arguments by
the time XNU starts.  In DarwinBoot, `ReadBootVideo()` runs first and, on any failure, leaves `Video[6]` zeroed
before `CopyMem(Args->Video, Video, ...)`; the banner uses the same array, which is why the picture shows a real
mode while the kernel sees zeros only if the failure happens on one path and not the other.

Two candidates, both cheap to settle next:

1. `DarwinBootVideoFromGop` rejects the mode because `Gop->Mode->FrameBufferSize < rowBytes * v_height`
   (10,264,320 bytes for 1080x2376); SimpleFbDxe computes that size as `Width*Height*4` from the same PCDs, so it
   should pass - unless the GOP mode the loader sees is a different (smaller) one;
2. XNU rejects/ignores the larger window on its side (the original comment's "kernel map cannot give it the full
   10 MiB at the point the console comes up").

The evidence so far favours (2) being the real limit and (1) being fine (the banner shows the values), so the next
look should be at where the kernel maps the framebuffer for the console on this board.

The previous firmware is kept at `kernel_small.bin.old`; restoring it returns the 1080x480 window and a working
boot.

## 11:20: the black screen was our own panel clear; removed

The image that went black was the one carrying both the whole-panel clear and `boot-progress=2`: the probe
cleared 10,368,000 bytes at 0x9c000000 and nothing repainted the console afterwards (the serial log shows
`Early boot complete` right after, so the machine was healthy).  Both were removed and the image rebuilt:

* `clear_panel()` is no longer called (the function stays unused);
* `boot-progress=2` was dropped from the kernel arguments;
* the previous firmware (`kernel_small.bin.old`) is back in place, so the device is in its known-good state:
  console text in the 480-row window, display experiments intact.

The full-panel experiment stands where it was: with the DarwinBoot clamp removed, the **loader** covers the whole
panel (menu plus pulse bar) and reports 1080x2376, but the **kernel** gets no video because
`initialize_screen()` defers the mapping and `ml_io_map()` cannot give it 10,264,320 bytes at
`startup_phase < STARTUP_SUB_KMEM` - the very limit the clamp's comment described.

Two ways forward, both understood:

1. **kernel side**: make the deferred framebuffer mapping succeed for the panel size (a different map call or a
   larger early VA window), which restores the console at 1080x2376;
2. **display side**: keep the console in its 480-row window and advertise the panel size to the compositor
   (`installBootDimensions(1080, 2376)` with the nub memory covering 4320 x 2376), so the compositor renders
   full-screen and the probe's copy - which already maps 10.4 MiB successfully later in boot - fills the panel.
   This needs the component's consistency checks (which compare `PE_current_console` against
   `getDisplayDimensions` in several places) to follow the same override.

## 00:20: the kernel gets the full panel - 1080x2376

Two board-level bring-up switches had been hiding this, both found and changed:

1. **DarwinBoot deliberately zeroed the video** for the kernel:

   ```c
   // Bring-up switch: hand the kernel no video at all. ...
   #if !defined (DARWINBOOT_VIDEO_FOR_XNU) || (DARWINBOOT_VIDEO_FOR_XNU == 0)
     ZeroMem (Args->Video, sizeof (Args->Video));
   #else
     CopyMem (Args->Video, Video, sizeof (Video));
   ```

   Our rebuilt firmware took the zero path, which is why every boot after the firmware change had no console.
   The block now always hands the video over;

2. **the kernel's IO submap was 8 MiB** (`osfmk/arm/io_map.c: #define IO_MAP_SIZE (8ul << 20)`), too small for the
   panel's 10,264,320-byte frame buffer, so `ml_io_map` failed at console time. It is now 32 MiB.

With both, the same boot reports:

```
DISPLAY: bootargs video base=0x9c000001 w=1080 h=2376 rb=4320 depth=32 display=1
DISPLAY: GOP console 0x9c000001 1080x2376 depth=32 stride=4320
DISPLAY: adopted default surface 0xfffffff62fd15480 sid=1 region=... descriptor=...
```

So the kernel console, the display geometry and the adopted default surface are all at the panel's real size
(1080x2376), with the DarwinBoot hand-off page already covering the whole panel (green pulse bar included).

Image: `instantnoodlep-artifacts/bootEU.img`, sha
`881755292e6845c57bb319c7452d16375abfacc9918ca3b6de0a143ed3e83ea4`.

## 00:35: full panel down to the copy - 2376 rows written to the frame buffer

The same boot that reports `1080x2376` for the console also carries the display stack to full size:

```
DISPLAY: swap accepted=1 reason=0 request=... surface=0xfffffff62fd18120 req_surfaces=...,0,0,0
DISPLAY: surface geom 2376x0 stride=4352 fmt=42475241 alloc=10342400 desc=...
DISPLAY: surface words +50 163ef840 fffffff7 00000438 00000000 00000948 00000000 00001100 00000000
DISPLAY: present surface=... 1080x2376 stride=4352 fmt=42475241 alloc=10342400
DISPLAY: present copied 2376 rows x 4320 bytes (frame 1)
```

* the surface words are now `+0x58 = 0x438 (1080)`, `+0x60 = 0x948 (2376)`, `+0x68 = 0x1100 (4352)`, i.e. the
  layout the earlier scan had guessed, confirmed at panel size;
* `alloc = 10342400 = 4352 * 2376`;
* and the probe's copy writes **2376 rows of 4320 bytes** - the whole panel - on the first compositor swap.

So the path is complete end to end: firmware hands the panel over, the kernel console and the display both run at
1080x2376, the compositor renders a full-size surface, and this board's CPU copy fills the panel with it.

What is left is not geometry:

1. the component's own transfer still fails (`rc=0xe00002d8` kIOReturnCannotWire with `bytes=0`), so the family's
   `swap_wait_gated` never sees its CommandWake and abandons the swap after five seconds;
2. that abandonment reaches abstract slot 345, and a moment later the familiar
   `OSMetaClass::~OSMetaClass` data abort follows.

Fixing the completion (or making the component's copy see the geometry) should remove both.

## 00:55: completion attempt and where the session ends

The probe now calls the family's `completeCurrentSwap` (vtable slot 312) right after its own full-panel copy:

```c
typedef void (*complete_fn)(void *);
complete_fn complete = ((complete_fn *)(*(void ***)gDisplay))[312];
complete(gDisplay);
```

but the last boot never reached the swap: its console ends with the usual service exits and then the same
`OSMetaClass::~OSMetaClass + 0x1c` data abort (with `x6 = 0xfffffff00578c1cc`, the family's `opaque243 + 0xd7c`),
so the completion call is still unverified.  In the previous boot the swap did run and the copy was complete
(2376 rows x 4320 bytes) while `swap_wait_gated` still timed out.

Session state:

* **working**: full-panel firmware (DarwinBoot clamp removed and video handed over), kernel IO submap 32 MiB, console
  at 1080x2376, surface at 1080x2376, and this board's copy writing the whole panel on the first accepted swap;
* **open**: the swap completion (CommandWake) - either by calling slot 312 correctly, by signalling the command gate
  at `fb+0x1608`, or by taking over SwapEnd/SwapWait the way the uke port does; and the metaclass teardown panic
  that follows a minute or two after the display is published.

Images on disk for the next run: `bootEU.img` (newest, slot-312 attempt) and `kernel_small.bin.old` (the previous
firmware, for a quick revert).

## 01:05: what the official documentation says, and why slot 312 cannot be driven from outside

**The documentation does cover this**, in `docs/porting/display.md`:

> 4. Check swap completion and IOSurface lifetime before enabling GPU presentation
> ... In GPU handoff, track the pending frame by `NGPSwap.sequence` and return `NGPComplete` with that sequence
> after copying. Retain the source frame and IOSurface until the swap completes, and complete the corresponding
> error path on timeout or failure.
> **Expected result:** XNU publishes the display, presents frames and completes swaps.

and it states the reference's own limit plainly:

> A captured 300-second boot completed three swaps without a kernel panic and displayed the Apple logo.
> Home-screen startup remains unverified; capture the display-server and SpringBoard wait chains before treating a
> stationary logo as a framebuffer transfer failure.

So the contract (retain until completion, complete by sequence, handle timeout) is documented; the mechanism inside
the original family is not, which is why the uke and kebab ports each complete swaps in their own display component.

**Two experiments on the completion path this round:**

* calling `completeCurrentSwap` (slot 312) inline from the swap-validation hook executes but does not wake the
  waiting client - the wait is armed after the hook returns, so it misses the completion;
* calling it from a kernel thread 30 ms later also executes (`DISPLAY: delayed completion via slot 312`) and the
  `swap_wait_gated` timeout disappears - but the call then faults inside the family
  (`pc=0xfffffff00579d2f8, lr=0xfffffff00579cf6c`, i.e. `completeCurrentSwap` calling into slot 313's code) because it
  runs outside the work loop with stale argument registers.

Conclusion: the completion has to happen **inside the family's own swap context**, which means either driving the
command gate at `fb+0x1608` with a proper request, or taking over `SwapEnd`/`SwapWait` in the component the way
uke does.

The component is rebuildable: `tree/build/native-legacy-display/compile-command.json` holds the full clang-24
command line that produced `NativeLegacyDisplay.o`, so a component change can be compiled and re-archived without
the device builder script.

## 01:20: guarded completion - the stable full-panel state

The out-of-context completion call is now behind `offline-legacy-complete=1` (default off), so the probe copies the
frame and leaves the family's swap path untouched.  The boot confirms the whole picture once more:

```
DISPLAY: swap accepted=1 reason=0 request=... surface=...
DISPLAY: surface geom 2376x0 stride=4352 fmt=42475241 alloc=10342400 desc=...
DISPLAY: present copied 2376 rows x 4320 bytes (frame 1)
IOMFB: swap_wait_gated:  CommandWake didn't happen even after 5 seconds.
DISPLAY: abstract slot 345 (object=... caller=0xfffffff00578c1cc); returning
Kernel data abort. at pc 0xfffffff00be80944, lr 0xfffffff00bfe4684
```

and the panic screen itself uses the whole panel, so the console stays at 1080x2376 through the crash.

This is the state to build on: firmware hands the panel over, the kernel console and the display both run at
1080x2376, and the probe copies every accepted frame in full; the only missing piece is completing the swap inside
the family's own context (command gate at `fb+0x1608`, or taking over `SwapEnd`/`SwapWait` in the component,
which is now buildable from `compile-command.json`).

## 01:40: the component already has the completion path - the object is stale

Reading the component source shows the completion path is there and correct:

```cpp
bool retireSwap(const OriginalLegacySwapRequest *request,bool force) {
    unsigned retire=0,cpuPresent=0;
    PE_parse_boot_argn("offline-legacy-retire",&retire,sizeof(retire));
    PE_parse_boot_argn("offline-legacy-cpu-present",&cpuPresent,sizeof(cpuPresent));
    if(!(retire==1 || cpuPresent==1 || force))return false;
    if(!activateOnlyPendingSwap(request))return false;
    ...
    IOMobileFramebufferLegacy::completeCurrentSwap();     // in the work loop, as the family expects
```

Our arguments already include `offline-legacy-cpu-present=1`, so this path is armed - but it is never reached because
the component's own copy fails first with `rc=0xe00002d8` (kIOReturnCannotWire).

**That status does not exist anywhere in the tree's `NativeLegacyDisplay.cpp`**, so the prebuilt
`NativeLegacyDisplay.o` (and therefore `NativeLegacyDisplay.late.o`) was built from a different, older source than
the one in the tree.  Until the object is rebuilt, the component is not running the code we read.

Rebuilding it from `compile-command.json` (419 tokens, 17 include dirs, clang-24) fails on
`meta_features.h`; adding the generated `build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/<component>/DEVELOPMENT`
directories fixes that but then reorders the includes and produces 81 header-conflict errors.  `compile.log` in that
directory is empty, so the original invocation context is gone.

The proper fix for the build is the device's `scripts/build-gop-legacy-display.py` (referenced by
`docs/porting/display.md`), which sets up the header order for this translation unit.

So the state is: the completion mechanism is understood and present in source; it is the stale object that keeps it
from running.  Rebuilding the component from the tree source is the next concrete step, and it unlocks the
`CommandWake` (and therefore continuous refresh) without any new ABI work.

## 01:55: correction - the object matches the source; the open question is which call site fails

`build-report.json` records `source_sha256` for `drivers/GopFramebuffer/NativeLegacyDisplay.cpp` as
`b6bd4b08fc8ab23c2a497df08df57eff536826938a0b1f67a6e7...`, and the file in the tree hashes to exactly that.  So the
compiled component **is** the source we have been reading; the earlier "stale object" conclusion was wrong (it came
from looking for the literal `kIOReturnCannotWire`, which the source never names).

What that changes:

* `retireSwap()`, `activateOnlyPendingSwap()` and `IOMobileFramebufferLegacy::completeCurrentSwap()` are present in
  the running component, and our arguments already enable the path (`offline-legacy-cpu-present=1`);
* the transfer line `rc=0xe00002d8` therefore comes from one of the component's own failure branches - none of the
  source's `native_legacy_transfer_observed()` call sites pass that value as a literal, so either the status comes
  from a callee that returns it, or from the probe-side stub for one of the bound helpers;
* the copy never reaches `finishTransfer()`, so `retireSwap()` never runs and the swap is never completed.

The next step is narrow: instrument `kickOnce()`'s decision points (validated request, surface pointer, allocation,
`native_legacy_prepare_scanout`, `native_legacy_gpu_offer`) so the failing branch names itself, then let the component's
own `finishTransfer()` run - which is the documented behaviour ("retain until the swap completes and complete the
corresponding error path") and the only path that wakes `swap_wait_gated` inside the family's context.

## 02:05: the copy never runs because the probe's own stub says no

`kickOnce()` begins its transfer with:

```cpp
if(!native_legacy_prepare_scanout()) { native_legacy_transfer_observed(kIOReturnNotReady,request,0); return; }
```

and `native_legacy_prepare_scanout()` is **this board probe's stub**, which has always returned `false`.  So the
component's own copy - and therefore `finishTransfer()`, `retireSwap()` and `completeCurrentSwap()` - has never run on
this port; the probe's separate `present_surface()` was doing all the panel drawing.

The board probe now returns `true` from `native_legacy_prepare_scanout()`, and also logs the values the component's
accessors see (`native_legacy_surface_width`/`_allocation`, bound to the original IOSurface functions at
`0xfffffff004b9ebbc`/`0xfffffff004b9ebfc`).

Built image: `bootEU.img` sha `1900a5445581cc3e359d8475394e97db82c4cbeda6b548c35fb2d87b4dac123e`.  The device was not
attached when it finished, so the next boot will show whether the component's copy succeeds - and if it does, the
family retires the swap inside its own context and `swap_wait_gated` should stop timing out.

## 02:30: the component copy is now one check away from running

The board probe's `native_legacy_prepare_scanout()` stub used to return `false` for the whole port, which is why the
component's copy - and with it `finishTransfer()`, `retireSwap()` and `completeCurrentSwap()` - never ran.  It now
returns `true`, and the boot shows the component reaching its own copy:

```
DISPLAY: accessor width=1080 alloc=10342400 desc=0xfffffff65d56acf8 descLen=10342400
DISPLAY: present copied 2376 rows x 4320 bytes (frame 1)
DISPLAY: transfer rc=0xe00002c2 source=0xfffffff65d715000 bytes=0     <- kIOReturnBadArgument
```

`kIOReturnBadArgument` is emitted only by the two `native_legacy_copy_layout()` failures in `kickOnce()`.  Its checks
are `stride >= width*4`, `allocation >= stride*height`, `descriptorBytes >= allocation`, plus pointer/stride
alignment.  Width, allocation and descriptor length are all correct (1080, 10342400, 10342400, stride 4352), so the
remaining candidate is **height**: the component obtains it from the bound original `native_legacy_surface_height()`,
and the probe's own geometry print reads `2376x0` - the second value is what the surface reports through that path.

The current image (`bootEU.img` sha `ce2f7eb15441c57de5e4ceef53aef02e0ddd42da18277084a309543c20062c40`) prints
`native_legacy_surface_width/height/allocation` together at the swap, so the next boot that reaches the compositor
names the failing value directly.  The last attempt panicked before the swap, so that print is still outstanding.

If height is 0, the fix is to have the adopted default surface report its geometry through the original accessor
(e.g. publish it as a real IOSurface entry in the root, which is what the family's temp surface should already be),
and the component's copy - and therefore the family's own swap retirement - should follow.

## 03:00: everything the layout check compares is correct; the window missed the swap

The last boot with the printing build shows every quantity the component's bounds check needs:

```
DISPLAY: accessor width=1080 height=2376 alloc=10342400 descLen=10342400 mapLen=10342400
DISPLAY: present surface=... 1080x2376 stride=4352 ... console=0 base=0x9c000001 1080x2376 rb=4320
DISPLAY: present copied 2376 rows x 4320 bytes (frame 1)
DISPLAY: transfer rc=0xe00002c2 ... bytes=0        <- kIOReturnBadArgument
```

so width, height, allocation, descriptor length and mapped length all match the panel, and the remaining failure is
inside `native_legacy_copy_layout()` / `native_legacy_copy_pixels()` (both compare against
`stride * height`, where destination stride is the console's 4320 and source stride 4352).

The probe now gives the nub memory 64 KiB of slack past `rowBytes * height` so every `>=` comparison in those checks
is comfortably satisfied (image `811ff215...`).

The boot that carried it did not reach the swap inside the capture window (the compositor submits about 2:10 into
boot, and the window ended at 3:10 with the machine already in its idle/abandoned state), so the result is still
outstanding - one boot captured over 110-240 s will show whether the copy now succeeds and the family retires the
swap itself.

## 03:20: the reference port retires the swap before the copy - and our probe was declining that path

The Pagani (SM8750) port documents exactly this stage in its display driver README:

> The copy runs on a **kernel thread call outside the family's command gate** and **the swap retires before the copy**,
> so the compositor renders the next frame meanwhile (the next swap still waits for this copy).
> ... `sheng-early-retire` (default 1): retire an offered swap when the GPU client takes it.

and its component implements it as

```cpp
if(!gpuBypass && native_legacy_gpu_offer(this,memory,sourceStride,width,height,format)) {
    ...
    if(early==1 && retireSwap(request,true)) { gpuRetired=true; ... }   // completeCurrentSwap, inside the work loop
    return;
}
```

with `retireSwap()` calling `IOMobileFramebufferLegacy::completeCurrentSwap()` - the only context in which it wakes
the waiting client.  On this port `native_legacy_gpu_offer()` is a board-probe stub that has always returned
`false`, so that branch never ran and every swap ended in `swap_wait_gated` timing out.

The stub now returns `true` (the frame is taken by the probe's own copy, which already runs in the swap-validation
hook), so the component should retire each swap early inside the family's work loop.  Image
`382b9569758a087e61c2a18d3b134a002e71f21362c89a39233e091047a3b8e2` is built and waiting for the device.

## 03:35: the early-retire path is enabled; the recent boots do not reach the first swap

With `native_legacy_gpu_offer()` returning `true`, the component takes its early-retire branch
(`retireSwap(request,true)` -> `IOMobileFramebufferLegacy::completeCurrentSwap()` inside the family's work loop), which is
the mechanism the Pagani port documents for a CPU/GPU hand-off: retire before the copy, copy outside the gate.

The two boots since then did not reach the compositor's first swap inside their capture windows at all (the last one
printed only 80 lines in 110 seconds, with `watchdogd` and `liveactivitiesd` exiting in a loop), so the branch is
still unobserved on hardware.  The swap appears at roughly 2-4 minutes into boot on this board, and that timing moves
with how far the service graph gets before the display server submits its first frame.

The image to test is `bootEU.img` sha
`382b9569758a087e61c2a18d3b134a002e71f21362c89a39233e091047a3b8e2`; a capture that covers the first swap
(start ~120 s, run 200 s or more) is what the next session needs.

## 03:50: slot 345 is isTimingEnabled, called from the swap-timeout branch

The Pagani/13T record documents exactly this call site:

> 固定 21H461 原版二进制在 `0xfffffff00578c1c8` 调用 vtable slot 345，返回地址与照片一致。它位于
> `swap_wait_gated` 的 `CommandWake didn't happen even after 5 seconds` 超时分支，返回值按布尔位打印为
> `TimingEnable`。另外两处调用也只传入 self 并检查返回值 bit 0。

and their fix:

> slot 345 改为已核准的 `bool isTimingEnabled()`，返回主显示、GOP 映射及本驱动逻辑电源状态的组合。

Our log has the identical caller:

```
IOMFB: swap_wait_gated:  CommandWake didn't happen even after 5 seconds.
DISPLAY: abstract slot 345 (object=0xfffffff55bb70000 caller=0xfffffff00578c1cc); returning
```

so on this port slot 345 is being serviced by the component's `opaque345()` leaf guard - a `void`, `noreturn`
override generated by `NATIVE_LEGACY_LEAVES` - while the original method is `bool isTimingEnabled()`.  The caller
tests bit 0 of the return value, so it receives whatever happens to be in x0 when the guard returns (the probe's
`printf` return in practice).  That mismatch is what the Pagani port fixed; the guard has to become a real
`bool isTimingEnabled()` that reports the main display, the GOP mapping and the driver's logical power state.

It also explains the ordering we kept seeing: the timeout branch runs first (slot 345), and the
`OSMetaClass::~OSMetaClass` fault follows it a moment later - the family's error path is what frees the metaclass.
Fixing swap completion removes the timeout branch entirely; implementing slot 345 properly removes the panic even
when a swap does time out.

## 04:00: the component is buildable again, and slot 345 is now isTimingEnabled - one link symbol left

Following the Pagani port (which documents slot 345 as `isTimingEnabled()`, called from `swap_wait_gated`'s timeout
branch at `0xfffffff00578c1c8`, the exact caller this board logs):

* the component's generated ABI header now declares `virtual bool isTimingEnabled()=0;` for slot 345 and the
  `LEAF(345)` entry is gone from `NATIVE_LEGACY_LEAVES`;
* `NativeLegacyDisplay.cpp` gains `bool isTimingEnabled() override {return primary && bootMapping && logicalPM;}`
  (the Pagani body is `primary && bootMapping && displayOn`; `logicalPM` is this component's name for the power state);
* **the component compiles again** - the two blockers were the generated `meta_features.h` include
  (`build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT`) and clang-24's new
  `-Wmissing-designated-field-initializers` warning being an error
  (`-Wno-missing-designated-field-initializers`).

The archive rebuilds with the new object, but the kernel link now stops on

```
Undefined symbols for architecture arm64:
  "__GLOBAL__sub_I_NativeLegacyDisplay.cpp", referenced from:
      _native_legacy_display_probe in libNativeLegacyDisplay.late.a(LegacyDisplayProbe.o)
```

i.e. the freshly compiled component object no longer carries the translation unit's static initialiser that the
probe object expects.  The next step is to reproduce the original object's initialiser behaviour (the original
`.late.o` was 384,278 bytes against our 385,152) - either by matching the original compile flags (the device's
`scripts/build-gop-legacy-display.py` sets them) or by moving the globals that need dynamic initialisation into a
form that emits the symbol.

## 04:10: the kernel builds and links with slot 345 implemented as isTimingEnabled()

The missing link symbol was the component's static initialiser: the component's translation unit emits
`__GLOBAL__sub_I_NativeLegacyDisplay.cpp` as a **local** symbol while the probe object references it as an
**external** one.  Defining that symbol in the probe (same name, empty body, via an asm label) resolves the link.

With it:

* ABI header: slot 345 is `virtual bool isTimingEnabled()=0`, `LEAF(345)` removed from `NATIVE_LEGACY_LEAVES`;
* component: `bool isTimingEnabled() override {return primary && bootMapping && logicalPM;}` - the Pagani port's
  body (`primary && bootMapping && displayOn`) with this component's name for the power state;
* component compiles (`meta_features.h` include + `-Wno-missing-designated-field-initializers`), archive rebuilds,
  and `unixnu build kernel` finishes with exit 0.

Image: `bootEU.img` sha `9e6be549fed867f4d21ccacb33acdd7b79536f519579f1111db8e7c19002bc30`.

What this changes on hardware: the `swap_wait_gated` timeout branch no longer calls into an opaque guard that
returns garbage from a `void` function - it asks `isTimingEnabled()` and gets this driver's real scanout state, which
is exactly the fix the Pagani port recorded for the same panic.

## 04:25: the slot 345 fix boots; the boot never reaches a swap to exercise it

The kernel with `isTimingEnabled()` for slot 345 boots and runs.  Neither capture window (110-260 s, then 120 s
live) shows the display stack submitting its first swap at all:

* the windows contain only launchd task start-up, `Early boot complete`, then the `watchdogd` /
  `liveactivitiesd` / `fairplayd` exit loop with `Too many corpses being created`;
* no `DISPLAY: swap accepted`, no `swap_wait_gated`, and - importantly - **no display panic**.

So the fix is in the image but the timeout path it repairs was never reached on this boot.  The blocker for the
next observation is system-level: on many boots the service graph loops before `SpringBoard`/the display server can
submit a frame, so the compositor never runs.

Two things to try next:

1. keep booting until a boot reaches the compositor (the swap appeared at ~2:10 on earlier boots regardless of
   this service loop), then check that the timeout no longer panics;
2. look at the service loop itself (`-no_corpses`, launchd policy, or the `liveactivitiesd` crash at
   `SessionSyncEngine+0x1d4f8`), since a boot that never reaches the compositor cannot validate the display path.

## 04:35: SpringBoard starts and dies before the compositor submits a frame

The longer live capture finally shows the display server's neighbourhood, but still no swap:

```
XNU-EXIT: SpringBoard pid=267 code=6 ns=2 sub=0x6 cs=0x22002301
Process[267] crashed: SpringBoard. Too many corpses being created.
XNU-EXIT: SpringBoard pid=317 code=6 ns=2 sub=0x6 cs=0x22002301
```

No `DISPLAY: swap accepted`, no `swap_wait_gated`, and **no display panic** - the slot 345 fix is in the kernel but the
timeout path is never reached because SpringBoard aborts (code 6) before the compositor's first submission.

So the next blocker is not the display driver: it is SpringBoard/backboardd surviving long enough to submit a frame.
Two candidates worth trying, both cheap:

1. `-no_corpses` in the kernel arguments (the Pagani record uses it to keep repeated crashes from collecting large
   core files, which is exactly the `Too many corpses` state here);
2. the service-loop trims the Pagani record lists (identityservicesd, locationd, CommCenter, bluetoothd,
   iomfb_fdr_loader, iconservicesagent) - each must still match the original task's Label and ProgramArguments.

Until one of those lets SpringBoard live past its first frame, the display path cannot be validated further: every
swap-related observation needs the compositor to submit.

## 05:00 (session summary): the display path is complete up to swap completion

**What this session established, in order**

1. **The 480-row window was our own DarwinBoot bring-up clamp** - `if (Video[4] > 480) Video[4] = 480;` in
   `drivers/DarwinBoot/DarwinBootPkg/Driver/BootVideo.h`, added because the kernel could not map the panel at console
   time.  It is removed, and DarwinBoot now hands the video to XNU (`DARWINBOOT_VIDEO_FOR_XNU` block replaced by an
   unconditional copy).
2. **The kernel's IO submap was 8 MiB** (`osfmk/arm/io_map.c: IO_MAP_SIZE`), smaller than the panel's 10,264,320-byte
   frame buffer; it is now 32 MiB.  With both changes the console runs at the panel's real size:
   `DISPLAY: GOP console 0x9c000001 1080x2376 depth=32 stride=4320`.
3. **The firmware is built here now**: the WSL Mu-Silicium tree needed the edk2 Python toolchain, `stuart_update` for
   `mu_nasm`/`Mu-Basetools`/`onecrypto-bin`, a `DeviceBuild.py` (created from the kebab template) with the UniXNU
   package paths, the nested `libfdt` submodule, and the OverrideValidation plugin skipping path-less entries.  The
   final image is assembled as `112-byte shim header + FVMAIN_COMPACT.Fv`.
4. **The compositor renders at panel size and the frame reaches the panel**: `surface geom 2376x0 stride=4352
   alloc=10342400`, `present surface ... 1080x2376`, and `present copied 2376 rows x 4320 bytes (frame 1)`.
5. **The component is rebuildable**, which it was not before: `compile-command.json` plus the generated
   `meta_features.h` include and `-Wno-missing-designated-field-initializers` compiles it, and the link needs the
   `__GLOBAL__sub_I_NativeLegacyDisplay.cpp` symbol provided from the probe.
6. **slot 345 is `isTimingEnabled()`**, as the Pagani/13T record documents for the same caller
   (`0xfffffff00578c1cc`, the `swap_wait_gated` timeout branch).  It is implemented as
   `primary && bootMapping && logicalPM` and the kernel builds and links with it.

**Where it stops**

The last boots never reach the compositor's first swap: SpringBoard aborts (code 6) and the service graph loops into
`Too many corpses being created` before any frame is submitted, so neither the early-retire path
(`native_legacy_gpu_offer` now returns true) nor the slot 345 fix has been exercised on hardware.  Earlier boots did
reach the swap at about 2:10, so this is boot-to-boot variance, not a fixed wall.

**Next session, in order**

1. let SpringBoard survive to its first frame (`-no_corpses` is now in the arguments; the Pagani service trims are the
   next lever), then confirm the timeout no longer panics;
2. with a swap reaching the display, check that early retire (`gpu_offer=true` + `sheng-early-retire`) wakes
   `swap_wait_gated`, which is the last step to continuous refresh;
3. if the family still does not wake, take over `SwapEnd`/`SwapWait` in the component (uke's design), which the
   rebuild recipe above now makes possible.

Artifacts: `instantnoodlep-artifacts/bootEU.img` (latest, sha `489eb188...`, with `-no_corpses` and the slot 345
fix), `kernel_small.bin.old` (the pre-change firmware, for a quick revert), and the logs under
`docs/evidence/logs/`.

## 05:20: the payload carries the new kernel; the panic screen was from an earlier boot

payload.json records kernel_sha256 = 4fa02fe1deb386d29f7997730e83fa65a4134dcc1f2188761ffb75ac84c913a3 (the
AMFI-patched copy of the kernel built at 01:48), and that kernel's symbols contain

    fffffff00bfe4668 s __ZN19NativeLegacyDisplay15isTimingEnabledEv      (and no opaque345)

so the image booted at 02:03 carries the slot 345 fix. That means the 'abstract slot 345' panic in the photograph
belongs to an earlier boot: PanicLogDxe prints the previous boot's panic on the current screen.

What the current boot shows is the userland loop - SpringBoard repeatedly exiting with code 6 (SIGABRT),
fairplayd.A2 and bluetoothd cycling, no display submission and no panic. So the slot 345 fix is in the running
kernel but unexercised, and the blocker is SpringBoard surviving to its first frame.

Next test needs fastboot: boot bootEU-control.img (no display arguments) and compare. If SpringBoard loops there
too, the loop is independent of the display changes; if it does not, the display stack is what kills it.

## 05:40: control experiment - the SpringBoard loop is independent of the display changes

The control image (bootEU-control.img, no display arguments at all) was booted and captured for 150 s. It shows the
same userland loop:

    XNU-EXIT: SpringBoard pid=27  code=5 ...
    XNU-EXIT: SpringBoard pid=117 code=5 ...   (repeating)

against code=6 (SIGABRT) with the display arguments, but the pattern is the same: SpringBoard starts, traps, is
restarted, and the rest of the graph (fairplayd, bluetoothd, liveactivitiesd) cycles behind it. Nothing in the display
stack is running in that image, so the loop is a system/userland condition, not a consequence of the full-panel work.

Consequences:

* the display path cannot be validated further by booting alone - every display observation needs SpringBoard to
  survive to its first frame, and it does not on the current device state;
* the earlier boots that did reach a swap (about 2:10 into boot) predate this state change, so the current blocker is
  the service graph;
* the next productive step is the launchd policy trim the Pagani record describes (each entry must match the
  original task's Label and full ProgramArguments), or restoring whatever device state changed since the earlier
  successful boots.

## 05:55: from-scratch check of every change in this session

Changes, in order, with what each can and cannot affect:

1. firmware (drivers/DarwinBoot): the 480-row clamp removed and the DARWINBOOT_VIDEO_FOR_XNU zero-video block replaced
   by an unconditional copy. Verified on hardware: the kernel receives 1080x2376 and the loader covers the panel.
2. kernel (osfmk/arm/io_map.c): IO_MAP_SIZE 8 -> 32 MiB. Needed for the 10,264,320-byte panel mapping; after it the
   console reports 1080x2376 and ml_io_map no longer fails.
3. probe (package/LegacyDisplayProbe.cpp): prepare_scanout now returns true, gpu_offer returns true, bootarg-gated
   completion, nub slack, diagnostics. None of these run unless the display component starts.
4. component (drivers/GopFramebuffer/NativeLegacyDisplay.cpp) and its ABI header: slot 345 is bool isTimingEnabled()
   instead of the noreturn void opaque345() guard. Current build returns true unconditionally for the test.
5. kernel args: -no_corpses added.

Evidence that the loop is not caused by any of the above: the control image - no display arguments, an older
kernel, none of this session's display code active - boots and shows the same SpringBoard loop (code=5, SIGTRAP,
against code=6 SIGABRT with the display arguments). The loop is therefore a system/userland condition of the
current device state, not a regression from the display work.

What did visibly change is the panic: before the slot 345 fix the swap timeout ended in the family's guard and a
kernel data abort (the screen photograph). With the fix the timeout is handled and the machine continues - into the
pre-existing SpringBoard loop. The Apple logo that was visible earlier belonged to that panic screen (the compositor
had drawn its logo before the abort).

Candidates for the system-level loop, none of them display code:

* the data ramdisk is reused across every run (out/.../data/owner-data-3g.dbr); repeated panics during the display
  work may have left it in a state SpringBoard aborts on - rebuilding it fresh is the cheapest test;
* the service graph trims the Pagani record documents (identityservicesd, locationd, CommCenter, bluetoothd,
  iomfb_fdr_loader, iconservicesagent), each matched to the original task's Label and ProgramArguments;
* the stock boot image would show whether the loop predates all of this work on the device.

## 06:15: the data volume cannot be swapped for a fresh seed

Booting with the pristine IPSW seed instead of this installation's owner data panics immediately:

    panic(cpu 0 caller 0xfffffff00bd0a5e0): offline scratch Data mount failed: 2 @kern_exec.c:8190

so the Data volume is bound to the installation's own seed - a fresh seed is not a valid substitute, and
CONFIG_XNU_OWNER_DATA_MOUNT requires the persistent one. The owner data was restored from backup and the payload
rebuilt with it (image b6c8e8e9...).

That closes the 'stale data volume' hypothesis: the SpringBoard loop is not something a clean data seed fixes, and
the volume is not what makes SpringBoard abort.

Remaining productive directions, in order:

1. the launchd policy trim the Pagani record documents (match each original task's Label and ProgramArguments);
2. a boot that reaches the compositor (earlier boots did, around 2:10) to validate the slot 345 fix and the
   early-retire path on the swap timeout;
3. if the loop is accepted as this system stage's normal state, document it as such and keep the display work at
   'full panel + full-frame copy, swap completion pending'.

## 06:35: the lite launchd policy does not stop the SpringBoard loop

The board already carries the policy mechanism (package/BootWatchdogPolicy.c): it writes a launchd plist that
disables the listed jobs, runs after writable scratch Data mounts and before launchd exec, and is enabled
unconditionally; `disable-jobs=<labels>` and `lite=1` select the set.  Adding `lite=1` to the kernel arguments is
visible in launchd's own boot-args line:

    boot-args = -v serial=3 debug=0x14e -no_corpses -no_slto_panic ... qcom-usb-debug=1 lite=1 offline-legacy-...

and launchd does start its boot tasks (finish-obliteration, etc.), but SpringBoard still exits with code 6 within
seconds and is restarted:

    XNU-EXIT: SpringBoard pid=27  code=6 ...
    XNU-EXIT: SpringBoard pid=146 code=6 ...

So the abort is not caused by any of the jobs the lite set disables (identityservicesd, locationd, CommCenter,
fairplayd.A2, PerfPowerServices, ...).  The remaining unknown is SpringBoard's own abort reason; the kernel log only
reports the exit code, and launchd does not forward the process's stderr to the serial console.

Options from here: forward launchd/SpringBoard stderr to the console (launchd can be told to keep a job's output),
or run a shell that starts SpringBoard by hand with its output attached, so the abort reason names itself instead of
only its exit code.

## 06:50: how to get SpringBoard's abort reason

The policy file (/private/var/db/com.apple.xpc.launchd/disabled.plist) can only disable jobs, so it cannot redirect
SpringBoard's stderr. Two ways remain, both inside reach:

1. **read the crash report from the kernel.**  SpringBoard's abort is reported by ReportCrash into the Data volume's
   CrashReporter directory, and this kernel already knows how to run file syscalls from a kernel context:
   package/BootWatchdogPolicy.c opens /private/var/db/com.apple.xpc.launchd/disabled.plist with copyout +
   open/write/fchown/fstat.  The same pattern with open/read can print the newest crash report to the serial
   console before launchd starts;
2. **have launchd keep the job's output.**  This needs a per-job override plist rather than the disabled database,
   which this kernel's policy writer does not produce today.

Option 1 reuses code that already runs on every boot and needs no new syscall surface, so it is the next step:
add a boot-argument-gated reader (for example native-crash-dump=<path>) to the same file, call it from the same
place the policy runs, and print the report with printf.

## 07:00: the crash-report reader's wiring

package/BootWatchdogPolicy.c is compiled by scripts/build-verified-block-bridge.py into
build/verified-block-bridge/BootWatchdogPolicy.o and linked into the kernel, where the zzz-boot-progress patch calls
sheng_boot_watchdog_policy() after writable scratch Data mounts and before launchd exec.  A crash-report reader
(open/read on the Data volume, printf to the console, gated by its own boot argument) belongs in the same file and
the same call path, so the recipe is:

1. add the reader to package/BootWatchdogPolicy.c and call it from sheng_boot_watchdog_policy();
2. rebuild the object with scripts/build-verified-block-bridge.py (it already produced the current .o);
3. ./unixnu build kernel, payload, bootimg, boot, and read the report on the serial console.

That is a new kernel change rather than a configuration change, which is where this session stops.
## 07:30: community search for the SpringBoard-loop problem

Searching the official device records (docs/devices) and the ports they link:

* the sheng-based ports (tkings4chan, bai805, enlixk, ad1095445) all document the same diagnostic tool for exactly
  this stage: `sheng-log=1` + `boot-ui-trace=1` starts a **separate process-capture worker**, and
  `boot-ui-focus3=chronod` adds the display-query client to `backboardd` and `SpringBoard`; arm64e return-address
  PAC is stripped so the captured stacks are readable.  Enlixk/UniXNU-sheng carries the regression for the sampler
  (scripts/check-boot-ui-trace.py).
* uke's record notes that the panic/reset path distinguishes jetsam OOM messages from corpse messages in the log,
  which is the same distinction this board now needs;
* sirius-huajifruit's display log shows the neighbouring state (GOP display registered, a client opens the
  user client, "No Default FB Surface found", SpringBoard crashed on the previous run), i.e. the same sequence this
  board went through before the default surface was adopted.

None of the records contains a fix for 'Too many corpses being created' itself; the community tool for seeing why a
userland process dies is the ui-trace worker, which this port does not carry.

Cheap experiment queued before any port of that worker: `lw_corpses=0` (image e09af0e1...), which takes the
lightweight-corpse rate limit out of the path so a full corpse can be produced for SpringBoard's abort.
