# 天体现象量

文档状态：当前说明
最后审阅：2026-07-02
主要头文件：`include/taiyin/runtime/phenomena.h`

Phenomena API 计算天体的现象量，类似 SwissEph `swe_pheno_ut()` 的常用输出，但不使用 `attr[20]` 这种魔法数组。

当前入口：

```cpp
Status calc_body_phenomena_ut(
    const NativeCalcContext* context,
    int body_id,
    double jd_ut,
    uint64_t flags,
    BodyPhenomena* out,
    EphemerisEvalDiagnostic* diagnostic);

Status calc_body_phenomena_tt(
    const NativeCalcContext* context,
    int body_id,
    double jd_tt,
    uint64_t flags,
    BodyPhenomena* out,
    EphemerisEvalDiagnostic* diagnostic);
```

`flags` 的低 32 位是普通 native position flags。当前没有 phenomena 专用高位 flags；如果传入高 32 位，会返回 unsupported。API 内部会强制使用 XYZ 输出计算角距离。

## 最小调用示例

这个 API 不需要额外的 phenomena 专用配置。调用前先初始化 runtime，按普通位置计算一样准备 `NativeCalcContext`，然后传入 body id、JD 和 native position flags：

```cpp
#include "taiyin/body_id.h"
#include "taiyin/runtime/phenomena.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

taiyin::runtime::EphemerisRuntimeConfig config;
config.data_root = "data";
config.load_packaged_data = true;
taiyin::runtime::initialize_global_ephemeris_runtime(config);

taiyin::runtime::NativeCalcContext context;
taiyin::runtime::native_context_set_geocentric_observer(
    &context,
    taiyin::TAIYIN_BODY_EARTH,
    taiyin::TAIYIN_BODY_EARTH);
taiyin::runtime::native_context_use_solar_deflector(&context);
context.apparent_options.flags =
    taiyin::TAIYIN_APPARENT_LIGHT_TIME
    | taiyin::TAIYIN_APPARENT_ABERRATION
    | taiyin::TAIYIN_APPARENT_DEFLECTION;

const double jd_ut = taiyin::julian_day({2024, 4, 8, 18, 0, 0.0});
taiyin::runtime::BodyPhenomena moon;
taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
const taiyin::Status status = taiyin::runtime::calc_body_phenomena_ut(
    &context,
    taiyin::TAIYIN_BODY_MOON,
    jd_ut,
    0u,
    &moon,
    &diagnostic);

if (status == taiyin::TAIYIN_STATUS_OK) {
    // moon.phase_angle_rad
    // moon.illuminated_fraction
    // moon.solar_elongation_rad
    // moon.apparent_diameter_rad
    // moon.apparent_magnitude
    // moon.horizontal_parallax_rad
}
```

如果只想按当前 `context` 的 apparent 配置计算，可以把 `flags` 传 `0u`。如果要和某个位置计算调用保持一致，就传同一组 native position flags。不要把 `TAIYIN_APPARENT_*` bits 传给这里的 `flags` 参数；apparent correction 开关应设置在 `context.apparent_options.flags` 上。

如果调用方明确接受“用主行星质心近似本体”，可以传 `TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX`。启用后，位置计算会先按请求的本体 ID 正常尝试；如果当前 route 没有本体路线，或者遇到覆盖范围/组合组件缺口，Mars、Jupiter、Saturn、Uranus、Neptune、Pluto 会改试对应 barycenter。Mercury/Venus 在内置规则里本来就 alias 到 barycenter，因为当前打包数据使用这个约定身份。Earth/Moon 不走这个近似 flag，它们有单独的 EMB/Moon 组合路线。近似成功时，`diagnostic.target_id` 仍保留调用方请求的 body id，`diagnostic.component_target_id` 记录实际使用的 barycenter。

这个近似路径是 strict-first：如果某个 route 结构上缺少 body/COB 数据，每次调用都可能先付出一次本体尝试失败的成本，然后才 retry barycenter。这样可以保持语义简单，也避免未来补上真实本体数据后被静默跳过；如果高频搜索后续真的把这里变成热点，再增加 route-level 优化。

## 输出字段

`BodyPhenomena` 当前包含：

| 字段 | 含义 |
| --- | --- |
| `phase_angle_rad` | 相位角，也就是 observer-body-Sun 夹角。 |
| `illuminated_fraction` | 被照亮比例，按 `(1 + cos(phase_angle)) / 2` 计算。 |
| `solar_elongation_rad` | 距日角，也就是 observer 看出去的 body 与 Sun 夹角。 |
| `apparent_diameter_rad` | 视直径，按内置约定半径表和 observer-body 距离计算。 |
| `apparent_magnitude` | 视星等，按经验视星等模型计算。 |
| `horizontal_parallax_rad` | 当前只对 Moon 返回地心水平视差；其他天体返回 `NaN`。 |

角度单位为弧度，`illuminated_fraction` 为 `0..1` 的无量纲比例，`apparent_magnitude` 是常规天文学视星等。

`illuminated_fraction` 是理想球体几何量：它表示可见圆盘按几何面积有多少被太阳照亮，不表示亮度比例。Moon 半月附近虽然几何点亮比例接近 `0.5`，但真实亮度不会是满月的一半，因为月面反照率、opposition surge、libration、波段响应和地形散射都会影响光度。

## 当前边界

- `apparent_magnitude` 是经验光度模型，不是严格辐射传输或表面物理模型。
- 当前星等模型覆盖 Sun、Moon、Mercury、Venus、Mars、Jupiter、Saturn、Uranus、Neptune、Pluto。Mercury、Venus、Mars、Jupiter、Saturn、Uranus、Neptune 采用 Mallama & Hilton (2018) 的经验公式和分段规则；Sun 使用平均 V-band 太阳视星等按距离缩放；Moon 使用 Astronomical Almanac 风格的 before/after full Moon 相位模型，并在极细月牙区间保留连续性 fallback；Pluto 当前使用 H-G phase function，`H=-0.55`，`G=0.15`。细节见 [`phenomena_magnitude_models.md`](phenomena_magnitude_models.md)。
- Moon 星等当前是历书级经验模型，定位类似 SwissEph `swe_pheno*()` 的常用现象量输出；它不是 ROLO lunar irradiance model，也不会输出观测波段、月面 libration 或地形相关的高精度月面光度。
- Mars 星等公式需要 Mars 本体位置和 IAU 自转元素推导的 sub-observer/sub-solar longitude；如果当前 route rule 只能提供 Mars barycenter 而不能提供 Mars body，API 默认会按普通位置计算规则返回无路由错误。只有显式传 `TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX` 时，才会改用较宽松的 barycenter 近似。
- Saturn 当前返回默认带环视星等。论文中的带环公式只覆盖地心可见的小相位角范围；超出这个范围时不会伪造 globe-only 结果。
- Uranus 星等包含 planetographic sub-latitude 项；测试会加载 Uranus COB slice，确保本体路由存在。
- 支持的半径表覆盖 Sun、Moon、Mercury、Venus、Mars、Jupiter、Saturn、Uranus、Neptune、Pluto。
- Barycenter ID 没有物理半径，当前返回 unsupported。
- `horizontal_parallax_rad` 第一版只实现 Moon 的地心水平视差；SwissEph `SEFLG_TOPOCTR` 下的 topocentric parallax 语义暂未实现。
- 几何量使用当前 `NativeCalcContext` 和 native position flags，因此 route rule、observer、apparent correction、frame model 和普通位置计算保持一致。

## 与 SwissEph 的关系

SwissEph `swe_pheno_ut()` 返回：

```text
attr[0] phase angle
attr[1] illuminated fraction
attr[2] elongation
attr[3] apparent diameter
attr[4] apparent magnitude
attr[5] Moon horizontal parallax
```

Taiyin 当前对齐 `attr[0..4]` 的常用含义，并对 Moon 覆盖 `attr[5]` 的地心水平视差。

测试使用 SwissEph 生成的固定 oracle，用于确认输出语义和常用参考实现处在同一量级。由于 Taiyin 使用自己的 OPM2/runtime apparent 链路，默认 apparent 语义不会和 SwissEph 逐位一致；测试容差按参考对齐设置，而不是 ABI/compat 级别的严格对齐。
