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
