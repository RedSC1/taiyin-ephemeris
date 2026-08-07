# 内置半解析星历

文档状态：当前说明
最后审阅：2026-07-21
主要代码：`src/semi_analytic.cpp`、`src/internal/semi_analytic_coefficients.inc`

Taiyin 内置了一套不需要运行时数据文件的冻结半解析 fallback。当 SPK 或 OPM2
没有可用路线时，它可以继续提供长期星体状态；默认优先级低于 SPK/OPM2，高于
TKC1/Kepler。

## 覆盖范围和路线

行星级数覆盖 JD TDB `625295.0` 到 `2816795.0`，约为公元前 3000 年至
公元 3000 年。月球修正覆盖范围略窄；Earth/Sun 使用 EMB 与月球覆盖范围的交集。

直接支持：

```text
Mercury/Venus/EMB/Mars/Jupiter/Saturn/Uranus/Neptune/Pluto barycenter / Sun
Earth / Sun
Moon / Earth
```

显式路线是 `TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC`，不会加载或分发
Moshier/PLAN404 代码和系数表。

## 模型结构

- 水星到冥王星和 EMB 使用独立拟合 JPL DE441 的紧凑谐波级数。
- 月球使用已在 `NOTICE` 署名的寿星天文历截断 XL1，随后叠加独立拟合的 DE441
  残差修正。
- Earth/Sun 由 EMB/Sun 与按地月质量比缩放的 Moon/Earth 向量组合得到。
- 月球的当日黄道结果先用 P03 岁差模型转换到 J2000 黄道，最终所有路线统一返回
  ICRF/J2000 赤道直角坐标状态。
- 位置、速度、加速度通过二阶自动微分一次求出，不使用有限差分伪造导数。

冻结 C++ 表记录生成源 revision 和 Python 系数文件 SHA-256。只有显式运行
`tools/generate_semi_analytic_builtin.py` 才会重新生成；普通构建不会依赖 Python 工程。

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

修正后的地心月球模型在独立 32 日网格上的角度 RMS 为 `0.704 角秒`，距离 RMS
为 `0.263 km`；该网格最大误差分别为 `5.22 角秒` 和 `1.52 km`。

这些数值描述冻结基础状态模型，不等于最终 apparent/topocentric 误差。光行时、
光行差、引力偏折、岁差章动、观测者几何和时间尺度策略仍由正常 runtime 层处理。

## 来源记录

导入模型来自 `taiyin-exp` revision
`27d33df2089ee1213a13a68782d5eff4ca2b2681`；其 `coefficients.py` SHA-256 为
`67beddfed388e5a8b934b8834a0f011dd69fa9888c6373a7a6becbd39eb01516`。
XL1 的寿星天文历来源和原作者许可声明保留在仓库 `NOTICE`。
