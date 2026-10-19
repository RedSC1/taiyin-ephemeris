# C++ → Wasm 最小链路实验

这是独立的可行性验证，不是已发布的 Web SDK。现有原生接口和数据格式保持
兼容；本分支另新增八字反查 C++／C ABI（见下文），并提取共用排盘时钟，
不重写天文或历法算法。Dart、JS/TS 和 Python 绑定在各自开发仓库适配。
本分支没有更新发布版本、打 tag 或生成发布包。

## 扩展桥接与八字反查

`astronomy_bridge.generated.inc` 为有限 C ABI 函数／结构生成逐字段传输代码；
`astronomy_extra.inc` 处理变长恒星／站心观测／数据源信息，
`astronomy_callbacks.inc` 提供同步自定义天体、岁差值和分宫回调。
生成器 `generate-astronomy.mjs` 随本仓库保存，从 `include/taiyin/c/` 的公开
头文件及有限操作清单生成桥接代码和协议指纹，不依赖旁边的 JS 仓库。
修改相关 C ABI 后运行：

```sh
node experiments/wasm/generate-astronomy.mjs
node experiments/wasm/generate-astronomy.mjs --check
node experiments/wasm/test_generator.mjs
# 或配置实验 CMake 后：
cmake --build build-wasm-native --target taiyin_check_astronomy_bridge
```

`--check` 只比较，不写文件；未重新生成的字段或协议变化会令检查失败。
Node 仅用于生成／显式校验，不是普通原生构建的必需依赖。
协议指纹变化后，消费端也必须同步其字段／操作清单；版本不一致应拒绝调用。
默认仍只支持 `web`；JS 构建可显式选 `TAIYIN_WEB_ENVIRONMENT=web,node`。
运行时 flags 保持独立，不把 64 位整数压成 JS Number；int64 数据输入限定为
JS 安全整数范围，uint64 使用两个 uint32 字传输。容量、ID、flags 字和
其他整数在转换前检查有限性、整数性及目标类型范围；不会把小数静默截断。
逐字节传输的字符串每项最多 1 MiB，字节须为 1–255；拒绝嵌入 NUL，
避免原生 C 字符串只读取前缀。UTF-8 中文等非 ASCII 字节保持原样。
自定义天体句柄使用桥接层独立的所有权代号；旧句柄不能注销后来同 ID 的
回调，runtime 重置也不会复用代号。回调错误会携带对应状态、目标和 TDB 历元。
首次初始化会保留 setup 阶段已注册的回调及其所有权；只有实际重置才清除它们，
与原生 C API 的初始化生命周期一致。

八字反查新增 `taiyin_bazi_reverse_lookup_at_ut1`，返回完整四柱匹配的物理 UT1
半开区间；三种子时规则、节气／历史节界和太阳时换算复用现有实现。
详见[八字反查接口](../../bazi_astrology/reverse_lookup.md)。八字不依赖紫微动态库；
原紫微时钟符号保留为共用时钟的转发入口。现有 Web 历法传输补齐
`historical_year`，使用原先保留的 lunar slot 7／month slot 9，不改变缓冲区布局。

本机测量结果与限制见 [RESULTS.md](RESULTS.md)。

## 构建

需要 CMake、Node.js（含全局 `atob`）和官方 Emscripten SDK。本次工具链固定
为 6.0.12。激活 SDK 环境后，在仓库根目录执行：

```sh
cmake -S experiments/wasm -B build-wasm-native -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm-native --target taiyin_probe -j 10
emcmake cmake -S experiments/wasm -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm --target taiyin_probe -j 10
node experiments/wasm/compare.mjs \
  build-wasm/taiyin_probe.mjs build-wasm-native/taiyin_probe \
  data/ephemerides/opm2/major-bodies/de442-full build-wasm/results.json
```

回调桥接回归测试不依赖 JS SDK：

```sh
emcmake cmake -S experiments/wasm -B build-wasm -DCMAKE_BUILD_TYPE=Release \
  -DTAIYIN_WEB_ENVIRONMENT=web,node
cmake --build build-wasm --target taiyin_web -j 10
node experiments/wasm/test_callbacks.mjs build-wasm/taiyin_web.js
node experiments/wasm/test_transport.mjs build-wasm/taiyin_web.js
```

后一个测试直接向日月食、八字、紫微及固定缓冲区接口传入 NaN、无穷、
越界／非整数 JD、容量、flags 和 ID，以及 NUL 字符串／非法字节，要求返回
invalid argument，并验证错误后的正常调用。原生整数边界测试可单独执行：

```sh
cmake --build build-wasm-native --target taiyin_transport_test -j 10
./build-wasm-native/taiyin_transport_test
```

两者均使用 Release `-O3`、关闭浮点乘加收缩，不启用 `fast-math`；但编译器和
数学库仍不同，不能预设逐位一致。Wasm 使用单线程、wasm32、BigInt i64 接口、
可增长线性内存和 C++ 异常支持。当前导出的实验接口仅覆盖本实验所需功能，
不是完整的生产 C ABI 导出列表。

## 加载数据

```js
import createModule from './taiyin_probe.mjs';
import {createExperimentRuntime} from './runtime.mjs';

const module = await createModule();
const runtime = createExperimentRuntime(module);
const response = await fetch('./sun.opm2');
if (!response.ok) throw new Error(`HTTP ${response.status}`);
runtime.loadOpm2Bytes('sun.opm2', new Uint8Array(await response.arrayBuffer()));
// 或 runtime.loadOpm2Base64('sun.opm2', base64String)。同名不能重复加载。
// 完整地心计算还需相应的 EMB、月球等组成文件，不能只加载 sun.opm2。
```

数据链路：Base64（可选）→ Uint8Array → MEMFS 文件 → 原有 source-path
注册 → 原有 OPM2 parser/catalog/route。文件名及原 OPM2 头保留来源选择语义。
整套精密星历建议以二进制文件传输，避免 Base64 带来的体积开销。

注册和初始化是 setup-time 操作，应在开始计算前完成。文件必须在 runtime 使用
期间保留；本实验不提供卸载、覆盖或无拷贝的内存数据源接口。只有显式调用
`createContext(1)` 才使用 OPM2-only；`3` 是半解析-only。

## 测什么

- 同一份 C++ 驱动和相同 C ABI 配置：10 个目标、8 个历元、3 种位置模式。
- 默认视位置、速度、几何 XYZ；远期帧转换及 OPM2-only 覆盖外错误。
- 三个农历日期（含 2033 闰十一与其后的正常十二月）、2026 夏至、2024 日食
  食甚和全球接触时刻。时间差使用 Split-JD 分开计算。
- 11 个 OPM2 文件的首／末及内部接缝，包含 split fraction ±1e-10 日偏移；
  正常的覆盖外拒绝也要求两端一致。接缝测试以 TDB 输入、固定 J2000 几何帧
  比较；该项 TT=TDB 只是固定帧几何测试约定，不是时间尺度转换实现。
- 二进制和 Base64 加载、损坏内容、坏文件名、重复加载，以及失败时的
  64 位 CallResult 状态和全部低 32 位标记。
- 冷初始化、1/3/5/7/9/10 目标连续计算、JS→Wasm 调用和结果复制。
- `.wasm`、加载器、外部数据及压缩体积分别统计。

原生对照由脚本在空工作目录运行并清除 `TAIYIN_DATA_ROOT`，防止悄悄读取本机
其他数据。精密档显式注册 11 个文件并强制 OPM2 路由；半解析档不注册外部数据。
这里测的是 Wasm 相对同内核原生构建的变化，不是相对 NASA 观测真值重新定标。

位置基准使用递增历元并保留同一 ctx。两端每项均重复五次取中位数。
结果不得当作跨机器或跨浏览器性能保证。日食性能项先预热、再重复五次取中位数，
不能称为独立冷启动性能；suite 中的单次 `elapsedUs` 不用于主性能结论。

数据加载计时里二进制和 Base64 各加载了不同的一组文件（交替），只能用于本次
初始化分项，不能把两个总耗时直接当作同字节数的公平吞吐比较。

## 浏览器

在仓库根目录执行 `node experiments/wasm/serve.mjs`（只提供实验所需文件），
打开 `http://127.0.0.1:8765/experiments/wasm/browser.html`。测试页走真实浏览器的
Wasm 与虚拟文件系统；不需要 pthread 或跨源隔离响应头。

首次实验先复用现有 DE442 全覆盖文件，尚未生成 1900～2100 裁剪包或小行星
Web 数据包。`taiyin_probe` 是独立测量目标，不代表整个 Dart Web API；后续
Dart 适配使用下面的 `taiyin_web` 目标。Web Worker 尚未接入。

## Dart Web 初步适配

后续增加了独立的 `taiyin_web` 目标（`web_bridge.cpp`）：

```sh
cmake --build build-wasm --target taiyin_web -j 10
```

它输出 classic-script 加载器 `taiyin_web.js` 与相邻的 `.wasm`，供 Dart 的
异步初始化使用。与上面的 `taiyin_probe` 测量目标分开，不把实验 suite 当成公开
计算 API。bridge v1 适配初始化、数据注册、上下文、单目标/批量位置、单目标状态、
时间转换、农历/四柱、太阳时、日月食、八字、紫微和部分配置。
32 位状态和结果标记分别传递，Split-JD 分量保持分开；不改变原生 C ABI。

Dart 仓库分支 `codex/wasm-web-backend` 已用同一调用场景通过 Dart2JS 与
Dart2Wasm 浏览器对照（2026-10-18 共 1,255 条数值记录，包含时间、农历、批量调用、
日月食、八字、紫微和失败诊断；最大混合分量差约 `6.49e-9`，不代表新的绝对精度界限）。
位置、时间、农历、日月食及扩展通过共用 Dart 接口／编排层对接 FFI/Wasm。
农历包含历史改历、三种历法模式、
两类日界和三种子时规则。原生三包回归 339 通过，2 个可选压力测试跳过。
日月食覆盖全部 39 个公开方法；八字包含三种时钟、起运、司令及自定义神煞回调；
紫微包含本命、五级流运、规则选项／JSON 模块、早晚子时导航、反查与手动／数字／随机起盘。
回调异常保持 Dart 原对象，执行中重入和关闭有防护。尚未发布这些开发分支改动。

此目标把三个 C++ 模块链接到同一 Wasm 实例，借用的 calendar/context 句柄不会跨实例。
默认紫微六个 TOML 文件通过 `--embed-file` 嵌入 `/taiyin/ziwei/rules/`，每个 catalog
只解析一次；已有 context 保留编译后的规则快照。原生生产构建、API、ABI 不变。
Web 必须显式关闭句柄；没有原生 finalizer 的兜底。回归记录中的秒、公里及部分钟表秒
会按测试驱动归一化，因此最大混合差值不能当成角度精度。随机结果只核验合法范围，不做同抽样对比。

适配层还复用原解析器接入了 OPM2、BSP/SPK、TKC1、TKE1 字节数据，以及 EOP、
TLL1 和 TSC1/TSF1 恒星表。各格式的加载职责与路由规则不变；BSP/SPK 不默认
下载或随 Web 包附带，浏览器回归只使用 4 KiB 的合成 SPK 样本。
