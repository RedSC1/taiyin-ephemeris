# 月球交点与远地点

状态：当前
主要头文件：`include/taiyin/astrology/lunar_points.h`
位置 target 头文件：`include/taiyin/astrology/targets.h`

这个可选 extension 提供约定性的平均月球点与地心瞬时月球点。`True Lilith`、
`Mean Lilith` 等熟悉的占星名称只是别名，不代表它们存在唯一、普世优先的定义。

## API

```cpp
calc_lunar_mean_node_ut(&context, jd_ut, TAIYIN_LUNAR_NODE_ASCENDING,
                        reference_flags, &node, &diagnostic)
calc_lunar_true_node_tt(&context, jd_tt, TAIYIN_LUNAR_NODE_DESCENDING,
                        native_position_flags, &node, &diagnostic)

calc_lunar_mean_apogee_ut(&context, jd_ut, reference_flags, &apsis, &diagnostic)
calc_lunar_osculating_apogee_tt(
    &context, jd_tt, native_position_flags, &apsis, &diagnostic)
calc_lunar_fitted_apogee_ut(
    &context, jd_ut, reference_flags, &apsis, &diagnostic)
```

## 位置 Target 注册

链接 `taiyin_astrology_extension` 的应用可以在 setup 阶段注册一次内建月球点：

```cpp
using namespace taiyin::astrology;

register_builtin_astrology_targets();
runtime::calc_position_tt(
    &context,
    TAIYIN_ASTROLOGY_TARGET_TRUE_NODE,
    jd_tt,
    runtime::TAIYIN_NATIVE_POSITION_SPEED,
    out,
    &diagnostic);
```

注册的 ID 包括 `TRUE_NODE`、`TRUE_DESCENDING_NODE`、`MEAN_NODE`、
`MEAN_DESCENDING_NODE`、`MEAN_LILITH`、`OSCULATING_LILITH` 与
`FITTED_LILITH`。它们和物理天体一样走 `calc_position_*` / `calc_positions_*`：
瞬时点会正常请求月球位置，因此底层仍使用 OPM/SPK/半解析路由与 segment cache；
拟合点直接使用生成的系数表，运行时不需要挂载 DE441。

交点和平均 Lilith 是方向，不是物理三维天体，它们的球坐标 `distance_au = NAN`；
瞬时与拟合 Lilith 都会给出距离。`XYZ` 请求也接受：仅方向 target 自然返回 `NAN`
笛卡尔分量，瞬时与拟合 Lilith 则返回由该距离导出的笛卡尔位置与速度。
Topocentric 请求同样接受，但目前保留这些约定定义的地心语义，不人为加入虚点视差。
下面的 typed API 仍保留较窄的“只返回定义量”契约。

evaluator 也可以注册精确的 `NativeStateEvaluatorFn`；`calc_state_*()` 会原样采用它给出的
position、velocity、acceleration。只有 position evaluator 的 target 才会用默认 `0.001 day`
步长的中心差分回退：已有的有限 velocity 保留不动，缺失 velocity 从位置差分得到，
acceleration 优先从相邻速度差分，否则从相邻位置差分。邻近时刻无法求值时，只让缺失导数为
`NAN`。

`LunarNodePosition` 返回经度、瞬时经度速度和实际使用的参考 frame。
`LunarTrueNodePosition` 保留为首版真交点 API 的源代码兼容别名。

`LunarApsisPosition` 返回经度、纬度、二者速度、参考 frame 与 `definition`：

- `TAIYIN_LUNAR_APSIS_DELAUNAY_MEAN`：Delaunay 平均远地点方向，也是占星里常说的
  **平均黑月 / Mean Lilith**。它不是人为假定半径的物理位置，因此
  `distance_au` 和 `distance_rate_au_per_day` 为 `NAN`。
- `TAIYIN_LUNAR_APSIS_OSCULATING_TWO_BODY`：由经过修正的地心月球状态在该时刻
  拟合出的二体椭圆远地点。占星软件常称其为 **True Lilith**；这里会填充瞬时远地点
  距离和距离速度。
- `TAIYIN_LUNAR_APSIS_DE441_FITTED_NATURAL`：Taiyin 明确定义的一种连续自然
  远地点。分段 Delaunay-Poisson 级数拟合 DE441 的物理月球远地点事件；每个事件
  方向先由 Delaunay 平均方向和球面切平面修正量在固定 ICRF 中重建，再用非均匀三次
  Hermite 曲线连接相邻事件向量。它既不是把两个经度做线性插值，也不声称与其他
  星历软件的插值远地点共享系数。

## 定义

平均交点直接采用 IERS 2003 Delaunay 参数 `Omega(T)`。Delaunay 平均远地点先计算
`varpi = F + Omega - l`，再使用 `5.145396` 度的约定平均月轨道倾角绕平均交点旋转。
这是明确的模型定义，不会用“更贴近当天真实月球位置”为理由偷偷塞长期经验修正表。

瞬时远地点由经过修正的地心月球状态直接构造：

```text
h = r x v
e = (v x h) / mu - r / |r|
远地点方向 = -normalize(e)
远地点距离 = a * (1 + |e|)
```

角速度和距离速度使用同一份状态加速度及参考面矩阵导数；这里不使用日期有限差分，
也不触发远地点事件搜索。

拟合自然远地点使用 NASA/JPL DE441 中提取的 402,838 次地心月球远地点事件；每个
事件都通过 `dot(r, v) = 0` 求出。整个 DE441 区间分成 30 段，通常每段 1000
儒略年，末尾不足 1000 年的碎段并入相邻段。每段包含三次长期项和 16 个筛选后的
Delaunay 周期项；训练时两侧扩展 100 年，运行时在分段边界两侧各 50 年平滑混合。
方向残差不是会包角的经纬差，也不是随远地点长期转动的全局笛卡尔分量，而是 DE441
单位向量相对 Delaunay 平均方向的球面 `log-map`，分解到该平均方向的局部 east/north
切基。平均方向和切基固定按 IAU 2006 平黄道转换到 ICRF，再由球面 `exp-map` 重建
拟合单位向量。连续位置和解析速度来自 ICRF 中相邻重建事件向量的非均匀三次 Hermite
曲线。用户选择的岁差模型只作用于最终输出 frame，不会反过来改变拟合对象本身。

系数覆盖 JD `-3100015.5` 至 `8000016.5`。区间外不会突然报错，而是继续外推最近的
边界段，同时把 `LunarApsisPosition::extrapolated` 设为 `true`，避免把区间外结果
伪装成 DE441 覆盖内精度。

重新生成系数表：

```sh
python3 tools/fit_lunar_apogee_de441.py \
  --de441 /path/to/de441.bsp \
  --output src/astrology/generated/lunar_apogee_de441_fit.h
```

## 参考面与 flag 约定

`NativeCalcContext` 的输出 frame 决定结果参考面，所有 runtime 输出 frame 都支持：

- `TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC`
- `TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE`
- `TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE`
- `TAIYIN_APPARENT_FRAME_ICRF`
- `TAIYIN_APPARENT_FRAME_J2000_MEAN_EQUATOR`
- `TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE`
- `TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE`
- `TAIYIN_APPARENT_FRAME_CIRS`

瞬时点永远使用**地心**月球状态。即使 context 已安装地表观察者，计算也会刻意移除
topocentric offset。

瞬时点接受正常月球位置修正控制：`TRUEPOS`、`ASTROMETRIC`、`NO_ABERR`、
`NO_GDEFL`、`NONUT`。`EQUATORIAL` 选择当日真赤道（带 `NONUT` 时选择当日平赤道）。
typed API 拒绝 `XYZ`、`RADIANS`、`SPEED` 等输出形状 flag；注册后的 position target
会按普通 `calc_position_*` 语义接受 `RADIANS`、`SPEED`、`XYZ` 与 `TOPOCENTRIC`。

平均点只来自约定 Delaunay 参数，只接受用于选参考面的 `EQUATORIAL`、`NONUT`。
`TRUEPOS`、`ASTROMETRIC`、`NO_ABERR`、`NO_GDEFL` 会被拒绝，不会静默假装能改变
平均模型。

拟合自然点采用同样的“只选择参考面”flag 契约。它的定义已经固定在生成的 DE441
拟合系数里，视位置修正 flag 不会改变该模型。

请求 `NONUT` 且 context 原来选择 `TRUE_ECLIPTIC_OF_DATE` 或
`TRUE_EQUATOR_OF_DATE` 时，结果会标出对应平参考面；CIRS 没有无章动等价面，因此
和 `NONUT` 的组合会被拒绝。

## 验证

测试锁定 IERS 公式本身的平均交点与平均远地点定义，并记录同一现代历元的 Swiss
Ephemeris Moshier sanity 数值。OPM2 和 Moshier 的瞬时交点仅差数角秒；瞬时远地点会放大
月球速度模型的微小差异，测试历元的经度差约为 43 角秒。

拟合模型另有独立 DE441 事件 oracle：在 JD `2460420.5913274437`，经度、纬度误差
约为 21.9、15.5 角秒，距离误差约 0.66 km。覆盖完整 DE441 区间的分层留出样本不会
参与谐波选择，也不会参与临时验证拟合；各段事件时刻最大误差约 431 至 813 秒，
拟合事件方向残差约 0.5 至 3 角分，距离残差约 5 至 12 km。生成正式系数前会再使用
全部事件重拟合。这些数字描述模型误差，不是浮点断言容差。
