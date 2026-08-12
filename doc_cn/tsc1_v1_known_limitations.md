# TSC1 v1 已知限制

文档状态：当前限制说明
最后审阅：2026-08-12
主要头文件：`include/taiyin/star_catalog_tsc1.h`, `include/taiyin/star_provider_tsf1.h`

本文说明第一版 TSC1 precision-star catalog 的能力边界、数据假设和设计取舍。它面向需要加载恒星 catalog、调用恒星位置 API，或评估历史天空精度边界的使用者。

## 范围

TSC1 v1 面向本地 precision-star catalog，当前主要覆盖：

1. `stars-fixed-traditional`
2. `stars-bright-gaia-bsc`
3. `stars-hipparcos-gaia`
4. `lite/stars-bright-v5`

它不是 deep render-star catalog，也不是完整 stellar dynamics model。TSC1 的目标是给命名恒星、亮星和 Hipparcos/Gaia 级别恒星提供可查询、可传播、可接入 Taiyin apparent/observed flags 的本地数据格式。

## 文件和加载关系

TSC1 是二进制 precision-star catalog。TSF1 是面向用户自定义恒星的小型文本格式；runtime 加载 TSF1 时会先把它编译成内存中的 TSC1 catalog，再复用 TSC1 provider 计算。

当前公共入口在：

```text
add_global_tsc1_star_catalog
add_global_tsc1_star_catalog_from_memory
add_global_tsf1_star_catalog
clear_global_star_catalogs
calc_star_position_ut / tt / tdb
calc_star_positions_ut / tt / tdb
calc_observed_star_ut
calc_observed_stars_ut
```

TSC1/TSF1 恒星 catalog 不进入太阳系星历的 OPC descriptor catalog。OPC 负责 OPM2/SPK/TKC1 等太阳系星历数据；恒星由全局 star store 单独持有。查找时先查已加载的 TSC1 provider，再查 TSF1 provider；同类 provider 内按加入顺序查找。

## 天体测量模型

### 只使用线性空间运动

TSC1 v1 使用线性 3D space-motion model：

```text
position(t) = position_ref + velocity_ref * dt
velocity(t) = velocity_ref
acceleration(t) = 0
```

源 catalog 字段从：

```text
RA / Dec / proper motion / parallax / radial velocity / reference_epoch
```

转换成：

```text
3D reference position + 3D reference velocity
```

这比直接应用线性 RA/Dec delta 更好，因为把 3D vector 投影回天球时，会自然捕捉一部分非线性 apparent angular behavior，例如径向速度带来的 perspective effects。

这个模型不是 Taiyin 自己发明的恒星公式，而是 Gaia/Hipparcos 风格 astrometric catalog 的常规参数传播：catalog 给出 `RA/Dec/proper motion/parallax/radial velocity/reference_epoch`，runtime 把它们转换成参考历元的 3D 位置和速度，然后按匀速直线运动传播。它和 IAU SOFA `starpm` / `pmsafe` 这类“用 proper motion、parallax、radial velocity 推进恒星 astrometry”的思路是同一类模型。

TSC1 v1 的权威性主要来自源 catalog：

- Gaia DR3/EDR3：提供 J2016.0 的位置、视差和自行等 astrometric solution；
- Hipparcos：提供 J1991.25 的 ICRS/HCRF astrometry；
- BSC5/manual fallback：只作为补齐亮星和特殊方向的低优先级来源。

因此，TSC1 的恒星位置是“源 catalog astrometry + 标准线性空间运动传播”，不是类似行星星等那种经验光度公式。

### 没有显式恒星加速度

大多数普通恒星 catalog 不提供可靠的逐星 acceleration。Gaia/Hipparcos/BSC 风格输入数据通常提供 position、proper motion、parallax、radial velocity 和 reference epoch，但不提供 acceleration。

因此 TSC1 v1 把 stellar acceleration 设为零。

已知受影响情况：

- 很近且自行很大的恒星；
- 几千年或更长时间跨度；
- perspective acceleration 显著的恒星；
- 未分辨或近距离双星/多星系统；
- astrometric solution 非线性的恒星。

后续格式可以扩展为：

```text
model_type = LinearSpaceMotion
model_type = AcceleratedMotion
model_type = BinaryOrbit
```

或等价 flags/extension records。

### 没有双星轨道模型

TSC1 v1 不建模双星或多星的轨道运动。

这类系统需要专门模型才能更好处理，例如：

```text
Sirius
Alpha Centauri
Castor
Algol
61 Cygni
Proxima Centauri / Alpha Centauri system
```

在 v1 中，这些都按普通 catalog astrometry row 处理。

## 历史精度

配合正确的岁差、章动、黄赤交角和 frame conversion 时，TSC1 v1 预期可用于普通历史天空重建，适合百年到千年量级。

一两千年范围内最大的 apparent change 通常是地球岁差，不是恒星加速度。

但是，TSC1 v1 不定位为对任意恒星、任意超长时间跨度都达到 microarcsecond 级的历史 astrometry solution。

古代/远未来使用的已知限制：

- 高自行恒星会累积更大误差；
- 缺失或不准的 radial velocity 会影响 perspective motion；
- 缺失或不准的 parallax 会影响距离和 transverse velocity；
- 忽略双星轨道运动；
- catalog 测量绑定到源 epoch 和源质量；
- 古代观测还依赖 Delta-T、历法转换和大气/观测不确定性。

## 缺失或不完整源字段

### 星等字段不是光度模型

TSC1/TSF1 里的 `magnitude` 是 catalog 字段，会随 record 一起保存和读取。当前 runtime 不根据距离、颜色、变星曲线、消光或观测波段重新计算恒星视星等。

这和 `calc_body_phenomena_*()` 的太阳系天体 `apparent_magnitude` 不同：太阳系天体星等是 runtime 经验公式；恒星星等是 catalog photometry value。变星、双星合光、不同 bandpass、星际消光和历史亮度变化都不在 TSC1 v1 的模型内。

### 缺失 radial velocity

很多恒星没有 radial velocity。TSC1 v1 保留 `HAS_RADIAL_VELOCITY` flag。

radial velocity 缺失时，runtime 应在线性传播模型中把 radial velocity 当作 unknown/zero。

### 缺失 parallax

部分 fallback rows 可能没有可靠 parallax。TSC1 v1 保留 `HAS_PARALLAX` flag。

parallax 缺失或非正时，runtime 可使用很大的 placeholder distance 做 direction-only propagation，匹配现有 fixed-star approach。

### 混合 reference epochs

TSC1 rows 不全是 J2000：

```text
Gaia DR3 rows:    reference_epoch = 2016.0
Hipparcos rows:   reference_epoch = 1991.25
BSC5 fallback:    reference_epoch = 2000.0
Manual rows:      record-specific, currently 2000.0 for special directions
```

Runtime 必须使用每条 record 自己的 `reference_epoch`，不能假定所有恒星都是 J2000。

## 源/Fallback 质量

当前 enrichment hierarchy：

```text
Gaia DR3 source_id / HIP best-neighbour
  -> Hipparcos fallback
  -> BSC5 fallback
  -> missing/manual/special handling
```

严格 fallback validation 后的当前已知数量：

```text
total identity rows: 118,332
Gaia DR3:             99,525
Hipparcos:            18,430
BSC5:                    101
missing:                 276
```

Missing rows 会被 compiler 跳过，除非它们是已知 manual/special records。

因此生成的 Hipparcos-level TSC1 catalog 当前比 identity manifest 少一些星：

```text
identity rows: 118,332
compiled stars: 118,058
```

## Manual/Special Records

TSC1 v1 包含特殊方向 records，例如：

```text
galactic_center_j2000
sgr_a_apparent
```

它们标记为：

```text
astrometry_source = Manual
SPECIAL_DIRECTION flag set
```

已知限制：这些不是普通 Gaia/Hipparcos point-source stars，runtime code 需要小心处理。它们是方向 placeholder/special targets，不是普通恒星运动 record。

## Alias 处理

Aliases 存在 side table 中，通过 normalized alias + FNV-1a 64-bit hash 查找。

已知限制：

- alias hash 只是加速器；runtime 必须验证 string equality；
- ambiguous aliases 目前由 compiler policy 确定性解析，除非使用 strict mode；
- 一些 catalog designations 可能仍然不完整，或和外部工具 normalization 不同；
- alias coverage 可通过 SIMBAD/name curation 继续改进。

## Runtime Reader 限制

当前 C++ TSC1 runtime 支持包括：

```text
memory-backed loading
file-backed loading
POSIX mmap on macOS/Linux
Windows file mapping implementation
fallback owned-buffer file loading
alias lookup
header/offset/string validation
Tsc1StarProvider runtime evaluation
lazy internal runtime-id registration
per-star StorageEphemerisBlock compilation
EphemerisSegmentCache integration
global star catalog store
high-level star position / observed-star API
```

已知限制：

- Windows memory mapping 通过窄字符路径 API `CreateFileA`、`CreateFileMappingA` 和 `MapViewOfFile` 实现，但尚未在真实 Windows 机器验证。macOS/Linux mmap 已在本地构建和测试。
- 如果平台 mapping 失败，`MappedFile` fallback 到把整个文件读入 owned byte buffer；当前 fallback reader 仍使用窄字符文件路径。
- 因此 Windows 上非 ASCII 数据路径暂不保证可用。需要跨语言路径支持时，应把 Windows file IO 路径改成 wide-character API，例如 `CreateFileW` 和对应的宽字符 fallback reader。
- Reader 当前假定 native little-endian runtime。Catalog validation 会检查这一点，所以 unsupported endian layout 会安全失败，而不是静默误读字段。
- `Tsc1StarProvider` 在星体被解析时惰性分配内部 `runtime_id`，不会在 catalog load 时 bulk-register 每颗星。这些 ID 只用于 cache keys 和 diagnostics；HIP、HR、HD、Gaia DR3 和 canonical aliases 仍然是 catalog metadata。
- 当前 star store 是全局注册表，不是每个 `NativeCalcContext` 独立持有。需要隔离不同恒星 catalog 集合的应用，应在调用边界显式管理 `add_global_*` 和 `clear_global_star_catalogs`。
- TSC1/TSF1 不参与主星历 route-rule 优先级。恒星 key 的覆盖规则来自 star store 的查找顺序，而不是太阳系星体的 `AUTO/SPK/OPM2/半解析` route rule。

## Cache 设计限制

当前 runtime cache `EphemerisSegmentCache` 保存 compiled calculation blocks，不保存最终 per-time results。

TSC1 v1 使用两层模型：

```text
.tsc1 file
  -> mmap / OS page cache for raw catalog bytes
Tsc1StarRecord
  -> position_ref_au + velocity_ref_au_per_day + reference_jd
  -> StorageEphemerisBlock
  -> EphemerisSegmentCache
```

`.tsc1` 文件本身不会插入 `EphemerisSegmentCache`；cache 保存由 `Tsc1StarProvider` 按需创建的 per-star compiled evaluator blocks。

TSC1 provider 自身只缓存 compiled evaluator block，不缓存某个时刻的最终 apparent/observed 输出。输出级别的复用若有必要，应由调用方批处理或 solver 局部状态承担，不属于 TSC1 文件格式。

## 不是 Deep Render Catalog

TSC1 v1 面向 precision/named/BSC/Hipparcos-scale stars。

它不是为数百万或数十亿恒星的 dense Gaia render catalog 设计的。

渲染级 catalog 应使用独立格式，可能按天空区域和星等做 spatial tiling，例如：

```text
TSR1 or equivalent render catalog
HEALPix/spatial tile index
magnitude bins
view-dependent tile loading
```

## 打包限制

仓库保留完整生成 catalog，位于：

```text
data/stars/catalogs/
```

面向分发的星表放在独立目录：

```text
data/stars/catalogs/lite/stars-bright-v5.tsc1
```

它从 `stars-bright-gaia-bsc.tsc1` 机械生成：保留 `V <= 5.0` 的 catalog
records、两个 manual special-direction records，以及 `lite/required_stars.json` 中的
明确覆盖要求。该要求保证二十八宿全部 28 颗距星（含中文与拼音 alias），并保证西方
十二黄道星座各有一颗代表亮星。它保持为适合语言绑定默认随包表的体积；父目录中的完整
亮星表仍然保留 9,098 颗星（约 1.9 MB），Hipparcos/Gaia 表保留 118,058 颗星（约 21 MB），
供需要更广覆盖的应用显式选用。

这 28 颗距星是具名的 Taiyin v1 reference profile；它不宣称宋代、陈卓、清代及其他
历史重建一定采用相同成员星。完整历史星官图需要另行版本化的 sky-culture overlay，
其中包含星官成员、连线、时代、来源和 provenance；TSC1 仍只承担天体测量恒星层。

runtime 不强制一种打包方法：分发包可以默认携带 lite 表、把大表做成可选数据，也可以
加载用户提供的 TSC1/TSF1 catalog。当前 lite 文件按上文规则从亮星表机械生成；该
维护者专用生成工具有意不包含在公开源码快照中。

## v1 意图总结

TSC1 v1 有意优先：

```text
local binary catalog
safe reader
fast alias lookup
per-row reference epoch
Gaia/Hipparcos/BSC source preservation
simple linear 3D space motion
```

本版不承诺：

```text
stellar acceleration
binary orbit modeling
microarcsecond ancient astrometry
deep render catalog tiling
output-level result cache semantics
```

这些取舍意味着 TSC1 v1 适合做 runtime-capable precision-star catalog 的第一版基础格式；如果应用需要双星轨道、超长时间跨度高精度天体测量，或大规模渲染级星表，应使用后续专门格式或扩展模型。
