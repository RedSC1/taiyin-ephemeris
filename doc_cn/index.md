# Taiyin 中文文档索引

文档状态：当前说明
最后审阅：2026-08-12

这些文档描述 Taiyin 当前运行时能力、数据格式、事件搜索和日月食搜索接口。阅读时优先参考标记为“当前”的文档。

## 当前运行时文档

这些文档描述 `main` 上的当前实现。

| 主题 | 文档 | 代码入口 |
| --- | --- | --- |
| 第三方软件与数据 | [`third_party.md`](third_party.md) | `NOTICE`、`src/third_party/`、随包数据旁的 README 与 manifest |
| 运行时架构 | [`ephemeris_runtime_architecture.md`](ephemeris_runtime_architecture.md) | `include/taiyin/runtime/runtime.h`, `include/taiyin/runtime/ephemeris_engine.h`, `src/runtime/` |
| 内置半解析星历 | [`semi_analytic_ephemeris.md`](semi_analytic_ephemeris.md) | `include/taiyin/internal/semi_analytic.h`, `src/semi_analytic.cpp` |
| C ABI 与 FFI 入口 | [`c_api.md`](c_api.md) | `include/taiyin/c/taiyin.h`, `src/c_api/` |
| Catalog 和 segment cache | [`catalog_cache_model.md`](catalog_cache_model.md) | `include/taiyin/internal/ephemeris_catalog.h`, `include/taiyin/internal/ephemeris_segment_cache.h` |
| OPC 持久化 catalog | [`opc_catalog_format.md`](opc_catalog_format.md) | `include/taiyin/internal/opc_catalog_persistent.h`, `src/opc_catalog_persistent.cpp` |
| 事件搜索 | [`event_search.md`](event_search.md) | `include/taiyin/runtime/event_search.h`, `src/runtime/event_search.cpp` |
| 轨道事件 | [`orbital_events.md`](orbital_events.md) | `include/taiyin/runtime/orbital_events.h`, `src/runtime/events/orbital_events.cpp` |
| 均时差与地方太阳时 | [`solar_time.md`](solar_time.md) | `include/taiyin/runtime/solar_time.h`, `src/runtime/events/solar_time.cpp` |
| 太阳可见性 | [`solar_visibility.md`](solar_visibility.md) | `include/taiyin/runtime/solar_visibility.h`, `include/taiyin/c/visibility.h`, `src/runtime/visibility/solar_visibility.cpp` |
| 中国农历转换 | [`chinese_calendar.md`](chinese_calendar.md) | `include/taiyin/chinese_calendar/calendar.h`, `include/taiyin/c/chinese_calendar.h`, `src/chinese_calendar/` |
| 可选八字扩展 | [`bazi.md`](bazi.md) | `bazi_astrology/include/taiyin/bazi/bazi.h`, `bazi_astrology/include/taiyin/c/bazi.h` |
| 恒星黄道与 ayanamsha | [`astrology_sidereal.md`](astrology_sidereal.md) | `include/taiyin/astrology/sidereal.h`, `src/astrology/ayanamsha_models.cpp` |
| 占星宫位基础 | [`astrology_houses.md`](astrology_houses.md) | `include/taiyin/astrology/houses.h`, `src/astrology/houses.cpp` |
| 月球交点与远地点 | [`astrology_lunar_points.md`](astrology_lunar_points.md) | `include/taiyin/astrology/lunar_points.h`, `include/taiyin/astrology/targets.h`, `src/astrology/lunar_points.cpp` |
| 天体现象量 | [`phenomena.md`](phenomena.md) | `include/taiyin/runtime/phenomena.h`, `src/runtime/phenomena.cpp` |
| 天体星等模型 | [`phenomena_magnitude_models.md`](phenomena_magnitude_models.md) | `src/runtime/phenomena.cpp` |
| 日月食搜索 | [`eclipse_search.md`](eclipse_search.md) | `include/taiyin/runtime/eclipse_search.h`, `src/runtime/*eclipse*` |
| 月缘模型 | [`lunar_limb_model.md`](lunar_limb_model.md) | `include/taiyin/lunar_limb_tll1.h`, `src/runtime/lunar_limb.cpp` |
| 掩星搜索 | [`occultation_search.md`](occultation_search.md) | `include/taiyin/runtime/occultation_search.h`, `src/runtime/occultation_search.cpp` |
| 可运行示例 | [`../examples/README.md`](../examples/README.md) | `examples/` |

## 审计和限制说明

这些文档用于说明特定模块的验证覆盖、精度边界或已知限制。

| 主题 | 文档 | 说明 |
| --- | --- | --- |
| TSC1 v1 限制 | [`tsc1_v1_known_limitations.md`](tsc1_v1_known_limitations.md) | 当前恒星 catalog 格式的有意限制。 |
| 项目限制总览 | [`current_limitations.md`](current_limitations.md) | 当前状态和旧计划救回来的混合记录；当成依据前要对照代码。 |

## 维护规则

- 公共 API 变化应同步更新对应主题文档。
- Runtime 数据、路由或 cache 变化应更新 `ephemeris_runtime_architecture.md` 或 `catalog_cache_model.md`。
- 日月食行为变化应更新 `eclipse_search.md`。
- 临时实验、性能排查和内部对照记录不放入面向用户的文档。
