# CPU 与 ARM 指令集描述审计（2026-11-14）

审计范围：`docs/`、`patches/`、`board.toml`、`configs/`、框架 `docs/devices/`。

## 结论一览

| # | 主题 | 说法 A | 说法 B | 判定 |
| --- | --- | --- | --- | --- |
| 1 | 核心构成 | `zzz-max-cpus-8.patch`："四小 + 三大 + 一超大" | `2026-11-09-device-inventory.md`：cpu0-3 Kryo Silver(0x51/0x805)、**cpu4-7 全是 A77(0x41/0xd0d)** | **冲突** |
| 2 | 4/3/1 的归属 | 补丁注释暗示它属于 MPIDR 分组 | 框架 `docs/devices/mondrian.md`：4/3/1 是**缓存拓扑**，MPIDR 是**扁平 0x000..0x700** | **冲突（解释层面）** |
| 3 | 逻辑核映射 | 我们的 `sheng-smp-max` 按逻辑序号，隐含 logical = phys/0x100 | OP8 移植：`cpumask=0x13` + `ml_get_cpu_number(0x400)==2` | **不一致（待实测）** |
| 4 | ISA / PAC | 清单：Armv8.2-A、**无 FEAT_PAuth**、用 arm64 profile | 部分文档提到 arm64e | 内核 arm64，**一致**；arm64e 假设属其他设备 |
| 5 | MAX_CPUS / 簇 | 我们 `MAX_CPUS 8` / `MAX_CPU_CLUSTERS 1` | 扁平 MPIDR + 全 'P' 拓扑 | **一致** |

## 详细

### 1. 核心构成（真实冲突）

- `patches/xnu/zzz-max-cpus-8.patch:8`：`SM8250 has eight cores: four little, three big, one prime`
- `docs/evidence/2026-11-09-device-inventory.md:20-21`：
  `cpu0-3: implementer 0x51, part 0x805 (Kryo 585 Silver, A55 class)`
  `cpu4-7: implementer 0x41, part 0xd0d (Cortex-A77)`

`0xd0d` 是 Cortex-A77，Cortex-X1 是 `0xd44`。清单是按真机读数写的，因此 **cpu7 实际报的是 A77**，
与补丁注释的"一超大核"不符。两种可能：

1. 这台 IN2020 的 prime 核在 MIDR 上就报 A77（运营商/批次差异）；
2. 清单读数时把 cpu7 与 cpu4-6 合并记录了。

无论哪种，**补丁注释与实测清单冲突**，应以实测为准并注明 4/3/1 只是缓存/频率分组。

### 2. 4/3/1 的正确归属（解释冲突）

框架 `docs/devices/mondrian.md:123` 写得很清楚：

> SM8475 的硬件 MPIDR 是扁平的 0x000..0x700，Aff1 = 核序号。Android sysfs 里的 4/3/1（A510/A710/X2）是
> **L2/L3 缓存拓扑**，不是 MPIDR 亲和性；内核的 cluster-type 属性（全 'P'）把八个核塌缩成一个拓扑簇。

SM8250 同理。所以补丁注释把 4/3/1 挂在 `MAX_CPUS` 上描述**容易误导**——它不影响 MPIDR 或簇数。

### 3. 逻辑核映射（需要一次实测）

我们的门控 `zzz-smp-core-gate.patch` 用 `logical <= sheng-smp-max` 选择核心，隐含假设
`logical == phys / 0x100`。参考端口用 `cpumask=0x13` 并**显式校验** `ml_get_cpu_number(0x100)==1`、
`ml_get_cpu_number(0x400)==2`——说明在它那儿 **0x400 对应逻辑 2，而不是 4**。

若我们的板子也是这种映射，则 `sheng-smp-max=7` 覆盖的**不是全部 8 个物理核**。
验证方法：启动时打印 `ml_get_cpu_number(0x100*i)`（i=0..7）的实际返回值。

### 4. ISA / PAC（一致）

- 清单：`No FEAT_PAuth: the SoC is Armv8.2-A, so this board uses the arm64 profile, not arm64e`
- 我们构建的 XNU：arm64（非 arm64e）✓
- 操作系统选的是 **iPad7,1**（A10X，同样是 arm64 无 PAC），因此用户态也不需要 arm64e

其余文档里的 arm64e 说法属于其他设备或 source-storage 包，不构成本板冲突。

### 5. 簇数与拓扑（一致）

`MAX_CPU_CLUSTERS 1` 与「全 'P'、扁平 MPIDR」自洽；`cpu_regs = 0,0x100,...,0x700` 与扁平表一致。

## 建议动作

1. 改 `zzz-max-cpus-8.patch` 注释为：`8 cores, flat MPIDR 0x000..0x700; the 4/3/1 grouping is cache topology`；
2. 增加一次 `ml_get_cpu_number` 映射打印，确认 8 核选择覆盖全部物理核；
3. 若确认真机 cpu7 是 A77，则在设备清单里注明"prime 核 MIDR 报 A77"而不是 X1。