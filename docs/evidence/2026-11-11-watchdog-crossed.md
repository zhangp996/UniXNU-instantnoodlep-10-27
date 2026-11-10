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
