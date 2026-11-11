# 2026-11-11 03:05: the watchdogd wall is down

## What the board does now

`bootCU.img` (`85c59f2c`) is the bootBW baseline (512 MiB owned window, E553E004 Data seed, the nine
board patches, no diagnostics) plus the community RamWatchdog backend and two panel markers:

* a green block once `fuxi_ram_watchdog_prepare()` returns 0 (the service is published and matched),
* a cyan block that walks one slot per watchdog tick, cleared each tick so it is a travelling dot.

On hardware the panel shows the green block, then the cyan dot keeps moving for minutes.  The
baseline without the backend dies at about 60 s with `watchdogd[..] exited -- exit reason namespace 2
subcode 0x5`, so the boot now passes that service and the kernel keeps scheduling.

## Why it works

`watchdogd` does not exit for its own reasons: the original `AppleARMWatchdogTimer` personality needs
`IOProviderClass = AppleARMIODevice` and `IONameMatch = wdt,s5l8960x`, a board ADT without that device
never registers a watchdog service, and `watchdogd` then breaks out of its `IOServiceOpen` failure
path.  The backend installs a real `IOWatchdog` subclass (attached to `IOResources`, original base
init, original client dispatch, software deadline observer) so the open succeeds.

Two things were needed to make the vendored package build here:

1. `ram-system-watchdog=1` in the kernel arguments, otherwise the prepare function returns EINVAL
   before doing anything (the first run showed a red block for exactly that reason),
2. a stand-in for `llvm-objcopy`'s Mach-O surgery (LLVM for Linux cannot remove sections from Mach-O):
   `package/vendor-ram-watchdog/macho-surgery.py` zeroes `__mod_init_func` and globalizes the
   constructor symbol, which is exactly what the upstream builder asks objcopy to do.

## What is still missing

The kernel runs but the panel never leaves the loader page and the serial ring stays empty, so the
service graph itself is still unobserved.  The next step is the display/console channel (the
deferred framebuffer mapping never completes) or a second panel channel that reports which launchd
job is running, now that drawing from the kernel is proven to work.

Artifacts: bootCQ (watchdog, red block), bootCR (watchdog with its argument, green block),
bootCT (first heartbeat, row fills), bootCU (moving dot, the one that keeps running).

## 03:40: what the console attempt showed

The deferred framebuffer mapping never runs.  Two edits were tried on the way to that conclusion:

1. `ml_io_map_unmappable` was replaced by `ml_io_map` (two arguments on this kernel; three does not
   compile), because the board's own panel drawing already maps the same physical base with it;
2. the `startup_phase >= STARTUP_SUB_KMEM` guard was dropped, since `kPEAcquireScreen` runs right
   after the EARLY_BOOT start-ups and KMEM is a later phase, so the guard could never be true.

Neither produced a line on the panel or a map: `bootCV` and `bootCW` show only the watchdog's green
block and the moving cyan dot, so `initialize_screen`'s deferred branch is still not entered -
`deferred_fb_pa` is probably still zero, i.e. the first half of the patch (the branch that records
the panel base when `initialize_screen` is called with a real `boot_vinfo`) is not taken either.

The next diagnostic is a panel mark inside `initialize_screen` itself: one colour when it runs with a
non-NULL `boot_vinfo`, another when it runs for `kPEAcquireScreen`, and a third when `deferred_fb_pa`
is set.  That needs no console and no panic screen, and the panel channel is proven to work.

Everything else from tonight stands: the watchdog backend keeps the boot alive past `watchdogd`, the
panel draws from the kernel, and the machine survives minutes where the baseline died at one minute.
## 04:00: the frame buffer maps; the console still does not draw

bootCZ is the same configuration plus four panel marks inside `initialize_screen`: slot 4 colours by
`boot_vinfo` (white when non-NULL, grey when NULL), slot 5 colours by `op` (yellow for
`kPEAcquireScreen`), slot 6 marks entry to the deferred branch, slot 7 marks a successful
`ml_io_map`.

On hardware the panel shows grey, yellow, blue, magenta.  So:

* `initialize_screen` reaches `kPEAcquireScreen` (yellow),
* the deferred branch is entered, which means `deferred_fb_pa` was recorded by an earlier call with a
  real video info (its white mark was overwritten by the later NULL call, which is the grey),
* `ml_io_map` succeeds (magenta).

The frame buffer is therefore mapped and `boot_vinfo` is set for the console, yet the panel keeps
showing the loader page: no kernel text is drawn.  The remaining suspects are the tail of
`initialize_screen` (`vc_probe`/`vc_init` on the synthesised `PE_Video`, including `v_physaddr` which
the deferred record leaves at zero) and `disableConsoleOutput`, which the video path sets once it
takes over.

The next diagnostic is two more marks: one after `vc_probe` returns inside the `if (boot_vinfo)` block
and one after the console is marked acquired.


## 04:20: the console prints, and the boot reaches launchd's exec

Two community findings closed the visibility problem in one step:

1. DarwinBoot hands the GOP frame buffer over **without** the low-bit 'physical' marker, so
   `makeMapping` in `initialize_screen` decides the buffer is already mapped.  uke's patch adds
   `|| (newVideoVirt < VM_MIN_KERNEL_ADDRESS)` to that test.  In this port the deferred mapping
   already produces a kernel virtual address, so the equivalent fix is the one already in place:
   the deferred branch maps with `ml_io_map` and sets `boot_vinfo` to the mapped video info.
2. **kprintf is silent in graphics boot** until the console is switched to text.  uke switches it in
   `vcattach()` with `initialize_screen(NULL, kPETextScreen)` when `gc_graphics_boot` is set.  With
   that added, the panel shows the full kernel log.

## What the panel now shows (bootDB)

```
apfs_log_op_with_proc: disk1s1 mounting volume OwnerData, requested by: Init (pid 1)
apfs_log_op_with_proc: disk1s1 mount-complete volume OwnerData
RAM_WATCHDOG registered matched started: original IOWatchdog/client; software deadline backend
owner watchdog override ready
load_init_program: attempting to load /usr/appleinternal/sbin/launchd.development
load_init_program: attempting to load /sbin/launchd
shared_region: 0x60550bc3fe98c61d ((launchd)) check_np(0x16f522bb0) vm_shared_region_start_address() failed
shared_region: 0x60550bc3fe98c61d ((launchd)) check_np(0x16f522150) vm_shared_region_start_address() failed
```

So the Data volume mounts, the watchdog backend registers with the original protocol, the launchd
job policy is written, and `load_init_program` reaches `/sbin/launchd` - which then fails in the
shared region check.  That is the next wall, and it is a known one: the shared region (dyld shared
cache) has to be established before PID 1 can exec, which is what the dyld cache carrier component
is for.  The failure means `vm_shared_region_start_address()` returned zero, i.e. the shared region
was never initialised for this boot.

## Reproduce

`bootDB.img` (`c5b9c491`): bootBW baseline + community RamWatchdog (with `ram-system-watchdog=1`) +
deferred frame buffer mapping with `ml_io_map` + the text-console switch.  Everything else from the
night stands; the panel is now a real console, which makes every next step observable.

## 04:25: watchdogd still exits, but the console names the reason: the Data volume is 32 MB

With the console printing, the boot up to the panic is readable end to end:

```
apfs_log_op_with_proc: disk1s1 mount-complete volume OwnerData
RAM_WATCHDOG registered matched started: original IOWatchdog/client; software deadline backend
load_init_program: attempting to load /sbin/launchd
fs_tx_enter_internal:27549: disk1s1 containermanagerd_system: ENOSPC: ttype INSERT ...
apfs_lowdisk_check_volume:285: disk1s1 sending lowdisk notification ...
pid 121 exited -- exit reason namespace 2 subcode 0x5, description none
coredump (watchdogd, pid 121): failed to open /private/var/cores/watchdogd.core: error 2
panic(cpu 0 caller ...): watchdogd[121] exited -- exit reason namespace 2 subcode 0x5
```

And the space statistics on the same panel:

```
spaceman_fxc_print_stats:473: disk1 dev 0 sfree 7801/8192 ... range 391:7801 95.22%
```

8192 blocks of 4096 bytes is **32 MB**: the seed Data volume is far too small for a real userland.
`containermanagerd_system` runs out of space during its first transaction, the volume raises a
low-disk notification, and `watchdogd` exits shortly after - this time not because the watchdog
service is missing (it registered), but because the system it depends on is out of room.

## Next step

Grow the Data seed.  The porting guide's `seed-data-grow` path exists for exactly this, and the OS
profile's data size option controls it; 32 MB cannot hold the launchd database, cores, and the
services' first writes.  Once Data has real room, the ENOSPC/low-disk storm should stop and the
critical-exit panic at pid 121 should be re-tested against a healthy filesystem.

## 04:45: a 2 GiB Data declaration carries the boot much further

The packed seed header (`DBRDISK1`) stores the declared disk size, so the 32 MB seed can be declared
as 2 GiB without changing its content; the RAM block driver then presents a sparse device of that
size.  `bootDC` is that change.

The panel now shows a boot that goes far beyond the previous wall:

```
fs_tx_enter_internal: disk5s1 cfprefsd: ENOSPC ...
fs_tx_enter_internal: disk5s1 apsd: ENOSPC ...
Process[166] crashed: livenessd. Too many corpses being created.
pid 143 exited -- exit reason namespace 2 subcode 0x5
panic(cpu 0 caller ...): watchdogd[143] exited
```

Compare with the previous image, which stopped at `containermanagerd_system` (pid 121) on disk1s1:
`cfprefsd`, `apsd` and `livenessd` were never reached before.  So the larger declaration works and the
service graph starts for real.

The remaining ENOSPC is now on **disk5s1**, a different volume from the seed's disk1s1 - most likely
the persistent overlay (or another mounted store) whose size is independent of the seed header.
`free_blocks 5249` with `num_blocks 6010` requests also suggests an APFS transaction that needs more
contiguous space than the volume can give.

Next: identify what disk5 is and give it room, or drop the persist overlay for bring-up, then re-test
whether the ENOSPC storm - and therefore the repeated service exits that end in the watchdogd
critical-exit panic - disappears.

## 05:30: the official seed grow needs macOS, and CI will not start

The official tool is `scripts/seed-data-grow.py --gib N BASE.dbr OUT.dbr` (docs/zh/guide/userdata.md:71):
it attaches a sparse raw image, runs `diskutil apfs resizeContainer`, and packs the result again; the
accepted range is 3-1024 GiB.  requirements.md lists it among the macOS-only steps, so it cannot run
on this Linux host.  Declaring a larger `logical` in the DBRDISK1 header (bootDC) presents a larger
sparse device but leaves the APFS container at 32 MB, which is why ENOSPC simply moved to disk5s1.

A workflow was added to the device repository (`seed-grow` branch, `grow-seed.yml`) that checks out
unixnu/UniXNU and runs the tool on a `macos-14` runner, but GitHub cancels the job before assigning a
runner (`runner: (none)`, zero steps, conclusion cancelled).  That is an account-level Actions policy
for macOS runners, not a workflow bug: the repository is public and the YAML is accepted (the run
appears), so the next move is either allowing macOS runners in the account's Actions settings, or
running `seed-data-grow.py --gib 8` on any Mac.

With a real 8 GiB container the ENOSPC storm should disappear, and the service exits that end in the
watchdogd critical-exit panic can then be judged on their own merits.

## 05:00: the block device map, and the wall that is now in front

`bootDD` prints every IOMedia with its size and UUID:

```
BLK: disk1 size=33554432   uuid=E553E004-A6C6-4992-B474-25EB0F160524
BLK: disk2 size=67108864   uuid=5811b9f7-...
BLK: disk3 size=33554432   uuid=8846FE78-...
BLK: disk4 size=67108864   uuid=992523ff-...
BLK: disk5 size=2147483648 uuid=2E2E1D80-7E05-4D95-9F2F-5B64E1A63C04
BLK: disk6 size=3038101400
```

So `disk5` is this image's Data: the device really is 2 GiB (the declared size works), while the
APFS container inside it is still 8192 blocks - `spaceman_fxc_print_stats: disk5 dev 0 sfree
7801/8192` - which is exactly the 32 MB seed.  The ENOSPC storm comes from services writing to that
container, so growing it is still the fix and still needs macOS.

On this image the boot does not reach the services at all: the panel stops at

```
load_init_program: attempting to load /sbin/launchd
shared_region: 0x... ((launchd)) check_np(...) vm_shared_region_start_address() failed
```

so `launchd`'s exec fails because the shared region (the dyld shared cache mapping) was never
established.  That is now the wall in front, and it is independent of the disk: fix the shared
region first, then the container size.

## 05:50: the official grow works on macOS CI, but this driver reports the grown seed as not ready

The macOS CI path is solved (the `macos-26` label is the one this account gets; `macos-14` is never
assigned, and the workflow needed `submodules: recursive`, a `--gib '${GIB:-N}'` default, and
`mkdir -p build`).  `seed-data-grow.py --gib 3` produced a packed seed with `logical` 3221225472 and
6 extents, i.e. a container that really is 3 GiB.

On hardware, that seed and the 8 GiB one both leave the board looping on

```
blockStorageDriver[108]: executeRequest: request failed to start!
blockStorageDriver[108]: device is not ready.
```

while the original 32 MB seed and the 2 GiB-declared variant (32 MB container, 3 extents) both give a
ready device: disk5 appears with the declared size and the boot reaches launchd and the services.
So the difference is not the declared size - it is the grown seed itself (6 extents, and an APFS
container whose metadata now lives across the device).

Candidates for the next session, in order:

1. check `NativeRamBlock`'s volume seed path for the grown seed: `native_ram_block_volume_seed` and
   the `persist::windowBlocks(pages)` store open, which needs a window on the UFS store; a 3 GiB disk
   has 786432 pages, so its bitmap alone is 96 KiB and the window must fit the real userdata
   partition.  A refusal there only leaves the disk volatile, but a failure to publish the media
   would explain a driver that never sees it ready.
2. compare the extents: 3 (working) against 6 (not ready), and read the seed back with the packed
   format parser to confirm the first and last pages.
3. try a grow that keeps the container small but inside the declared device, to separate 'container
   size' from 'seed identity'.

Everything before this step still stands: watchdogd is passed, the console prints, and the boot
reaches launchd with the 32 MB seed.

## 06:20: tree state note for the next session

The cached XNU tree was re-extracted during the seed experiments and is now inconsistent: the mockfs
archive step (`archive-07-build-mockfs-fixture`) fails compiling `bsd/miscfs/mockfs/mockfs_fsnode.c`
because the mockfs patches are not applied to that fresh copy yet.  Before the next kernel build:

```sh
rm -rf out/<device>/<profile>/<config>/tree/.cache/xnu-work
python3 ./unixnu build kernel -D ../UniXNU-instantnoodlep
```

so the tree is extracted and patched from scratch (about 15-20 minutes).  The RamBlock diagnostic edit
that was going to report `available()` fields was reverted, so the submodule is untouched.

Night's final state: bootDD remains the best image (console printing, watchdog backend registered,
2 GiB declared Data, boot reaching cfprefsd/apsd/livenessd); the macOS CI produces real grown seeds
but this driver reports them as `device is not ready`.

## 06:35: the 3 GiB seed is accepted; the panic is about the root manifest

With the block-device diagnostic in place the 3 GiB seed panics with a much more useful message than
the earlier loop:

```
mnt:3776: disk0 device_handle block size 4096 block count 786432
apfs_extract_root_hash_arm:3396: could not retrieve root hash (...): No such file or directory (2)
panic(cpu 0 caller 0xfffffff00bdd8410): UFS ROOT FAILURE: root manifest rejected
```

786432 blocks is exactly 3 GiB, so the grown seed **is** accepted through the >2 GiB path and the
earlier `blockStorageDriver ... device is not ready` loop was the consequence of the new disk taking
disk0 and the root publication failing behind it.  The real failure is in root publication:
`apfs_extract_root_hash_arm` cannot retrieve the root hash (ENOENT), so the manifest that the verified
root needs is rejected and the kernel panics.

Two things follow:

1. the RamBlock `available()` diagnostic never printed `RWBLK-DIAG`, which fits: the panic happens in
   root selection before the Data seed is consulted, so the Data disk had not been asked yet;
2. the next investigation is `apfs_extract_root_hash_arm` (ShengUfs/VerifiedRoot): why the root hash
   cannot be read for this image, and whether the grown Data disk changes the APFS volume layout that
   the extraction scans (disk numbering moved from disk1/disk5 to disk0).

The block-device diagnostic stays in the driver for now; it is a local edit to the RamBlock
submodule and must be reverted before any commit there.

## 06:20: userdata flash fixed the root manifest; the 3 GiB seed still stalls the storage stack

`fastboot -S 256M flash userdata userdata-root.sparse.img` finished (35/35 chunks, 247 s).  After that
the `UFS ROOT FAILURE: root manifest rejected` panic is gone, so the root manifest is back on the
device - the panic had been caused by the Android boots wiping the userdata partition this port's
manifest lives in.

The next wall is the one seen before: with the 3 GiB seed the panel loops on

```
blockStorageDriver[102]: executeRequest: request failed to start!
blockStorageDriver[102]: device is not ready.
```

and the USB console stops re-enumerating after a replug, so the kernel's storage path is wedged
rather than merely slow.  The RamBlock `RWBLK-DIAG` line that would report `online`, `isBig`,
`capacity` and `nonzero` never became visible because the loop floods the console and the serial
ring could not be read afterwards.

Next session, in order:

1. power-cycle the board so the storage stack and console come back clean;
2. drive the diagnostic to a readable place: either print it once into the panic screen path, or
   make `available()` cache its answer and print the first failure with a unique prefix that survives
   the flood (for example print the line only when `ok` is false, and also store the values in a
   global the block-device report can print later);
3. compare the `>2 GiB` path (`big.*`) against the `<=2 GiB` path: the 2 GiB-declared seed is ready
   and the 3 GiB grown one is not, and the docs say the typed provider switches to storing only the
   non-zero pages above 2 GiB, so that switch is where the difference must be.

Working images: bootDD (32 MB seed, reaches launchd and the services) and the 32 MB seed itself;
bootDG (3 GiB seed + RamBlock diagnostic) is the one that stalls.

## 06:45: the 3 GiB seed is healthy in the driver; the selection refuses two matches

`RWSTATE: online=1 mem=1 capacity=6291456 big=292` prints on every watchdog tick, so for the 3 GiB
seed the RAM block driver is fully healthy: the media is online, its backing store is present, the
capacity is 6291456 blocks (3 GiB) and the big path holds 292 non-zero pages.

The panel still loops on `disk1: device is not ready`, and the block list explains why:

```
BLK-TICK: disk0 size=3221225472
BLK-TICK: disk1 size=3221225472
BLK-TICK: disk1s1 size=3221225472
```

the same 3 GiB volume is published twice (disk0 and disk1) plus its partition.  `selectVolumeSeed`
counts matches by UUID and requires **exactly one**:

```c
if (++matches != 1) { selected = false; break; }
```

so with two candidate media the Data volume is never selected, the mount fails, the services that
need it fail, and the boot ends in the watchdogd critical-exit panic - the loop is the storage stack
retrying a device that the volume selection refused.

Next: print the UUID of each disk in the tick report (a string property) to confirm both carry the
seed UUID, then either de-duplicate the publication or relax the selection to prefer the partition
(`diskXs1`) when several media share the UUID.

## 06:50: launchd runs and finishes its early boot

With the 3 GiB seed, the partition-only selection and the diagnostics in place, the panel shows the
first userland boot in this port's history:

```
com.apple.xpc.launchd ... (tzinit) <Notice>: Doing boot task
[System Event] [Subsystem: launchd] [Event: finish-restore] Doing boot task
... (finish-demo-restore) (sysstatuscheck) (prng_seedctl) (launchd_cache_loader)
... (workload-properties-init) (init-exclavekit)
com.apple.xpc.launchd[1] ... [Event: boot] Early boot complete. Continuing system boot.
```

So `launchd` executes, runs its boot tasks and completes early boot.  The storage driver still
loops on `disk1: device is not ready` alongside it, which means the Data volume (`/private/var`) is
not mounted, but launchd itself is alive and progressing.

Notes for the next session:

* the loop names `disk1`, the whole media, while the seed volume is `disk1s1`; the mount path that
  asks for the whole disk is the thing to find (`native_owner_data_prepare` and the volume seed
  callers),
* `SEL-DIAG` output scrolled out of the visible area; next time print it only on failure and after
  the loop starts, or keep a copy in a global the tick report can print,
* the 32 MB seed still gives a complete boot to the service graph, so it remains the fallback while
  the grown-seed mount is fixed.

## 07:00: the 3 GiB seed runs on the sparse path; watchdogd still exits

Raising the big-path threshold from 2 GiB to 4 GiB puts the 3 GiB seed on the sparse path, which has
a volatile write mode.  The panel then reports

```
RWSTATE: online=1 mem=1 capacity=6291456 big=0
```

and the `device is not ready` loop is gone: the APFS mount no longer has its writes refused.  The
volume UUID `E553E004-A6C6-4992-B474-25EB0F160524` appears on `disk5s1` in the block list, i.e. the
grown Data volume is published and selected.

The boot still ends in the critical-exit panic, now at an early pid:

```
pid 98 exited -- exit reason namespace 2 subcode 0x5, description none
coredump (watchdogd, pid 98): failed to open /private/var/cores/watchdogd.core: error 2
panic(cpu 0 ...): watchdogd[98] exited -- exit reason namespace 2 subcode 0x5
```

`/private/var/cores` returning ENOENT suggests `/private/var` is still not usable when watchdogd runs,
so the next question is whether the Data volume finished mounting before launchd started the
services (the earlier `OwnerData` mount-complete line is not visible in this boot's tail).

Sequence to check next:

1. print the mount-complete line for the Data volume into the tick report (the kernel knows
   `native_owner_data_stage`), so the panel always shows whether `/private/var` is up;
2. if the mount completes late, start the watchdog backend earlier (it is prepared from the same
   hook that runs after the Data prepare, so its timing follows);
3. if the mount never completes, the sparse write path needs the same treatment the big path got.

## 07:10: the 3 GiB Data volume mounts (dataStage=4) and the service graph starts

`RWSTATE: online=1 mem=1 capacity=6291456 big=0 dataStage=4` on every tick, with
`BLK-TICK: disk5s1 size=3221225472 uuid=E553E004-A6C6-4992-B474-25EB0F160524` in the same list:

* `dataStage=4` is the Data prepare success stage, so `/private/var` is mounted;
* `big=0` with `capacity=6291456` confirms the 3 GiB seed runs on the sparse path (writes allowed);
* the volume UUID is the one the configuration expects;
* `device is not ready` no longer appears.

The panel also shows `Process[176] crashed: livenessd. Too many corpses being created.`, i.e. launchd
is starting services and some of them crash - the same class of failure this port saw with the 32 MB
seed, but now on a 3 GiB writable Data volume.

Two changes made this work: the partition-only volume selection (whole media are skipped, so the
exactly-one rule matches the seed partition) and raising the sparse/big threshold from 2 GiB to 4 GiB
(the big path has no volatile write mode before the persistent store attaches, which is what produced
the endless `device is not ready` loop).  Upstream keeps seeds below 2 GiB for exactly this reason
(`docs/zh/devices/mondrian-mzyxsl.md:267`).

Next: the crashing services.  With Data writable the earlier ENOSPC cascade should be gone, so the
crashes must have another cause - their exit reasons and any `too many corpses` context are the
next thing to capture.

## 07:20: every service exits with the same code - a systemic dyld/shared-region failure

The exit observation hook prints one line per process, and the pattern is unambiguous:

```
XNU-EXIT: chronod          pid=230 code=9 ns=1 sub=0x4
XNU-EXIT: containermanager pid=213 code=9 ns=1 sub=0x4
XNU-EXIT: contextstored    pid=222 code=9 ns=1 sub=0x4
XNU-EXIT: nfcd             pid=197 code=9 ns=1 sub=0x4
XNU-EXIT: ptpd             pid=207 code=9 ns=1 sub=0x4
XNU-EXIT: securityd        pid=151 code=9 ns=1 sub=0x4
XNU-EXIT: watchdogd        pid=99  code=5 ns=2 sub=0x5
```

Everything except watchdogd exits with the **same** status (9, namespace 1 subcode 4), so this is not
per-service breakage: the binaries cannot run at all.  That matches the shared region failure the
console reported earlier:

```
shared_region: ... check_np(...) vm_shared_region_start_address() failed
```

`vm_shared_region_start_address` returns `KERN_INVALID_ADDRESS` when the shared region has no mapping
(`sr_first_mapping == -1` in `vm_shared_region.c`), i.e. the dyld shared cache was never mapped into
the shared region.  Without it dyld cannot start any image, which is exactly what a uniform exit code
across every job looks like.  watchdogd's different code (namespace 2) is its own breakpoint path.

Next: establish the shared region before PID 1 and the services run.  The port carries a dyld cache
carrier component for this; the things to check are whether it is selected in the configuration and
whether it publishes the cache in a way `vm_shared_region` accepts (region created, first mapping
recorded) before launchd starts spawning jobs.

## 13:35: where the dyld shared region stands after the docs and repository survey

What the documentation says:

* `docs/zh/xnu/patch-reference.md:55` - `offline-owner-cache-carrier` "selects the owner dyld cache
  carrier and validates the device and path"; `concepts/security.md:47` calls it the code-signing
  cache carrier.
* `devices/merlinx:197` - the owner path mounts the SystemOS cache carrier and a packed Data seed;
  `devices/nabu:220` - the SystemOS cryptex union-mounts as the cache carrier.
* The community port that reaches SpringBoard (GFander/UniXNU-kebab) solves the same problem from the
  other side: `kebab_dyldcache.c` pre-maps the split cache into the shared region with
  `vm_shared_region_map_file()`, the primitive dyld itself reaches through
  `shared_region_map_and_slide_2_np()`, and `0103-kebab-dsc-slide.patch` applies the slide the
  pre-mapped path leaves at zero.

What this port shows:

* `RWSTATE ... carrier=0/0`: the carrier hook inside `vm_shared_region_map_file` (added by
  `offline-owner-cache-carrier.patch`) is never reached, so dyld never maps a cache through the
  syscall the hook guards;
* the uniform `code=9 ns=1 sub=0x4` on every service means they fail before they run, which is
  consistent with dyld being unable to establish the shared region;
* the cache itself is present: scanning `userdata-root.img` finds `/System/Library/dyld`,
  `dyld_shared_cache` and `com.apple.dyld` (the exact `dyld_shared_cache_arm64e` name is not in the
  image's plain text, which fits a split/cryptex cache).

Conclusion: the missing piece is the shared region mapping step, not the cache file.  The two
documented ways to supply it are the carrier path this port already patches (dyld maps the cache and
the carrier authorises it) and the kebab approach (the kernel pre-maps it before launchd).  Checking
whether dyld ever calls `shared_region_map_and_slide_2_np` - a counter at the entry of
`vm_shared_region_map_file` - is the next measurement, and the kernel-side pre-map is the fallback if
dyld never gets that far.

## 13:35: the shared region is fine and SpringBoard is being spawned

`SR-MAP: calls=1 failures=0` puts the shared-region question to rest: dyld does call
`vm_shared_region_map_file`, and the call succeeds, so the cache is mapped and the earlier
`carrier=0/0` only meant the carrier hook was not the path taken (the cache was on the process root's
own mount).

The exit log then shows how far userland has come:

```
XNU-EXIT: SpringBoard      pid=132 code=5 ns=2 sub=0x5
Process[132] crashed: SpringBoard. Too many corpses being created.
XNU-EXIT: liveactivitiesd  pid=133 code=5 ns=2 sub=0x5
XNU-EXIT: bluetoothd       pid=134 code=0 ns=0 sub=0x0
XNU-EXIT: fairplayd.A2     pid=126 code=0 ns=0 sub=0x0
```

* `bluetoothd` and `fairplayd.A2` exit cleanly (code 0, no reason), i.e. those services ran;
* `SpringBoard` and `liveactivitiesd` die on a breakpoint (`ns=2 sub=0x5`), which is a normal
  bring-up failure for a service that finds something missing, and SpringBoard being spawned at all
  means launchd got far enough to start the UI process.

Next: catch SpringBoard's breakpoint - the kernel already reports the user backtrace for a critical
exit in some community ports (`0277-kebab-critical-watchdog-user-backtrace.patch`); the same
treatment for SpringBoard, or reading its crash reason from the panic text, is the next step.

## 14:10: the serial tee does not deliver, and the watchdog deadline is no longer fatal

Two attempts closed out the night:

* `vc_paint_char` was patched to mirror every drawn character into `qcom_usb_debug_putc` (patch kept
  as `zzz-video-console-tee.patch`, not in the series).  The board boots with it but the host reads
  zero bytes at both 115200 and 921600, so the module does not flush what the tee writes; the patch
  is out of the series until that is understood.
* the RamWatchdog backend no longer panics when its software deadline expires: it prints
  `RAM_WATCHDOG deadline passed (not fatal)` once and stops the timer.  The deadline had fired with
  `beats=0` because nothing pets the watchdog on this port (watchdogd exits early), and that panic
  was masking whatever happens next.

Latest image: `bootDW` (`15d09978`) has the non-fatal deadline; `bootDU` (`df7853a5`) is the one that
reached SpringBoard with `SR-MAP: calls=1 failures=0`.


## 14:50: services loop with exit status 9; the shared region is in use

On `bootDZ` the panel shows a steady stream of

```
XNU-EXIT: announced      pid=586 code=9 ns=1 sub=0x5
XNU-EXIT: triald         pid=638 code=9 ns=1 sub=0x4
XNU-EXIT: transparencyd  pid=631 code=9 ns=1 sub=0x4
XNU-EXIT: corespeechd    pid=624 code=9 ns=1 sub=0x4
XNU-EXIT: coreduetd      pid=629 code=9 ns=1 sub=0x4
XNU-EXIT: bluetoothd     pid=646 code=0 ns=0 sub=0x0
```

so consecutive generations of the same jobs keep starting and exiting, while `bluetoothd` exits
cleanly.  Status 9 is the usual 'daemon could not initialise' exit, and a whole class of jobs sharing
it means a common dependency is missing rather than a per-service bug.

The same panel also shows

```
MobileGestaltHelper[647] triggered unnest of range 0x1f6400000->0x1f6600000 of DYLD shared region
```

which proves the shared cache is mapped and in use, closing that question for good.

The heartbeat block stops by design on this image: the non-fatal watchdog change disarms the timer on
the first expired deadline.

Next: find the common dependency.  The cheapest measurements are (a) print the first N exec paths that
`load_init_program`/`posix_spawn` launch, so the failing binaries are known by name, and (b) catch one
job's failure with more detail - for example run its executable from a serial console once the system
stays up.

## 15:10: why the serial log is not deliverable yet

Three attempts to capture the panel log over the USB console all returned zero bytes:

1. reading the ring after the boot - empty, because the video console draws instead of printing;
2. teeing `vc_paint_char` into `qcom_usb_debug_putc` - boots, host sees nothing;
3. teeing into `qcom_usb_debug_enqueue` (backlog only, the late worker drains it) - same.

So the module is not delivering *late* console data to the host even though it enumerated and
served the early boot.  The status structure (`include/QcomXnuUsbDebug.h`) carries everything needed
to find out why - `phase`, `buffer_pending`, `buffer_accepted`, `buffer_overflow`,
`transport_pending`, `poll_calls` - and printing `phase` from the watchdog tick is the next
measurement.  Two attempts at that print failed on C++ linkage and on where the declaration landed,
so the patch is reverted and the tree is clean again.

State left behind: `zzz-video-console-tee.patch` exists but is not in the series; the RamWatchdog
backend is back to its working form (non-fatal deadline, tick report, no USB calls).

## 15:10: the serial tee works; the console module's transmit path is stuck

`USBPHASE` prints the console module status from the watchdog tick:

```
USBPHASE: phase=3 pending=207  accepted=16671 transport=16384
USBPHASE: phase=3 pending=485  accepted=16869 transport=16384
USBPHASE: phase=3 pending=720  accepted=17104 transport=16384
USBPHASE: phase=3 pending=918  accepted=17302 transport=16384
```

The phase enum in `include/QcomXnuUsbDebug.h` is

```
0 NOT_INITIALIZED  1 DISABLED  2 PREPARING  3 ACTIVE  4 STOPPED  5 REFUSED  6 IO_FAILED
```

so `phase=3` is **ACTIVE**: the module is alive, `accepted` grows with every painted character, and
the tee is doing its job.  What does not move is `transport` (16384 bytes, constant) while `pending`
grows, i.e. the USB IN transmit path stopped draining after the early boot.  That is why the host
reads zero bytes even though the queue is filling.

Next: the transmit path inside the console module.  `transport_pending` staying at exactly 16384
suggests one queued IN transfer never completes (endpoint not re-armed, or the poll that would re-arm
it no longer runs after the video console takes over).  Printing `poll_calls` and `io_failed` from the
same status structure, and the transfer length at issue, is the next measurement.

## 15:25: poll runs, no IO errors, one 16 KiB IN transfer never completes

```
USBPHASE: phase=3 poll=5096 ioFail=0 pending=408  accepted=16792 transport=16384
USBPHASE: phase=3 poll=5081 ioFail=0 pending=681  accepted=17265 transport=16384
USBPHASE: phase=3 poll=5106 ioFail=0 pending=1019 accepted=17403 transport=16384
```

So the console module is ACTIVE, its poll runs thousands of times, no I/O error is recorded and the
tee keeps filling the queue - but `transport_pending` is pinned at exactly 16384 bytes while
`buffer_pending` grows.  That is one queued IN transfer that never reports completion; once the
transport buffer is full the module waits for an event that never arrives, and the host reads
nothing.

Three candidates, in order of likelihood:

1. the DWC3 IN endpoint's TRB chain is not re-armed after the early boot;
2. the host driver keeps no read pending when the port is only opened (Windows can hold reads until
   an application asks), so the IN transfer has nobody to complete against;
3. the endpoint was reset or disabled and never re-enabled.

The next measurement is the IN endpoint state in `usb/Dwc3.c` (endpoint RUN/ACTIVE bits and the TRB
dequeue pointer) and a transfer length/status print at submit and completion.

## 15:20: the console only transmits while the host holds DTR

The serial mystery is host-side, not kernel-side.  `usb/Dwc3.c` `pump()` starts an IN transfer only
when the host has raised DTR:

```c
if (!d->dtr || d->active[EP_IN]) return 0;
```

and .NET's `SerialPort.DtrEnable` defaults to false.  Every capture that read zero bytes opened the
port without DTR, so the module kept buffering (`accepted` and `pending` growing, `transport` pinned
at the empty buffer) and never sent anything.  The one early capture that returned 3843 bytes had
`DtrEnable = $true`.

So the transport telemetry was right all along: the module was waiting for DTR.  Correct capture:

```powershell
$s = New-Object System.IO.Ports.SerialPort 'COM14',115200,'None',8,'One'
$s.DtrEnable = $true
$s.Open()
# read while DTR stays raised, then close
```

This also explains why the late-boot log never arrived even though the tee worked: the module had a
16 KiB buffer and a queue, and with no DTR it never submitted the transfer that would drain them.

## 15:55: the late-boot crash is a NULL dereference inside thread_call_invoke

The panel ends with

```
panic(cpu 0 caller ...): Kernel data abort at pc 0xfffffff00b8d91d8, lr 0xfffffff00b8d91bc
esr: 0x96000005   far: 0x0000000000000000
```

and `llvm-nm` puts `0xfffffff00b8d91d8` at `_thread_call_invoke + 0x94`.  `far = 0` means the faulting
address was NULL, so something dereferences a null pointer inside the callout machinery.

The watchdog backend is the obvious first suspect, but its timer is a normal
`thread_call_allocate(expired, this)` and the crash persists with the timer capped at sixty beats,
so the failure is in the thread_call subsystem itself rather than in the re-arm rate.  That matters
far beyond the console: XNU's own periodic work rides the same machinery, and a broken thread_call
would also explain services failing uniformly with status 9.

Next, in order:

1. run one boot with the watchdog backend **not selected** (no `ram-system-watchdog=1`) and see
   whether `thread_call_invoke` still faults - that separates 'our timer' from 'the subsystem';
2. if it still faults, inspect how this port's platform timer feeds the callout thread (the ARM
   generic timer, `machine_routines` and the timer interrupt path);
3. symbolise any further faulting PC the same way (`llvm-nm --numeric-sort` on `kernel.macho`),
   which has been the fastest way to name a crash all night.
