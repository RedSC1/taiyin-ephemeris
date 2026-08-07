# 事件搜索

文档状态：当前说明
最后审阅：2026-07-01
主要头文件：`include/taiyin/runtime/event_search.h`

事件搜索是 Taiyin runtime 提供的底层数值求根能力。它只处理“某个角度条件在什么时候成立”，不直接生成历书、节气表、占星事件对象或可视化结果。

## 适用范围

当前公共 API 覆盖：

- 太阳黄经穿越；
- 月亮黄经穿越；
- 任意星体黄经穿越；
- 两个星体的相对黄经穿越；
- 月相，也就是 `Moon - Sun = phase`；
- 精确相位，也就是一组 aspect separation 的命中；
- 黄经速度为零的留点；
- 两个星体的三维天球角距最小值。

这些函数都使用调用方传入的 `NativeCalcContext`。因此 observer、时间模型、岁差章动模型、route rule、数据源选择和普通 `calc_position_*` 一致。事件搜索不会绕过 context，也不会自己换一套数据源。

## Flags

每个事件搜索入口都接收一个 `uint64_t flags`：

```text
低 32 位：native position flags
高 32 位：event-search option flags
```

低 32 位会传给普通位置计算路径。黄经、相对黄经、月相、exact-aspect 和 station 搜索内部会强制加入：

```text
TAIYIN_NATIVE_POSITION_SPEED
TAIYIN_NATIVE_POSITION_RADIANS
```

所以调用方不需要手动打开这两个 flag。三维角距和大距搜索内部会改用 XYZ + SPEED 取样。以下输出模式仍由搜索函数自行管理，调用方不能主动请求：

```text
TAIYIN_NATIVE_POSITION_XYZ
TAIYIN_NATIVE_POSITION_EQUATORIAL
```

当前唯一公开的 event-search option flag 是：

```text
TAIYIN_EVENT_SEARCH_REVERSE
```

它只用于 `search_solar_longitude_*` 和 `search_moon_longitude_*` 这种单事件 estimate search。区间搜索函数已经由 `start_jd` 和 `end_jd` 明确方向，不接受 reverse；如果传入高 32 位搜索选项，会返回 unsupported。

## 时间尺度

UT 入口使用普通 UT 位置计算路径：

```cpp
search_solar_longitude_ut(...)
search_body_aspect_crossings_ut(...)
```

TT 入口接收 TT，并使用 `NativeCalcContext` 中的 TDB model 在内部派生 TDB：

```cpp
search_solar_longitude_tt(...)
search_body_aspect_crossings_tt(...)
```

公共事件搜索接口不会同时要求调用方传 TT 和 TDB。调用方只需要按函数名传 UT 或 TT。

## 输出约定

单事件函数返回一个 JD：

```cpp
double jd = 0.0;
Status status = search_solar_longitude_ut(
    &context,
    target_longitude_rad,
    estimate_jd_ut,
    flags,
    &jd,
    &diagnostic);
```

区间函数使用调用方提供的输出数组：

```cpp
double events[16];
size_t event_count = 0;
Status status = search_body_longitude_crossings_ut(
    &context,
    body_id,
    target_longitude_rad,
    start_jd_ut,
    end_jd_ut,
    max_step_days,
    flags,
    events,
    16,
    &event_count,
    &diagnostic);
```

`event_count` 返回实际写入的事件数。容量不足时返回容量/内存错误；不会悄悄截断并返回成功。Station 和 exact-aspect 接口有可选的附加输出数组：

```text
station:      out_longitude_rad
exact-aspect: out_target_aspect_rad
```

这些附加数组可以传 `nullptr`。

## 黄经搜索

### 太阳和月亮单事件

```cpp
search_solar_longitude_ut(...)
search_solar_longitude_tt(...)
search_moon_longitude_ut(...)
search_moon_longitude_tt(...)
```

这些函数从 `estimate_jd` 附近找一个黄经穿越。默认向未来搜索；带 `TAIYIN_EVENT_SEARCH_REVERSE` 时向过去搜索。

它们适合“找下一个/上一个太阳黄经”或“找下一个/上一个月亮黄经”。它们不用于一般行星，因为会逆行的星体在留附近可能多次穿越同一黄经。

### 任意星体区间搜索

```cpp
search_body_longitude_crossings_ut(...)
search_body_longitude_crossings_tt(...)
search_body_longitude_crossings_auto_step_ut(...)
search_body_longitude_crossings_auto_step_tt(...)
```

这些函数在 `[start_jd, end_jd]` 内搜索所有满足：

```text
body longitude - target longitude = 0
```

的事件。显式 step 形式接收 `max_step_days`；auto-step 形式使用 `recommended_longitude_search_step_days(body_id)`。

算法会先按 step 采样，再对 sign-change bracket 用 safeguarded Newton/bisection 精修。Step 是搜索提示，不是数学证明；如果 step 过大，留点附近成对出现的 root 或切触 root 仍可能漏掉。

## 留点搜索

```cpp
search_body_longitude_stations_ut(...)
search_body_longitude_stations_tt(...)
search_body_longitude_stations_auto_step_ut(...)
search_body_longitude_stations_auto_step_tt(...)
```

这些函数搜索：

```text
d(longitude) / dt = 0
```

返回值只包含 station JD 和可选 station longitude。它不会标注“逆行开始”“逆行结束”“顺行留”。如果应用层需要这些标签，可以在返回 JD 前后各算一次 longitude speed。

### 参考事件：2003 年 8 月 28 日水星留点

下面的参考事件展示了留点搜索在水星转逆行边界附近的表现。逆行开始应理解为 station 之后黄经速度转负；北京时间下午仍是顺行但速度很小，真正过零在晚上。

| 事件 | 参考/结果 | UTC | 北京时间 | 相对 Taiyin 差异 | 备注 |
| --- | ---: | --- | --- | ---: | --- |
| 水星留点，转逆行 | Swiss Ephemeris 2.10.03 | 2003-08-28 13:41:22.175 | 2003-08-28 21:41:22.175 | +0.96 s | 秒级参考 |
| 水星留点，转逆行 | Taiyin OPM2 | 2003-08-28 13:41:23 | 2003-08-28 21:41:23 | 当前结果 | Taiyin 输出 |
| 水星留点范围 | JPL Horizons DE441 | 2003-08-28 13:40-13:42 | 2003-08-28 21:40-21:42 | 落在范围内 | `ObsEcLon` apparent ecliptic-of-date 分钟表峰值平台 |

同一天还有 2003 年火星冲日参考：

| 事件 | 参考/结果 | UTC | 北京时间 | 相对 Taiyin 差异 | 备注 |
| --- | ---: | --- | --- | ---: | --- |
| 火星冲日 | Swiss Ephemeris 2.10.03 | 2003-08-28 17:58:47.166 | 2003-08-29 01:58:47.166 | -0.006 s | Mars/Sun apparent 黄经差 180 度 |
| 火星冲日 | Taiyin OPM2 | 2003-08-28 17:58:47 | 2003-08-29 01:58:47 | 当前结果 | Taiyin 输出 |
| 火星冲日 | SEDS 参考 | 2003-08-28 17:58:49 | 2003-08-29 01:58:49 | -1.84 s | 公开参考值 |

## 相对黄经、月相和精确相位

相对黄经 crossing：

```cpp
search_body_aspect_crossings_ut(...)
search_body_aspect_crossings_tt(...)
search_body_aspect_crossings_auto_step_ut(...)
search_body_aspect_crossings_auto_step_tt(...)
```

它求解：

```text
body_a longitude - body_b longitude - aspect = 0
```

月相 wrapper：

```cpp
search_lunar_phase_crossings_ut(...)
search_lunar_phase_crossings_tt(...)
search_lunar_phase_crossings_default_step_ut(...)
search_lunar_phase_crossings_default_step_tt(...)
```

月相本质上就是 `Moon - Sun = phase`。`default_step` 版本使用内置月相 step，适合朔、上弦、望、下弦这类常见搜索。

精确相位：

```cpp
search_body_exact_aspects_ut(...)
search_body_exact_aspects_tt(...)
search_body_exact_aspects_auto_step_ut(...)
search_body_exact_aspects_auto_step_tt(...)
```

它接收一个或多个 aspect separation。0 度和 180 度只生成一个 target；其他 separation 会展开成两个方向。例如 120 度会展开为 120 度和 240 度。

精确相位搜索会做两类检测：

```text
sign-change crossing
relative-station tangent hit
```

这可以覆盖普通穿越，也能覆盖局部极值处刚好触碰相位的情况。返回的 `out_target_aspect_rad` 是命中的 target angle，不是相位名称。

## 水星/金星大距搜索

大距搜索当前面向 Mercury/Venus。它按调用方的 `NativeCalcContext`、route rule、输出 frame 和 native position flags 计算观测者看到的行星-太阳三维距角，并返回东大距或西大距。

内部求值会把 `center_id` 固定为 Sun。这不是把观察者改到太阳上，而是把 Sun 设为 ephemeris evaluation origin。大距几何仍然是：

```text
angle(observer -> body, observer -> Sun)
```

例如默认地心语义下，内部会让 apparent/native pipeline 取：

```text
body -> Sun
observer(Earth) -> Sun
Sun -> Sun
```

然后 pipeline 自己形成：

```text
observer -> body = body/Sun - observer/Sun
observer -> Sun  = Sun/Sun  - observer/Sun
```

这样做是为了让 OPM2 和内置半解析模型都走稳定路线。半解析模型的
Mercury/Venus/major planet 数据是 Sun-centered；如果直接沿用调用方传入的任意
`center_id`，某些 route 会缺失或被迫走额外 fallback。这里固定的是中间求值原点，
不改变最终 observer 语义。

当前求解策略是保守路线：

```text
scan samples -> 用 position/velocity/acceleration 求 elongation rate 和 rate derivative -> bracket rate=0 -> guarded Newton/bisection refine
```

大距 refine 会使用 `calc_state_*()` 暴露的 acceleration 计算 Newton candidate。这里不区分 acceleration 是原生导数还是有限差分导数；只要 candidate 有限、落在现有 bracket 内，并且能继续收缩 bracket，就接受，否则回到二分。

因此 acceleration 可以加速收敛，但不是正确性的唯一依赖；bracket 和二分兜底仍然保留，避免 Newton step 跳出搜索窗口或跨过 root。

这个入口只返回真正 bracket 到的 `elongation rate = 0` 大距事件。如果调用方区间内没有东大距/西大距驻点，会返回 `TAIYIN_EVENT_ERROR_NOT_FOUND`，不会把普通区间内的最大距角伪装成大距事件。

下表给出当前大距搜索与 JPL Horizons apparent vector 派生参考值的对比。JPL 参考值由行星和太阳的地心 apparent vectors 计算三维距角，并在 Taiyin 结果附近用 7 个采样点做二次拟合得到。

| 事件 | JPL JD UT | Taiyin JD UT | Taiyin - JPL 时间差 | JPL 大距 | Taiyin - JPL 角差 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Mercury eastern, 2024-03 | 2460394.440334700 | 2460394.440334365 | -0.029 s | 18.701601185 deg | +0.0025" |
| Mercury western, 2024-05 | 2460440.395385454 | 2460440.395385969 | +0.044 s | 26.365604784 deg | +0.0021" |
| Venus eastern, 2023-06 | 2460099.958895525 | 2460099.958895225 | -0.026 s | 45.399231306 deg | +0.0020" |
| Venus western, 2023-10 | 2460241.468374033 | 2460241.468373870 | -0.014 s | 46.413181097 deg | +0.0017" |

公开回归测试还会用独立的内置半解析路线运行这些搜索，检查事件类型一致且结果与
OPM2 保持接近。该模型相对 DE441 的 held-out 精度见
[内置半解析星历](semi_analytic_ephemeris.md)。

## 最小天球角距

最小角距入口：

```cpp
search_minimum_angular_separation_ut(...)
search_minimum_angular_separation_tt(...)
```

它搜索两个星体方向向量之间的三维夹角最小值。这个量不同于 `search_body_aspect_*()` 的黄经差：即使两个天体黄经相同，纬度不同也可能有明显天球角距。因此它适合给 appulse、掩星、凌日和“最近角距”类功能做底层 primitive；事件名称、是否算“合”、orb 规则和展示语义应由上层扩展决定。

当前 solver 与大距使用同一组角距运动学纯函数：先用 `separation_rate` 找到局部极小值 bracket，再用 `separation_acceleration` 构造 guarded Newton candidate；candidate 不有限或跑出 bracket 时继续二分。如果没有找到 sign-change bracket，则在最佳采样点附近用角距值做保守最小化 fallback。

下表给出 2024-04-08 日食附近的 Sun-Moon 最小角距参考校验。参考值不是 JPL 直接给出的事件表，而是由 JPL Horizons apparent vectors 派生：

```text
Moon(301), Sun(10), center=Earth geocenter
EPHEM_TYPE=VECTORS
TIME_TYPE=UT, VEC_CORR=LT+S
TLIST 覆盖 2024-04-08 18:15:00..18:20:00 UTC，每 10 秒一个采样点
分别取 Moon 和 Sun 的 apparent vector
本地计算两个向量的三维夹角
取最小采样点前后三个点，共 7 点，做二次拟合得到参考最小值
```

| 事件 | 参考/结果 | UTC | JD UT | 最小角距 | 相对 Taiyin 差异 |
| --- | ---: | --- | ---: | ---: | ---: |
| Sun-Moon 最小天球角距 | JPL Horizons DE441 vector | 2024-04-08 18:17:20.494 | 2460409.262042756 | 0.3476802575 deg | +0.013 s, -0.0000074" |
| Sun-Moon 最小天球角距 | Taiyin OPM2 | 2024-04-08 18:17:20.507 | 2460409.262042910 | 0.3476802555 deg | 当前结果 |

## 水星/金星凌日搜索

凌日入口：

```cpp
search_next_solar_transit_ut(...)
search_next_local_solar_transit_ut(...)
compute_local_solar_transit_ut(...)
```

`search_next_solar_transit_ut()` 从给定 UT 起点向前搜索下一次 Mercury/Venus 凌日，返回全球地心视角下的最大凌日时刻、最小天球角距、太阳和行星视半径，以及 T1/T2/T3/T4 接触时刻。传入 `TAIYIN_EVENT_SEARCH_REVERSE` 时向过去搜索上一次事件。这个入口会拒绝 topocentric native flag 和已经设置为地表观测者的 `NativeCalcContext`，以保证返回值始终是全球地心口径。

`search_next_local_solar_transit_ut()` 不要求地心结果已经确认成凌日。它从给定起点按 Mercury/Venus 内合周期逐个检查候选，再直接解指定经纬度的 topocentric 最小距和接触时刻；这样擦边事件不会因为地心口径先被过滤掉。返回值同时包含地心候选信息、本地 topocentric T1/T2/最大/T3/T4、这些时刻的太阳高度/方位、可见性 bit，以及凌日期间的日出/日落时刻。`compute_local_solar_transit_ut()` 用于已经有 `SolarTransitSearchResult` 或候选结果的场景：它不会重新搜索候选，只补本地 topocentric contact 和 visibility。local 入口默认使用带折射的 apparent altitude；调用方需要在 context 中设置 atmosphere。需要 true-altitude 判断时传入 `TAIYIN_EVENT_SEARCH_NO_REFRACTION`。

当前筛选流程是多层漏斗：

```text
k candidate -> empirical conjunction seed -> inferior-distance filter -> node/latitude gate -> apparent minimum separation -> contact root solve
```

第一层先用 Mercury/Venus 内合 `k` 定位候选。`k` seed 使用 DE441 拟合得到的经验修正项：平均内合周期加慢变多项式和若干泊松项（形如 `t^n sin(2πfk)` / `t^n cos(2πfk)`）；这个 seed 只用于缩小搜索窗口，不直接决定事件时刻。当前经验窗口为 Mercury `±3 day`、Venus `±1 day`。如果经验 seed 小窗口没有找到合适内合，搜索会退回平均 `k` 的保守宽窗口。第二层在候选附近用内部 canonical 黄道 frame 精修黄经合日，并用 observer-body 距离小于 observer-Sun 距离来排除外合。第三层是和日月食 `F` 粗筛对应的 node/latitude gate：内合附近行星黄纬若超过保守阈值 `2°`，则认为这个候选不可能凌日并跳过。

凌日搜索不会把调用者的 `NativeCalcContext::apparent_options.output_frame_id` 当成候选搜索语义。候选内合和 latitude gate 内部使用 true ecliptic-of-date；如果传入 `TAIYIN_NATIVE_POSITION_NONUT`，则使用 mean ecliptic-of-date。最终是否成凌日仍由三维最小角距和接触求根确认。

`tools/validate_solar_transit_seed_de441.py` 是非 CI 的 DE441 验证工具，用来检查 `jd -> k` bootstrap、经验 seed 覆盖范围和平均 `k` 宽窗口兜底。默认采样下，Mercury 经验 seed 相对 DE441 内合根的最坏误差约 `2.38 day`，Venus 约 `0.33 day`，均落在生产小窗口内。工具会按 BSP 实际覆盖范围选择可验证的 `k`，避免把星历文件边界误判成搜索算法错误。

这个 latitude gate 是必要条件筛选，不是事件确认。也就是说 `abs(latitude) > 2°` 会被当作 definitely no transit；`abs(latitude) <= 2°` 只表示候选仍可能凌日。最终是否成凌日仍由 Sun-body 三维最小角距和接触求根确认。这个阈值通过 DE441 hard-scan 多点回归防漏：测试会在 DE441 古代、中段、近代和未来边界附近，用不依赖 `k` 的三维最小角距硬扫结果对比 `k` 搜索结果。

最后一层只在通过筛选的候选附近搜索 Sun-body 三维最小角距；再解 `separation(t) - (solar_radius(t) ± body_radius(t)) = 0` 得到接触时刻。接触求根会在每个 JD 重新计算太阳和行星的 apparent radius，不把最大凌日时的半径固定套到全程。

第一版只支持 Mercury/Venus。外行星不会从地球视角凌日太阳，调用时会返回 invalid argument。local 版本不是一般掩星框架；月掩星、行星掩恒星、卫星凌日等事件会在后续 occultation/appulse API 中实现。

当前回归测试覆盖 2006-11-08 水星凌日、2019-11-11 水星凌日和 2004-06-08 金星凌日。水星参考值来自 NASA Eclipse Web Site 的 *Seven Century Catalog of Mercury Transits: 1601 CE to 2300 CE*，金星参考值来自 *Six Millennium Catalog of Venus Transits: 2000 BCE to 4000 CE*。这些表给出地心 UT 接触时刻和最大凌日时刻，时间精度为分钟，最小中心距精度为 0.1″。因此这里把 NASA 数据作为外部权威 sanity check，而不是秒级 contact oracle。

参考链接：

- [NASA Mercury Transit Catalog](https://eclipse.gsfc.nasa.gov/transit/catalog/MercuryCatalog.html)
- [NASA Venus Transit Catalog](https://eclipse.gsfc.nasa.gov/transit/catalog/VenusCatalog.html)

| 事件 | 项目 | NASA 参考 | Taiyin OPM2 | 说明 |
| --- | --- | ---: | ---: | --- |
| Mercury 2006 | Contact I | 2006-11-08 19:12 UT | 19:12:04.2 UT | 落在 NASA 分钟级参考内 |
| Mercury 2006 | Contact II | 2006-11-08 19:14 UT | 19:13:57.1 UT | 落在 NASA 分钟级参考内 |
| Mercury 2006 | Greatest transit | 2006-11-08 21:41 UT | 21:41:04.2 UT | 落在 NASA 分钟级参考内 |
| Mercury 2006 | Contact III | 2006-11-09 00:08 UT | 00:08:16.1 UT | 落在 NASA 分钟级参考内 |
| Mercury 2006 | Contact IV | 2006-11-09 00:10 UT | 00:10:09.0 UT | 落在 NASA 分钟级参考内 |
| Mercury 2006 | Minimum center separation | 422.9″ | 422.9144″ | 与 NASA 0.1″ 参考一致 |
| Mercury 2019 | Contact I | 2019-11-11 12:35 UT | 12:35:26.8 UT | 落在 NASA 分钟级参考内 |
| Mercury 2019 | Contact II | 2019-11-11 12:37 UT | 12:37:08.2 UT | 落在 NASA 分钟级参考内 |
| Mercury 2019 | Greatest transit | 2019-11-11 15:20 UT | 15:19:48.0 UT | 落在 NASA 分钟级参考内 |
| Mercury 2019 | Contact III | 2019-11-11 18:02 UT | 18:02:32.9 UT | 落在 NASA 分钟级参考内 |
| Mercury 2019 | Contact IV | 2019-11-11 18:04 UT | 18:04:14.3 UT | 落在 NASA 分钟级参考内 |
| Mercury 2019 | Minimum center separation | 75.9″ | 75.9351″ | 与 NASA 0.1″ 参考一致 |
| Venus 2004 | Contact I | 2004-06-08 05:13 UT | 05:13:33.8 UT | 落在 NASA 分钟级参考内 |
| Venus 2004 | Contact II | 2004-06-08 05:33 UT | 05:33:05.0 UT | 落在 NASA 分钟级参考内 |
| Venus 2004 | Greatest transit | 2004-06-08 08:20 UT | 08:19:44.2 UT | 落在 NASA 分钟级参考内 |
| Venus 2004 | Contact III | 2004-06-08 11:07 UT | 11:06:37.9 UT | 落在 NASA 分钟级参考内 |
| Venus 2004 | Contact IV | 2004-06-08 11:26 UT | 11:25:54.7 UT | 落在 NASA 分钟级参考内 |
| Venus 2004 | Minimum center separation | 626.9″ | 626.8902″ | 与 NASA 0.1″ 参考一致 |

2019 水星凌日还用 JPL Horizons apparent vector 派生值做秒级回归测试。它用于确认 Taiyin 与 JPL-derived geometry 在亚秒和亚毫角秒量级内一致；下表中的 `Taiyin OPM2 与 JPL 差异` 受 UT1/UTC、Delta T、apparent-vector 定义、太阳/行星半径约定和 OPM2/DE441 数据差异共同影响，不应解释成绝对物理误差。生成方式：

```text
Mercury(199), Sun(10), center=Earth geocenter
EPHEM_TYPE=VECTORS
TIME_TYPE=UT, VEC_CORR=LT+S
TLIST 覆盖 2019-11-11 12:30:00..18:10:00 UTC，约 10 秒采样
分别取 Mercury 和 Sun 的 apparent vector
用 Taiyin 当前太阳/水星视半径约定解 contact residual
最大凌日由最小角距附近 7 点二次拟合得到
```

| 项目 | JPL vector-derived oracle | Taiyin OPM2 一致性 |
| --- | ---: | ---: |
| Contact I | 2019-11-11 12:35:26.985 UT | 差异小于 1 s |
| Contact II | 2019-11-11 12:37:08.361 UT | 差异小于 1 s |
| Greatest transit | 2019-11-11 15:19:48.114 UT | 差异小于 1 s |
| Contact III | 2019-11-11 18:02:33.101 UT | 差异小于 1 s |
| Contact IV | 2019-11-11 18:04:14.493 UT | 差异小于 1 s |
| Minimum center separation | 75.9351759″ | 差异小于 0.01″ |

## Step 选择

显式 step 接口最可控。调用方应该根据事件类型、时间窗口和目标星体运动速度设置 `max_step_days`。

便捷函数提供内置建议：

```cpp
recommended_longitude_search_step_days(body_id)
recommended_aspect_search_step_days(body_a_id, body_b_id)
```

当前内置提示偏保守：

```text
Moon       0.25 day
Mercury   0.5 day
Venus     1.0 day
Mars      1.0 day
Sun       2.0 days
Jupiter   2.0 days
Saturn    2.0 days
Uranus    3.0 days
Neptune   3.0 days
Pluto     3.0 days
unknown   0.5 day
```

`recommended_aspect_search_step_days()` 会取两个星体建议 step 中较小的一个。自定义星体如果运动规律特殊，建议使用显式 step 接口。

## 错误行为

事件搜索通过 `Status` 和可选 `EphemerisEvalDiagnostic` 报告失败。

常见情况：

- 空指针、无效时间窗口、无效 step、无效 capacity 返回参数错误；
- 请求 XYZ 或 equatorial 输出返回 unsupported；
- 区间搜索传入 event-search option flags 返回 unsupported；
- 请求窗口内没有 root 返回 `TAIYIN_EVENT_ERROR_NOT_FOUND`；
- 数据覆盖缺口、组合星体缺组件或没有 route 会被归一化为 `TAIYIN_EVENT_ERROR_NOT_FOUND`，diagnostic 保留底层失败信息；
- 输出数组容量不足返回容量/内存错误。

搜索函数不会静默切换坐标模式、数据源或 route rule。如果所选 context 和 flags 无法在请求窗口内计算目标 body，事件搜索会返回失败状态。

## 测试覆盖

公开 `test_event_search` 覆盖：

- 太阳和月亮黄经 crossing；
- UT 和 TT 入口；
- reverse Sun search；
- 太阳黄经 oracle；
- bounded longitude search 的 no-event 和 capacity 行为；
- auto-step longitude/aspect wrapper；
- 月相 wrapper 和通用 Moon-Sun aspect 一致性；
- exact-aspect 方向展开和切触 exact-aspect 检测；
- 最小天球角距 Sun/Moon sanity 和 OPM2/半解析路线对照；
- synthetic 和真实黄经留；
- route-rule fixed/no-fallback 行为；
- data-boundary 和 component coverage-gap 终止；
- 拒绝 reverse bounded search；
- 拒绝 XYZ/equatorial 输出模式。

兼容性对照测试可以放在私有测试或外部仓库中维护。它们对开发兼容行为有用，但不是公开 runtime contract 的一部分。

## 扩展边界

`event_search.h` 提供可组合的基础事件搜索能力。历法、可见性和占星扩展会在这些基础结果之上添加名称、分类、展示字段和领域规则。

已经可以用当前基础搜索能力组合出来、后续会在上层模块提供命名入口的例子：

- 节气：搜索太阳黄经 target，然后在历法模块附加节气名；
- 朔望弦：搜索 `Moon - Sun` phase，然后在历法模块附加 `朔`、`望`、`上弦`、`下弦`；
- 逆行事件：搜索 station，然后由上层用前后速度标注顺行留、逆行留、逆行开始或逆行结束；
- 命名相位：搜索 exact-aspect，然后由占星扩展应用 orb、name、display、入相/出相规则。

后续更适合放在天文/历法扩展层的方向：

- 多日升落、过中天、昼夜长短和 twilight 表格；
- 太阳、月亮、行星和恒星的可见性窗口；
- 偕日升、偕日降、晨见、夕见、acronychal/cosmical 等传统可见性事件；
- 行星现象量，例如相位角、距角、照明比例、视直径和亮度；
- 水星、金星东大距/西大距；
- 月掩批量星表、行星互掩、行星合月的最小角距/appulse；
- 水星/金星凌日；
- 更面向历法展示的地方日月食可见性表、观测摘要和批量查询。

后续更适合放在占星扩展层的方向：

- 星座入宫、宫位入宫、ASC/MC 和宫制相关事件；
- 行星回归、transit-to-natal、synastry/composite 事件；
- 带 orb、入相/出相和解释语义的命名相位；
- combust、under beams、cazimi 等太阳距离规则；
- 庙旺弱陷、月宿、空亡月、择日规则；
- 阿拉伯点、midpoint、Lilith 变体、虚拟点和其他 synthetic chart points。

这些扩展会复用 runtime 的黄经、相对黄经、station、visibility、eclipse 和未来 occultation 能力，并在各自模块中提供更贴近使用场景的 API。

## 优先实现功能

后续功能优先参考 Swiss Ephemeris 已经提供的公开 API 能力范围。这里的“参考”指功能覆盖和用户工作流，不表示 Taiyin 主库会复刻 Swiss 的 C ABI、flag 细节或错误字符串。

| 优先级 | 功能方向 | 参照的 SwissEph API 能力 | Taiyin 计划形态 |
|---:|---|---|---|
| P0 | 现有事件搜索收口 | `swe_solcross*`, `swe_mooncross*`, `swe_helio_cross*` | 保持太阳/月亮黄经、通用黄经、相对黄经、月相、station 和 exact-aspect 的底层搜索稳定，补足参考测试和边界测试。 |
| P0 | 日月食能力收口 | `swe_sol_eclipse_*`, `swe_lun_eclipse_*` | 现有日食、月食、地方日食、路径/接触和地方月食可见性继续补文档、参考测试、边界测试和性能测试。 |
| P1 | 升落、过中天和可见性表 | `swe_rise_trans`, `swe_rise_trans_true_hor`, `swe_azalt`, `swe_azalt_rev` | 已有太阳、月亮、行星升落/过中天和 custom horizon 基础能力；下一步补更多 custom-horizon oracle、必要时抽通用 body visibility、多日表和更多高纬边界。 |
| P1 | 恒星位置和恒星可见性 | `swe_fixstar*`, `swe_fixstar*_ut`, `swe_fixstar*_mag`, `swe_rise_trans` with star | 在现有恒星 catalog/API 基础上补固定星升落、过中天、circumpolar/no-event、星等输出和参考测试。 |
| P1 | 行星现象量 | `swe_pheno`, `swe_pheno_ut` | 已有相位角、距角、照明比例、视直径、经验星等和 Moon 水平视差 API；下一步补更多 oracle，并让大距、晨昏见等事件复用这些结果。 |
| P2 | 水星/金星大距和相关现象 | SwissEph 用户通常由 `swe_pheno*` 加搜索得到 | 提供东大距/西大距搜索和测试，先做 Mercury/Venus；当前用 guarded Newton/bisection refine，再考虑把同样策略推广到一般化的距角极值。 |
| P2 | 掩星、凌日和最小角距 | `swe_lun_occult_*`, solar transit/occultation workflows | 已有 guarded Newton/bisection minimum angular separation primitive、Mercury/Venus 凌日、第一版月掩恒星和月掩太阳系 body next-search，并返回 maximum/begin/end/contact 和基础本地可见性摘要；下一步强化指定目标月掩的 seed/refine、`where` 风格可见区域和更多 oracle。批量星表扫描不是 SwissEph 单目标 API 对齐项，后置到 catalog/almanac 层。 |
| P2 | 节点、拱点和轨道量 | `swe_nod_aps*`, `swe_get_orbital_elements`, `swe_orbit_max_min_true_distance` | 通用密切轨道量、物理节点/拱点搜索，以及可选 extension 的平均/真月球交点、Delaunay 平均远地点和瞬时远地点已经可用。自然/插值远地点和流派虚点仍留在 extension 后续工作。 |
| P3 | 偕日升降和传统可见性 | `swe_heliacal_ut`, `swe_heliacal_pheno_ut`, `swe_vis_limit_mag`, `swe_heliacal_angle`, `swe_topo_arcus_visionis` | 基于点源的晨见/晨没/昏见/昏没搜索已提供 Belokrylov (2011) 与 Schaefer (1993) profile。下一步是城市光害、月牙专用初见、传统事件别名和外部行为 oracle。 |
| P3 | 宫位和占星事件扩展 | `swe_houses*`, `swe_house_pos`, `swe_gauquelin_sector` | 放在占星扩展中实现 ASC/MC、宫制、星座/宫位入宫、回归、transit-to-natal 和 Gauquelin sector。 |

优先顺序的核心原则是先补基础观测量，再补事件包装：先有可靠的位置、地平坐标、现象量、角距离和可见性判断，再实现大距、掩星、凌日、偕日升降和占星事件。
