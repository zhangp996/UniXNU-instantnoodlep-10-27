# 交接：2026-11-11 晚（instantnoodlep / iPadOS 17.7.11 21H461）

## 一句话状态

IPadOS 在这个端口上**完整启动到服务图**：内核、根卷、SystemOS cryptex、3 GiB Data、launchd、
蓝牙/公平播放/文件系统服务全部正常。**唯一的失败**是三个进程在用户态执行 `brk` 陷阱：
`watchdogd`、`liveactivitiesd`、`SpringBoard`。

## 已验证可用

| 项 | 值 |
| --- | --- |
| 镜像 | `D:\新建文件夹\instantnoodlep-artifacts\bootET.img`（`15512e3c3d81d0a60cca60b640487144`） |
| 内存窗口 | `owned_memory_size = "0x100000000"`（4 GiB，文档上限 8 GiB） |
| AMFI 平台策略 | `[adt.chosen.u32] "amfi-only-platform-code" = 0` |
| 种子 | `owner-data-3g.dbr`（真实 CI 扩容 3 GiB） |
| 串口 | COM14 @115200，**必须 `DtrEnable=$true`**，且**启动后 50 秒再打开**（早开会导致卡死） |

## 启动/抓日志的固定流程

```powershell
# 1) 烧写（只 fastboot boot，绝不 flash 固件）
fastboot boot bootET.img
# 2) 等 50-60 秒（让系统跑起来、串口重新枚举）
# 3) 打开串口，开 DTR，持续读 40 秒
$s = New-Object System.IO.Ports.SerialPort 'COM14',115200,'None',8,'One'
$s.DtrEnable = $true; $s.Open()
```

日志落在 `D:\新建文件夹\instantnoodlep-artifacts\console-*.log`。

## 今天解决的七件事（都写进了 docs/evidence/2026-11-11-watchdog-crossed.md）

1. `watchdogd` 的临界退出 panic → 社区 RamWatchdog 后端（`ram-system-watchdog=1`）
2. 屏幕无输出 → `ml_io_map`（2 参数）+ `vcattach` 里切 `kPETextScreen`；另加清屏与 2 倍字体
3. 串口读不到 → 模块 `usb/Dwc3.c::pump()` 只在主机 **DTR** 置位时提交 IN 传输
4. Data 卷不可写 → 3 GiB 种子 + 只选分区媒体（`selectVolumeSeed` 要求 UUID 唯一）+ 阈值 2→4 GiB
5. 服务成片 SIGKILL → 解码 exit reason：`ns=1` 是 **JETSAM**
6. jetsam 风暴 → **4 GiB 窗口**后完全消失（`vm_page_bootstrap: 992422 free pages`）
7. 剩余三个 SIGTRAP → `handle_breakpoint` 打印 PC/ESR：`watchdogd +0x50b0c brk #1`、
   `liveactivitiesd 0x1d2dea4f8 brk #1`、`SpringBoard 0x1b08d32ec brk #0`

## 下一步：把 PC 解析成镜像

事实：dyld 共享区域是 `0x1f7800000 -> 0x1fcc00000`，而 `0x1b08d32ec`、`0x1d2dea4f8` **低于**它，
所以它们属于**进程私有映射**，不在共享缓存里。liveactivitiesd 两个实例的地址完全相同，说明该映射
没有随机化。

三条候选路线：

1. **内核钩子解析 vnode 路径**：在 `handle_breakpoint` 里对 `pc` 做 `vm_map` 查找，取当前进程的
   映像列表（`p->p_vmspace` → `vm_map` → entry → `vnode_getpath`），打印命中镜像的路径。最直接。
2. **dyld 打印**：启动参数里打开 dyld 的镜像打印（例如 `DYLD_PRINT_SEGMENTS` 等），让每个进程把
   自己的镜像基址报到控制台。
3. **brk 注释**：`brk #1` 的 ISS 只带注释号 1；若 Swift 运行时用 `brk #0x100` 明确标识，可以绕过
   库解析直接知道是 Swift 的 `fatalError`。

## 工作树与构建

```sh
cd /home/dev/unixnu/UniXNU
export XNU_EXTRA_LD_ARCHIVES="-Wl,-force_load,$PWD/out/instantnoodlep/ipados-17.7.11-21H461/instantnoodlep/tree/build/ram-watchdog/libRamWatchdog.a"
./unixnu build kernel -D ../UniXNU-instantnoodlep      # 注意：路径必须绝对，否则链接时报 file not found
./unixnu build payload -D ../UniXNU-instantnoodlep --kernel <patched kernel> \
  --unique-chip-id 0123456789abcdef --trust-cache out/ipsw/21H461/trust.bundle \
  --data-ramdisk out/instantnoodlep/ipados-17.7.11-21H461/instantnoodlep/data/owner-data-3g.dbr
```

**树的坑**：`tree/.cache/xnu-work` 是物化缓存；对它 `git reset --hard` 会让工具以为补丁已应用而跳过，
从而编译失败（`mockfs_fsnode.c` 的 `kalloc_type` 报错）。修复：`rm -rf` 该目录后重跑 `build kernel`。

## 设备仓库补丁（13 个）

```
zzz-boot-progress（arch 系列带入）      zzz-defer-framebuffer-mapping
zzz-arm-panic-region-guard / -late      zzz-qcom-usb-debug-console / -worker
zzz-root-unverified                      zzz-qcom-usb-report
zzz-owner-report                         zzz-owner-watchdog-override
zzz-exit-service-release-path            zzz-exit-observe
zzz-exit-reason-text                     zzz-trap-pc
zzz-shared-region-probe
```

外加两个 AMFI 二进制的**校验式**补丁脚本（在 payload 构建前对内核副本执行）：

* `scripts/patch-amfi-nopmgr.py` — 跳过无 PMGR 的 panic
* `scripts/patch-amfi-trustcache-gate.py` — `0xfffffff004ab73b8` 的 `tbz` 改 `nop`，允许任何进程查询
  trust cache（bring-up 用）

## 纪律

* 只 `fastboot boot`；需要写 userdata 时才 `fastboot flash userdata`。**绝不**刷 xbl/abl/tz。
* 进过 Android 就必须重刷 userdata（会清掉根 manifest，报 `UFS ROOT FAILURE`）。
* Windows 读不到 WSL 路径，产物要复制到 `D:\新建文件夹\instantnoodlep-artifacts\`。

