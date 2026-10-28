# xnu patches for this board

| Patch | State |
| --- | --- |
| `kona-ufs-driver.patch` | applied by `series` |
| `kona-ufs-integration.patch` | applied by `series` |
| `kona-hfs-sources.patch` | **staged, not in `series`** |
| `kona-hfs-build.patch` | **staged, not in `series`** |
| `kona-hfs-register.patch` | **staged, not in `series`** |

## UFS driver

Ported from GFander/UniXNU-kebab `0070`–`0072` (commit 786f317). The driver is BSD-level:
it registers a `bdevsw` disk over LUN 0, finds one GPT partition by name (`ufsroot=`,
default `userdata`) and exposes it as `ufsroot0`; the root hook lets `rd=ufsroot0` skip
IOMedia matching. It needs no `IOStorageFamily` kext, which is why it avoids the
`IOBlockStorageDevice` vtable that the archive-form ShengUfs cannot link.

The register map it relies on matches this board's measurements: host `0x1d84000`, request
list run/stop at `0x60`, PRDT entries `{addr_lo, addr_hi, rsvd, dbc}` with `dbc = chunk-1`.

## HFS

The XNU source feed for this profile carries **no** `bsd/hfs` sources, so the kebab port
vendored Apple's `hfs-627` into `bsd/kebab_hfs`. The three staged patches do the same for
`bsd/kona_hfs`, but their `bsd/conf/files` and `bsd/conf/Makefile.template` hunks were
generated against XNU 8796 and **must be regenerated against this tree's 10063.141.1**
before they join `series`; the machine guard also has to say `BCM2837` rather than the
kebab machine name.

Enable order once regenerated:

1. add `kona-hfs-sources.patch`, `kona-hfs-build.patch`, `kona-hfs-register.patch` to `series`
2. build the root volume with the kebab `scripts/build-newfs-hfs.sh` recipe (a static
   `/sbin/launchd` from `scripts/make-init.py` plus a `/dev` directory)
3. boot with `rd=ufsroot0`
