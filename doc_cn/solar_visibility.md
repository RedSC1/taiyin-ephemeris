# 太阳可见性

文档状态：当前说明  
最后审阅：2026-08-07  
主要头文件：`include/taiyin/runtime/solar_visibility.h`、`include/taiyin/c/visibility.h`

本模块为地理观测者搜索太阳升、落、晨昏蒙影和过中天事件。context 提供星历
route、时间尺度模型、视位置口径、观测者/大气配置和折射模型。C++ 入口使用
`SplitJulianDate`；对应 C ABI 使用 `taiyin_split_julian_date` 和 `taiyin_*` 名称。

## 精确搜索

```cpp
search_solar_rise_set_ut(...)
search_solar_rise_set_at_horizon_ut(...)
search_solar_twilight_ut(...)
search_solar_transit_ut(...)
```

`search_solar_rise_set_ut()` 使用普通视地平线；`_at_horizon_` 版本额外接收弧度
单位的显式几何地平高度。两者均接收升/落事件类型、太阳上缘/中心/下缘选择和
visibility flags。`search_solar_twilight_ut()` 选择民用、航海或天文晨昏蒙影；
`search_solar_transit_ut()` 搜索上中天或下中天。

升落或过中天搜索均返回 `SolarVisibilityEventResult`，其中包含高度状态、穿越
方向、精修后的 UT 时刻、残差极值及采样/精修次数。区间内没有普通穿越时，状态
可能是 `ALWAYS_ABOVE`、`ALWAYS_BELOW` 或 `TANGENT`；调用方必须检查
`altitude_state`，不能只假设事件时刻总是有限数。

## 快速单日升落和过中天

```cpp
compute_solar_rise_set_fast_tt(
    context, center_jd_tt, longitude_deg, latitude_deg, height_m,
    limb_kind, horizon_altitude_rad, solar_visibility_flags, out, diagnostic)

compute_solar_transit_fast_tt(
    context, center_jd_tt, longitude_deg, latitude_deg, height_m,
    out, diagnostic)
```

快速升落入口是面向单日、TT 的便捷计算：先用本地解析 seed，再作少量视位置
几何精修；高纬或困难穿越会退回普通 visibility search，因此 limb 和 refraction
语义不变。结果返回 TT 升/落时刻和高度状态。需要任意时间范围、显式穿越方向或
完整残差诊断时，应使用区间搜索入口。

`longitude_deg` 东正；`latitude_deg` 取值 `[-90, 90]`；`height_m` 是相对 WGS84
椭球的观测者高度。即使源 context 没有保存 observer location，本次调用给出的
位置仍会用于计算。

`limb_kind` 可取：

- `TAIYIN_SOLAR_VISIBILITY_LIMB_UPPER`；
- `TAIYIN_SOLAR_VISIBILITY_LIMB_CENTER`；
- `TAIYIN_SOLAR_VISIBILITY_LIMB_LOWER`。

`horizon_altitude_rad` 是几何地平线偏移。普通地平线传 `0.0`，再通过 limb 和
refraction 选项选择观测口径。

## Visibility 与大气 Flags

以下 C++ 常量在 C ABI 中有对应的 `TAIYIN_VISIBILITY_` 前缀名称。

- `TAIYIN_SOLAR_VISIBILITY_FLAG_REFRACTION` 请求带折射的 apparent altitude。
- `TAIYIN_SOLAR_VISIBILITY_FLAG_NO_REFRACTION` 请求几何 true altitude。
- `TAIYIN_SOLAR_VISIBILITY_FLAG_FIXED_DISC_SIZE` 使用文档约定的固定太阳视圆面，
  而不是随距离变化的物理角半径。
- `TAIYIN_SOLAR_VISIBILITY_STRICT_METEOROLOGY` 是高 32 位 flag，要求带折射
  请求不得使用标准大气回退。

两个折射 flag 都不设置时，公共太阳升落 API 默认选择折射。`REFRACTION` 和
`NO_REFRACTION` 互斥。几何模式不需要大气数据；带折射模式读取
`NativeCalcContext` 中的大气字段，缺失时只有 context 显式启用
`TAIYIN_NATIVE_ATMOSPHERE_ALLOW_STANDARD_FALLBACK` 才会使用标准大气回退。
`STRICT_METEOROLOGY` 会关闭该回退，因此必须由调用方提供有效的大气字段。

非法 limb、冲突或未知 flags、无效坐标，或者请求了不可用的折射模型，都会返回
`TAIYIN_ERROR_INVALID_ARGUMENT`。
