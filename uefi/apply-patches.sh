#!/bin/sh
# Copy the local changes over the shared checkouts. See README.md for the reasons.
set -e
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/patches
: "${MU_ROOT:?set MU_ROOT to the Mu-Silicium checkout}"
: "${EDK2_ROOT:?set EDK2_ROOT to the edk2 checkout}"

cp "$here/mu/Silicon/Silicium/SiliciumPkg/Sec/Sec.c" \
   "$MU_ROOT/Silicon/Silicium/SiliciumPkg/Sec/Sec.c"
cp "$here/mu/Silicon/Silicium/SiliciumPkg/Sec/AArch64/ModuleEntryPoint.S" \
   "$MU_ROOT/Silicon/Silicium/SiliciumPkg/Sec/AArch64/ModuleEntryPoint.S"
cp "$here/mu/Silicon/Silicium/SiliciumPkg/SiliciumPkg.dsc.inc" \
   "$MU_ROOT/Silicon/Silicium/SiliciumPkg/SiliciumPkg.dsc.inc"
cp "$here/mu/Silicon/Silicium/SiliciumPkg/Library/MemoryInitPeiLib/MemoryInitPei.c" \
   "$MU_ROOT/Silicon/Silicium/SiliciumPkg/Library/MemoryInitPeiLib/MemoryInitPei.c"
cp "$here/edk2/BaseTools/Conf/tools_def.template" \
   "$EDK2_ROOT/BaseTools/Conf/tools_def.template"

if [ -n "${UNIXNU_ROOT:-}" ]; then
  cp "$here/unixnu/DarwinBootPkg/Driver/DarwinBoot.c" \
     "$UNIXNU_ROOT/drivers/DarwinBoot/DarwinBootPkg/Driver/DarwinBoot.c"
  cp "$here/unixnu/patches/zzz-boot-progress.patch" \
     "$UNIXNU_ROOT/patches/xnu/zzz-boot-progress.patch"
fi

echo "patched $MU_ROOT and $EDK2_ROOT${UNIXNU_ROOT:+ and $UNIXNU_ROOT}"
echo "the platform package itself is copied by uefi/apply-platform.sh"
