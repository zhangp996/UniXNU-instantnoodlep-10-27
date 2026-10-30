# Boot progress renderer

Vendored, byte-identical, from UniXNU-sheng at commit
`08cde20edc3254ed6f7df8d0f5384fb887786e8f` (2026-10-26), files
`package/BootProgress.c`, `package/BootProgressCore.h`,
`package/BootProgressDisplay.h`, `package/BootProgressLifecycle.h` and
`package/BootProgressPulse.h`. The source carries `SPDX-License-Identifier: MIT`
(UniXNU contributors); the vendored `Font.h` is Mu-Silicium's copy of the AOSP
glyph table and keeps its own BSD-3-Clause text.

This directory is the board package: `scripts/unixnu/tree.py` assembles it as
`kits/instantnoodlep` in the build tree, which is where
`scripts/build-boot-progress.py` reads it from.

Only the renderer is vendored. sheng's four companion probes
(`BootFaultProbe.c`, `BootWatchdogPolicy.c`, `BootDiagExport.c`,
`AdspFirmware.c`) cover its watchdog policy, its file export and its ADSP
bring-up; this port builds none of them, which is why
`scripts/build-boot-progress.py` here compiles a single object.

The renderer reads the storage counters `native_ufs_block_reads`,
`native_ufs_block_failures`, `native_ufs_integrity_failures` and the
`native_ufs_progress_text()` row writer. The KonaUfs component defines all
four (drivers/KonaUfs/NativeUfs.cpp), and every other storage symbol it
mentions is a `weak_import`, so no stub is needed.

`BootProgressDisplay.h` includes `Font.h` from Mu-Silicium's
`FrameBufferSerialPortLib` and uses its `GLYPH_FONT` table, the same font
the panic screen draws with.
