# Local patches for the shared trees

The firmware only builds and boots with a few changes in the shared trees. They live
here as whole files because the Mu-Silicium and edk2 checkouts used for this port are
assembled from pinned archives, not git clones.

```sh
MU_ROOT=~/sources/Mu-Silicium EDK2_ROOT=~/sources/edk2 \
UNIXNU_ROOT=~/sources/UniXNU ./uefi/apply-patches.sh
```

| File | Change | Why it is needed |
| --- | --- | --- |
| `mu/.../Sec/AArch64/ModuleEntryPoint.S` | stores `x0` in `mFdtAddress` before `PlatformAssemblyInitialize` | the incoming device tree pointer is otherwise lost, and `UniXNURamDiskDxe` reads `/chosen/linux,initrd-start` from it |
| `mu/.../Sec/Sec.c` | declares `mFdtAddress`, builds the entry-argument HOB (`DAEE82E6-8090-4E72-BFB9-78C13CD4E359`) after `PlatformPeim`, and writes zero to `0x17C10008` | without the HOB the payload volume is never published; without the watchdog write a fault resets the phone before the panel can be read |
| `mu/.../SiliciumPkg.dsc.inc` | `DXE_RUNTIME_DRIVER` links with `/ALIGN:0x1000 /FILEALIGN:0x1000` | GenFv refuses to rebase images whose section and file alignment differ |
| `mu/.../MemoryInitPeiLib/MemoryInitPei.c` | no allocation HOB for `EfiConventionalMemory` regions | DxeCore subtracts every allocation HOB before sizing its pools, so a conventional region used to leave zero free memory and trip `Length >= MinimalMemorySizeNeeded` |
| `edk2/BaseTools/Conf/tools_def.template` | AArch64 CLANGPDB link flags gain `/FILEALIGN:0x1000` | same GenFv alignment requirement, at the toolchain level |
| `unixnu/DarwinBootPkg/Driver/DarwinBoot.c` | validates the console GOP and falls back to the platform one | ConSplitter's virtual GOP has `FrameBufferBase = 0`, which the loader rejected as `Compromised Data` |
| `unixnu/patches/zzz-boot-progress.patch` | the hunk that needs the sheng `XNU_SHENG_PMGR` block is dropped | a kona tree has no such block, so the patch never applied |

Files under `mu/` and `edk2/` are copied over the checkouts. The `unixnu/` files belong to
the UniXNU checkout itself, which is a git clone, so a fork or a local commit is the
durable place for them.
