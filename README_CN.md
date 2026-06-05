# taiyin-ephemeris

[English](README.md) · [Roadmap](ROADMAP.md)

`taiyin-ephemeris` 是 OpenDestiny 的星历运行时层。版本 `0.0.1` 聚焦于星历源加载、目录选择、共享缓存、视位置计算以及一个最小的运行时管线（pipeline）原语。它有意不构建为一个完整的终端用户应用框架；天文、占星扩展及其他领域层可以在此之上搭建。

## 0.0.1 范围

本版本的目标是让底层星历运行时可用、可测试：

- OPM4 星历源的发现与加载。
- SPK、Chebyshev/Kepler 式、恒星及自定义星历源的底层管线。
- 供调用方共享的全局星历运行时。
- 全局目录/缓存路径，其中源描述符是事实来源，缓存条目可被驱逐/重新加载。
- 方法优先级选择，包括针对特定目标的覆盖。
- 带权重的缓存驱逐与 singleflight 加载。
- 自定义星历源注册与生命周期测试。
- 视位置辅助函数，支持光行时、光行差、太阳引力偏转、岁差与章动。
- 供用户自定义图表结构使用的最小 `void*` 帧运行时管线。

## 0.0.1 非目标

当前 MVP 刻意避免构建固定的下游应用模型：

- 星历源路径是显式的，暂无默认系统扫描。
- 完整星历数据集不内嵌于库中。
- 不规定图表/应用结构、学派/分支模型、宫位系统模型或领域扩展注册表。
- 小天体与小行星取决于调用方提供的源文件。
- 运行时不持有类型化的产物（artifact），也不自动连接步骤输出。调用方拥有图表/中间数据与步骤包装器。

## 构建

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

仅构建 bare-chart 管线示例：

```sh
cmake --build build --target example_bare_chart_pipeline
```

## 快速上手：全局星历运行时

运行时初始化一次，然后将显式源路径添加到全局目录。目录记忆源描述符；缓存仅存储已加载/已计算的区块，可在不丢失源路径信息的前提下驱逐条目。

```cpp
#include "taiyin/body_id.h"
#include "taiyin/runtime/taiyin_runtime.h"

using namespace taiyin::internal;
using namespace taiyin::runtime;

EphemerisRuntimeConfig config;
config.cache_max_bytes = 512 * 1024 * 1024;
initialize_global_ephemeris_runtime(config);

add_global_ephemeris_source_path("/path/to/data_integrated_opm4");

EphemerisRequest request;
request.target_id = TAIYIN_BODY_MERCURY_BARYCENTER;
request.center_id = TAIYIN_BODY_SUN;
request.frame = EphemerisFrame::IcrfJ2000Equatorial;
request.jd_tdb = 2460310.500800740905;

EphemerisResult result;
if (eval_global_ephemeris_state(request, &result)) {
    // result.state.position_au
    // result.state.velocity_au_per_day
    // result.descriptor.method_id
    // result.cache_hit
}
```

## 快速上手：管线（Pipeline）

管线有意保持极简。它仅在每个步骤传递三个由调用方持有的指针：

```cpp
struct PipelineFrame {
    void* chart;
    void* scratch;
    void* user_data;
};
```

一个步骤是一个函数指针：

```cpp
typedef bool (*PipelineStepFn)(PipelineFrame* frame, void* step_data);
```

推荐模式：

1. 定义你自己的图表结构体。
2. 定义你自己的中间数据结构体。
3. 编写小型包装步骤，将 `frame->chart`、`frame->scratch` 和 `step_data` 转型回你的类型化数据。
4. 在包装器内部保持实际数学/运行时调用的强类型。

完整示例：

```text
examples/bare_chart_pipeline.cpp
```

用显式 OPM4 源路径运行：

```sh
./build/example_bare_chart_pipeline /path/to/data_integrated_opm4
```

或：

```sh
TAIYIN_OPM4_ROOT=/path/to/data_integrated_opm4 ./build/example_bare_chart_pipeline
```

该示例尝试构建一个最小的用户自定义图表，包含：

```text
水星、金星、火星、木星、土星、天王星、海王星、冥王星、月球
```

调用方源数据中不可用的天体将以警告形式跳过，使示例在部分本地数据集上仍可用。

示例演示如下流程：

```text
OPM4 源路径
        |
        v
全局星历运行时
        |
        v
PipelineFrame(chart/scratch)
        |
        +--> eval_bodies_step
        +--> project_longitudes_step
        +--> write_chart_step
        |
        v
用户自定义 BareChart
```

本示例有意不是一个完整的星盘计算。它演示的是源驱动的运行时求值模式与用户拥有的管线模式。生产级星盘代码可以在不改动管线运行器的前提下添加视位置修正、宫位、相位、恒星、小行星或学派特定步骤。

## 架构

核心运行时以 源/目录/缓存 为导向：

```text
显式源路径 / 自定义描述符
        |
        v
EphemerisBlockCatalog（星历区块目录）
        |
        v
EphemerisService（星历服务）
   |                 |
   v                 v
优先级选择         EphemerisBlockCache（星历区块缓存）
                   带权重驱逐
                   singleflight 加载
        |
        v
EphemerisResult（星历结果）
        |
        v
用户代码或 PipelineFrame(chart/scratch/user_data)
```

关键规则：源文件与描述符是事实来源。缓存驱逐不应丧失重新加载源驱动区块的能力。

## 精度检查

测试套件包含针对视位置 OPM4 路径的 Swiss Ephemeris（`swisseph`）对照覆盖。在当前私有 OPM4 数据集与 `swisseph` 对照设置下，已测试的主行星与月球在选定历元上与 `swisseph` 视位置匹配约 `0.0001` 至 `0.0013` 角秒。

该数值是针对已测试天体、历元、标志与数据版本的对照测试结果，不应理解为对所有源文件、小行星、历元或调用方自定义管线的普适保证。

## 测试

重要测试组包括：

- `test_global_ephemeris_runtime` — 全局运行时初始化、显式源路径、目录/缓存访问。
- `test_ephemeris_mvp_end_to_end` — 源/目录/缓存/运行时 MVP 行为。
- `test_custom_source_lifecycle` — 自定义源注销/删除/缓存命中/重新加载行为。
- `test_pipeline` — 最小管线运行器机制。
- `test_pipeline_bare_chart` — 模拟多方法 bare chart 管线。
- `test_pipeline_opm4_vs_swiss` — OPM4 驱动的视位置管线对照 `swisseph`。
- `test_apparent_opm4_vs_swiss` — 视位置 OPM4 直接对照 `swisseph`。

对照与外部数据测试是可选的。它们会在相关环境变量未设置时自动跳过，例如：

```text
TAIYIN_DE441_PATH
TAIYIN_OPM4_ROOT
TAIYIN_SWISS_PYTHON
TAIYIN_SWISS_EPHE
TAIYIN_VSOP87_MERCURY_PATH
TAIYIN_MER404_TS_PATH
TAIYIN_MAIN_BELT_ASTEROIDS_SPK_PATH
TAIYIN_NEAR_EARTH_ASTEROIDS_SPK_PATH
TAIYIN_JUPITER_SATELLITES_SPK_PATH
TAIYIN_SATURN_SATELLITES_SPK_PATH
```

运行全部测试：

```sh
ctest --test-dir build --output-on-failure
```

## 许可证

Copyright 2026 RedSC1.

本项目使用 [Apache License 2.0](LICENSE) 许可证。
