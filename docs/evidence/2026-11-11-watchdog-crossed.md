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
