# 天体现象量星等模型

文档状态：当前说明
最后审阅：2026-07-02
代码入口：`src/runtime/phenomena.cpp`

本文说明 `calc_body_phenomena_ut()` / `calc_body_phenomena_tt()` 返回的 `BodyPhenomena::apparent_magnitude` 是怎么来的。这里的星等是经验 V-band 视星等模型，用于和常见天文历、SwissEph `swe_pheno*()` 一类输出保持同一类语义；它不是严格的辐射传输、表面散射或观测系统标定模型。

## 通用距离项

除 Sun 外，反射天体默认使用：

```text
m = phase_term + 5 log10(r * Delta)
```

其中：

- `r` 是 Sun-body 距离，单位 AU；
- `Delta` 是 observer-body 距离，单位 AU；
- `phase_term` 是该天体的相位经验项。

Sun 不是反射天体，不使用 `r * Delta` 距离项。

## Sun

Sun 当前使用平均 V-band 太阳视星等按 observer-Sun 距离缩放：

```text
m = -26.74 + 5 log10(Delta)
```

`Delta` 是 observer-Sun 距离，单位 AU。`-26.74` 是常用平均太阳 visual magnitude 常数；它和某些历书或 SwissEph 内部采用的太阳星等零点可能相差约 `0.1 mag`。这类差异主要是 photometric zero point 约定差异，不代表几何位置计算错误。

## Moon

Moon 当前使用 Astronomical Almanac 风格的相位经验式：

```text
m = 0.21 + 5 log10(r * Delta) + phase_term(alpha)
```

其中 `alpha` 是相位角，单位 degree，`H=0.21` 是 Moon 在 Solar System body absolute magnitude 定义下的常用值。`phase_term` 在 `alpha <= 150 deg` 时按 before full Moon / after full Moon 分别使用不同多项式。代码会通过比较 `jd +/- 1 hour` 的 Moon phase angle 判断当前位于满月之前还是满月之后；这比文档里写“waxing/waning”更贴近公式原始语义。

在极细月牙区间，`alpha > 150 deg` 时，多项式不再作为主模型使用；当前保留一个 thin-crescent fallback，避免新月附近返回 `NaN`。这个 fallback 只保证数值连续和大致量级，不应当用于精密月面光度研究。

这套 Moon 星等模型是有意选择的 core runtime 默认：它足够表达历书级现象量，也方便和 SwissEph `swe_pheno*()`、常见天文历输出做 sanity comparison。更高精度的月面 irradiance / photometry 模型应作为独立模型接入，例如以后通过 context 选择 ROLO 风格模型；不要把它和 `illuminated_fraction` 这个几何量混在一起。

## Mercury 到 Neptune

Mercury、Venus、Mars、Jupiter、Saturn、Uranus、Neptune 当前按 Mallama & Hilton (2018) 的 V-band 经验公式和分段规则实现。

这些模型包含一些天体专用项：

- Mars：使用 sub-observer / sub-solar longitude 推导 rotation 与 orbit 修正；
- Jupiter：使用论文的高相位角分段；
- Saturn：使用带环亮度项，并按论文定义处理 Earth/Sun 看到的环面有效倾角；
- Uranus：使用 planetographic sub-latitude 项；
- Neptune：使用时间项和高相位角分段。

这些公式覆盖的是历书意义上的经验星等。它们依赖当前 `NativeCalcContext` 的星历路由和 apparent/true position flags；如果 route 只能提供 barycenter 而不能提供对应本体，API 默认会返回普通无路由错误。调用方明确接受 major-planet barycenter 近似时，可以显式传 `TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX`。

## Pluto

Pluto 当前使用小天体常见的 H-G phase function：

```text
H = -0.55
G = 0.15
m = H_G(alpha) + 5 log10(r * Delta)
```

`H=-0.55` 采用 Pluto 常用 physical parameter 表里的 absolute magnitude；`G=0.15` 采用 IAU H-G / MPC/JPL 小天体星等体系在缺少专门 slope parameter 时的默认 slope。也就是说这里不是任意调出来的两个数，而是“Pluto 的常用 H + 标准 H-G 默认 G”。

Pluto 的真实亮度仍会受到相位曲线、表面反照率分布、观测波段和历元的影响；当前实现是历书级近似，不是 Pluto 专用高精度光度模型。

长期更好的方向是把 Pluto、asteroids、TNOs 等小天体的 `H/G` 或更现代的 `H/G1/G2` 参数放进 catalog metadata，而不是硬编码在 runtime 里。

## 参考来源

- Mallama, A. and Hilton, J. L. (2018), [Computing Apparent Planetary Magnitudes for The Astronomical Almanac](https://arxiv.org/abs/1808.01973).
- Willmer, C. N. A. (2018), [The Absolute Magnitude of the Sun in Several Filters](https://arxiv.org/abs/1804.07788), 可作为太阳 V-band 零点差异背景参考。
- Moon 的 `H=0.21`、before/after full Moon phase polynomial 和 `alpha <= 150 deg` 适用范围见 [Absolute magnitude: Solar System bodies](https://en.wikipedia.org/wiki/Absolute_magnitude#Solar_System_bodies) 汇总的 Astronomical Almanac 推荐近似式。
- Pluto `H=-0.55` 可见常用 Pluto physical parameter 表；例如 [Pluto](https://en.wikipedia.org/wiki/Pluto) 的 physical characteristics 汇总列出 `Abs Magnitude = -0.55`，其引用来源包括 Planetary Physical Parameters。
- Minor-planet H-G phase function 采用 Bowell-style `H,G` 模型；公式形式和 `G=0.15` 默认约定可见 [Absolute magnitude: Solar System bodies](https://en.wikipedia.org/wiki/Absolute_magnitude#Solar_System_bodies)。

## 和 SwissEph 对比

当前测试里保留了 SwissEph 生成的 sanity oracle。它的作用是确认 Taiyin 输出和常见参考实现处于同一量级，不是保证 photometry zero point 完全一致。

尤其注意：

- Sun 的常数约定会导致约 `0.1 mag` 的系统差；
- Moon 的 waxing/waning branch 和细月牙 fallback 会导致新月附近更敏感；
- Pluto 当前是 H-G 经验模型，和 SwissEph 或其他历书可能存在模型差异。
