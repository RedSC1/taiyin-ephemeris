# 日月食搜索

文档状态：当前说明
最后审阅：2026-07-01
主要头文件：`include/taiyin/runtime/eclipse_search.h`

本文描述 Taiyin 的日月食搜索算法、模型约定和公开 C++ API。Eclipse 层是数值 runtime 功能：它返回 Julian dates、分类、食分、几何量、路径和地方情况。历法展示、本地化标签和应用层格式化属于这一层之上。

性能测量、第三方实现对比和实验记录属于维护者资料，不作为公开 API 文档的一部分。

## 范围

Eclipse API 覆盖：

- 月食 solve 和 search；
- 全球日食 solve 和 search；
- 地理观测者的地方日食情况；
- 日食 Besselian elements、route rows、route curves 和地方 boundary helpers。

所有入口都使用调用方的 `NativeCalcContext`，所以结果依赖同一个 context 中配置的 ephemeris routes、apparent-position options、time-scale policy、Delta T model、eclipse shadow/radius models。调用这些 API 前，全局 ephemeris runtime 必须已经用覆盖请求日期范围的数据初始化。

## 来源和算法

### 共享搜索 seed

月食和日食搜索都使用 Jean Meeus, *Astronomical Algorithms*, 2nd ed., chapter 52 作为 lunation pre-filter：

- 月食用 chapter 52 argument-of-latitude threshold 测试满月；
- 日食用对应 node-distance threshold 测试新月；
- Meeus 公式在 runtime 计算完整星历几何前，提供廉价的近似最大食时间。

这个 pre-filter 只用于生成候选朔望月和初始时间，不决定最终结果。最终分类、食分、接触时间和路线几何都来自配置好的 ephemeris runtime。地方日食搜索会在全球候选之后再做 observer-local probe、Besselian seed 和 topocentric exact contact refinement。

### 月食

月食 solver 是寿星万年历（`sxwnl`）`eph.js` / `eph0.js` 中月食几何路线的 C++ 移植，底层星历输入接 Taiyin runtime ephemerides。

在候选满月处，它计算 apparent Sun/Moon 的 longitude、latitude、distance 和 speed。Moon center 在角坐标中和地球本影轴比较。Solver 通过在 shadow plane 中线性化局部运动、最小化 Moon center 到 shadow axis 的距离来精修食甚。

分类使用精修后的几何：

- `TAIYIN_ECLIPSE_TOTAL`：月亮完全进入本影；
- `TAIYIN_ECLIPSE_PARTIAL`：月亮和本影相交但没有完全进入；
- `TAIYIN_ECLIPSE_PENUMBRAL`：月亮只和半影相交；
- `TAIYIN_ECLIPSE_NONE`：该朔望月没有月食。

食分使用传统直径比例公式：

```text
umbral_magnitude    = (umbra_radius + moon_radius - rho) / (2 * moon_radius)
penumbral_magnitude = (penumbra_radius + moon_radius - rho) / (2 * moon_radius)
```

其中 `rho` 是 Moon center 到 shadow axis 的距离。

设置 `TAIYIN_ECLIPSE_INCLUDE_CONTACTS` 时，solver 填充：

- `P1`：半影食始；
- `U1`：初亏；
- `U2`：食既；
- `Greatest`：食甚；
- `U3`：生光；
- `U4`：复圆；
- `P4`：半影食终。

不适用的接触时间是 `NaN`，例如偏食时 `U2` 和 `U3` 为 `NaN`。

#### 2025-09-07 紫金山天文台月食 Oracle

2025-09-07 月全食 UT 回归 fixture 来自中国科学院紫金山天文台（PMO）表格 `2025年9月7日月全食概况`：

- `https://pmo.cas.cn/xwdt2019/kpdt2019/202412/t20241223_7508765.html`

PMO 表同时给出 Terrestrial Dynamical Time (`TD`) 和北京时间。下表的 JD UT 由北京时间减 8 小时换算得到：

| PMO row | PMO TD | PMO Beijing Time | Taiyin field | PMO-derived JD UT |
| --- | ---: | ---: | --- | ---: |
| `半影食始` | `2025-09-07 15:28.0` | `2025-09-07 23:26.9` | `P1` | `2460926.143681` |
| `初亏` | `2025-09-07 16:27.9` | `2025-09-08 00:26.8` | `U1` | `2460926.185278` |
| `食既` | `2025-09-07 17:31.5` | `2025-09-08 01:30.4` | `U2` | `2460926.229444` |
| `食甚` | `2025-09-07 18:13.0` | `2025-09-08 02:11.8` | `Greatest` | `2460926.258194` |
| `生光` | `2025-09-07 18:54.4` | `2025-09-08 02:53.2` | `U3` | `2460926.286944` |
| `复圆` | `2025-09-07 19:58.0` | `2025-09-08 03:56.9` | `U4` | `2460926.331181` |
| `半影食终` | `2025-09-07 20:57.8` | `2025-09-08 04:56.6` | `P4` | `2460926.372639` |

对应的 PMO-derived UT 时钟时间：

| Taiyin field | UT |
| --- | ---: |
| `P1` | `2025-09-07 15:26:54` |
| `U1` | `2025-09-07 16:26:48` |
| `U2` | `2025-09-07 17:30:24` |
| `Greatest` | `2025-09-07 18:11:48` |
| `U3` | `2025-09-07 18:53:12` |
| `U4` | `2025-09-07 19:56:54` |
| `P4` | `2025-09-07 20:56:36` |

PMO 值四舍五入到 `0.1` 分钟。`TD` 和北京时间两列因此只能推出约一分钟精度的 Delta T，和预期 2025 值以及独立 NASA decade-table 的 greatest-eclipse TD 值（`18:12:58 TD`）在表格精度内一致。

Runtime comparison，最后检查于 2026-06-30：

| Event | PMO JD UT | Taiyin JD UT | Taiyin - PMO |
| --- | ---: | ---: | ---: |
| `P1` | `2460926.143680556` | `2460926.143621061` | `-5.14s` |
| `U1` | `2460926.185277778` | `2460926.185200211` | `-6.70s` |
| `U2` | `2460926.229444444` | `2460926.229415872` | `-2.47s` |
| `Greatest` | `2460926.258194444` | `2460926.258204186` | `+0.84s` |
| `U3` | `2460926.286944444` | `2460926.286975643` | `+2.70s` |
| `U4` | `2460926.331180556` | `2460926.331099947` | `-6.96s` |
| `P4` | `2460926.372638889` | `2460926.372520987` | `-10.19s` |

Taiyin 对比使用和 TS/sxwnl regression fixtures 相同的模型 preset：

| Setting | Value |
| --- | --- |
| Ephemeris data | Taiyin OPM2 major-bodies 600y files |
| Shadow model | `ECLIPSE_SHADOW_CHAUVENET` |
| Shadow Earth scale | `1.02 * 0.998340` |
| Shadow Sun scale | `1.02` |
| Shadow parallax scale | `1.02` |
| Moon radius model | `ECLIPSE_MOON_ALMANAC` |
| Moon radius | `0.2725076 * 6378.1366 km` |
| Apparent options | light-time, aberration, deflection, true ecliptic of date |
| Segment cache setting | `segment_cache=4096` |

PMO 表精度是 `0.1` 分钟，所以单个 PMO 时间约有 `±3s` rounding uncertainty。使用 Chauvenet/Almanac preset 时，Taiyin 的 `Greatest`、`U2` 和 `U3` 在该发布精度内；`P1`、`U1`、`U4` 和 `P4` 有约 `5-10s` 的模型层差异。

启用随附 Kaguya TLL1 月缘模型后的 PMO 误差如下；食甚只由月心与影轴
几何决定，因此保持不变：

| Event | 圆月误差 | TLL1 月缘误差 |
| --- | ---: | ---: |
| `P1` | `-5.14s` | `+3.10s` |
| `U1` | `-6.70s` | `-1.44s` |
| `U2` | `-2.47s` | `-1.29s` |
| `Greatest` | `+0.84s` | `+0.84s` |
| `U3` | `+2.70s` | `+4.33s` |
| `U4` | `-6.96s` | `-3.02s` |
| `P4` | `-10.19s` | `-5.55s` |

七个时刻的 MAE 从 `5.00s` 降到 `2.79s`，RMSE 从 `5.83s` 降到
`3.22s`。单项并非全部改善：`U3` 变远；同时 PMO 的 `0.1` 分钟发布精度
意味着约 `±3s` 的四舍五入不确定度。

### 全球日食

全球日食 solver 从 Meeus 新月 pre-filter 生成候选，然后通过 Taiyin runtime 计算 Sun/Moon vectors。它构造月影轴，并测试该轴和 WGS84 地球椭球的关系。接触时间以 Besselian/root seed 起步，最后回到 apparent geometry 做收尾。

食甚定义为月影轴最接近地球的瞬间，即 `axis_distance_km` 最小。它不是 penumbral margin 最小值。这和常见 NASA/PMO global greatest-eclipse 定义一致，也避免最大时间耦合到变化中的半影半径。

全球日食接触时间使用这些约定：

- `P1`：全球偏食开始，半影第一次接触地球；
- `C1`：全球中心食开始，本影/伪本影轴第一次到达地球；
- `Greatest`：影轴最近地球；
- `C4`：全球中心食结束；
- `P4`：全球偏食结束。

全球 API 不描述特定观测者看到什么。观测者情况使用地方日食入口。

#### 2024-04-08 紫金山天文台全球日食 Oracle

2024-04-08 全球日食回归 fixture 来自中国科学院紫金山天文台（PMO）公开天象资料：

- `https://www.pmo.cas.cn/xwdt2019/kpdt2019/202312/P020240201511299456727.txt`

PMO 表题为 `2024年 4月 8日日全食概况`。全球事件行映射到 Taiyin global solar contact fields 如下。下表的 JD UT 由 PMO UT 换算得到；Taiyin 当前输出见后面的 runtime comparison 表。

| PMO row | PMO UT | Taiyin field | PMO JD UT |
| --- | ---: | --- | ---: |
| `偏食始` | `2024-04-08 15:42:13` | `P1` | `2460409.154317129` |
| `全食始` | `2024-04-08 16:39:59` | `C1` | `2460409.194432870` |
| `食甚` | `2024-04-08 18:17:20` | `Greatest` | `2460409.262037037` |
| `全食终` | `2024-04-08 19:54:28` | `C4` | `2460409.329490741` |
| `偏食终` | `2024-04-08 20:52:21` | `P4` | `2460409.369687500` |

同一 PMO 行给出食甚位置；fixture 使用这些 rounded values：

| Quantity | PMO value | Fixture value |
| --- | ---: | ---: |
| Greatest latitude | `+25°17.1′` | `25°17.1′` |
| Greatest longitude | `-104°8.6′` | `-104°8.6′` |

PMO 时间四舍五入到整秒。Taiyin 当前 regression 输出使用 Taiyin OPM2 major-bodies 600y files 作为星历数据源，和 PMO rounded table 对该事件约在两秒内一致。Swiss Ephemeris 可作为独立比较，但不是这个 fixture 的 baseline，因为它的 global P4/C4 convention 和模型选择与 PMO/Taiyin regression 值有数秒差异。

Runtime comparison，最后检查于 2026-07-01：

| Event | PMO JD UT | Taiyin JD UT | Taiyin - PMO |
| --- | ---: | ---: | ---: |
| `P1` | `2460409.154317129` | `2460409.154338569` | `+1.852s` |
| `C1` | `2460409.194432870` | `2460409.194446871` | `+1.209s` |
| `Greatest` | `2460409.262037037` | `2460409.262039739` | `+0.233s` |
| `C4` | `2460409.329490741` | `2460409.329500663` | `+0.858s` |
| `P4` | `2460409.369687500` | `2460409.369681376` | `-0.529s` |

启用 Kaguya TLL1 并联立求解 WGS84 椭球切点后，受月缘影响的 `P1/P4`
误差分别变为 `-0.047s` 和 `-0.396s`；两项都比圆月结果更接近该 PMO 表值。
`C1`、食甚和 `C4` 是影轴/地球几何，保持不变。

食甚位置 comparison：

| Quantity | PMO value | Taiyin value | Taiyin - PMO |
| --- | ---: | ---: | ---: |
| Greatest latitude | `25.285000°` | `25.289609°` | `+0.004609°` |
| Greatest longitude | `-104.143333°` | `-104.147999°` | `-0.004665°` |

因此 global `P1/P4` 只把 Besselian roots 当作 seed，最终在每个候选时刻
联立最小化 WGS84 椭球面上的方向相关半影余量，再对该最小值求根。用廉价
Besselian projected-ellipse scalar 或固定的径向投影切点作为最终 `P1/P4`
模型，都会让 global partial contacts 产生可见的秒级偏移。

#### 2026-08-12 紫金山天文台全球日食 Oracle

中国科学院紫金山天文台已经发布 `2026年8月12日日全食` 公开资料：

- 页面：`https://pmo.cas.cn/xwdt2019/kpdt2019/202512/t20251231_8093683.html`
- 概况附件：`https://pmo.cas.cn/xwdt2019/kpdt2019/202512/P020251231400816723831.txt`
- 路线附件：`https://pmo.cas.cn/xwdt2019/kpdt2019/202512/P020260624590816163664.txt`

该事件的全食带从俄罗斯极北部开始，经北冰洋、格陵兰岛、冰岛、大西洋东北部和西班牙，在地中海西部结束；偏食覆盖北美洲北部、北冰洋、大西洋北部、非洲西北部、欧洲大部和亚洲极北部。

概况附件给出 global contact、食甚位置、食分、全食时长和全食带宽。当前 regression fixture 使用下列字段：

| PMO row | PMO UT | Taiyin field | PMO JD UT |
| --- | ---: | --- | ---: |
| `偏食始` | `2026-08-12 15:34:14` | `P1` | `2461265.148773148` |
| `全食始` | `2026-08-12 17:00:06` | `C1` | `2461265.208402778` |
| `食甚` | `2026-08-12 17:45:56` | `Greatest` | `2461265.240231481` |
| `全食终` | `2026-08-12 18:32:12` | `C4` | `2461265.272361111` |
| `偏食终` | `2026-08-12 19:57:59` | `P4` | `2461265.331932870` |

食甚位置和路径量：

| Quantity | PMO value |
| --- | ---: |
| Greatest latitude | `+65°13.3′` |
| Greatest longitude | `-25°15.2′` |
| Magnitude | `1.040` |
| Total duration | `2m21.2s` |
| Path width | `300.3 km` |

Runtime comparison，最后检查于 2026-07-06：

| Event / quantity | Taiyin - PMO |
| --- | ---: |
| `P1` | `+1.686s` |
| `C1` | `+1.389s` |
| `Greatest` | `+0.629s` |
| `C4` | `+0.531s` |
| `P4` | `+0.098s` |
| Greatest latitude | `+0.001996°` |
| Greatest longitude | `+0.012161°` |
| Total duration | `-2.677s` |
| Path width | `-7.555 km` |

启用 Kaguya TLL1 并联立求解 WGS84 椭球切点后，`P1/P4` 误差分别从
`+1.686s / +0.098s` 变为 `-2.269s / -0.467s`。两项仍在 PMO 整秒表约
`±3s` 的发布分辨率内，但单项并不保证比圆月模型更贴近圆整后的表值。合并
2024 和 2026 两场共十个 global 时刻，MAE 从 `0.90s` 降到 `0.80s`，
RMSE 从 `1.07s` 降到 `1.02s`。

这条 fixture 主要覆盖高纬度、北大西洋/欧洲路径和 2024 北美日全食以外的路径几何。PMO 概况表是公开历书资料，contact 时间给到整秒，路径量给到 `0.1` 分/公里量级；因此它适合作为秒级/公里级 sanity oracle，不作为更高精度的内部几何常数来源。`path_width_km` 表示中心线法线方向的局部横向食带宽估计；实现会优先用中心线法线与南北界曲线求交，不把同一时刻北界到南界的地表弧长当作食带宽。

### 地方日食

地方日食例程是寿星万年历（`sxwnl`）`eph.js` / `eph0.js` 中 solar-eclipse Besselian/local geometry 的 C++ 移植，底层使用 Taiyin runtime positions。它们计算某地经纬度和高度下的 topocentric Sun/Moon 情况。

地方搜索先扫描全球日食候选，再用本地 probe table 判断该观测者是否可能见食；接触时间先用 Besselian local scalar 取 seed，再用 topocentric apparent geometry 精修。这样可以覆盖最大食在地平线下、但日出或日落时仍可见偏食的情况。

地方结果包括：

- observer-visible kind bits；
- 地方食甚时间；
- 食分和遮蔽率；
- 食甚时太阳高度和方位；
- 接触时间 `C1`, `C2`, `C3`, `C4` 和 local `Greatest`；
- 第一次/最后一次接触的 position angle 和 vertex angle；
- 全食/环食持续时间；
- 相关时刻的 sunrise/sunset magnitudes。

除非观测者经历全食或环食，否则 `C2` 和 `C3` 是 `NaN`。

### Solar Route 和 Besselian Helpers

Route API 暴露更底层的路径产品：

- `compute_solar_besselian_elements_tt` 计算某一瞬间的 Besselian elements；
- `compute_solar_besselian_polynomial_tt` 在时间跨度内采样并拟合 polynomial；
- `compute_solar_eclipse_route_row_*` 返回某一瞬间附近的 route row；
- `compute_solar_eclipse_route_*` 在区间内采样 rows；
- `compute_solar_eclipse_route_curves_*` 返回 route 和 limit curve points；
- `compute_solar_eclipse_route_product_*` 返回核心食带的 north/south limits，并提供方便渲染的 core path polygon；
- `compute_solar_eclipse_route_map_product_*` 把 core、penumbral、half-magnitude 各层闭合成 map-product polygons；物理南北界缺少一侧时，宽层使用晨昏食甚边界闭合；
- `compute_local_solar_eclipse_boundary_*` 计算给定点和时间附近的地方边界。

这些函数用于地图/路径生成和 diagnostics。简单查询“这个日期附近有没有食”不需要它们。

Route curves 和 route products 都来自 Taiyin route rows。它们包含几何上存在的中心线、偏食界限、本影/伪本影核心界限和半食分界限，并通过当前 `NativeCalcContext` 配置的星历运行时采样。Polygon point 会保留 unwrapped longitude 字段，方便跨 180 度经线的路径包络在渲染时不断裂；调用方可以在投影后再把单个点归一化。这些 API 返回的是数值几何产品，不是完整地图图层：地图投影、抽稀、分段、样式和瓦片/视口裁剪仍由下游完成。

默认路线曲线采样密度为 `TAIYIN_SOLAR_ROUTE_DEFAULT_SAMPLE_COUNT`，也就是
在源路线时间段上采样 400 份，贴近 sxwnl 风格地图流程。
`compute_solar_eclipse_route_curves_*`、`compute_solar_eclipse_route_product_*`
和 `compute_solar_eclipse_route_map_product_*` 的 `_with_options` 版本可以
传入显式 `route_sample_count`，范围由
`TAIYIN_SOLAR_ROUTE_MIN_SAMPLE_COUNT` 和
`TAIYIN_SOLAR_ROUTE_MAX_SAMPLE_COUNT` 限定。这个参数只控制导出的曲线和
polygon 点密度，不改变星历、Delta T、半径、影锥或月缘模型。

默认光滑路线使用一次参数化的 `sxwnl::solar::jieX()` 扫描同时生成
中心线、南北界和晨昏食甚线；`mQie` 有解/无解切换处会在相邻采样间
二分精修，避免原始一次线性端点估算在高纬地区产生点序回折。
中心线与核心界的切换区间还会按默认密度下最大 `0.05°` 的球面段长自适应细分，因此最终点数
可以略多于基础 `route_sample_count`。
中心食路线还会输出 `core_begin_horizon` 与 `core_end_horizon`：它们是用核心半径
求出的两端晨昏圈交线，用来连接南北界。只有两段曲线都存在时才导出 core polygon，
不会退化成端点之间的人造直线封口。Penumbral 和
half-magnitude 层会同时输出
偏食始终接触线、日出/日落食甚线和物理上存在的南北极限界。极区日食或
非中心偏食缺少一侧极限界时，polygon 使用相应晨昏食甚线替代，不伪造
不存在的第二条极限界；各边界在 `mQie` 有解/无解切换处使用精修端点。
开启 TLL1 时，南北主界和接触时刻仍使用方向相关月缘修正。

所有 route row、curve 和 product 入口都显式接收 `uint64_t flags`。Route
只接受 `TAIYIN_ECLIPSE_TRUEPOS` 和
`TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION`；搜索方向、contact 输出等与路线
几何无关的 eclipse flags 会被拒绝。开启月缘修正时，每个采样时刻只准备
一次月球姿态和 apparent Sun/Moon vectors，然后对 core、penumbral 和
half-magnitude 南北界分别迭代其方向相关月缘。路线行的全食或环食时长以
光滑影面速度结果为 seed，再用同一月缘模型精修中心点 C2/C3；中心线不受
月球半径影响。关闭该 flag 时仍保留原有光滑月缘路线和时长语义。

对于非中心日偏食，`compute_solar_eclipse_route_curves_*` 会返回实际存在的
单侧半影界；只有事件达到 0.5 食分时才返回半食分界。它不返回中心线或
本影/伪本影界。完整偏食可见区域的另一侧由日出、日落和接触时刻对应的
地平线几何闭合。`compute_solar_eclipse_route_map_product_*` 使用这条晨昏
边界生成 penumbral polygon，而不是伪造第二条半影界；事件达到 0.5 食分
时，同样生成 half-magnitude polygon。浅偏食的 half-magnitude 曲线和
polygon 为空属于正常结果。

## 时间尺度

每个 eclipse API 都有 TT 和 UT 版本：

- `*_tt` 函数接收并返回 TT Julian dates；
- `*_ut` 函数接收并返回 UT Julian dates，并且在 result struct 有字段时报告 `delta_t_seconds`。

内部星历位置通过 context 的 TDB model 从 TT instant 计算。UT 版本通过 context 的 Delta T policy 转换。和 ephemeris-time 表对比时用 TT；面向民用时钟应用时用 UT。

## Flags

Eclipse 函数接收 `uint64_t flags`。低 32 位是 native position semantic flags；高 32 位是 eclipse-specific options。

当前 eclipse API 接受的低位 native flags 是：

- `TAIYIN_NATIVE_POSITION_TRUEPOS` / `TAIYIN_ECLIPSE_TRUEPOS`。

`TAIYIN_ECLIPSE_TRUEPOS` 是 `TAIYIN_NATIVE_POSITION_TRUEPOS` 的兼容别名。`XYZ`、`SPEED`、`EQUATORIAL`、`RADIANS`、`TOPOCENTRIC` 这类输出形态 flags 会被拒绝，因为 eclipse result 的输出结构是固定的。`ASTROMETRIC`、`NO_ABERR`、`NO_GDEFL` 这类还没有贯通 eclipse fast path 的 position-convention flags 也会被拒绝，而不是静默忽略。

Eclipse-specific options 在高 32 位：

- `TAIYIN_ECLIPSE_INCLUDE_CONTACTS`：计算接触时间；
- `TAIYIN_ECLIPSE_EXCLUDE_PENUMBRAL`：忽略只发生半影月食的事件；
- `TAIYIN_ECLIPSE_BACKWARD`：搜索上一次而不是下一次；
- `TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION`：使用全局 runtime 加载的
  TLL1 模型精修日月食接触时刻。

月缘修正必须显式开启，并要求全局 runtime 已加载模型。加载方式、生命周期、覆盖范围与接触语义见
[`lunar_limb_model.md`](lunar_limb_model.md)。

默认情况下，eclipse calculation 使用内置 apparent-position 路线。也就是说，light-time、annual aberration、gravitational deflection、Shapiro delay、frame model、TDB/Delta T policy 等设置会进入 Sun/Moon 几何、Besselian seed、correction window 和 contact refinement。默认 context 应使用推荐的 apparent-position 口径；这是和公开历书、PMO/NASA/Swiss-style oracle 对比时应优先说明的配置。

`TAIYIN_ECLIPSE_TRUEPOS` 不是“更高精度”开关，而是模型切换。设置后，eclipse geometry 使用 geometric true positions，并关闭 light-time、aberration、deflection 和 Shapiro delay 等 apparent 修正。它适合模型实验、debug 和 regression tests；通常公开历书应使用默认 apparent-position 路线。

Astrometric-only、no-aberration、no-deflection 这类单项 eclipse 修正开关目前还不是公开 API。只有等 solar、lunar、local、Besselian 和 correction-window 路径都能消费同一套语义之后，再把这些 flags 放开。

## 月影模型

月食接触时间和食分强依赖采用的地影模型。日食接触、路径宽度和 route-limit 产品也受 shadow radius convention 影响，虽然全球日食食甚仍然是 shadow-axis closest approach。模型通过 `native_context_set_eclipse_shadow_model` 在 `NativeCalcContext` 上选择。

内置模型：

- `dispatch::ECLIPSE_SHADOW_NASA_DANJON`：NASA-style empirical 1% enlargement；
- `dispatch::ECLIPSE_SHADOW_CHAUVENET`：2% enlargement 加 Earth-oblateness factor；
- `dispatch::ECLIPSE_SHADOW_GEOMETRIC`：纯几何，无大气 enlargement；
- `dispatch::ECLIPSE_SHADOW_RAW_DANJON`：Danjon 的 1/85 enlargement。

`NativeCalcContext` 当前默认 `ECLIPSE_SHADOW_NASA_DANJON` 和 `ECLIPSE_MOON_ALMANAC`。这个组合在当前验证集里最匹配 NASA lunar catalog 的 duration 和 magnitude。`Chauvenet` 是有效替代约定，但它的 contacts 和 magnitudes 不应直接和 NASA catalog values 比较。

重要 oracle caveat：NASA HTML lunar catalog 给 greatest-eclipse time、magnitudes 和 phase durations（`P4-P1`, `U4-U1`, `U3-U2`）。它不给单独的 `P1/U1/U2/U3/U4/P4` 接触时间。不能用 `greatest +/- duration/2` 推导单独 contacts；接触区间通常不以食甚对称。

## 公共入口

### 月食

```cpp
solve_lunar_eclipse_at(...)
solve_lunar_eclipse_at_ut(...)
search_next_lunar_eclipse_tt(...)
search_next_lunar_eclipse_ut(...)
search_lunar_eclipses_tt(...)
search_lunar_eclipses_ut(...)
compute_local_lunar_eclipse_visibility_tt(...)
compute_local_lunar_eclipse_visibility_ut(...)
search_next_local_lunar_eclipse_tt(...)
search_next_local_lunar_eclipse_ut(...)
```

已知日期接近某次 eclipse lunation 时用 `solve_*`。查下一次/上一次用 `search_next_*`。区间查询用 bounded `search_lunar_eclipses_*`。

`compute_local_lunar_eclipse_visibility_ut()` 和 `search_next_local_lunar_eclipse_ut()` 用于观测者地方可见性。它们从 `NativeCalcContext::observer_location` 读取观测者经纬高；如果 context 没有设置 observer location，会返回 `TAIYIN_ERROR_INVALID_ARGUMENT`。它们不重新求月食几何，而是在全局月食 contact 上采样 Moon center 的地平高度/方位，并在 P1-P4 区间内查找 moonrise/moonset。可见性结果写入 `LocalLunarEclipseResultUt::visibility_flags`，使用 `TAIYIN_ECLIPSE_*_VISIBLE` bits。

默认不加大气折射；如果设置 `TAIYIN_LOCAL_LUNAR_ECLIPSE_REFRACTION`，采样和 moonrise/moonset 会使用 context 的 refraction model 和 atmosphere fields。

### 全球日食

```cpp
solve_solar_eclipse_at(...)
solve_solar_eclipse_at_ut(...)
search_next_solar_eclipse_tt(...)
search_next_solar_eclipse_ut(...)
search_solar_eclipses_tt(...)
search_solar_eclipses_ut(...)
```

全球结果描述月影是否到达地球，以及全球事件何时达到最大。它不提供观测者地方可见性。

### 地方日食

```cpp
solve_local_solar_eclipse_at_tt(...)
solve_local_solar_eclipse_at_ut(...)
search_next_local_solar_eclipse_tt(...)
search_next_local_solar_eclipse_ut(...)
compute_local_solar_circumstances_tt(...)
compute_local_solar_circumstances_ut(...)
```

这些入口用于地理观测者。它们从 `NativeCalcContext::observer_location` 读取观测者经纬高；如果 context 没有设置 observer location，会返回 `TAIYIN_ERROR_INVALID_ARGUMENT`。`search_next_local_solar_eclipse_*` 先扫描全球日食候选，再计算地方情况，并把请求的 kind filter 应用于 observer-local result。例如，全球日全食如果在观测者处只是偏食，就不会在 `TAIYIN_ECLIPSE_TOTAL` 地方搜索中返回。

### Solar Path Products

```cpp
compute_solar_besselian_elements_tt(...)
compute_solar_besselian_polynomial_tt(...)
evaluate_solar_besselian_polynomial(...)
compute_solar_eclipse_route_row_tt(...)
compute_solar_eclipse_route_row_ut(...)
compute_solar_eclipse_route_tt(...)
compute_solar_eclipse_route_ut(...)
compute_solar_eclipse_route_curves_tt(...)
compute_solar_eclipse_route_curves_ut(...)
compute_solar_eclipse_route_curves_tt_with_options(...)
compute_solar_eclipse_route_curves_ut_with_options(...)
compute_solar_eclipse_route_product_tt_with_options(...)
compute_solar_eclipse_route_product_ut_with_options(...)
compute_solar_eclipse_route_map_product_tt_with_options(...)
compute_solar_eclipse_route_map_product_ut_with_options(...)
compute_local_solar_eclipse_boundary_tt(...)
compute_local_solar_eclipse_boundary_ut(...)
```

## 使用示例

### 某日期附近的月食

```cpp
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/runtime/eclipse_search.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/time.h"

using namespace taiyin;
using namespace taiyin::runtime;

NativeCalcContext ctx;
native_context_set_geocentric_observer(&ctx, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH);
native_context_set_eclipse_shadow_model(&ctx, dispatch::ECLIPSE_SHADOW_NASA_DANJON);
native_context_set_eclipse_moon_radius_model(&ctx, dispatch::ECLIPSE_MOON_ALMANAC);

LunarEclipseResultUt eclipse;
EphemerisEvalDiagnostic diag = {};
const double guess_ut = julian_day({2025, 9, 7, 18, 0, 0.0});

Status st = solve_lunar_eclipse_at_ut(
    &ctx,
    guess_ut,
    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
    &eclipse,
    &diag);

if (st == TAIYIN_STATUS_OK && eclipse.kind != TAIYIN_ECLIPSE_NONE) {
    // eclipse.maximum_jd_ut and eclipse.contact_jd_ut[] are populated.
}
```

### 某观测者处的月食可见性

```cpp
LocalLunarEclipseResultUt local;
EphemerisEvalDiagnostic diag = {};
native_context_set_observer_location(
    &ctx,
    native_observer_location_degrees(116.4074, 39.9042, 43.0));

Status st = search_next_local_lunar_eclipse_ut(
    &ctx,
    julian_day({2025, 9, 7, 0, 0, 0.0}),
    TAIYIN_ECLIPSE_TOTAL,
    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
    &local,
    &diag);

if (st == TAIYIN_STATUS_OK
    && (local.visibility_flags & TAIYIN_ECLIPSE_MAXIMUM_VISIBLE) != 0u) {
    // 食甚时月亮在地平线上方。
}
```

### 下一次日全食

```cpp
SolarEclipseResultUt eclipse;
EphemerisEvalDiagnostic diag = {};

Status st = search_next_solar_eclipse_ut(
    &ctx,
    julian_day({2024, 1, 1, 0, 0, 0.0}),
    TAIYIN_ECLIPSE_TOTAL,
    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
    &eclipse,
    &diag);
```

### 某观测者处的地方日食

```cpp
LocalSolarEclipseResultUt local;
EphemerisEvalDiagnostic diag = {};
native_context_set_observer_location(
    &ctx,
    native_observer_location_degrees(-96.7970, 32.7767, 131.0));

Status st = solve_local_solar_eclipse_at_ut(
    &ctx,
    julian_day({2024, 4, 8, 18, 0, 0.0}),
    TAIYIN_ECLIPSE_INCLUDE_CONTACTS,
    &local,
    &diag);
```

## 验证说明

公开 regression tests 使用几类来源：

- PMO 公开资料：2024 和 2026 全球日食 contact/食甚位置、2025 月全食 contact；
- NASA eclipse catalogs / decade tables：食甚时间、分类、食分和 duration；
- NASA city-table values：地方日食分钟级 sanity check；
- 寿星万年历 `eph.js` / `eph0.js`：移植几何函数的 oracle fixtures；
- OPM2/SPK 数据对照测试：确认星历读取和 route 组合没有偏离。

PMO/NASA 等公开来源优先用作行为基准。旧 TypeScript fixture 主要用于迁移回归和 diagnostic table；除非某行明确绑定到 PMO/NASA 等公开来源，否则不把它当作权威 eclipse oracle。

地方日食已有多层测试：Mazatlan/New York 等固定 regression、Dallas NASA city-table 分钟级检查、observer-local kind filter、truepos 路径、以及 sunrise/sunset 可见偏食样例。这些测试能覆盖 classification、contacts、地方搜索过滤和可见性标记。若要声明任意地点地方接触时间达到秒级外部精度，还需要更多发布过的 local-circumstances table。

Swiss Ephemeris 对比可作为兼容性或模型差异分析，但不是 license-neutral public oracle，也不是公开测试套件的基准。

## 当前边界

- 首个 release 前 public API 尚未冻结，结构体字段和 flags 仍可能随验证结果调整。
- Eclipse 结果依赖 `NativeCalcContext` 中的星历路线、apparent options、time-scale policy、Delta T model、shadow model 和 Moon-radius model。比较结果时需要同时说明这些设置。
- Solar route curves 是数值 map/path products，下游通常还需要做投影、抽稀、分段和地图渲染处理。
- 地方日食 API 计算几何可见性和接触情况，不建模天气、云量、地形遮挡、观测设备或人眼视觉效果。
- 月食 contact、duration 和 magnitude 强依赖 shadow model 与 Moon-radius model。没有说明模型时，不应把不同来源的秒级或百分比差异直接当作误差。
- NASA HTML lunar catalog 不给单独 `P1/U1/U2/U3/U4/P4` contact times，只给 greatest time、magnitudes 和 phase durations；单独 contact oracle 需要使用明确发布 contact 的来源。
