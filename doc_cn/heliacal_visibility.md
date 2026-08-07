# 晨昏初见可见性

`calc_body_heliacal_visibility_ut()` 与
`calc_star_heliacal_visibility_ut()` 判断某个天体在给定地点、给定 UT 时刻是否
满足晨昏初见的可见性条件。结果包含目标与太阳的地平坐标、大气消光、极限星等和
可见裕量。`visibility_margin_magnitude > 0` 表示目标比当前 profile 的极限星等更亮；
只有能反解太阳高度的 profile 才会填充 `required_sun_altitude_rad` 与
`solar_depression_margin_rad`。

地点由 `NativeCalcContext` 提供。此计算使用未折射的目标/太阳真高度：晨昏
profile 自己建模大气消光，不能把普通升落计算的折射开关误当作消光模型。

所有公开晨昏 API 都接收 `uint64_t flags`。低 32 位支持
`TAIYIN_NATIVE_POSITION_TRUEPOS`、`TAIYIN_NATIVE_POSITION_ASTROMETRIC`、
`TAIYIN_NATIVE_POSITION_NO_ABERR` 和 `TAIYIN_NATIVE_POSITION_NO_GDEFL`；
输出形状位会被拒绝，因为该 API 固定计算地表地平坐标。高 32 位属于晨昏策略：
`TAIYIN_HELIACAL_VISIBILITY_INCLUDE_MOONLIGHT` 开启月光背景，
`TAIYIN_HELIACAL_VISIBILITY_STRICT_METEOROLOGY` 禁止自动补全气象条件。

## 内置 Profile

`NativeCalcContext` 默认使用 `dispatch::HELIACAL_VISIBILITY_SCHAEFER_1993`。
它是本仓库 SwissEph 外部行为 oracle 使用的物理点源 profile。若未显式传入
`HeliacalVisibilityConditions::extinction_mag_per_airmass`，Taiyin 会按 Schaefer 2000
的分量模型组合瑞利散射、水汽、臭氧和气溶胶消光。只有在 context 的大气策略中设置
`TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK` 才允许标准晴空条件：观测高度处的
ISA 气压/温度和 40% 相对湿度；这是模型约定，不代表实时当地天气。

要使用当地观测条件，可直接传入消光系数；也可通过
`native_context_set_atmosphere()` 提供气压、温度、相对湿度，并传入
`native_context_set_meteorological_range_km()` 提供能见度距离。设置
`TAIYIN_HELIACAL_VISIBILITY_STRICT_METEOROLOGY` 后，除非显式提供消光系数，否则这四项
必须齐全；缺失时返回 `TAIYIN_ERROR_INVALID_ARGUMENT`，不会回退到标准晴空值。

`dispatch::HELIACAL_VISIBILITY_BELOKRYLOV_2011` 是可显式选择的另一条观测拟合关系，
采用 Belokrylov、Belokrylov、Nickiforov 在 2011 年给出的暮光恒星可见性模型：

- 用明亮/较暗目标的分段关系给出目标可见所需的太阳低角；
- 用论文中的近地平空气质量关系修正目标消光；
- 当目标距太阳小于 58 度时，加入论文中的近太阳暮光背景修正。

这是晴空裸眼暮光观测的校准模型。若未覆盖，默认采用论文参考值
`0.25 mag/airmass`；有当地实测值时，传入
`HeliacalVisibilityConditions::extinction_mag_per_airmass`。

首版尚未包含月光、城市光害、云层、颜色响应、光学器材和观察者个人视敏度。
所以它是明确的可见性判据，不是“人一定能看到”的保证。

body 入口会明确拒绝 Moon。月球初见/隐没需要月牙宽度和月面背景几何等专用模型，
不能混用这里的点源判据。

论文原文：[Model of the Stellar Visibility During Twilight (2011)](https://www.astro.bas.bg/AIJ/issues/n16/08_MNikifor2.pdf)。
近地平消光的处理思路也与 [Schaefer (1993)](https://ntrs.nasa.gov/citations/19950037102)
对空气质量分量的讨论一致，但两套 profile 不会被静默混合。

`dispatch::HELIACAL_VISIBILITY_SCHAEFER_1993` 是默认的物理点源
profile。它按 Schaefer (1993) 合成日间、暮光和夜空背景，再用 Hecht (1947)
的点源视觉阈值比较经消光后的目标照度。结果会额外给出
`sky_brightness_nanolambert`、`threshold_illuminance_footcandles`、
`target_illuminance_footcandles`，以及与 profile 无关的
`visibility_margin_magnitude`。

这个首版 Schaefer profile 假定晴空、裸眼、暗夜地点；城市光害暂不混入默认模型。
月光是显式开关：在函数 `flags` 设置
`TAIYIN_HELIACAL_VISIBILITY_INCLUDE_MOONLIGHT` 后，会使用
Krisciunas-Schaefer (1991) 的 V-band 散射月光模型，输入包括月相角、目标-月亮角距
和独立的散射空气质量，并通过 `moonlight_brightness_nanolambert` 返回它的背景贡献。
原论文报告的预测误差约为 8% 到 23%，所以它是有物理依据的估计，不是现场光度计。
论文 Table 2 的 V-band 数值已作为回归测试保留。原文见
[A Model of the Brightness of Moonlight (1991)](https://articles.adsabs.harvard.edu/pdf/1991PASP..103.1033K)。
未提供实测天空背景时，对没有声明月光组件的 profile 请求模型月光会返回
`TAIYIN_ERROR_UNSUPPORTED`，不会静默忽略。

调用者如果有目标方向的实测天空背景，可传
`HeliacalVisibilityConditions::sky_brightness_nanolambert` 覆盖模型背景；该值会覆盖
日间/暮光/夜空/月光的合成背景。默认暗夜项为 `180 nL`，也可用
`night_sky_brightness_nanolambert` 覆盖。

## 模型注册

profile 使用和岁差、章动、折射一样的全局 `dispatch` 注册表：

```cpp
taiyin::runtime::native_context_set_heliacal_visibility_model(
    &context,
    taiyin::dispatch::HELIACAL_VISIBILITY_SCHAEFER_1993);
```

`HeliacalVisibilityResult` 会返回 profile ID 与消光/暮光/视阈组件 ID。自定义
profile 使用 `dispatch::add_heliacal_visibility_model()` 注册，ID 必须不小于
`HELIACAL_VISIBILITY_CUSTOM_START`；回调接收
`HeliacalVisibilityModelInput`，填充 `HeliacalVisibilityResult`。

两个内置 profile 都会在运行时初始化时注册。Schaefer profile 会标出
Schaefer (2000) 消光、Schaefer (1993) 暮光组件、Krisciunas-Schaefer (1991) 月光组件和
Hecht (1947) 的点源视阈组件；它并不替代基于观测拟合的 Belokrylov 判据。

## 晨昏事件搜索

`search_next_body_heliacal_visibility_ut()` 与
`search_next_star_heliacal_visibility_ut()` 用来找下一次晨见、晨没、昏见或昏没。
调用者必须显式给出 `max_search_days`，不会暗中做跨数年的搜索。

它会对每个 UT 日找出太阳中心从 `-18 deg` 到 `-0.85 deg` 的对应无折射晨昏窗口，
在窗口内采样共享的可见裕量；只有靠近阈值、或相邻两天从可见切换到不可见时，才做
有界 Brent 最大化。抛物线候选若未安全地落在 bracket 内，就退回 golden-section 步。
高纬地区没有完整天文昏影窗口的日期会被跳过，不会被误判为不可见。

返回的 `jd_ut` 是选中窗口中可见性最好的采样/精修时刻，并不声称是人眼恰好第一次
看见目标的精确瞬间。`window_start_jd_ut`、`window_end_jd_ut` 与内嵌的
`HeliacalVisibilityResult` 会一并返回，调用者可以据此叠加更严格的当地规则。

## 外部 Oracle

测试保留了 Schaefer profile 的固定 Swiss Ephemeris SWIEPH se1 Venus 事件最佳时刻：
地点 `(0, 0, 0)`，气压 `1013.25 hPa`、温度 `15 C`、相对湿度 `40%`、消光系数
`0.25`、裸眼观察者，并关闭月光。四种晨/昏首见/末见都会把 Taiyin 的最佳窗口时刻与
Swiss `swe_heliacal_ut()` 的 `dret[1]` 比较，容差为十分钟。这是外部行为回归，不表示
两个库的大气或视敏度语义逐项兼容；Belokrylov 仍按自身论文关系验证，不会被强行要求
匹配 Swiss 的日期。
