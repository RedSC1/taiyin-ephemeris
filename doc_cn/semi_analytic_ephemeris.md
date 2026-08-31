# 内置半解析星历模型

文档状态：当前说明
最后审阅：2026-08-31
主要代码：`src/long_range_analytic.cpp`、`src/semi_analytic.cpp`

Taiyin 对外只提供一套冻结、无需运行时数据文件的半解析星历。它的优先级低于
SPK/OPM2、高于 TKC1/Kepler；公开显式路线只有
`TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC`。内部会根据目标和年代选择最合适的组件，
调用者不需要在“紧凑模型”和“长年代模型”之间做第二次路由。

当前主实现包括：

- 水星至海王星及物理地球的 VSOP2013/TOP2013 来源 L/B/R 表；
- 一套全年代统一校准的 ELP 来源月球级数；
- 1600–2200 年高精度冥王星模型和一套宽年代低精度 fallback；
- 内部重建的 EMB 与 Sun/SSB；
- 原有卫星和物理中心／COB 修正模型。

旧紧凑主星和月球实现已归为 legacy。只有旧冥王星中段和 Sun/SSB 组件在实测更准或
明显更快的区间继续被统一 provider 内部使用；它们不再注册成第二套公开 provider。

## 覆盖范围和路线

主要 Sun、行星、EMB、Earth、Moon 和 Pluto 路线统一覆盖 JD TDB `-470455.0` 到
`5373545.0`，约为天文纪年 -6000 至 +10000 年。卫星路线仍使用各自单独声明的
覆盖区间，并且不外推。

直接支持：

```text
Mercury/Venus/EMB/Mars/Jupiter/Saturn/Uranus/Neptune/Pluto barycenter / Sun
Sun / SSB
Earth / Sun
Moon / Earth
Phobos and Deimos / Mars，Mars / Mars barycenter 由两者重建
Io、Europa、Ganymede、Callisto / Jupiter，以及 Galilean-dominant 的
  Jupiter / Jupiter barycenter 修正
Charon / Pluto，以及由 Charon 和四颗有质量小卫星共同重建的
  Pluto / Pluto barycenter
Triton / Neptune，以及 Triton-dominant 的 Neptune / Neptune barycenter 修正
```

显式无数据路线为：

```text
TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC  Taiyin 半解析星历
```

该路线不会加载或分发 Moshier/PLAN404 代码和系数表。

## 主行星模型

运行时采用经典 Poisson／Fourier-monomial 形式：

```text
sum(T^n * A * cos(phase + frequency * T))
T = (JD_TDB - 2451545.0) / 365250
```

并分别计算日心黄经、黄纬和距离。水星、金星、物理地心和火星来自完整
VSOP2013 椭圆轨道要素解的离线转换；木星至海王星来自 TOP2013 L/B/R 表。
离线换基、选项和残差拟合以 DE441 为目标，拟合出的多项式与周期残差已折入最终
系数表，不在运行时叠第二层修正。

签入的八颗行星产物包含 8,697 项、209 个坐标／时间幂 group。坐标转换严格使用
源 JavaScript 正式实现的同一固定矩阵：官方理论原生轴先转 ICRF，再转 Taiyin 的
平均 J2000 黄道；运行时最终返回 ICRF/J2000 赤道直角状态。这里不能换成小角度近似
逆矩阵，否则外行星距离会把极小旋转误差放大到数百公里。

二阶 jet evaluator 一次解析求出位置、速度和加速度，不使用有限差分。EMB 由物理
地球和月球重建；Sun/SSB 由九个行星系统质量加权重建。旧表可靠区间的内部仍使用
更快的紧凑 Sun/SSB evaluator，并在覆盖边界做十年 C2 连续过渡。

### DE441 精度和求值成本

下表使用 1600–2200 年间 401 个等距 TDB 样本，直接对照 DE441
`target -> Sun` 直角坐标。Release 本地微基准测量 compiled block 的一次完整 state
求值（含解析位置、速度、加速度，不含 catalog 路由）；耗时依赖机器。

| 目标 | 位置 RMS km | 角度 RMS 角秒 | us/state |
| --- | ---: | ---: | ---: |
| Mercury | 6.11 | 0.0218 | 5.4 |
| Venus | 10.48 | 0.0193 | 2.7 |
| Earth | 15.34 | 0.0211 | 6.8 |
| Mars | 40.50 | 0.0364 | 8.3 |
| Jupiter | 79.80 | 0.0209 | 10.9 |
| Saturn | 91.68 | 0.0132 | 17.0 |
| Uranus | 373.12 | 0.0254 | 8.4 |
| Neptune | 793.78 | 0.0347 | 2.8 |

在完整 -6000 至 +10000 宣称区间内，每个目标取 1,201 个等距样本，L/B/R 的位置
RMS／角度 RMS 为：

| 目标 | RMS km | RMS 角秒 | 最大 km |
| --- | ---: | ---: | ---: |
| Mercury | 25.67 | 0.0911 | 103.09 |
| Venus | 96.65 | 0.1829 | 505.58 |
| Earth | 98.57 | 0.1350 | 494.93 |
| Mars | 409.64 | 0.3691 | 1458.10 |
| Jupiter | 760.50 | 0.1971 | 3104.76 |
| Saturn | 2352.06 | 0.3121 | 17664.74 |
| Uranus | 14405.56 | 1.0124 | 50081.60 |
| Neptune | 6119.70 | 0.2596 | 22708.21 |

这些是稀疏确定性网格结果，不是形式化最坏误差保证；它们描述冻结几何状态模型，
不等于最终 apparent/topocentric 结果。

## 月球和冥王星

月球使用 1,241 个折叠项和 763 个共享相位参数，三个坐标复用相位并解析求导。
对 DE441 的 RMS 为：1600–2200 年 `0.282 km / 0.129 角秒`（601 点），
-3000–+3000 年 `0.833 km / 0.421 角秒`（1,201 点），完整 -6000–+10000 年
`2.251 km / 1.173 角秒`（2,001 点）。

冥王星在 1600–2200 年使用直接拟合 DE441 的 Chebyshev 模型，在旧覆盖区内通过
十年 C2 过渡连接到精度更好的 legacy 紧凑模型；旧覆盖边界再用十年过渡连接到宽年代
25-group Legendre/Fourier fallback。现代 601 点结果约为
`891 km / 0.025 角秒` RMS。完整 -6000–+10000 年 composite 在 2,001 点上约为
`786,000 km / 25.5 角秒` RMS，只能视为“仍可计算”的粗略 fallback，不能宣传成
精密冥王星星历。

### 卫星精度边界

这些卫星路线是无需数据文件的 fallback 状态，不是高精度卫星星历。文档中的独立
留出验证误差从部分残差表的几十公里，到紧凑 Galilean 模型的数百公里不等。按质量
加权得到的行星物理中心修正可以远小于单颗卫星的位置误差，但这不代表卫星状态本身
变成高精度。需要精确卫星测量或卫星现象时，应加载同源 SPK 或 OPM2 数据包。

内置模型没有土星或天王星卫星路线。物理土星/天王星请求因此需要直接数据；只有在
调用者明确接受时，才会使用普通的系统质心近似。

## Legacy 紧凑组件

旧主星系数产物已经移动到 `src/legacy/`。其中有界冥王星与 Sun/SSB 组件在实测更准
或更省时的区间继续由统一 provider 内部调用；旧月球和普通行星路径不再参与生产
路由。卫星与 COB 模型仍是当前组件，不受这次主模型迁移影响。

新生成表记录源 JavaScript revision，并对 C++ 生成器实际读取的完整系数 payload
保存 SHA-256。重新生成属于维护流程，不是普通构建步骤。

### Mars 系统卫星残差层

MAR099 提供 `Phobos (401) -> Mars (499)` 与 `Deimos (402) -> Mars (499)`，覆盖 JD TDB
`2305447.5` 至 `2670691.5`（约 1600–2600），超出范围不外推。两者使用 residual-table
evaluator；确定性留出验证的 RMS 分别为 Phobos `65.0 km`、Deimos `5.95 km`。这套紧凑
模型优先保证物理火星中心修正，不是火星表面卫星现象用的精密星历。

MAR099 没有其他有质量卫星条目，因此 runtime 用嵌入的 Phobos、Deimos 与完整系统 GM
显式求 `Mars (499) -> Mars barycenter (4)`。两颗卫星的模型误差在此中心修正中分别衰减到
约 `0.046 mm` 与 `0.013 mm` RMS。生成后的 C++ 表为
`src/internal/mars_satellite_coefficients.inc`；采样拟合输入保留为私有维护产物。

### 木星系统 Galilean L1.2 层

无数据文件 fallback 提供 `Io (501)`、`Europa (502)`、`Ganymede (503)`、`Callisto (504)`
相对物理木星中心 (`599`) 的状态，覆盖 JD TDB `2305456.5` 至 `2524602.5`（约
1600-01-10 至 2200-01-10）。它保留 Astronomy Engine 发布的完整紧凑 L1.2 四大卫星
级数，并由 Taiyin 自己的二阶状态 evaluator 计算。L1.2 自变量为 TT；此处 runtime 的
TDB 时刻只差毫秒量级，对声明精度无实质影响。

在共同覆盖区的确定性两日网格上，相对 JUP365 的相对位置 RMS / P95 / 最大误差为：Io
`847.6 / 1553.1 / 1743.3 km`，Europa `382.4 / 723.6 / 975.4 km`，Ganymede
`295.8 / 472.1 / 640.2 km`，Callisto `403.4 / 593.8 / 900.2 km`。它们适合普通几何和
apparent-position fallback，不是精密卫星星历。

四颗卫星的 JUP365 GM 用于质量加权重建 `Jupiter (599) -> Jupiter barycenter (5)`。这是
明确的 **Galilean-dominant** 修正：未包括 Amalthea、Thebe、Adrastea、Metis。同一网格上，
该物理木星 offset 相对 JUP365 直接 `599 -> 5` 为 `52.8 m` RMS、`94.8 m` P95、`151.7 m`
最大误差，因此不能表述为完整木星系统重建。保留系数与 MIT notice 在
`src/third_party/astronomy_engine/`；其实现把基础理论标为 Duriez、Lainey、Vienne 的 L1.2。
JUP365 仅用于验证，不随仓库分发。

### 冥王星系统 Charon 残差层

内置路线提供由 PLU060 校准的 `Charon (901) -> Pluto (999)`，覆盖其确切共同 SPK 区间
JD TDB `2378497.5` 至 `2524591.5`（约 1800-01-02 至 2199-12-30）。模型由紧凑解析
carrier 与 400 年残差段组成；每段有半径、面内 phase、法向高度三个 channel，各使用
degree-4 Chebyshev secular 部分和一个线性调制 carrier harmonic，相邻段有 14 天 C1
blend。首尾段绝不外推，区间外不宣传 Charon fallback。

该路线还提供 `Pluto (999) -> Pluto barycenter (9)`，以 PLU060 系统 GM 合并 Charon 以及
Nix、Hydra、Kerberos、Styx 的 two-basic-angle Poisson table。独立 32 日 PLU060 网格中，
四颗小卫星在物理冥王星修正中的残差为 `1.28 m` RMS、`1.72 m` P95、`1.92 m` 最大；
Charon 的 `11.1 m` 相对状态 RMS 经质量比例衰减到约 `1.2 m`，组合后的 data-free
Pluto-center fallback 因而是几米量级。这些数字只对源 SPK 成立，不是对 PLU060 模型
外的精度承诺。

生成是使用本地 `plu060.bsp` 的离线私有维护流程；输入 SPK 与采样 state 均不随 runtime
artifact 分发。生成后的 runtime 表位于
`src/internal/charon_plu060_coefficients.inc` 与
`src/internal/pluto_small_satellite_coefficients.inc`。

### 海王星系统 Triton 残差层

同一 generic residual-table evaluator 提供 NAIF NEP098 校准的 `Triton (801) -> Neptune
(899)`，覆盖 JD TDB `2378496.5` 至 `2524592.5`（约 1800-01-01 至 2200-01-01）。每个
400 年段含 degree-4 Chebyshev 部分及四个振幅按段内时间二次变化的 carrier harmonic；
同样采用 14 天 C1 overlap，且严格禁止外推。

逐段确定性日采样留出结果为 `18.960 km` RMS、`35.272 km` P95、`56.112 km` 最大误差。
NEP098 的 Triton GM 与完整海王星系统 GM 用于 `Neptune (899) -> Neptune barycenter (8)`。
这是 **Triton-dominant** 而不是完整系统重建：相对 NEP098，精确 Triton-only 质量公式与
海王星直接中心状态相差 `0.045 km` RMS、`0.051 km` 最大，因为 Naiad、Thalassa、Despina、
Galatea、Larissa、Proteus 尚未加入；Triton 插值误差最多再贡献约 `0.012 km`。它可作无需
数据文件的物理中心修正，不能宣传为完整海王星系统 COB 解。

## Held-out 精度

源模型在公元前 3000 年至公元 3000 年相对 DE441 的 held-out 日心角度 RMS：

| 目标 | RMS |
| --- | ---: |
| Mercury | 1.66 角秒 |
| Venus | 0.66 角秒 |
| EMB | 0.56 角秒 |
| Mars | 2.29 角秒 |
| Jupiter | 3.31 角秒 |
| Saturn | 0.29 角秒 |
| Uranus | 3.65 角秒 |
| Neptune | 0.21 角秒 |
| Pluto | 1.53 角秒 |

修正后的地心月球模型在覆盖全区间、共 68,484 个历元的独立 32 日网格上，相对
DE441 的位置 RMS 为 `0.666 km`、角度 RMS 为 `0.337 角秒`、径向 RMS 为
`0.236 km`；最大误差分别为 `4.845 km`、`2.691 角秒` 和 `1.739 km`。上一版
XL1 冻结模型在同一网格上的位置 RMS 为 `1.336 km`、角度 RMS 为 `0.704 角秒`。

在 1700 至 2300 年区间，新模型的位置 RMS 为 `0.323 km`、角度 RMS 为
`0.157 角秒`。原始截断 ELP 在现代区间已经很准，但覆盖整个六千年区间仍必须使用
DE441 稀疏修正。

这些数值描述冻结基础状态模型，不等于最终 apparent/topocentric 误差。光行时、
光行差、引力偏折、岁差章动、观测者几何和显式 UTC/时间尺度转换仍由正常 runtime 层处理。

## 来源记录

行星冻结模型来自 `taiyin-exp` revision
`a5bdf675f921804874dc4e0a0838beebfbcf2b32`。
Sun/SSB 重建采用 NAIF JPL DE440 constants kernel
[`gm_de440.tpc`](https://naif.jpl.nasa.gov/pub/naif/generic_kernels/pck/gm_de440.tpc)
中的太阳系 GM。
月球冻结产物及其可复现拟合工具位于
生成后的 C++ 表中记录其产物校验和。私有维护拟合流程从完整 ELP/MPP02 DE405 表
确定性选择项、保留连续样本块不参与 sparse fit，并打包选出的相位和拟合系数。它只在
重新拟合时需要 NumPy、jplephem、完整 ELP 表和 DE441 BSP，普通构建与运行时均不需要
这些外部文件；该流程不包含在公开源码快照中。记录的 4,096 个历元中心差分速度对照
DE441 SPK 导数结果为当前速度向量 RMS `0.273 km/day`，上一版为 `0.417 km/day`。
