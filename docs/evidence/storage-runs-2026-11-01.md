# Storage bring-up runs, 2026-10-31 and 2026-11-01

Device: OnePlus 8 Pro IN2020 (instantnoodlep, kona, SM8250), 8 GiB, active slot a,
bootloader unlocked. Firmware: `SILICIUM_UEFI.fd` 4,194,304 bytes behind a
112-byte BootShim (`FD_BASE` 0x9E400000, `FD_SIZE` 0x400000). Every image below was
started with `fastboot boot <image>`; the diagnostic page was photographed about a
minute later and these fields are read from the photographs.

Kernel hashes are the CI artifact followed by the kernel the payload embedded after
`scripts/patch-amfi-nopmgr.py`. Image hashes are `boot-hdr.img`.

| Run | Kernel (CI -> payload) | Image sha256 | Arguments on top of the baseline | Page |
| --- | --- | --- | --- | --- |
| 1 | 55d36b05 -> 9f5d0baa | 2cda626e | delay=10 wake-first | root=50029, monitor=11 |
| 3 | 55d36b05 -> 9f5d0baa | 5c15549a | settle=0 | root=50091, monitor=11 |
| 4 | 55d36b05 -> 9f5d0baa | 847a23f3 | settle=0 noirq=1 | published=2, monitor=11 |
| 5 | 824b799b -> 7d5fd986 | 79826eca | settle=0 noirq=1 heartbeat=1 | manifest s1 m47630e6e00000000 |
| 8 | 0e1a82f2 -> e03379ac | 7df112d3 | settle=0 noirq=1, after the userdata reflash | root=50190, manifest s1 |
| 9 | 34021226 -> 0fcef66f | f9ad5b05 | settle=0 noirq=1 gic-mask=1 | published=2 on one boot, root=50091 on another |
| 10 | 34021226 -> 0fcef66f | 5ceb91fb | + heartbeat=1 | root=50069, monitor=11 |
| C | 34021226 -> 0fcef66f | 2c40caef | no wake-first (NOP, power query, SSU) | root=50091 |

`monitor=11` means the page stopped at its eleventh tick: level 2 runs
continuously, and the runs whose bring-up failed early (a rejected manifest) reached
monitor=60, so an active bring-up is what stops the machine, about one second after
the thread starts.

Runs 5 to 8 reported `manifest s1` because a boot into Android recovery had rewritten
offset 0 of userdata. `fastboot flash userdata userdata-root.img`
(9,382,658,048 bytes, 1880 s, twelve sparse chunks) restored the `SHNGROOT` block and
the string stopped appearing.
