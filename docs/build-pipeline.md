# Build pipeline

The official flow splits the work by host: [Building](https://unixnu.com/building)
runs the kernel step with Xcode, and the userdata and payload steps need
`hdiutil`. This port therefore uses two lanes.

| Lane | Host | Steps |
| --- | --- | --- |
| A. Local | Windows + WSL2 Ubuntu 22.04 | `defconfig`, `status`, `tree`, UEFI firmware (EDK2 aarch64 cross build) |
| B. CI | GitHub `macos-26` runner | `fetch`, `build kernel`, later `build userdata` and `build payload` |

Neither lane builds the boot image: `bootimg` needs the firmware volume from
lane A and the payload from lane B, so it runs where both artifacts are present.

## Lane A: WSL

The `unixnu` CLI refuses a Python older than 3.11, and Ubuntu 22.04 ships 3.10.
`uv` installs a standalone 3.12 in the home directory, so no root password is
needed:

```sh
pip3 install --user uv
uv python install 3.12
PYBIN=$(uv python find 3.12)
```

Then, from the UniXNU checkout that sits beside this repository:

```sh
cd /mnt/c/Users/132/Documents/oneplus/unixnu
"$PYBIN" unixnu defconfig instantnoodlep_defconfig -D ../UniXNU-instantnoodlep
"$PYBIN" unixnu status -D ../UniXNU-instantnoodlep
"$PYBIN" unixnu tree   -D ../UniXNU-instantnoodlep
"$PYBIN" scripts/porting-record.py check --device ../UniXNU-instantnoodlep
```

`tree` assembles `out/instantnoodlep/ipados-17.7.11-21H461/instantnoodlep/tree`
with the device package at `kits/instantnoodlep`. The capture is in
`docs/evidence/host-tree-wsl.log`.

Windows Python cannot run `tree`: assembling the tree creates symbolic links and
a non-elevated Windows session refuses them (`WinError 1314`). Use WSL for that
step.

The Ubuntu image already provides what the firmware build needs:
`aarch64-linux-gnu-gcc`, `iasl`, `nasm`, `dtc`, `mtools`, `g++`, `uuid/uuid.h`
and `openssl/ssl.h`. `aarch64-linux-gnu-g++` and `xorriso` are absent and are not
required for an AArch64 EDK2 build.

## Lane B: GitHub macOS runner

`.github/workflows/macos-kernel.yml` is a thin, self-contained copy of
UniXNU's own `macOS build` workflow. The upstream file is `workflow_dispatch`
only, so this repository cannot call it; the copy runs the same documented path
on `macos-26` with Xcode 26 and publishes text logs only.

Dispatch it with:

| Input | Value |
| --- | --- |
| `unixnu_repository` | the UniXNU fork or `UniXNU/UniXNU` |
| `soc_repository` | the published `UniXNU-sm8250-common` (owner/name) |
| `defconfig` | `instantnoodlep_defconfig` |
| `xcode` | `26.6` |
| `pre_build` | empty, or the board's staged-input commands |

The job checks out UniXNU at `unixnu/`, the SoC repository at
`UniXNU-sm8250-common/` and this repository at `device/`, so the `local` pin in
`feeds.toml` (`../UniXNU-sm8250-common`) resolves exactly as it does locally.

## What is still blocked

| Gate | Effect |
| --- | --- |
| `UniXNU-sm8250-common` is not published | lane B cannot check it out; `soc_repository` has no real value yet |
| `Platforms/OnePlus/instantnoodlepPkg` does not exist | `build firmware` has no DSC; lane A is limited to `tree` |
| `board.example = true` | `build` and `bootimg` refuse to run until the measured inputs replace the REQUIRED values |
| No IPSW, trust cache or Data seed | `build userdata` and `build payload` have no inputs yet |

## Order once the gates open

1. Lane A: `build firmware --uefi ../uefi-instantnoodlep`.
2. Lane B: `build kernel`, then `build userdata`, then `build payload`.
3. Local: `bootimg --uefi ../uefi-instantnoodlep --dtb <board.dtb>`, then
   `fastboot boot` with the bootloader unlocked.
