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

