# 轨道事件

文档状态：当前说明
主要头文件：`include/taiyin/runtime/orbital_events.h`

本模块提供几何、两体密切轨道量，以及真实交点和近远地点搜索。它不是
Lilith、平均交点、真交点或宫位 API。

## 支持的天体与中心

中心由 `body_id` 固定决定：

| 天体 | 中心 |
| --- | --- |
| Moon | Earth |
| Earth、EMB、各大行星本体与各大行星 barycenter | Sun |

Sun 和 Solar System Barycenter 会被拒绝。行星 barycenter 可以正常使用；它
表示该行星系统质心绕太阳的轨道。

## 几何状态约定

所有函数都基于同一时刻的几何相对状态：

```text
r = body(t) - fixed_center(t)
v = d(r) / dt
```

实现会强制几何 ICRF 相对 state。光行时、光行差、引力偏折和地表 topocentric
观测者不是物理轨道的一部分，因此不能作为调用 flag 传入。底层星历 route、时间
模型和可选的 barycenter approximation 仍由 `NativeCalcContext` 控制。

`reference_frame_id` 决定返回角度与交点所相对的参考面，接受已有的
`TAIYIN_APPARENT_FRAME_ICRF`、`TAIYIN_APPARENT_FRAME_J2000_MEAN_EQUATOR`、
`TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC`、
`TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE`、
`TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE`、
`TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE` 与
`TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE`、`TAIYIN_APPARENT_FRAME_CIRS`。
ICRF 到目标 frame 的变换（包括 J2000 frame bias）完全由既有 apparent-frame 管线
负责。半长轴、偏心率、距离和能量仍使用惯性 state，不把 of-date 参考面本身的转速
混进物理根数。

## 密切轨道根数

```cpp
calc_body_osculating_orbit_ut(...)
calc_body_osculating_orbit_tt(...)
```

`BodyOsculatingOrbit` 返回当前瞬时的标准圆锥曲线量：半长轴、偏心率、倾角、
升交点黄经、近地点幅角、真/平近点角、当前距离、近远地点距离和对应两体周期。

这些量由当前 `r, v` 和中心/目标总引力参数直接计算。支持的 GM 使用 NAIF DE440
常数。它描述的是当前 N 体轨迹的切触两体圆锥，不是长期轨道传播器；语义与
[NAIF SPICE `oscelt_c`](https://naif.jpl.nasa.gov/pub/naif/toolkit_docs/C/cspice/oscelt_c.html)
和 [JPL Horizons](https://ssd.jpl.nasa.gov/horizons/manual.html) 的 state-to-
osculating-elements 一致。

接近圆轨道或接近参考面的轨道，其近地点方向或升交点黄经在数学上本来就不稳定。
完全退化时，对应角度字段归零，不伪造方向。

## 给定历元的轨道参考点

```cpp
calc_body_orbit_reference_points_ut(...)
calc_body_orbit_reference_points_tt(...)
```

`BodyOrbitReferencePoints` 复用同一条瞬时密切轨道，一次返回升交点、降交点、近点、
远点和第二焦点。每个点都包含相对固定物理中心的笛卡尔位置、所选参考 frame 下的
黄经/赤经方向、纬度和距离。

模型会显式记录为 `TAIYIN_BODY_ORBIT_REFERENCE_POINTS_OSCULATING`。这些点描述
“请求历元处拟合出的圆锥”而不是受摄动真实天体下一次经过这些点的时刻；真实事件
时间应使用下面的搜索入口。实现不会静默换成平均根数流派或
barycentric-osculating 兼容约定。

## 近远地点搜索

```cpp
search_next_body_apsis_ut(..., TAIYIN_BODY_APSIS_PERICENTER, ...)
search_next_body_apsis_ut(..., TAIYIN_BODY_APSIS_APOCENTER, ...)
```

搜索的物理条件是：

```text
f(t)  = r(t) . v(t) = 0
f'(t) = v(t) . v(t) + r(t) . a(t)
```

时间正向时，近地点是负到正的穿越，远地点是正到负的穿越。
`TAIYIN_ORBITAL_EVENT_REVERSE` 搜上一次。若起点恰好就是事件，函数会继续找
下一次或上一次，不会原样返回起点。

当前密切轨道的平近点角只用于给局部 seed。Taiyin 会用当前选择的真实星历验证
seed，优先使用保护 Newton，必要时退为 secant，并在最后用略大于一个局部密切
周期的 bracket 扫描兜底。seed 是优化，不是长期事件历表。

## 参考面升降交点搜索

```cpp
search_next_body_plane_node_ut(..., TAIYIN_BODY_NODE_ASCENDING,
                               reference_frame_id, ...)
search_next_body_plane_node_ut(..., TAIYIN_BODY_NODE_DESCENDING,
                               reference_frame_id, ...)
```

它们求所选 frame 的 XY 平面穿越：

```text
f(t) = z_reference_frame(t) = 0
```

参考纬度从负到正是升交点，从正到负是降交点。结果的
`reference_plane_angle_rad` 同样位于所选 frame：黄道 frame 下是黄经，赤道 frame
下是赤经方向。`ICRF` 指 ICRF 赤道 XY 平面。当前 apparent-frame 约定下，当日平黄道
与当日真黄道的交点使用同一黄道平面：真黄道通过章动改变黄道原点，因此返回黄经会
变，但交点时刻不会变。

## Flags 与时间尺度

低 32 位只接受：

```text
TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX
```

高位搜索选项是：

```text
TAIYIN_ORBITAL_EVENT_REVERSE
```

UT/TT 入口沿用 Taiyin 普通的 UT/TT 到 TDB 转换。搜索结果的 `jd` 字段遵循函数名
所标记的时间尺度。
