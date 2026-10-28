# 分析：kebab 的阶段 6 / 7 在本板上的可行性

依据：GFander/UniXNU-kebab 的 `docs/BRING-UP-STATE.md`（commit 786f317）与其补丁集。

## kebab 的实际做法（与我们的路线对比）

| 环节 | kebab | 我们当前 |
| --- | --- | --- |
| 块设备发布 | **BSD 层 `bdevsw`**，直接发布 `ufsroot0`（补丁 0072），不经过 IOMedia | 试图链接 ShengUfs → 依赖 IOStorageFamily kext 的 `IOBlockStorageDevice` vtable，被文档要求的 live gdb 快照卡住 |
| 根文件系统 | **把 HFS 源码编进内核**并在 `vfs_mountroot` 前注册（补丁 0080–0082） | 未处理 |
| 根设备选择 | 启动参数 `rd=ufsroot0` 覆盖根匹配 | 未处理 |
| UFS 传输 | 926 行的内核内 UFS 驱动（补丁 0070），自带 UTRD/UPIU/PRDT 与分区窗口 | 固件探针已验证命令阶段（`ocs 0`）与队列启动（`UTRLRSR 0x1`），数据阶段未成 |
| 用户态 | 手工构建的静态 arm64 `launchd`（`scripts/make-init.py`）+ 内置 AMFI 占位（0088） | 未处理 |

## 阶段 6 可行性：高

关键结论：**kebab 完全绕开了 IOStorageFamily kext**。它不实现 `IOBlockStorageDevice`，而是注册 BSD 的 `bdevsw` 并发布一个 `rd=` 可选的根设备。这意味着：

- 不需要 `audit-storage-interface.py` 的实时元数据（我们拿不到）
- 不需要 `IOBlockStorageDevice` 的 vtable 解析（当前链接失败的原因）
- 不需要 ShengUfs 的 SMMU/kext 集成

本板已具备的前提：

| 前提 | 状态 |
| --- | --- |
| UFS 控制器寄存器与命令提交（UTRD/UPIU/门铃/OCS） | ✅ 固件探针已打通 |
| 队列 run/stop 寄存器（kona 在 `0x60`） | ✅ 已实测 |
| GCC/PHY/reset 常量 | ✅ 已实测记录 |
| DRAM/分区窗口 | ✅ GPT 已 dump |
| PRDT 的 `dbc` 字段 | ⚠️ kebab 的 PRDT 条目为 `{addr_lo, addr_hi, rsvd, dbc}`，我们此前未把它设成字节数——这可能正是数据阶段不搬的原因 |

风险点：kebab 的传输代码面向它自己的控制器状态，移植到 kona 需要按我们实测的寄存器布局调整；分区偏移与逻辑扇区大小要按本板 GPT 填入。

## 阶段 7 可行性：中高

kebab 的做法是可复现的：

1. 交叉编译一个静态 arm64 的 `/sbin/launchd`（`scripts/make-init.py`）
2. 放进 HFS 根卷（`scripts/build-newfs-hfs.sh`），根卷需含 `/dev`
3. 内置 AMFI 占位，让 `IOTaskHasEntitlement` 不空指针（补丁 0088）
4. 注意 `printf` 走 `_cnputs` 会被 `disableConsoleOutput` 吞掉（补丁 0090）

本板差异：我们需要自己构建这个静态 init 与 HFS 镜像，且在 macOS runner 上完成交叉编译 —— 工程量大但路径清晰。

## 结论

按 kebab 的路线，本板可以**跳过当前卡住的 IOStorageFamily 绑定**，直接走：

```
UFS bdevsw 驱动（移植 0070） → 内置 HFS（0080-0082） → rd=ufsroot0 → 静态 launchd
```

代价是放弃 ShengUfs（其 kext 形态在本板不可链接），收益是阶段 6 立刻变得可实施、阶段 7 也有现成配方。
