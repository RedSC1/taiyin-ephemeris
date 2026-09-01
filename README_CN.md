# Taiyin Ephemeris（太阴星历）

[English README](README.md) · [文档](doc_cn/index.md) · [路线图](ROADMAP.md)

> **预发布说明：** 当前的 `1.0.0-beta.7` 是开发中的预发布版本，并非最终稳定发布。
> 公开 API、随包数据边界、文档和计划中的语言绑定在首个稳定版发布前仍可能调整。

Taiyin Ephemeris 是一个可嵌入的天文库，用于计算太阳系天体位置、观测坐标、
可见性、天文事件、日月食、掩星、固定星、历法和真太阳时。它使用 C++ 编写，
并为应用程序和 FFI 绑定提供带版本号的 C99 ABI。
预发布的 [Python](https://github.com/RedSC1/py-ephemeris) 与
[Dart](https://github.com/RedSC1/dart-ephemeris) 包已经可用；JavaScript wrapper
仍在开发中。

> **民用时间限制：** Taiyin 的 native 历法和紫微层只使用固定 UTC offset（或显式
> 地方平太阳时经线），不内置命名时区数据库。对于实行夏令时/冬令时切换的国家和地区，
> 若朔或节气靠近切换时刻，按当地实际法定时间得到的农历归日标签可能错误。天文瞬间本身
> 仍有效，但不应将 native 返回的农历日期视为当地法定时间的复现。

该库使用 OPM2 星历格式，以及内置的半解析星历。典型主星 OPM2
产品相对其源 DE441 或 DE442 星历的压缩/还原差异约为 **0.001 角秒**。这描述的是
OPM2 状态压缩误差，而不是最终 apparent 或站心结果；后者还取决于时间尺度、观测者
几何和所选改正模型。

仓库为太阳、月球、水星、金星、EMB、火星、木星、土星、天王星、海王星和冥王星
随包提供两套主星 OPM2 产品：

- DE441 来源的 600 年产品，覆盖 **1800-01-01 至 2400-01-01**；
- DE442 来源的全覆盖产品，覆盖 DE442 的公共源区间，约为
  **1550 至 2650**。

可选的 **全量 DE441 OPM2 数据包**不提交进源码仓库，也不塞入各语言 wheel，
而是作为一个独立的 GitHub Release 资产发布。它包含 51 个约 600 年分片、
561 个最终 OPM2 文件和可迁移的 `index.opc`；公共产品区间为
JD `-3096455.499990447` 至 `7996074.500009106`，约 **30,369.69 儒略年**。
ZIP 大约 **85.3 MiB**。

全量精度验证对 51 个分片的每个拟合段各采样 512 点，共使用
**343,522,304 个地心角误差样本**与 DE441 对比。各天体中最大的“最差分片
p99”为 **0.001755 角秒**，全部采样点中观测到的最大误差为
**0.003545 角秒**，两者均出现在金星。解压后的发布包还通过了覆盖全部分片
接缝附近的 1,122 次 runtime 求值。下载、校验、加载方式和逐天体结果见
[`doc_cn/ephemeris_data_packages.md`](doc_cn/ephemeris_data_packages.md)。

runtime 会识别两套产品的 source identity；在覆盖重叠区间，AUTO 默认选择
DE442 来源 OPM2。需要复现旧数据结果时，应用仍可显式选择 DE441 产品，
或调整 provider 内部的来源优先级。

Taiyin 还可以读取当前 NASA/JPL SPK 文件，包括 DE441 以及小行星和其他目标的
SPK 数据。这里的“当前”是指采用与 DE441 时代数据相配套的 Delta T 和时间尺度
处理路线。Taiyin 没有实现 DE431 年代历史星历表所用的潮汐加速度补偿参数，因此
不应将它宣传为复现 DE431 历史星历的实现。

在没有更高优先级 SPK 或 OPM2 数据时，内置半解析 fallback 可覆盖约公历
**-3000 至 +3000**。其模型和验证细节见
[`doc_cn/semi_analytic_ephemeris.md`](doc_cn/semi_analytic_ephemeris.md)。

### 内置卫星 fallback 的范围

无数据文件路线还在各自声明的验证区间内提供 Phobos、Deimos、木星四大卫星、
冥王星系统和 Triton。这些都是紧凑的 fallback 模型，**不是高精度卫星星历**：
相对位置误差从部分残差表的几十公里，到紧凑 Galilean 模型的数百公里不等；按质量
加权后的行星中心修正通常小得多。需要精确卫星测量或卫星现象时，应使用对应的 SPK
或 OPM2 数据包。

1.0 没有注册土星或天王星的无数据文件卫星路线。这两个主卫星系统需要专门验证的
理论或外部卫星数据包；除非调用者显式开启对应 fallback，Taiyin 不会把请求的物理
行星静默替换为其系统质心。

## 可以做什么

- **天体位置：** apparent、astrometric、topocentric、赤道/黄道、球坐标、
  笛卡尔和速度输出。
- **观测计算：** UTC/UT 路线、地平坐标、大气和折射、leap second、EOP、
  polar motion 和 celestial pole offset。
- **事件与可见性：** 日出日落、晨昏蒙影、过中天、月相、角距事件、行星留点、
  偕日可见性和轨道事件。
- **日月食与掩星：** 全球和地方日食、月食、月掩、接触时刻、circumstances
  和可见性摘要。
- **固定星：** TSC1/TSF1 星表、alias、astrometry、自行、视差、观测位置和
  地平坐标。
- **历法和占星扩展：** 中国历法 primitive、恒星黄道、ayanamsha、宫位、
  月球交点/远地点，以及可选的 BaZi/Ganzhi 支持。
- **应用集成：** 带 opaque context、diagnostic、capability 查询和 FFI 友好
  ownership 规则的版本化 C99 API。C ABI version 10 是应用和绑定的兼容边界；
  C++ API 不承诺稳定二进制 ABI。

### 1.0 观测者范围

1.0 只支持地球表面的站心坐标、地平坐标和大气折射。非地球 `observer_id`
请求 topocentric 计算时会返回 `TAIYIN_ERROR_UNSUPPORTED`。固定星位置、固定星
观测和 body-star 搜索同样要求 observer 为地球。不请求站心坐标的太阳系天体
相对位置计算不受此限制，只要所选星历 route 能提供需要的状态即可。

## 数据来源

Taiyin 将 runtime 与大型星历文件分离。你可以使用打包数据、显式注册 data root，
或添加 SPK kernel、固定星 catalog 等外部数据源。runtime 会根据目标和时间范围
选择可用数据源，而不要求把源数据嵌入应用程序二进制文件。

仓库包含 DE441 600 年和 DE442 全覆盖主星 OPM2 产品、选定的小行星
OPM2 数据，以及紧凑的行星形心修正数据。全量 DE441 OPM2 是可选的 Release
下载，不属于源码仓库或语言 wheel 的默认载荷。其他外部数据集仍可单独选用；
数据包安装与验证见
[`doc_cn/ephemeris_data_packages.md`](doc_cn/ephemeris_data_packages.md)，
覆盖范围、数据包边界和已知限制见
[`doc_cn/current_limitations.md`](doc_cn/current_limitations.md)。

## 快速开始

构建库并运行测试：

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

### CMake presets

`CMakePresets.json` 让本地与 CI 使用同一套工具链选择。它要求 CMake 3.21 或更高
版本；直接使用普通命令行配置时，项目本身仍支持声明的 CMake 3.16 最低版本。
源码基线为 C++11（C API 为 C99）；编译器由 preset、命令行或 toolchain file 在
执行 `CMakeLists.txt` 前选定。

`modular-bazi` 是常用的原生验证构建：

```sh
cmake --preset modular-bazi
cmake --build --preset modular-bazi
ctest --preset modular-bazi
```

`linux-gcc`、`linux-clang`、`macos-appleclang`、`windows-mingw-gcc`、
`windows-llvm-mingw-arm64` 与 `windows-msvc` 分别选择相应主机编译器。Windows
x64 推荐 MinGW-w64 GCC，Windows ARM64 推荐 llvm-mingw Clang/LLD，二者均与
发布的 Python wheel 使用同一工具链；Visual Studio 2022/MSVC 继续由 CI 做兼容
性验证，但属于尽力支持，不作为 Windows wheel 发布的阻塞条件。`android-arm64`
使用 NDK toolchain，因此需要设置 `ANDROID_NDK`：

```sh
cmake --preset android-arm64
cmake --build --preset android-arm64
```

维护者专用的生成器、基准和实验工具默认不参与构建；私有开发树需要它们时显式加上
`-DTAIYIN_BUILD_MAINTAINER_TOOLS=ON`。公开源码快照不包含这些工具。

最小 C++ 调用示例：

```cpp
#include "taiyin/body_id.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

using namespace taiyin;
using namespace taiyin::runtime;

EphemerisRuntimeConfig config;
config.data_root = "/path/to/taiyin/data";
if (!initialize_global_ephemeris_runtime(config)) {
    return 1;
}

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

球坐标输出时，`positions[i][0..2]` 是经度、纬度和距离。加入
`TAIYIN_NATIVE_POSITION_SPEED` 后，`positions[i][3..5]` 输出对应速度。
稳定 C 接口见 [`doc_cn/c_api.md`](doc_cn/c_api.md)；C++ 的 typed API 见相应头文件。

## 构建结构与可选扩展

源码树将 `taiyin_ephemeris`、`taiyin_astrology_extension` 和 Chinese Calendar 的
静态 target 分开，以明确内部依赖。占星、中国历法和 Ganzhi 都是**内置能力**：它们
始终会构建，也始终被链接进基础 C ABI 库（模块化构建的 `taiyin`，或 legacy aggregate
构建的 `taiyin_c`）。因此 `taiyin_astrology_extension` 只是源码树里的 C++ 链接 target，
不是可单独选择的包或 shared library。

直接使用源码树 C++ target 时，核心天文链接 `taiyin_ephemeris`；调用恒星黄道、宫位或
月球点 C++ 入口时还需链接 `taiyin_astrology_extension`。C++ ABI 不作为稳定分发接口；
应用和 FFI 应使用基础 C ABI。

中国术数扩展默认关闭。未设置
`TAIYIN_BUILD_CHINESE_METAPHYSICS_EXTENSIONS=ON` 时，CMake 不会构建八字或
紫微代码、target、测试、C ABI symbol 或头文件。两个独立模块分别通过
`TAIYIN_BUILD_BAZI_EXTENSION=ON` 和 `TAIYIN_BUILD_ZIWEI_EXTENSION=ON`
启用，不需要额外编译器工具链。

紫微随附的 TOML catalog 是不可变的基础规则库。用户 JSON module 可以用一个
名称同时增加安星、亮度、四化、流曜、命主/身主 option，但不能覆盖或删除 TOML
已有 option。按 label 删除用户 module 时，会一次清除该名称贡献的全部内容；它只
影响之后创建的 context，已经创建的 context 继续持有原来的不可变快照。详见
[`ziwei_astrology/docs/api.md`](ziwei_astrology/docs/api.md)。

Ganzhi 属于中国历法的年、月、日、时纪法，固定包含在 Chinese Calendar 实现中，
不属于中国术数解释模块。未来的奇门、六壬等扩展会在同一术数门下使用各自独立的
opt-in 选项。

扩展相关文档见 [`doc_cn/index.md`](doc_cn/index.md)，包括
[恒星黄道](doc_cn/astrology_sidereal.md)、[宫位](doc_cn/astrology_houses.md)、
[月球点](doc_cn/astrology_lunar_points.md) 和
[中国历法](doc_cn/chinese_calendar.md)。
可选术数层另见[八字扩展](doc_cn/bazi.md)和
[紫微 API](ziwei_astrology/docs/api.md)。

## 文档

- [`doc_cn/index.md`](doc_cn/index.md) — 文档总览和 API 地图。
- [`doc_cn/c_api.md`](doc_cn/c_api.md) — 版本化 C99 ABI 和 FFI 契约。
- [`doc_cn/current_limitations.md`](doc_cn/current_limitations.md) — 覆盖范围、
  数据包和已知行为边界。
- [`doc_cn/bazi.md`](doc_cn/bazi.md) — 可选八字构建、所有权、计算与验证契约。
- [`ziwei_astrology/docs/api.md`](ziwei_astrology/docs/api.md) — 可选紫微规则、
  命盘、流运和可移除的用户 option module。
- [`doc_cn/event_search.md`](doc_cn/event_search.md) — event-search primitive。
- [`doc_cn/solar_visibility.md`](doc_cn/solar_visibility.md) — 太阳升落、twilight、
  transit、快速路线和折射约定。
- [`doc_cn/eclipse_search.md`](doc_cn/eclipse_search.md) — 日月食算法和 API 示例。
- [`doc_cn/occultation_search.md`](doc_cn/occultation_search.md) — 月掩搜索和地方
  可见性摘要。
- [`doc_cn/semi_analytic_ephemeris.md`](doc_cn/semi_analytic_ephemeris.md) — 内置
  fallback 模型和验证。
- [`doc_cn/ephemeris_runtime_architecture.md`](doc_cn/ephemeris_runtime_architecture.md)
  — 更底层的 runtime 设计。

## 许可证

Copyright 2026 RedSC1.

Taiyin 原创代码和文档使用 [Mozilla Public License 2.0](LICENSE) 许可证。
第三方代码和随仓库分发的数据继续遵守 [`NOTICE`](NOTICE) 及相邻 provenance 文件中
记录的许可证和条款。
