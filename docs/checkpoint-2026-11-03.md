# Checkpoint 2026-11-03 — owner root, Data volume, launchd

State of the instantnoodlep port at the end of 2026-11-03. Everything here was measured on
hardware today; the numbers are the page readings, not intentions.

## Where the boot stands

```
bsd_init done → load_init_program enter
  → SystemOS mount enter / returned        owner cryptex mounted (no panic)
  → Data mount enter / returned            /private/var mounted read-write (no panic)
  → temporary watchdog launch policy enter / ready
  → exec load_machfile enter / returned
  → launchd exec process signature accepted
  → p1 init status=0 exit=0                PID 1 exits immediately
```

Page at rest: `root=1  procs=1  requests=330  published=2  RAMDISK reads=57 capMiB=32`,
machine alive, `cache=0/0`.

## What today settled

| Item | Evidence |
| --- | --- |
| Owner root policy signed over the window the manifest declares | `d8808bea…`, 1,483,776 blocks; the previous signature covered 0x100000 and 34,304 bytes more, so it never matched |
| userdata flashed completely (8.74 GiB, 12/12 chunks) | staging the image on WSL-local storage first; 957 s, 24 OKAY lines |
| owner root authenticated | `root=1`, four runs |
| SystemOS cryptex mounted | the cryptex stage returns; its volume UUID is the signed 8846fe78-… |
| Data volume mounted read-write | `native_owner_data_prepare` refuses a read-only mount, and it returned 0 |
| launchd execs and passes AMFI | `exec load_machfile returned` then `signature accepted` |
| PID 1 then exits 0 | no panic, and `load_init_program` panics if every candidate fails, so the program really ran and exited |

## The open question

`load_init_program()` panics when exec fails (`Process 1 exec of %s failed, errno %d`), and no
panic appears, so exec succeeded and launchd itself exited 0. docs/devices/fuxi-mouse233.md
records the same shape as *"dyld had no cache and the process ended before main"*, fixed there by
making the shared-region map succeed.

`CONFIG_CSR=0` on this kernel (the `nor preboot volume` string lives in the `#else` branch of
`bsd/vm/vm_unix.c`), so the carrier instrument's `!CONFIG_CSR` guard is satisfied. `cache=0/0`
therefore means the shared-region map was never called: dyld exits before mapping.

Two candidate cache paths disagree:
- kernel env: `DYLD_SHARED_CACHE_DIR=/private/preboot/System/Library/Caches/com.apple.dyld`
- system volume view: `/System/Cryptexes/OS -> ../../private/preboot/Cryptexes/OS`

## Next instrument (written, not yet in a green build)

`patches/xnu/kebab-route/kona-cache-path.patch` looks up both candidates after the SystemOS
mount and prints `cache a=<err> b=<err>`. It is currently **out of `patches/xnu/series`** because
the run that carried it died on a different link error first.

The link error was `_native_owner_cache_carrier_accepts/rejects` undefined: the counters were
declared `__attribute__((weak))` without an initializer, and clang's default `-fno-common`
turns that into an undefined reference. Fixed in `ceb120f` with `= 0`; CI for that commit is
what the next session should read first.

## Reproduce a run

```sh
# 1. build the image from a CI artifact
cd unixnu
DATA_SEED=/path/owner-data-32.cdr sh ../_staging/build-run.sh <ci-dir> out.img \
  native-ufs-delay=10 native-ufs-wake-first=1 native-ufs-paint=1 native-ufs-noirq=1 \
  native-ufs-gic-mask=1 native-ufs-settle=5000 native-ufs-heartbeat=1 \
  native-ufs-post-delay=200000 native-ufs-directcpy=1 native-ufs-slots=2 \
  -apfs_shared_datavolume

# 2. boot and PULL THE CABLE within two seconds: with USB attached the machine stops at ~260 reads
wsl -u root bash -lc 'usbipd attach --wsl --busid 1-1; fastboot boot out.img'
```

The Data seed must be the one from the same artifact: the kernel bakes its volume UUID
(`XNU_OWNER_DATA_VOLUME_UUID`).

## Repositories

- Device repository `main` at `ceb120f`, pushed. `porting.toml` carries the day in findings
  and `docs/evidence` the frames: `device-runBW-*`, `device-runBZ-panel.png`.
- `zhangp996/UniXNU` branch `instantnoodlep` at `6fdeb53`, pushed: the verified-root probe
  locates its recipe, the two RAM transports and KonaUfs compile against the policy, and the
  bilingual pages record the IOKit-media and whole-disk-window findings.

## Order for the next session

1. Read the CI result for `ceb120f`; if green, boot it and read `cache=` on the page.
2. Re-add `kona-cache-path.patch` to the series and read `cache a= b=`. Fix the mountpoint or the
   environment path accordingly (one line either way).
3. If the cache is found and launchd still exits, trace PID 1's exit path: the page already prints
   `p1 init status=0`, so the next step is a stage inside `exit_with_reason` that names
   `p->p_comm` after exec (kern_exit.c compiles `boot_progress_stage` as a no-op macro, so it must
   be declared without the header).
4. Only after launchd stays: watchdogd, xpcproxy, then display.
