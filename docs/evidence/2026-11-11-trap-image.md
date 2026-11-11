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
