# 均时差与地方太阳时

文档状态：当前说明
主要头文件：`include/taiyin/runtime/solar_time.h`

## 均时差

```cpp
calc_equation_of_time_ut(&context, jd_ut, &result, &diagnostic)
calc_equation_of_time_tt(&context, jd_tt, &result, &diagnostic)
```

Taiyin 采用下面的符号约定：

```text
均时差 = 地方真太阳时 - 地方平太阳时
```

`EquationOfTimeResult` 同时返回日和秒，并给出实际采用的 UT/TT、太阳地心视赤经与
GAST。结果为负表示日晷时间落后于平太阳时。

太阳时物理计算当前仍使用 Taiyin 的标量 `double` 历元路径。只有 native 星历求值、
岁差/章动和地球自转（GAST）路径，以及承载时间的结果字段都能端到端接受并传播
split TT/UT1 历元、全程不把它合并为一个 `double` 后，才会暴露 split-Julian-Date
API 变体。

太阳按当日真赤道的地心视位置计算。传入的 `NativeCalcContext` 仍决定星历 route、
Delta-T、岁差、章动、光行时、光行差与引力偏折模型；context 中已有的 topocentric
状态会被清掉，因为均时差是全局地心量。

## LMT 与 LAT

```cpp
local_mean_to_apparent_solar_time(
    &context, jd_local_mean, longitude_rad, &jd_local_apparent, &diagnostic)
local_apparent_to_mean_solar_time(
    &context, jd_local_apparent, longitude_rad, &jd_local_mean, &diagnostic)
```

经度单位为弧度、东正，范围必须是 `[-π, π]`。地方太阳时 JD 的定义是：

```text
LMT = UT1 + longitude / 2π
LAT = LMT + 均时差
```

LAT 转 LMT 需要迭代，因为均时差应在候选 LMT 对应的 UT1 瞬间求值。这两个函数只
转换太阳时间坐标，不处理时区或民用历法规则。
