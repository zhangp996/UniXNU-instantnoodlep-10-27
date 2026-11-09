# Kernel hand-off probes and the frame-buffer hand-off fix (2026-11-09)

## What the panic screen showed

Image `bootE.img` (payload with `panic_screen = true`, probe built with
`--no-reset`): the menu appeared, the default xnu entry ran, the panel drew the
green DarwinBoot bar, the blue kernel-entry bar and the magenta pre-MMU bar, and
the device then fell into Qualcomm 900E with no further text.

The three bars prove the bootloader hand-off, the MMU-off kernel entry and the
code immediately before the MMU is switched on. The missing text was not proof
that no panic happened: the panic screen reads `vinfo.v_baseaddr`, and that
stays zero until `PE_create_console()` reaches `initialize_screen()`.

## Reset probes

`[payload] entry_probe_reset_delay = 3` with `entry_probe_reset_after = "<symbol>"`
moves the probe from the MMU-enable point to three seconds after the named call.
A warm reset back to Android means the call returned; 900E means the kernel died
at or before it.

| Probe (call #1 in `_arm_init`) | Observation | Reading |
| --- | --- | --- |
| `_arm_vm_init` | device reset by the probe | `arm_vm_init` returned |
| `_kernel_startup_initialize_upto` | device reset by the probe | the KPRINTF startup entries completed |
| `_PE_create_console` | 900E after about six seconds | died in console creation |

## Cause

`initialize_screen()` in `osfmk/console/video_console.c` chooses whether to map
the frame buffer with

    makeMapping = (kernel_map == VM_MAP_NULL) || (0 != (1 & newVideoVirt));

Bit 0 of the handed-off video base marks a **physical** address that the kernel
must map itself; with an even base and the VM up, the value is used as a virtual
address. `DarwinBootVideoFromGop()` passed the raw GOP physical base
(`cont_splash_region@9c000000`, 0x2300000 bytes, panel 1080x2376 at 32 bpp), so
XNU wrote the console into unmapped address 0x9C000000 and the first store
faulted.

The stock device tree also names `dfps_data_region@9e300000` + 0x100000 and
`kboot_uboot_logmem@0x9FE00000`, which bound the free window the firmware volume
uses.

## Fix

`drivers/DarwinBoot/DarwinBootPkg/Driver/BootVideo.h`:

    Video[0] = Gop->Mode->FrameBufferBase | 1;

The firmware volume was rebuilt with the board's own platform
(`build -p Platforms/OnePlus/instantnoodlepPkg/instantnoodlep.dsc -a AARCH64
-b DEBUG -t CLANGPDB -D FD_BASE=0x9e400000 -D FD_SIZE=0x400000 -D FD_BLOCKS=1024`):
4,194,304 bytes, sha256
93dbf6b8ed97720c007a404e3f60f667d4e17c057cc464dc5691e16e47c2482f.

## Result

`bootJ.img` (new FD + panic-screen payload, 107,507,712 bytes, sha256
3906afb7e63cccefe6a86391845d9ba72eb813d4193f22fdf7618f7aa3248cc0) puts the kernel
console on the panel: after the three bars the `-v` kernel output is visible.
The text captured from that screen is the input to the next fix.

## Side observation

The generated ADT exports only `intc` (GICv3) under `arm-io`; there is no uart
node, so `serial_init()` reports no serial console and the panel is the only
console. `boot-progress=1` is a no-op in this build: the kernel has
`kern/boot_progress.h` but no implementation, and `XNU_BOOT_PROGRESS` is not
defined.
## Second round: the probe paints at the tagged address

With `Video[0] |= 1` in the firmware, the first image (`bootJ.img`) reached the
panic screen but reported `Data abort: Alignment fault` with `FAR 0x9C09C9F1`.
That address is `0x9C000000 + 2376 * 4320 / 16 + 1`: the entry probe's own blue
bar paints at `video base + framebuffer size / 16` with `stp`, so the tag bit
turned the write unaligned while the MMU was still off.

`scripts/patch-xnu-entry-probe.py` now clears the low three bits of the base
before painting (`bic x1, x1, #7`). `bootK.img` paints the bars correctly but
still ends in 900E about three seconds after them, with no console text.

## Console init still does not return

With the tagged base and the paint fix, a reset probe placed on
`_initialize_screen` inside `_PE_initialize_console` did not fire (900E about
five seconds after the bars).

The reference port for the same board (`F:\UniXNU-instantnoodlep`, recorded in
its `porting.toml`) reached the same wall and records three things that change
how the probes are read:

* `PE_state.video.v_baseAddr` is the physical frame buffer; drawing through it
  faulted at 0x9c00a8e0, and asking `ml_io_map` for the whole panel panicked in
  `kmem_alloc` (`round_page(1080 * 2376 * 4) = 10264576`, `KERN_NO_SPACE`).
* It draws through `vinfo.v_baseaddr` instead, which it describes as already
  mapped.
* The entry probe's inline assembly is only valid while the identity mapping is
  in place, so a probe that does not fire after the pmap switch is not proof
  that the kernel died there.

## Boot arguments from the reference port

`board.toml` now carries the reference port's arguments for this board:

    kernel_args = ["-v", "serial=3", "debug=0x14e", "-no_slto_panic",
                   "amfi_get_out_of_my_way=1"]

Its record says AMFI panics with "No PMGR?" until the board has a PMGR provider
and that the spinlock timeout panic has to be turned off before the PMGR work,
which matches the three-to-six second gap between the bars and 900E.
`bootN.img` is the image built with them.
