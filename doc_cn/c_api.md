# C ABI

状态：当前

Taiyin 提供带版本号的 C99 ABI，供 Dart FFI、Python、JavaScript/Node.js、
Rust 等绑定层使用。统一入口为：

```c
#include <taiyin/c/taiyin.h>
```

在支持版本化 SONAME 的平台，安装后的动态库名为 `taiyin`（Linux 为
`libtaiyin.so`，macOS 为 `libtaiyin.dylib`）。Windows 的 DLL 和 import
library 名称带 ABI 号，例如 `taiyin-10.dll` 和 `taiyin-10.lib`。动态加载时先检查
`taiyin_get_c_abi_version()`；`taiyin_get_library_version()` 返回与 ABI
版本独立的库语义版本，当前预发布版本为 `1.0.0-beta.5`；
`taiyin_get_library_codename()` 返回大版本代号，Taiyin `1.x.x` 的代号为
**Singularity**。版本和代号字符串拥有静态库生命周期，调用方不得释放；
`taiyin_get_capabilities()` 可查询当前库包含的位置、事件、日食、占星和
其他功能模块或细分能力。其中 `TAIYIN_CAPABILITY_SPLIT_TIME` 表示当前库
导出了 split Julian Date 时间 API。

八字是可选的中国术数扩展，故意不放进统一头文件。只有术数 policy gate 和
BaZi 子模块选项同时开启时才会构建：

```sh
cmake -S . -B build \
  -DTAIYIN_BUILD_CHINESE_METAPHYSICS_EXTENSIONS=ON \
  -DTAIYIN_BUILD_BAZI_EXTENSION=ON
```

调用方再显式包含：

```c
#include <taiyin/c/bazi.h>
```

启用后才会安装该头、导出 `taiyin_bazi_*` 并设置
`TAIYIN_CAPABILITY_BAZI`。关闭时三者都不存在；绑定层在选择不同构建产物时，
应使用 capability 位判断功能。

`taiyin_format_ephemeris_diagnostic()` 可生成稳定的单行诊断文本。先用空
buffer 和零容量查询包含结尾 NUL 在内的所需字节数，再传入调用方持有的
buffer。

## 构建与安装

默认构建仍提供 legacy aggregate `taiyin_c` target。模块化构建会生成一个基础动态库，
仅为已启用扩展生成独立动态库：

```sh
cmake -S . -B build-modular \
  -DTAIYIN_BUILD_MODULAR_C_API=ON \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-modular
cmake --install build-modular --prefix /your/prefix
```

模块化安装始终包含 `taiyin`，其中合并了 core、astrology、Chinese calendar 与
Ganzhi 的 C API 和 native 实现。启用 BaZi 时才会额外安装唯一可选的
`taiyin_bazi`；因此 BaZi 发布包恰好包含两个 shared library：`taiyin` 和
`taiyin_bazi`。所有启用依赖都会安装在同一目录；macOS 与 ELF 平台使用可重定位
loader path，Windows 按 CMake 常规 runtime/archive 目录安装 DLL 与 import library。

模块化调用方应先加载 `taiyin`，再加载扩展库；同一进程不要把 legacy aggregate
和模块化库混用。基础库会导出可选扩展所需的 native implementation symbol；该 C++
链接仅用于同一套 Taiyin 构建内部，不构成对第三方稳定的 C++ ABI。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target taiyin_c
cmake --install build --prefix /your/prefix
```

安装内容包括当前构建已启用的 `include/taiyin/c/` 头文件和动态库。源码树
构建还提供 `taiyin_c_static` target，供需要静态链接的应用使用；该 target 会传递
`TAIYIN_C_STATIC` 及内部静态依赖。安装结果还包括 `LICENSE` 和 `NOTICE`。
头文件要求 C99 或更高版本，也可直接被 C++ 包含。

安装结果和 CPack 二进制压缩包有意不携带 OPM2、TSC1、TLL1 或其他 runtime 数据包。
部署时应把选定的数据根目录放在应用旁，或在运行时注册单独分发的数据文件。

C ABI 当前版本为 `10`。版本 3 将标量儒略日参数和结果时间字段替换为
`taiyin_split_julian_date`；版本 5 扩展了 `taiyin_bazi_context_config`，加入
起运与大运策略；版本 7 为行星中天搜索加入 flags 参数，并确立 observed flags
“低 32 位 position / 高 32 位 option”的分层。版本 8 将三态时间尺度策略替换为
显式 UTC 超范围估算开关，并将 `*_ut` 入口固定为 UT1 语义。版本 9 将返回状态的
函数统一为打包 status 与 result flags 的 `taiyin_call_result`；版本 10 增加
历史历法归属年以及紫微流月的物理/有效月份字段。使用更早 ABI 头文件构建的调用方必须
重新编译。库版本遵循语义化版本：兼容性新增不改变 ABI major；
删除或改变已有 C symbol / struct contract 时必须提升 ABI major。
共享库实体文件独立使用 `ABI.0.0` 版本号，避免安装新 ABI 时覆盖旧 SONAME
指向的二进制；Windows 不把 `VERSION` 编入 DLL 文件名，因此改用
`taiyin-<ABI>` 输出名。包的语义版本不受此规则影响。C++ API
属于实现层接口，不承诺稳定的二进制 ABI。代号只标识语义版本的大版本，
不参与版本比较或 ABI 兼容性判断。

## 生命周期与所有权

- `taiyin_context_create()` 创建不透明 context，使用
  `taiyin_context_destroy()` 释放。
- 字符串、路径数组、输出缓冲区和回调 `user_data` 默认都归调用方所有。
- 全局运行时、星表和模型注册应在并发计算开始前完成。
- `taiyin_context_set_deflectors()` 会把输入记录复制进 context；函数返回后，
  调用方可以释放原数组。
- 调用方传入的顶层结果或选项结构体只要带 `struct_size`，使用前就必须调用对应
  `_init()`；数组中的元素记录由库负责初始化。
- 数组接口采用 `capacity + out_count`。声明允许时，可用空指针和零容量先查询数量。
- 调用方只有在返回 `TAIYIN_STATUS_OK` 时才能使用结果；结构化结果包装器不会写回
  失败的临时结果。传入 diagnostic 时，成功和失败都会更新诊断信息。

## 并发与自定义回调

互不修改的 context 和普通计算可并发使用。全局运行时与注册表修改属于初始化阶段。

自定义星体、Ayanamsha 和分宫制均可注册 C 回调，并携带 `void* user_data`。
注册后必须在回调或 `user_data` 失效前显式注销或清理，并且回调必须能够承受
并发调用。异常不能跨越 C ABI。模块化构建中，
占星功能仍位于基础 `taiyin` 动态库中，因此
`taiyin_astrology_module_shutdown()` 返回 `TAIYIN_ERROR_UNSUPPORTED`。

## Context 配置

`taiyin_context` 持有位置和事件 API 使用的计算策略。除了观测者位置、大气、
显式 UTC 回退开关和星历路线，C ABI 还提供：

- `taiyin_astro_model_config`：选择 TDB、岁差、章动、黄赤交角和 frame route；
- `taiyin_apparent_config`：选择视位置修正、输出参考系、光行时迭代、光行差、
  引力偏折和导数步长策略；
- 地心、显式 offset、简化地表观测者和依赖 EOP 的精密地表观测者设置；
- 天球极偏移、太阳或自定义偏折体，以及 Shapiro delay。

配置结构体需先调用对应 `_init()`。Topocentric 状态和 deflector 使用独立 setter；
修改 `taiyin_apparent_config` 时不会清掉这两类已安装状态。

未知的视位置 flag、Delta-T 模型或星历族 ID 会返回非法参数，不会静默选择回退。

UTC 和 UT1 入口的语义是固定的。`*_ut` 将 split JD 解释为 UT1，使用
context 的 Delta-T 模型得到 TT/TDB，不查 EOP。`*_utc` 将民用日历值解释为
UTC，默认要求闰秒和 EOP 覆盖该日期。EOP 缺失或超出范围返回
`TAIYIN_TIME_ERROR_EOP_OUT_OF_RANGE`；闰秒数据不可用返回
`TAIYIN_TIME_ERROR_LEAP_SECOND_UNAVAILABLE`。诊断中的 fallback reason 可继续区分
“未加载 EOP”和“EOP 超出范围”。

明确接受低精度回退的应用可在 context 初始化时开启：

```c
taiyin_context_set_allow_utc_out_of_range_estimate(context, 1);
```

当精密 UTC 解析不可用时，该回退会将输入的民用时间近似解释为 UT1，再应用
已配置的 Delta-T 模型。该开关不会改变任何 `*_ut` 函数的语义或路线。

## 拆分儒略日

原有以单个绝对 `double` JD 作为输入或输出的时间函数继续保留，适合普通计算和
兼容已有调用方。现代历元附近，这种表示的相邻数值约相差 40 微秒。

需要保留亚微秒时间坐标的绑定应使用 `taiyin_split_julian_date`。它把整数日和
归一化到 `[0, 1)` 的日内小数分开保存。配套接口覆盖 calendar 往返、规范化、
加秒与求差、UTC/TAI/TT/UT1 转换、两种 TDB 模型，以及完整的精密或估算时间尺度
结果结构。

```c
taiyin_split_julian_date utc;
taiyin_split_julian_date tt;

taiyin_split_julian_date_from_parts(2460409, 0.25, &utc);
taiyin_utc_to_tt_split(&utc, 37.0, &tt);
```

split 路径在应用时间偏移时不会先合并成单个 JD。TT/TDB 模型会用近似绝对 JD
求缓慢变化的修正量，再把修正量加回拆分坐标，因此不会丢掉输入低位。
`taiyin_split_julian_date_to_double()` 会主动回到单 `double`，属于有意降低精度。

位置、状态、事件搜索、日月食、掩星、可见性和占星等物理计算入口，以及它们的
时间结果字段，统一使用 `taiyin_split_julian_date`。旧的单 `double` JD 接口只在
纯时间换算工具中保留；星历文件覆盖范围、目录参考历元和 EOP 表节点仍以
`double` 存储，它们是数据集元数据，不是计算过程中的时间坐标。

## 功能范围

统一头文件覆盖：

- 运行时、context、时间、位置/状态、恒星、观测位置、可见性、现象和真太阳时；
- 黄经、相位、相位角、逆行、天体—天体/天体—恒星最小角距、大距、轨道事件和凌日搜索；
- 日月食、路线产品、月掩星/月掩行星和晨昏初见；
- Ayanamsha、恒星黄道、分宫、月球交点与远近地点；
- 中国农历冬至岁计算和公农历双向转换；
- 自定义星体、Ayanamsha 和分宫制回调注册。

绑定生成器也可以只包含 `taiyin/c/` 下对应模块的头文件。

恒星位置入口统一使用 64 位 flags。当前 native position flags 位于低
32 位；尚未支持的高位会被明确拒绝而不是静默截断，为后续恒星专属选项保留
ABI 空间。

自定义 Kepler 目标继续使用现有 Kepler/TKC1 文件格式，通过
`taiyin_runtime_add_source_path()` 或 runtime 配置中的 source paths 加载。
恒星自行数据可使用 TSC1 内存、TSC1 文件或 TSF1 文件。绑定层可以维护自己的
名称到 ID 映射；C ABI 不暴露内部进程级天体名称表。
