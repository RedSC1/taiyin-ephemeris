# 占星宫位基础

文档状态：当前说明
主要头文件：`include/taiyin/astrology/houses.h`

这个 extension 计算地方角点和第一批热带黄道宫位系统。它不依赖
sidereal/ayanamsha 选择；只有调用方明确需要恒星黄道盘时，才应在这些热带黄经上
再应用 ayanamsha 偏移。

## 输入与参考面约定

```cpp
calc_houses_ut(&context, jd_ut, system_id, &result)
calc_houses_tt(&context, jd_tt, system_id, &result)
calc_houses_from_armc(armc_rad, latitude_rad, true_obliquity_rad, system_id, &result)
```

`NativeCalcContext` 必须通过 `native_context_set_observer_location()` 或某个
topocentric observer setter 安装有效的观测者位置。经度东正。海拔、大气设置和
topocentric 位置改正不参与宫位几何。

`calc_houses_from_armc()` 是不依赖时间模型的纯几何入口。参数均为弧度，也不读取
`NativeCalcContext`；适合已经持有地方 ARMC 和目标历元真黄赤交角的调用方。

`calc_houses_ut()` 和 `calc_houses_tt()` 使用 context 当前选择的岁差、章动模型；
`calc_houses_from_armc()` 则直接使用调用方传入的 ARMC 和真黄赤交角：

- `armc_rad` 是地方视恒星时。
- `ascendant_rad`、`midheaven_rad`、`vertex_rad`、`east_point_rad` 和所有宫头
  都是当日真黄道上的热带黄经。
- `calc_houses_ut()` 的输入按 UT1 解释。`calc_houses_tt()` 会先用 context
  选择的 Delta-T 模型反推出 UT1，再计算恒星时。

两个按时间计算的入口还会返回 ARMC、ASC、MC、Vertex、East Point 和十二宫头的
中心差分角速度，单位是“弧度/对应输入时间尺度日”。`calc_houses_from_armc()`
没有时间轴，因此速度字段保持 `NAN`。若前后相邻样本解析成不同 fallback 宫制，
位置结果仍有效，速度保持 `NAN`，并设置
`TAIYIN_HOUSE_RESULT_SPEED_UNAVAILABLE`。

## 已支持系统

| 标识 | 含义 |
| --- | --- |
| `TAIYIN_HOUSE_SYSTEM_WHOLE_SIGN` | 第一宫头是 ASC 所在 30 度星座的起点。 |
| `TAIYIN_HOUSE_SYSTEM_EQUAL` | 第一宫头就是 ASC，后续宫头每隔 30 度。 |
| `TAIYIN_HOUSE_SYSTEM_PORPHYRY` | MC 到 ASC、ASC 到 IC 的黄道弧各三等分。 |
| `TAIYIN_HOUSE_SYSTEM_PLACIDUS` | 按半日弧、半夜弧的标准迭代构造，将各象限作时间三等分。 |
| `TAIYIN_HOUSE_SYSTEM_KOCH` | 将 MC 的升交时差三等分。 |
| `TAIYIN_HOUSE_SYSTEM_REGIOMONTANUS` | 等分天赤道，再通过宫圈投影到黄道。 |
| `TAIYIN_HOUSE_SYSTEM_CAMPANUS` | 等分卯酉圈，再通过宫圈投影到黄道。 |
| `TAIYIN_HOUSE_SYSTEM_ALCABITIUS` | 三等分 ASC 的半日弧和半夜弧。 |
| `TAIYIN_HOUSE_SYSTEM_POLICH_PAGE` | Polich/Page 的 topocentric 宫圈构造。 |
| `TAIYIN_HOUSE_SYSTEM_MORINUS` | 将天赤道上每隔 30 度的点转换到黄道。 |

`HouseResult::cusp_longitude_rad[0]` 是第一宫头。结果同时返回请求和实际采用的
system id。若 Placidus 在极圈内无解，或迭代未收敛，会回退到 Porphyry；
Koch 在极圈内也采用相同回退。`requested_system_id` 保持原始请求，
`resolved_system_id` 记录最终采用的系统，并设置
`TAIYIN_HOUSE_RESULT_USED_FALLBACK`。若最终采用 Porphyry，还会额外设置
`TAIYIN_HOUSE_RESULT_FALLBACK_PORPHYRY`。

## 连续宫头弧位置

```cpp
calc_house_position_from_longitude(&houses, longitude_rad, &position)
```

这个辅助函数把热带黄道经度放入已有 `HouseResult` 的宫头分区，返回 1 起算的宫号、
从本宫头走向下一宫头的比例，以及 `house_number + fraction`。恰好落在宫头上的点
归入后一个宫。

它明确只是“黄经/宫头弧分区”，不冒充 Placidus 或 Gauquelin 针对带纬度目标的
半日弧位置算法。需要后一种流派语义时，调用方不能拿这个结果替代。

## 自定义宫制

公开计算路径通过 extension 自己的 registry 选择宫制，不再依赖硬编码 switch。
自定义宫制可按下面的形状注册：

```cpp
bool my_houses(
    const HouseSystemDispatchData* data,
    double cusps_rad[12]
);

add_house_system_model(HouseSystemModelEntry(
    TAIYIN_HOUSE_SYSTEM_CUSTOM_START,
    &my_houses,
    TAIYIN_HOUSE_SYSTEM_PORPHYRY));
```

自定义 id 必须不小于 `TAIYIN_HOUSE_SYSTEM_CUSTOM_START`，不能覆盖内置或已有 id。
fallback 必须已经注册，因此按注册顺序天然不会形成环。callback 会收到归一化 ARMC、
大地纬度、真黄赤交角、ASC 和 MC；只有在填满 12 个有限的热带黄道宫头弧度值后，
才应返回 `true`。
同一 callback 可能被多个线程并发调用，且已注册模型不能注销；其代码必须在进程
剩余生命周期内保持已加载并可安全并发执行。

## 高纬行为

高纬地区的原始地平圈/黄道交点可能落在 ASC 的西侧分支。已支持系统会先采用标准的
对宫分支修正，再生成宫头，行为与常见的 Swiss-compatible Equal、Whole Sign、
Porphyry 一致。这不是 Porphyry fallback。

当 `abs(latitude) >= 90 度 - 当日真黄赤交角` 时，Placidus 和 Koch 无解，
按上面的约定回退 Porphyry。
