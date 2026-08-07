# taiyin-ephemeris

[English](README.md) · [Roadmap](ROADMAP.md)

`taiyin-ephemeris` 是一个星历运行时库。它负责加载星历数据、选择运行时路线、
管理数据源 segment cache，并通过 C++ 与带版本号的 C99 ABI 提供位置、可见性、
天象、日月食、掩星、晨昏初见和占星扩展计算。

库版本 `1.0.0` 是首个私有 core baseline。C ABI version 5 是 FFI 和应用程序的
兼容边界；C++ API 仍属于实现层接口，不承诺稳定二进制 ABI。

## 当前状态

当前 baseline：

- runtime、catalog、route、数据源 segment cache 和 body evaluator 架构已经启用。
- Native/C API 已覆盖位置、状态、观测坐标、固定星、可见性、天体现象、事件搜索、
  日月食、掩星、晨昏初见、轨道事件和真太阳时。
- 占星扩展已覆盖恒星黄道、Ayanamsha、宫位、月球交点/拱点、拟合远地点和
  自定义模型注册。
- OPM2/OPC、SPK、内置半解析模型、TKC1/Kepler、TSC1/TSF1 和自定义星体路线已经启用。
- 旧 pipeline 和旧 TSCA 内置固定星路线已移出 active build，归档在 `plans/legacy/`。
- 默认测试不依赖私有 oracle 数据。
- C ABI v5 已按 [`doc_cn/c_api.md`](doc_cn/c_api.md) 的契约冻结。

## 已完成能力

### Runtime 和数据加载

- 显式源注册与 packaged data root 加载。
- 以 source descriptor 作为事实来源。
- segment cache，支持按条目数驱逐和重新加载。
- singleflight route loading，避免并发 cache miss 重复加载。
- route-rule 选择，支持内置或用户注册的 route-rule table。
- Earth/Moon 等组合或内置天体的 runtime body evaluator。
- OPM2 读取与 OPC 持久目录/索引。
- SPK、Kepler/TKC1、内置半解析模型、TSC1/TSF1 和自定义源接入。

### C ABI 与 FFI

- `include/taiyin/c/` 下提供带版本号的 C99 统一 API。
- 动态库 target 为 `taiyin_c`，源码树静态 target 为 `taiyin_c_static`。
- 提供 opaque context、明确所有权、`struct_size` 扩展机制、diagnostic、
  capability 查询和 callback 注册。
- 自定义星体、Ayanamsha 和宫制不需要把 C++ 类型暴露给 FFI。
- 详见 [`doc_cn/c_api.md`](doc_cn/c_api.md)。

### 主星位置

- `calc_position_tdb`、`calc_position_ut` 和 batch 变体。
- `NativeCalcContext` 存用户上下文，例如 observer、atmosphere、模型、deflectors 和时间策略。
- flags 控制球坐标/XYZ、赤道/黄道、弧度、速度、truepos、astrometric、光行差、引力偏折和 topocentric 输出。
- apparent 计算支持光行时、Shapiro delay、年光行差、太阳/多体引力偏折、岁差、章动、黄赤交角和可选输出坐标系。

### 观测位置

- 主星 `calc_observed_ut`。
- 如果 `NativeCalcContext` 挂了 leap-second table 和 EOP table，主星
  `calc_observed_utc` 也可用。
- observer location 存在 `NativeCalcContext` 中。
- topocentric observer offset 作为单次调用 scratch 计算。
- precise UTC/EOP 路径覆盖 leap seconds、DUT1、polar motion 和 CPO。
- 地平坐标 azimuth/altitude 输出。
- 可选大气折射。
- 折射字段按模型校验：Bennett 类模型需要 pressure 和 temperature；SOFA 还需要 humidity 和 wavelength。

### 固定星

- 全局 TSC1/TSF1 星表注册。
- `calc_star_position_*` 和 `calc_star_positions_*` 支持 catalog astrometry、自行、径向速度/视差、frame routing、球坐标/XYZ 输出和可选速度。
- `calc_observed_star_ut` 和 `calc_observed_stars_ut` 使用与主星相同的 `TAIYIN_OBSERVED_*` flags。
- 固定星 observed 支持地心/站心视差、太阳引力偏折、年光行差、地平坐标和大气折射。
- 固定星查询以字符串为入口：用户 key 和 alias 解析到星表行。HIP、HR、HD、Gaia DR3 是该行 metadata，不是 runtime body id。
- provider 分配的 `runtime_id` 只是私有 cache/diagnostic key，不是可移植固定星编号。

### 事件搜索

- 低层黄经、相对黄经、月相、精确相位和 station 搜索位于
  `include/taiyin/runtime/event_search.h`。
- event-search 的 `uint64_t flags` 低 32 位保留给 native position flags，高
  32 位用于 event-search 选项。
- core runtime 返回原始数值事件：JD、黄经和目标角等。节气、星座 ingress、回归、相位名称、顺逆标签等领域封装留给下游模块。
- 详见 [`docs/event_search.md`](docs/event_search.md)。

### 日月食搜索

- 月食 solve/search、全球日食 solve/search、地方日食 circumstances 和日食路径辅助函数位于 `include/taiyin/runtime/eclipse_search.h`。
- 日月食搜索使用 Meeus 第 52 章作为朔望月预筛，然后用当前 runtime 星历细化最终几何。
- 月食地影模型和月球半径模型通过 `NativeCalcContext` 选择；算法来源、模型口径、验证边界和 API 示例见 [`docs/eclipse_search.md`](docs/eclipse_search.md)。

### 掩星搜索

- 第一版月掩恒星和月掩太阳系天体 next-search API 位于
  `include/taiyin/runtime/occultation_search.h`。
- 支持指定固定星或太阳系 body target 的地心搜索和地方/topocentric 搜索。
- 返回最大掩/最近角距时刻和接触时刻：恒星点目标返回 C1/C4，body 目标在存在内接触时返回 C1-C4。
- 本地可见性摘要 helper 可以在接触时刻和最大掩时刻采样 Moon、target、Sun 的高度/方位，并可选择使用折射高度。
- API 形状、当前 seed/refine 行为和限制见 [`docs/occultation_search.md`](docs/occultation_search.md)。

## 后续工作

1.0 baseline 已可使用，后续主要是：

- 更广的 observed oracle sweep，特别是 precise EOP/CPO、CIRS/equinox route、
  horizontal azimuth convention 和 refraction convention。
- 固定星对独立 catalog/runtime 输出的外部 oracle sweep。
- 更完整的目标盘面 metadata、不规则月缘/地形，以及更高保真光度和天空亮度模型。
- 完整星盘对象与流派解释层。
- 基于稳定 C ABI 的 Dart、Python、JavaScript bindings。
- 跨平台预编译动态库、签名和公开数据包。
- 第三方兼容 API/ABI 层。如果需要，应该单独放到 compatibility 项目里，避免许可证/语义兼容问题混入 core runtime。
- 在源码仓库内捆绑大型星历数据。

## 非目标

核心 runtime 不提供：

- 大范围系统目录或 home 目录扫描；
- 固定 chart/application struct；
- 低层 workflow/pipeline engine；
- 大型 SPK/OPM/oracle 数据集的完整分发。

流派解释、阿拉伯点、完整星盘组装和应用展示应放在下游应用中；占星计算
primitive 集中在 `taiyin_astrology_extension`，不混入 core astronomy runtime。

## 架构

应用层代码应优先从 native API 进入：

```text
NativeCalcContext + typed calculation functions
        |
        v
major-body / fixed-star apparent and observed helpers
        |
        v
EphemerisEngine
        |
        +--> EphemerisBlockCatalog
        +--> EphemerisSegmentCache
        +--> EphemerisRouteRule tables
        +--> EphemerisBodyRegistry
        |
        v
source files and descriptors
```

只有在做 raw state evaluation、route-selection 测试、cache 测试、provider 测试或内部工具时，才应该直接使用 `EphemerisEngine`。

关键规则：

```text
source descriptors 是事实来源；
compiled-block cache entries 是可重新加载的派生状态。
```

缓存驱逐不能导致 source-backed block 失去重新加载能力。

## 数据模型

当前支持的数据族包括：

- OPM2 packaged numerical ephemeris segments。
- OPC catalogs，用于 packaged-data 持久发现。
- SPK files，用于外部 kernel-backed route。
- TKC1/Kepler-style catalog data。
- 内置冻结半解析 fallback，覆盖公元前 3000 年至公元 3000 年的水星到冥王星、
  EMB、地球和月球。
- TSC1 precision fixed-star catalogs。
- TSF1 user fixed-star files，通过 TSC1 provider 路径转换和计算。

大型 raw datasets 不进入源码仓库。请使用显式 data root 或外部数据包。

## 快速开始

```cpp
#include "taiyin/body_id.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

using namespace taiyin;
using namespace taiyin::runtime;

EphemerisRuntimeConfig config;
config.segment_cache_max_entries = 512;
config.data_root = "/path/to/taiyin/data";
initialize_global_ephemeris_runtime(config);

NativeCalcContext context;
const int bodies[] = {
    TAIYIN_BODY_SUN,
    TAIYIN_BODY_MOON,
    TAIYIN_BODY_MERCURY_BARYCENTER,
};

const double jd_ut = julian_day({2024, 1, 1, 12, 0, 0.0});
double positions[3][6];
EphemerisEvalDiagnostic diagnostics[3];

const Status status = calc_positions_ut(
    &context,
    bodies,
    3,
    jd_ut,
    TAIYIN_NATIVE_POSITION_RADIANS,
    &positions[0][0],
    diagnostics);
```

球坐标输出时，`positions[i][0..2]` 是经度、纬度和距离。加上 `TAIYIN_NATIVE_POSITION_SPEED` 后，`positions[i][3..5]` 输出对应速度。

## 固定星快速开始

```cpp
#include "taiyin/runtime/star_position.h"

add_global_tsc1_star_catalog("/path/to/catalog.tsc1");

const char* stars[] = {"spica", "HIP 65474"};
double star_positions[2][6];

calc_star_positions_ut(
    &context,
    stars,
    2,
    jd_ut,
    TAIYIN_NATIVE_POSITION_RADIANS,
    &star_positions[0][0],
    nullptr);
```

如果需要 topocentric、horizontal 或 refraction 输出，使用 observed star API：

```cpp
ObservedPosition observed[2];

calc_observed_stars_ut(
    &context,
    stars,
    2,
    jd_ut,
    TAIYIN_OBSERVED_TOPOCENTRIC
        | TAIYIN_OBSERVED_HORIZONTAL
        | TAIYIN_OBSERVED_REFRACTION,
    observed,
    nullptr);
```

## 示例

```sh
cmake --build build --target example_apparent_ut_chart_table
./build/example_apparent_ut_chart_table data
```

输出地心 apparent ecliptic 裸星盘表格。

```sh
cmake --build build --target example_observed_ut_bare_chart
./build/example_observed_ut_bare_chart data
```

运行带 observer context 的 observed UT 路径，并输出地平坐标。

```sh
cmake --build build --target example_observed_utc_eop_bare_chart
./build/example_observed_utc_eop_bare_chart data
```

运行 precise observed UTC 路径，使用内置 leap seconds、内置 finals2000A
EOP、北京 observer context、地平坐标和大气折射。

## 构建和测试

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

八字是由 Pascal 规则核支持的可选扩展。启用它会同时启用干支历扩展，并要求
系统具有兼容的 Free Pascal 编译器：

```sh
cmake -S . -B build-bazi \
  -DTAIYIN_BUILD_BAZI_EXTENSION=ON
cmake --build build-bazi
ctest --test-dir build-bazi --output-on-failure
```

只有启用该扩展的构建才安装 `taiyin/c/bazi.h` 并导出 `taiyin_bazi_*`。
调用方必须显式包含该头，不能依赖 `taiyin/c/taiyin.h` 间接引入八字接口。

重要本地测试包括：

- `test_ephemeris_catalog`
- `test_ephemeris_segment_cache`
- `test_ephemeris_route_rule`
- `test_custom_ephemeris_method`
- `test_event_search`
- `test_occultation_search`
- `test_body_registry`
- `test_apparent_position`
- `test_apparent_position_oracles`
- 私有兼容性 / oracle sweep 放在 `private/` 下，不进入默认公开构建。
- `test_star_file`
- `test_tsc1_catalog_discovery`
- `test_opc_catalog_persistent`

可选 oracle 和外部数据测试会在缺少相关环境变量时自动 skip：

```text
TAIYIN_DE441_PATH
TAIYIN_OPM2_DATA_DIR
TAIYIN_MER404_TS_PATH
TAIYIN_MAIN_BELT_ASTEROIDS_SPK_PATH
TAIYIN_NEAR_EARTH_ASTEROIDS_SPK_PATH
TAIYIN_JUPITER_SATELLITES_SPK_PATH
TAIYIN_SATURN_SATELLITES_SPK_PATH
```

## 文档

- [`ROADMAP.md`](ROADMAP.md) — 方向和优先级。
- [`docs/current_limitations.md`](docs/current_limitations.md) — 当前缺口和已知限制。
- [`docs/event_search.md`](docs/event_search.md) — 低层事件搜索 API 和扩展边界。
- [`docs/eclipse_search.md`](docs/eclipse_search.md) — 日月食算法、模型口径和 API 示例。
- [`docs/occultation_search.md`](docs/occultation_search.md) — 月掩搜索和本地可见性摘要 API。
- [`docs/catalog_cache_model.md`](docs/catalog_cache_model.md) — descriptor catalog 与 compiled block cache。
- [`docs/opc_catalog_format.md`](docs/opc_catalog_format.md) — OPC 持久目录格式。
- [`docs/ephemeris_runtime_architecture.md`](docs/ephemeris_runtime_architecture.md) — 较底层 runtime/cache 架构。
- [`docs/tsc1_v1_known_limitations.md`](docs/tsc1_v1_known_limitations.md) — 固定星目录限制。

## Legacy

归档实现位于 `plans/legacy/`：

- `plans/legacy/pipeline_runner/` — 已否定的低层 workflow pipeline。
- `plans/legacy/star_tsca/` — 旧 TSCA 内置固定星原型。

这些内容仅保留历史，不应重新进入 active build。

## 许可证

Copyright 2026 RedSC1.

Taiyin 原创代码和文档使用 [Mozilla Public License 2.0](LICENSE) 许可证。
第三方代码和随仓库分发的数据继续遵守 [`NOTICE`](NOTICE) 及相邻数据来源文件中的许可证和条款。
