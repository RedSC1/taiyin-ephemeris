# 星历运行时架构

文档状态：当前说明
最后审阅：2026-08-12
主要头文件：`include/taiyin/runtime/runtime.h`, `include/taiyin/runtime/ephemeris_engine.h`, `include/taiyin/runtime/native_context.h`, `include/taiyin/internal/ephemeris_segment_cache.h`

本文说明 Taiyin 当前运行时如何发现数据、选择计算路线、加载 segment cache，并完成一次星历计算。

## 数据流

```text
source paths / packaged OPC / custom descriptor
        |
        v
EphemerisBlockCatalog
        |
        v
BodyRegistry
direct bodies + composite fallback rules
        |
        v
NativeCalcContext route_rule_id
        |
        v
EphemerisRouteRuleTable  ----->  EphemerisEngine
                                         |
                                         v
                                  EphemerisSegmentCache
                                         |
                                         v
                             StorageEphemerisBlock -> eval_compiled_ephemeris_block()
```

`EphemerisBlockCatalog` 保存从 OPC、源目录和用户新增 descriptor 发现出来的 descriptor。它是本机数据清单，不是方法优先级策略，也不是 runtime cache。Catalog 内部会为 descriptor 建索引，避免每次请求都线性扫描；索引和 segment cache 的细节见 [Catalog 和 Cache 模型](./catalog_cache_model.md)。

OPC 保存的是 SPK segment metadata，不是不可变的产品身份。加载 OPC 时，runtime 会按当前
文件路径规则重新识别 SPK descriptor 的 source product，因此早于命名 DE/卫星产品的旧
catalog 仍能获得当前的 source priority 与防混用规则。若 discovery 规则本身会改变
descriptor 集合（例如补出 Mars 中心卫星路线），独立的 `OPC_DISCOVERY_VERSION` 会使旧
catalog 失效，再由目录 discovery 重建。版本与 fingerprint 细节见
[OPC Catalog 格式](./opc_catalog_format.md)。

`BodyRegistry` 管星体层路由能力：

```text
direct-capable bodies      从 catalog descriptor 发现
fallback/composite rules   内置规则，例如 Earth/Moon 由 EMB + Moon/Earth 组合
```

Catalog 初始化或新增 descriptor 后，会先在 `BodyRegistry` 里标记 direct-capable bodies，然后注册内置 fallback/composite 规则。这样“某个 body 是否能直接算”和“某个 body 是否需要组合”在进入具体数据选择之前就已经明确。

`EphemerisRouteRuleTable` 管直接数据方法偏好。每条规则有一个数值 priority；priority 越高越先尝试，相同 priority 保留注册顺序。当前 AUTO 默认规则是：

```text
440  JPL DE442 SPK
435  Taiyin DE442-rebuilt OPM2（source id 2）
430  JPL DE441 SPK
420  JPL DE440 SPK
419..398  已识别的历史 JPL DE SPK（DE438 至 DE102）
397..390  已识别的 JPL 卫星 SPK fallback
389  其他 SPK
300  Taiyin prerelease OPM2（source id 1）
290  其他 OPM2
250  内置半解析模型
200  TKC1
100  Kepler file
```

内置 route-rule id 覆盖：

```text
AUTO          按默认优先级自动选择
SPK-only      只走 SPK
OPM2-only     只走 OPM2
semi-analytical-only  只走内置半解析模型
```

用户可以在 setup 阶段注册自己的 route-rule table，并通过 `NativeCalcContext` 选择对应 route-rule id。`NativeCalcContext` 会保存已解析的 table 指针，所以 route rule 应按“初始化后只读”的方式使用；如果需要另一套策略，应注册新的 route-rule id，而不是原地修改一个可能已经被 context 持有的 table。

Custom method 和 custom file method 注册后会进入 catalog，并按传入 priority 插入 AUTO route rule。也就是说用户扩展不需要绕过 runtime：只要 descriptor 的 target/center/method/source 语义正确，它就可以和内置 SPK、OPM2、半解析模型一起参与路由。

`EphemerisEngine` 计算 direct request 时，会解析 method ids，按 route rule 查询 catalog 中匹配 method/route/JD 的 descriptor。选中 descriptor 后，engine 先查 segment cache；cache miss 时加载对应 descriptor bucket，再计算 compiled block。

### Source Product Identity

container format 与 source product 是刻意拆开的：`format` 表示如何加载（如 `SPK`、
`OPM2`），`source_id` 则表示可以安全组合的一套产品。这样 AUTO 组合状态不会把
DE441 的 Earth 和 DE442 的 Moon 静默拼到一起。

对 raw JPL SPK，Taiyin 有限地识别历史 `DE` 系列（DE102 至 DE442）、MAR099、
JUP347/348/349/365、SAT415/441/455–459/480、URA117/182/184、NEP097/098/104/105
和 PLU060。`s`、`xl`、`_part-1` 等后缀仍归同一模型，例如 `nep098_part-2.bsp` 仍是
NEP098。这是文件名约定，不是文件认证；调用者重命名数据，即对这个声明负责。

OPM2 不从文件名推断来源；little-endian header 的 bytes 24–27 存 `uint32 source_id`：

```text
0  未定义；为兼容旧 version-1 文件，运行时映射到 prerelease
1  TAIYIN_PRERELEASE
2  TAIYIN_DE442_REBUILT
```

未来 Taiyin OPM2 产品使用递增 id；在 OPM2-only provider 内较大的 id 优先。未知正
id 仍可使用，但应用若自行安装第三方 id，就应自行确定它们的排序，或按文件显式覆盖。

未识别的 SPK/OPM2 仍能经 generic `SPK-only` / `OPM2-only` 使用，并会在 AUTO 中排在
已识别内置产品之后。命名 DE SPK 规则只把非 DE SPK 作为**卫星辅助路线**：例如
`mar099.bsp` 可与命名 DE 行星 kernel 组合，但组合成功必须实际使用至少一个该命名 DE
source 的 descriptor；独立卫星 kernel 不能伪装成缺失的高优先级 DE。其他命名 DE 永不
作为 auxiliary，因此不会混用 DE441 和 DE442。所有命名 DE 之后，每个已识别卫星产品
还有 exact-source fallback，让 `505 -> 599` 之类直接边可用。OPM2 没有此 auxiliary
例外。

### 同一路线的多文件选择

多文件覆盖同一 direct route 时，先按上面的 route-rule product/method 优先级选，再在
provider 内应用模型策略。SPK 使用有限、写死的模型表：DE442 > DE441 > DE440 > …；
火星卫星优先 MAR099，规则木星系统优先 JUP365，规则土星系统优先 SAT441，主天王星
系统优先 URA182，海王星优先 NEP098，冥王星优先 PLU060。其他命名卫星解作为低优先级
备选；只存在于一套产品的 irregular satellite 会自然按 coverage 选中该产品。

这里刻意不使用通用“编号最大”规则：`jup365.bsp` 与 `jup349.bsp` 是不同生产线，
`2000001.bsp` 又可能只是小行星编号。未知 Horizons/小天体 SPK 保持 discovery 顺序，
除非应用显式覆盖。OPM2 则直接比较 header 的 `source_id`。

应用可在 setup 阶段调用
`taiyin_runtime_set_ephemeris_source_priority(path_or_basename, priority)` 覆盖 provider
内的文件选择；精确 path 优先于 basename，较大值优先。显式数值替换该文件的 provider
默认数值：OPM2 默认为 header `source_id`，命名 SPK 使用内部模型表（如 JUP365 为 800、
JUP349 为 700）。所以既可提升，也可降级；未知未配置文件默认为零。AUTO 会在同一
provider 内跨 source-specific product rule 应用这个覆盖，但 route table 仍拥有 provider /
method 的边界，SPK 覆盖不会在 OPM2-only route 下选到 SPK。规则在每次选择时读取；应在
setup 时配置，不应和计算并发。

```c
/* 提升 JUP349，使其超过默认值 800 的 JUP365。 */
taiyin_runtime_set_ephemeris_source_priority("jup349.bsp", 900);

/* 降级，再清除覆盖以恢复 provider 默认值。 */
taiyin_runtime_set_ephemeris_source_priority("jup349.bsp", 600);
taiyin_runtime_clear_ephemeris_source_priority("jup349.bsp");
```

`taiyin_runtime_clear_all_ephemeris_source_priorities()` 清除全部用户覆盖。设置为零不等于
清除：零是有效显式优先级，可让文件排在所有正数内置产品之后。

运行时刻意不保留共享的 exact-JD 数值结果 cache。事件搜索和路线图通常访问不同
JD；全局 state/matrix cache 会在 writer lock 下反复写入低复用条目。复用只保留在
已加载的 source segment，以及单个请求对象内部同 JD 的局部状态。

## 运行时状态

`Runtime` 拥有：

```text
EphemerisBlockCatalog
EphemerisSegmentCache
EphemerisBodyRegistry
EphemerisRouteRule tables
EphemerisEngine
EarthOrientationTable
TLL1 lunar-limb mmap
```

全局 runtime 的配置与可变访问由 runtime read/write lock 保护。EOP 快照查询会取得读锁；被替换的 EOP 快照会保留到所属 `Runtime` 销毁，因此此前借用的只读指针不会悬空。EOP/TLL1 替换和 runtime 重新初始化仍属于 setup-time 操作，调用方不得让它们与计算并发执行。Descriptor loading 使用 `RouteInflightMap`，避免同一个 `EphemerisSegmentCacheKey` 对应的加载被重复执行。

`EphemerisRuntimeConfig` 控制全局 runtime 初始化：

```text
segment_cache_max_entries
source_paths / source_path_count
data_root
eop_path / load_builtin_eop
lunar_limb_path
load_packaged_data
strict_discovery
```

`initialize_global_ephemeris_runtime()` 会重建 catalog、segment cache、body registry 和 engine 绑定，并替换全局 EOP/TLL1 数据。leap-second 表是进程级内置只读数据。用户 `NativeCalcContext` 不持有这些数据表的裸指针。`add_global_ephemeris_source_path()` 会追加发现 descriptor、重建 body registry。`clear_global_ephemeris_cache()` 清空 compiled segment，并释放其 OPM2 source mapping；之后 OPM2 cache miss 会按需重新 mapping。

`get_global_registered_data_sources()` 会把成功发布的数据 inventory 以结构化记录暴露给调用方。星历 descriptor 按物理 source 与 format 聚合，EOP/TLL1 是单独 source kind。报告的 JD 区间只是 source 覆盖 envelope，不保证其中每条 route 每个时刻都存在；内置/内存 source 使用稳定标签和 flags，不伪装成文件路径。runtime replacement 是事务式的，初始化失败时旧 catalog 及其 inventory 保持不变。

`eop_path` 指向 finals2000A 文本并优先于 `load_builtin_eop`；二者都未设置时，
精密 UTC 路线没有 EOP，`TimeScaleAuto` 可按既有策略回退到 Delta T。
`lunar_limb_path` 为空时不加载月缘。setup 阶段也可用
`set_global_earth_orientation_table()` 安装一份深拷贝，或用
`load_global_lunar_limb_model()` 替换 mmap；这些操作不得与计算并发。

## 计算路径

```text
用户请求 body / center / JD / flags
        |
        v
NativeCalcContext 给出 route-rule id 和 observer/model 设置
        |
        v
BodyRegistry 判断 direct 还是 composite
        |
        v
direct request 按 route rule 找 descriptor
        |
        v
segment cache / source payload
        |
        v
raw Cartesian state
        |
        v
apparent / observed / output frame 变换
```

方法优先级在 catalog lookup 之前解析。Cache 是选中 catalog entry 之后的加速器，不是 route selector。

## `center_id` 与 `observer_id`

`NativeCalcContext` 里同时有 `center_id` 和 `observer_id`。这两个字段不要理解成同一个“观测中心”：

```text
center_id    用哪个共同原点取得 target 和 observer 的底层状态
observer_id  最终从哪个星体/观测者位置看 target
```

普通 apparent/native 位置计算会先在同一个 `center_id` 下取得：

```text
target -> center
observer -> center
```

然后在 apparent pipeline 中形成：

```text
observer -> target = (target -> center) - (observer -> center)
```

因此 `center_id` 是星历求值原点、route 组合原点或中间计算原点，不是最终观察者。真正决定“从哪里看”的字段是 `observer_id`，以及 topocentric observer offset。

例如：

```text
center=Sun, observer=Earth, target=Mercury
```

表示用 Sun-centered route 取得 `Mercury/Sun` 和 `Earth/Sun`，再得到 `Earth -> Mercury` apparent vector。同理：

```text
center=Sun, observer=Earth, target=Sun
```

中间的 `Sun/Sun` 是零状态，但最终 apparent vector 仍是：

```text
Sun/Sun - Earth/Sun = Earth -> Sun
```

这也是为什么 `center_id` 可以和 `observer_id` 不同。理论上，只要所有数据都能精确转换，`center=Sun`、`center=SSB` 或其他共同原点会得到同一条 observer-target 视线；实际工程中，数据源可用性、route fallback、覆盖范围和性能会不同。内置半解析模型天然提供 Sun-centered 行星状态，所以某些高层事件会显式选择 Sun-centered evaluation origin，以便稳定使用该路线。

## 内置方法中心约定

Descriptor 保留源方法真实的 target/center 身份。本体中心星历、质心星历、SSB-centered 数据和 Sun-centered 数据在 runtime 中按原始语义区分。

SPK descriptor 从 SPK segment summary 读取。`target_id` 和 `center_id` 就是 BSP segment 声明的值。Discovery 也会在同一个 kernel 内存在 preferred center 时暴露派生的同中心相对 route，例如 `target/SSB - Sun/SSB => target/Sun`。

打包的 OPM2 主体 descriptor 目前使用混合中心：

```text
Sun / SSB
Mercury barycenter / Sun
Venus barycenter / Sun
EMB / Sun
Moon / Earth
Mars/Jupiter/Saturn/Uranus/Neptune/Pluto barycenter / SSB
COB slices such as Uranus body / Uranus barycenter
```

Body fallback 把用户侧 NAIF body ids 映射到这些 route：Mercury/Venus body ids alias 到它们的 barycenter，Earth/Moon 使用 EMB + Moon/Earth 质量比组合，Mars 到 Pluto 的 body ids 在所选方法只提供 barycenter 数据时需要 body/barycenter COB offset。NAIF permanent `PNN`、extended permanent `P0NNN` 和 provisional `P5NNN` 形式的 natural-satellite id，会经对应物理 `P99` primary 组合，即使 Taiyin 没有该卫星的命名常量也一样。如果缺少这些 COB offset，native position 默认保持严格语义；只有显式传 `TAIYIN_NATIVE_POSITION_ALLOW_BARYCENTER_APPROX` 时，Mars 到 Pluto 才会改试对应 barycenter 近似，并在 `component_target_id` 里记录实际使用的 barycenter。

这个近似路径有意保持 strict-first：每次调用都会先尝试请求的本体路线，只有遇到 route、覆盖范围或组合组件失败后才 retry barycenter。这样以后补上真实 body/COB 数据时不会被静默绕过。如果 barycenter approximation 以后成为事件搜索或密集表格的热路径，更合适的优化方向是 route-level approximation decision cache，或者新增一个语义明确的 direct-barycenter 模式；当前 API 不缓存这个决策。

内置半解析模型在启用 packaged data loading 时注册到 runtime AUTO route table。它的中心约定是：

```text
Mercury/Venus/EMB/Mars/Jupiter/Saturn/Uranus/Neptune/Pluto / Sun
Sun / SSB
Moon / Earth
Earth body / Sun
```

Earth block 不是 SSB-centered；实现接受 `Earth/Sun` (`399/10`)，并由 `EMB/Sun` 加按质量比缩放的 `Moon/Earth` 向量构造。Runtime descriptor 使用相同的 target/center ids 注册。显式路线是 `TAIYIN_EPHEMERIS_ROUTE_SEMI_ANALYTIC`。

## 当前边界

OPM2 cache bucket selection 遵循其自然 Chebyshev segment grid。`compile_opm2_ephemeris_data_for_range()` 会把已加载 grid 切到选中的 segment range。当前实现会先解析源 payload 再切片；这保证语义正确，后续仍可继续优化 IO。

SPK、TKC1 和 custom file method 的 cache bucket 由 discovery 产生的 descriptor/cache policy 决定。后续仍可以继续优化某些格式的 bucket 粒度，但不应把这些策略塞回 catalog 或 route rule。

TSC1/TSF1 恒星 catalog 不走这条主星历 route selector。恒星高层 API 使用单独的 star provider/store；这是刻意拆开的边界，避免把恒星编号、星名查询和太阳系星体 route policy 混在一起。
