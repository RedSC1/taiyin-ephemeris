# 内置半解析星历

文档状态：当前说明
最后审阅：2026-08-12
主要代码：`src/semi_analytic.cpp`、`src/internal/semi_analytic_coefficients.inc`

Taiyin 内置了一套不需要运行时数据文件的冻结半解析 fallback。当 SPK 或 OPM2
没有可用路线时，它可以继续提供长期星体状态；默认优先级低于 SPK/OPM2，高于
TKC1/Kepler。

## 覆盖范围和路线

行星级数覆盖 JD TDB `625295.0` 到 `2816795.0`，约为公元前 3000 年至
公元 3000 年。月球拟合覆盖 JD TDB `625306.84` 到 `2816794.84`；Earth/Sun
使用 EMB 与月球覆盖范围的交集。

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

显式路线是 `TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC`，不会加载或分发
Moshier/PLAN404 代码和系数表。

### 卫星精度边界

这些卫星路线是无需数据文件的 fallback 状态，不是高精度卫星星历。文档中的独立
留出验证误差从部分残差表的几十公里，到紧凑 Galilean 模型的数百公里不等。按质量
加权得到的行星物理中心修正可以远小于单颗卫星的位置误差，但这不代表卫星状态本身
变成高精度。需要精确卫星测量或卫星现象时，应加载同源 SPK 或 OPM2 数据包。

内置模型没有土星或天王星卫星路线。物理土星/天王星请求因此需要直接数据；只有在
调用者明确接受时，才会使用普通的系统质心近似。

## 模型结构

- 水星到冥王星和 EMB 使用独立拟合 JPL DE441 的紧凑谐波级数。
- 月球使用从完整 ELP/MPP02 DE405 级数中按支持区间三维 RMS 贡献选出的
  1,175 项，随后在 J2000 黄经、黄纬和对数距离上叠加独立拟合的 DE441
  稀疏残差修正；三个 channel 分别保留 10、20、15 个 phase group。
- Earth/Sun 由 EMB/Sun 与按地月质量比缩放的 Moon/Earth 向量组合得到。
- Sun/SSB 由九个行星质心日心状态按 DE440 行星系统引力参数质量加权重建；这为
  固定星 apparent 计算提供重心原点，而不需要独立数据文件。
- 月球级数使用 ELP/MPP02 岁差参数直接转换到 J2000 黄道，最终所有路线统一返回
  ICRF/J2000 赤道直角坐标状态。
- 位置、速度、加速度通过二阶自动微分一次求出，不使用有限差分伪造导数。

冻结 C++ 表同时记录行星生成源 revision 和已签入月球产物的 SHA-256。重新生成属于
私有维护流程；普通构建和公开源码快照均不依赖该工具。

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
光行差、引力偏折、岁差章动、观测者几何和时间尺度策略仍由正常 runtime 层处理。

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
