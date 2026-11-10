# Root mounted, launchd blocked on the dyld cache (2026-11-10)

Continues `docs/evidence/2026-11-10-storage-link.md`. The userdata image is written, the root mounts
on the device, and the boot now stops at the shared cache instead of the mount.

## What the device did, in order

| Image | Change | Panel |
| --- | --- | --- |
| bootZA | personality compiled in, `native-ufs-probe=10` | `NATIVE_UFS root check`, `userdata first=3f13a8 last=1d197fa`, block 0 read back |
| bootZB | userdata flashed, root path only | `failed to mount root device` (publication gated on the owner policy) |
| bootZE | `native-ufs-allow-unverified=1`, diagnostics panic | `UFS Root FAILURE: SystemOS signed volume not found`, `stage=media publication`, a completed READ(10) in the dump |
| bootZG | the unverified build also skips the signed-UUID wait | `rootvp not authenticated after mounting @bsd_init.c:1107` - the root volume mounted |
| bootZH | `root-unverified=1` (`patches/xnu/zzz-root-unverified.patch`) | `initproc failed to start ... Library not loaded: /usr/lib/libSystem.B.dylib ... no dyld cache` |

The UFS stack is therefore complete on the phone through the mount: controller hand-off, DMA,
command path, GPT, both windows, IOMedia publication, APFS on the OS window, and the root vnode
handed to userland. What stops the boot now is the shared cache: `/usr/lib/libSystem.B.dylib` lives
in the SystemOS (Cryptex) volume, and the kernel does not mount it.

## The unverified switches

Both exist only in the unverified compile variant and both are opt-in at boot, so a verified build
behaves exactly as upstream:

- `native-ufs-allow-unverified=1` (`NativeUfs.cpp`): the publisher's identity phase has no signed
  policy to compare window identities against, so it publishes when the operator asks for it.
- `root-unverified=1` (`patches/xnu/zzz-root-unverified.patch`): `bsd_init.c` skips the
  `rootvp not authenticated` panic. Without the argument the check still panics.
- `native-ufs-diagnostics=1` turns the driver's soft root failures into panics that name the stage,
  which is what produced every line in the table above.

## The dyld cache wall

`XNU_OWNER_CACHE_MOUNT` requires `XNU_OWNER_ROOT_TRUST`, which requires `XNU_OWNER_ROOT_AUDIT`, which
requires `XNU_VERIFIED_ROOT_PROBE` or `XNU_SOURCE_STORAGE` - the owner-signed policy chain this board
has not built (`scripts/build-owner-os-policy.py` wants an owner key and a macOS-built
`verified-root.dylib`). The reference port's alternative is board code that mounts the Cryptex volume
without the owner signature.

## USB debug console

Integrated, not yet enumerating:

| Where | Change |
| --- | --- |
| device `feeds.toml` | pins `FFT-infra/QcomXnuUsbDebug` at `ab6e3635`; `local.toml` points at the working tree |
| device `patches/xnu/series` | `zzz-qcom-usb-debug-console.patch` and `-worker.patch` (the module's darwin23 set, which applies cleanly to XNU 10063.141.1) |
| UniXNU `components.toml` | `QcomXnuUsbDebug` entry, built by `build-qcom-usb-debug.py` |
| device `scripts/build-qcom-usb-debug.py` | runs the module's own builder with this tree's `pe_kprintf.o.json` and `soc/Sm8250.h`, then adds the board hooks |
| device `package/QcomUsbBoard.c` | `boot_progress_persist`, `boot_reset_bootloader` (PSCI SYSTEM_RESET), and `qcom_usb_board_check`, which panics with the module status under `qcom-usb-report=1` |
| UniXNU `build-xnu-4k-baseline.sh` | `XNU_QCOM_USB_DEBUG` validates and force-loads the archive |
| `board.toml` | `qcom-usb-debug=1 qcom-usb-ctl=1 qcom-usb-report=1` |

The console's poll worker starts in `bsd_autoconf()`, before the launchd attempt, so the report image
states on the panel why the controller hand-off was refused (phase, error, core result, PHY/VBUS
ownership, hardware parameters). No `VID_1209:316D` device appears on the host yet.
