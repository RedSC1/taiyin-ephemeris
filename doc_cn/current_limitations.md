# 当前能力与已知限制

文档状态：当前说明
最后审阅：2026-08-12

本文概述 Taiyin 当前已经可用的能力、仍然存在的限制，以及比较适合下一步推进的工作。具体 API 说明以对应主题文档和头文件为准：

- Runtime / 数据 / cache：`ephemeris_runtime_architecture.md`, `catalog_cache_model.md`, `opc_catalog_format.md`
- 事件搜索：`event_search.md`
- 日月食搜索：`eclipse_search.md`
- 恒星 catalog 限制：`tsc1_v1_known_limitations.md`

## 当前可用能力

### Runtime 和数据路线

当前 runtime 已经不是早期草稿状态。主线结构是：

```text
Runtime
EphemerisEngine
EphemerisBlockCatalog
EphemerisBodyRegistry
EphemerisRouteRuleTable
EphemerisSegmentCache
RouteInflightMap
```

支持的数据来源包括 OPM2、SPK、TKC1/Kepler、Taiyin Kepler file、内置半解析模型，
以及自定义 ephemeris method/file method。Catalog 负责发现和描述本机数据，route rule
决定 method 与 source-product 优先级，segment cache 缓存已加载数据段；没有共享的
exact-JD 数值结果 cache。AUTO 会优先保持同一命名 DE 产品内的组合，卫星 SPK 只作为
该 DE 的辅助路线；文件级 source-priority overlay 可在同一 provider 内提升或降级明确的
SPK/OPM2 文件。

### 主星位置和 apparent/observed 链路

主星位置入口已经覆盖 TDB、TT、UT、UTC：

```text
calc_position_tdb / calc_positions_tdb
calc_position_tt / calc_positions_tt
calc_position_ut / calc_positions_ut
calc_position_utc / calc_positions_utc
calc_observed_ut
calc_observed_utc
```

当前 apparent/observed 链路支持 light-time、annual aberration、solar/multi-body gravitational deflection、topocentric observer、horizontal az/alt、refraction、frame selection、UTC/EOP/UT1/polar motion/CPO 等组合。EOP、leap-second 与月缘数据由全局 runtime 持有；用户位置、气象参数、显式 UTC 超范围回退开关、模型 ID 和 route rule 放在 `NativeCalcContext` 上，计算开关通过 flags 控制。Observed API 统一使用 `uint64_t` 分层：低 32 位仅接受真正实现的计算语义（`SPEED`、`TRUEPOS`、`NO_ABERR`、`NO_GDEFL`、`ASTROMETRIC`、`TOPOCENTRIC`、`ALLOW_BARYCENTER_APPROX`），高 32 位用于 horizontal、refraction、meteorology 等 observed 自身选项。输出形状或 frame selector（`XYZ`、`EQUATORIAL`、`RADIANS`、`NONUT`）会返回 `TAIYIN_ERROR_UNSUPPORTED`，不会被静默忽略。

### 组合星体和数据 fallback

Earth/Moon、major body barycenter/body offset 等组合逻辑已经集中到 runtime body registry / builtin body rules 中。Catalog 初始化时标记 direct-capable body；不能直接从数据文件读取的 body 由 fallback evaluator 组合。

组合 evaluator 会通过 `EphemerisEngine` 请求组件路线。AUTO route 可以按优先级
fallback；指定 OPM2、SPK 或半解析等单一路线时，只尝试对应 route rule，避免搜索
或组合计算中静默混用不同 method。

当数据只提供 barycenter 时，major-planet body ID 默认仍保持严格语义。`TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX` 是 native position 的显式 opt-in：调用方接受后，Mars 到 Pluto 可以用对应 barycenter 作为近似；diagnostic 里 `target_id` 保留请求的本体 ID，`component_target_id` 记录实际使用的 barycenter。

### 无数据文件卫星 fallback 的边界

内置半解析路线在各自声明区间内提供 Phobos/Deimos、木星四大卫星、冥王星系统和
Triton。这些是紧凑 fallback 状态，不是精密卫星星历：单颗卫星的相对状态误差可从米、
数公里到数百公里，而质量加权后的物理行星中心修正可能小得多。精确卫星测量或卫星现象
需要使用对应 SPK 或 OPM2 source。1.0 没有内置土星或天王星卫星理论；物理
Saturn/Uranus 请求需要直接数据，除非调用者显式允许普通的 barycenter 近似。

### 恒星

恒星已有高层 API：

```text
add_global_tsc1_star_catalog / add_global_tsf1_star_catalog
calc_star_position_tdb / tt / ut
calc_star_positions_tdb / tt / ut
calc_observed_star_ut / calc_observed_stars_ut
```

TSC1/TSF1 provider 被全局 star store 封装，应用层通常不需要直接传 `Tsc1StarProvider*`。恒星支持 alias 查询、线性空间运动、自行传播、球坐标/XYZ 输出、速度输出，以及和普通主星对齐的 observed flags。

`calc_star_position_*` 返回 observer-relative 固定星位置，并使用 observed-star 路线里的固定星 apparent 修正：默认启用 annual aberration 和 solar gravitational deflection；`TRUEPOS`、`ASTROMETRIC`、`NO_ABERR`、`NO_GDEFL` 用来选择对应的简化口径。

1.0 API 中，固定星位置与固定星观测要求
`NativeCalcContext::observer_id == TAIYIN_BODY_EARTH`；非地球 observer 会返回
`TAIYIN_ERROR_UNSUPPORTED`。地球 observer 仍可使用历史半解析路线：内置模型会由
九个行星质心的日心状态质量加权重建 `Sun/SSB`，让 Earth、Sun 与恒星方向进入同一
个重心 frame，而不需要额外星历文件。

1.0 的站心观测同样只支持地球。非地球 observer 调用 topocentric context
setter，或请求 native/observed topocentric 结果，都会返回
`TAIYIN_ERROR_UNSUPPORTED`。要支持其他天体表面，还需要对应天体的参考椭球、
自转/定向模型以及（如适用）大气模型；这些能力明确不属于 1.0 范围。

### 事件搜索

底层事件搜索已经覆盖：

```text
太阳/月亮黄经 crossing
bounded body longitude crossing
longitude station
relative longitude / aspect crossing
exact aspect
lunar phase
UT / TT entry points
auto-step convenience wrappers
```

这些 API 是数值 primitive，不提供节气名称、星座入宫名称、相位名称、orb、applying/separating 或逆行标签。领域语义应由历法、八字或占星扩展在更上层组织。

### 日月食

日月食搜索已经覆盖：

```text
lunar eclipse solve/search
local lunar eclipse visibility
global solar eclipse solve/search
local solar eclipse circumstances/search
solar Besselian elements
route rows / route curves
local boundary helpers
```

公开的地方日食/月食 API 从 `NativeCalcContext::observer_location`
读取观测者位置。地方 API 会拒绝缺失观测者位置或纬度非法的 context；如果
context 原本是 topocentric 状态，内部会先归一化为 geocentric apparent
状态，再应用自己的地方几何计算。

模型约定、接触时间含义、PMO/NASA oracle 和算法来源见 `eclipse_search.md`。

### 可见性搜索

太阳、月亮和行星 visibility 已有公共入口：

```text
search_solar_rise_set_ut
search_solar_twilight_ut
search_solar_transit_ut
search_moon_rise_set_ut
search_moon_transit_ut
search_planet_rise_set_ut
search_planet_transit_ut
```

太阳/月亮支持 limb 选择、refraction、fixed disc size 和自定义 horizon；行星支持 rise/set/transit、refraction、limb 选择和自定义 horizon。`search_planet_transit_ut()` 的 `uint64_t flags` 会把低 32 位 native-position word 原样向下透传，高 32 位保留给将来的 transit 选项。因此历史半解析数据可以继续请求物理 Jupiter，同时传 `TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX`；transit solver 不需要知道 barycenter，strict-first fallback 仍由 position/apparent route 完成，diagnostic 也保留请求的本体 ID。普通公共 rise/set 入口默认使用带折射的 apparent altitude；需要 true-altitude 搜索时使用 `*_VISIBILITY_FLAG_NO_REFRACTION`。带折射请求需要真实 atmosphere 数据，除非 context 通过 `native_context_set_atmosphere_policy_flags(..., TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK)` 显式允许文档所述的 ISA 风格标准大气回退。相应的高 32 位 `*_VISIBILITY_STRICT_METEOROLOGY` flag 会对单次调用关闭该回退，并要求调用方提供 atmosphere fields。地方日食 API 有意不同：其可见窗口默认是几何窗口，只有 `TAIYIN_ECLIPSE_LOCAL_REFRACTION` 才请求带折射窗口；`TAIYIN_ECLIPSE_LOCAL_STRICT_METEOROLOGY` 必须与该选项一起设置，并禁止回退。当前已有公开 regression/oracle 测试覆盖 Denver、Longyearbyen 等样例。

### 天体现象量

已有 `calc_body_phenomena_ut` / `calc_body_phenomena_tt`，用于计算 phase angle、illuminated fraction、solar elongation、apparent diameter、经验 apparent magnitude，以及 Moon 的 geocentric horizontal parallax。Mercury 到 Neptune 的星等按 Mallama & Hilton (2018) 经验公式实现；Sun、Moon、Pluto 使用独立经验模型，详见 `phenomena_magnitude_models.md`。这些模型仍不等同于精密物理光度解算。尤其是 Moon：`illuminated_fraction` 是理想球体几何点亮面积，不是亮度比例；`apparent_magnitude` 是历书级经验模型，不是 ROLO 级月面 irradiance model。

## 当前限制

### C ABI 已冻结，C++ ABI 未冻结

从库版本 `1.0.0` 开始，带版本号的 C99 ABI 是绑定层和应用程序的兼容边界。
ABI major 7 内已有 C symbol 和 struct contract 必须保持源码与二进制兼容；
新增字段使用文档约定的 `struct_size` 机制。

C++ 头文件仍属于实现层接口，minor 版本之间可以演进，不承诺稳定二进制 ABI。
绑定层和分发应用应使用 `include/taiyin/c/`。

### 外部 oracle 覆盖仍不完整

当前已有 OPM2/SPK/JPL、SOFA/ERFA、PMO/NASA、sxwnl 和部分 SwissEph 对照，但还不是全覆盖。

仍需要加强的方向：

```text
更多日期/星体的 apparent oracle sweep
precise UTC/EOP/CPO topocentric observed 外部对照
horizontal azimuth / refraction convention 对照
SPK type 21 小天体 oracle 或 baked fixtures
local solar/lunar visibility 的更广外部表格
```

### 恒星模型是 TSC1 v1 线性模型

TSC1 v1 使用线性 3D space-motion model，不包含恒星加速度、双星轨道、多星系统动力学或 Gaia 非线性解。普通历史天空重建可以使用；对高自行近星、长时间跨度或双星系统，不应宣称 microarcsecond 级历史 astrometry。

详细限制见 `tsc1_v1_known_limitations.md`。

### Relativistic correction 仍是实用近似

Light-time、Shapiro delay 和 gravitational deflection 当前是实用 apparent-position 模型，不是完整 moving-deflector post-Newtonian integration。

已知边界：

```text
multi-body Shapiro 是一阶静态累加
deflector retarded time 未逐个求解
deflector velocity/c^2 项未建模
航天测距或高精掩星场景需要更强模型
```

### TT/TDB 和时间尺度模型仍需更多端到端验证

项目中已有 fast periodic TDB、SOFA-style full TDB、可配置 Delta-T 模型、leap-second table 和 EOP table 路线。普通主星和 observed 计算可用，但不同外部系统的 TDB/TT/UT1 convention 会造成细小差异。端到端 oracle 不应过早压到微角秒级。

### Eclipse/visibility 仍有模型约定差异

日月食接触时间、食分、月影半径和地方可见性高度依赖 shadow model、Moon radius model、refraction、limb convention 和外部表格的发布时间精度。Taiyin 当前会明确记录模型 preset，但不能把不同模型来源的秒级差异都当作 bug。

### 低层 flat kernel 仍保留长签名

`calc_apparent_batch` 等底层 kernel 仍然是显式参数风格。它的优点是行为透明、适合测试和内部组合；缺点是调用点较长。短期策略是继续在 runtime 层提供 typed wrapper，而不是把底层 kernel 包成复杂 pipeline。

## 尚未完成的高层能力

### ASC / MC 与宫位位于 astrology extension

可选 astrology extension 现已公开 ASC、MC、ARMC、Vertex、East Point、
纯 ARMC 几何入口、可注册的宫制表和十种 typed house system。这些能力有意不进入
core astronomy runtime。宫头/角点速度、fractional house-position 查询、
Gauquelin sector 和其余流派宫制仍未实现。

### Phenomena API

已经提供 `swe_pheno` 类的公开 scalar API：

```text
phase angle
elongation
illuminated fraction
angular diameter
apparent magnitude
```

当前 magnitude 使用公开经验模型，文档会标明模型来源和适用边界。高精度月球视觉星等、恒星星等模型和 Swiss-compatible 经验公式对齐仍属于后续兼容/模型工作。

### Occultation / transit / appulse

日月食已经有专门 API。当前 event-search 已提供三维最小角距 primitive、地心 Mercury/Venus transit of Sun 搜索，以及 Mercury/Venus 凌日的本地 topocentric 接触和可见性判断。Occultation 模块已有第一版月掩恒星和月掩太阳系 body next-search：可以从给定 UT 起点向前或向后搜索指定目标的地心/地方月掩事件；恒星点目标返回最大掩/最近角距时刻和 C1/C4，body 目标返回最大掩/最近角距时刻和 C1-C4，并返回全掩、偏掩、环掩、切掩、中心/非中心等类型 bit；如果当前模型组合不能计算中心线分类，会设置 `CENTRALITY_UNAVAILABLE` 而不是让主事件失败。搜索接触时刻可选用全局 TLL1 月缘模型；`where` 入口可返回中心线/最佳观测点，但其地表边界尚未接入 TLL1。它还提供基础本地可见性摘要，可以在接触时刻和最大掩时刻采样 Moon/target/Sun 的高度方位与可见性 bit。它还不是完整 occultation/appulse 系统；完整地表可见区域/路径摘要、批量星表扫描、行星互掩、卫星凌日、连续本地可见性区间和外部公告级 oracle 仍待补充。body target 当前使用平均物理半径计算目标视半径，尚未建模行星椭球或土星环。

## SwissEph 对齐边界

Taiyin core runtime 已经覆盖不少 SwissEph 工作流，但它还不是
Swiss-compatible API layer。下面这些能力仍未完全对齐，或更适合放到独立
compat / extension 包中：

```text
Swiss-shaped C API / Python API、global state、error string、文件路径语义
TRUEPOS/NOABERR/NOGDEFL/TOPOCTR/HELCTR/BARYCTR/SIDEREAL 等 Swiss flag 组合语义
Swiss body id、fixed-star name/alias、asteroid 文件约定和 hypothetical bodies
可选 astrology extension 已提供十种 typed house system、平均/真月球交点、Delaunay 平均远地点和
瞬时月球远地点，以及小型 typed sidereal baseline（Fagan/Bradley、Lahiri、Raman、
Krishnamurti、true Chitra/Spica 和 true Galactic-Center 0 Sagittarius）。当前已有
Taiyin-native 自定义 ayanamsha 注册表，但仍未内置完整的兼容模式表，也尚未提供
自然/插值 Lilith、lots 和其他流派 synthetic points
城市光害组件、月牙专用的月球初见模型和更完整的 limiting-magnitude profile；点源 heliacal 事件搜索已提供
完整月掩地表可见区域/路径、本地掩星期间升落区间、Swiss-style attr[] 字段
完整日月食/掩星 attr-style 输出和所有 Swiss local circumstance 字段
Swiss-compatible refraction、太阳盘内光线偏折、半径常数和经验星等约定
```

近期不建议直接复制 SwissEph 的完整 API 表面。更稳的路线是：

1. Core runtime 继续使用显式 `NativeCalcContext`、typed result structs 和
   model/profile 字段。
2. 先补强已经公开 API 的边界覆盖，尤其是 Moon/planet visibility 的高纬、
   custom horizon、tangent/grazing case。
3. 等 flags、ID、常数和 convention 映射整理清楚后，再在 compat layer 中提供
   Swiss-compatible shim。

### Engine / Store facade

当前全局 runtime 已可用，但现代 C++ 层的 `Engine / Store / Config / Scratch` facade 尚未落地。后续如果需要服务端多实例、不同 model profile 隔离、长期缓存共享和插件化配置，应先设计这层，而不是继续扩大 free-function 表面。

### Result assembly 和格式化

当前 runtime 返回数值数组或 C++ result struct。尚未提供 chart-level result assembly、JSON formatter、本地化名称、单位转换和 debug report。占星、历法、CLI 或服务端输出层应在 core runtime 之外组织。

## 后续规划方向

后续版本会围绕下面几个方向继续扩展。具体进入哪个版本，取决于 API 稳定性、外部 oracle 覆盖和数据准备情况。

```text
更完整的 observed UTC/EOP/CPO 外部验证
更清晰的 horizontal/refraction convention 文档和对照
太阳、月亮和行星 visibility 的更广外部表格
更高保真的光度和天空亮度模型
宫头/角点速度、fractional house-position 查询和其余宫制
Engine / Store facade，用于多实例和长期运行服务
```

以下方向属于更长期能力，不作为当前 core runtime 的基础能力承诺：

```text
完整 occultation/appulse 系统，包括地表可见区域/路径摘要、批量星表扫描、行星互掩和卫星凌日
复杂 chart workflow pipeline
重写底层 flat apparent kernel
把节气/星座/相位命名放进 core runtime
```
