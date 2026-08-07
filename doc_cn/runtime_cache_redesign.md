# Runtime Cache 设计参考

文档状态：维护者参考
最后审阅：2026-07-01
当前实现请优先阅读：

- [星历运行时架构](./ephemeris_runtime_architecture.md)
- [Catalog 和 Cache 模型](./catalog_cache_model.md)

本文说明 runtime/cache 设计为什么收敛成当前形态，以及后续可以继续优化的方向。

## 背景

早期 runtime 曾把很多职责压进一个通用 block cache：

```text
source discovery
descriptor catalog
method priority
route selection
JD bucket selection
compiled data cache
eviction scoring
diagnostics
```

这个设计的问题是心智成本和热路径成本都太高。一个简单位置计算会同时触发“选路线、找文件、切 bucket、加载数据、管理淘汰、拼 diagnostics”等多件事。后来重构的目标是把这些职责拆开，让每层只做一件事。

## 当前分层

当前实现把职责拆成几层：

```text
BodyRegistry
  判断 body 能不能直接算，或者是否需要 composite/fallback 规则。

EphemerisRouteRuleTable
  管 direct 数据方法优先级，例如 AUTO / SPK-only / OPM2-only / semi-analytical-only。

EphemerisBlockCatalog
  本机数据清单。回答某个 target/center/method/frame/JD 是否有 descriptor 覆盖。

EphemerisSegmentCache
  缓存已加载、已编译的格式数据段。

```

典型计算路径：

```text
NativeCalcContext
  -> route-rule id / model / observer settings
  -> BodyRegistry direct or composite
  -> EphemerisRouteRuleTable method order
  -> EphemerisBlockCatalog descriptor candidates
  -> cache_policy selects bucket for JD
  -> EphemerisSegmentCache hit/load
  -> eval compiled block
  -> apparent / observed / frame pipeline
```

Catalog 仍然在 active runtime lookup 路径上，但它现在是带索引的数据清单，不再承担方法优先级，也不是 cache 的替代品。

## 设计演进

当前实现已经从早期大 block cache 方案迁移出来，主要变化包括：

- runtime 调度中心从通用 `EphemerisBlockCache` 收敛到 route rule、catalog 和 segment cache 的组合；
- cache 容量策略从 byte size/reload cost/priority 混合打分，收敛到固定条目数和简单淘汰；
- 方法优先级从 catalog 插入顺序，迁移到显式 `EphemerisRouteRuleTable`；
- route selection 和 cache hit 分离，cache 只表示某段数据是否已经加载；
- diagnostics 从正常热路径中拆出，正常计算优先走轻量 lookup；
- cache key 保留结构化身份，hash 只作为加速 lookup 的实现细节；
- catalog 查询返回 descriptor copy，避免并发扩容时出现悬空指针。

当前代码已经改成 descriptor copy 模式：

```text
catalog.get(index, out)
catalog.find_method_candidates(query, method, out_vector)
EphemerisSelectionResult 保存 descriptor copy
```

这样 catalog 扩容或并发追加 descriptor 时，不会让计算路径持有悬空指针。

## Segment Cache

`EphemerisSegmentCache` 的 key 是结构化身份，不是单个 hash：

```text
kind
target_id
center_id
method_id
frame
source_key
item_id
```

hash 只用于加速 lookup；相等性比较使用完整 key。这样即使 hash 碰撞，也不会取错数据。

Segment cache 的值是 compiled runtime artifact，例如：

```text
OPM2 compiled segment
SPK segment/kernel-backed compiled block
Kepler elements block
TSC1 star provider 内部 compiled evaluator
```

Cache 使用固定条目数，不按 byte size 做复杂预算。淘汰策略保持简单，读写由 writer-preferred lock 保护。使用方通过 `with_data()` 在读锁保护期间访问 payload，避免数据被淘汰后继续使用裸指针。

## Cache Policy

Descriptor 上的 `cache_policy` 说明如何从 source descriptor 切出 bucket：

```text
CacheWholeEntry
CacheFixedSpan
CacheNaturalSegment
```

OPM2 使用自然 Chebyshev segment grid：

```text
origin_jd
span_days
first_index
count
```

给定 JD 后，runtime 通过 `make_cache_bucket_descriptor_for_jd()` 计算 bucket descriptor。OPM2 现在按源文件的 Chebyshev segment grid 选择 bucket。

SPK、TKC1、TKE1/custom Kepler file 的 bucket 由各自 discoverer 写入的 `cache_policy` 决定。格式自己的自然分段留在 discovery/loader 层，route rule 和 catalog priority 只负责数据路线选择。

## 数值求值复用

Taiyin 不再保留共享 exact-JD 数值结果 cache。这类 key 在搜索和路线生成中复用率低，
但每次 miss 都会争抢全局 writer lock。source segment 的复用仍由
`EphemerisSegmentCache` 负责；单个 solver/request 可在确有价值时保留局部同 JD 结果。

## Source Index 和文件数据

Catalog 里还有 `source_indexes_`。它用于让同一源文件的多个 descriptor 共享已解析的 source payload，例如：

```text
同一个 OPM2 文件的多个 route/bucket descriptor
同一个 SPK kernel 的多个 segment descriptor
同一个 TKC1 catalog 的多个 object descriptor
```

这不是 route selector，也不是全局文件 cache。文件页是否 resident 交给 OS page cache；Taiyin 只缓存解析后真正会被 runtime 使用的数据结构。

## Route Rule 和自定义方法

方法优先级由 `EphemerisRouteRuleTable` 表达。内置 route-rule id 包括：

```text
AUTO
SPK-only
OPM2-only
semi-analytical-only
```

用户可以在 setup 阶段注册新的 route-rule table，并让 `NativeCalcContext` 选择对应 id。`NativeCalcContext` 会保存已解析的 table 指针，所以 route rule table 应按“注册后只读”的方式使用。需要新策略时注册新 id。

Custom method 和 custom file method 注册后进入 catalog，并可按 priority 插入 AUTO route rule。扩展方法需要清楚声明自己的 target/center/method/source identity，这样 catalog 查找与 segment cache 复用才能保持一致。

## 后续优化方向

后续 runtime/cache 优化可以沿这些方向推进：

- OPM2 loader 避免先解析完整 payload 再裁剪 segment；
- SPK 按 kernel natural segment 做更细粒度 bucket；
- 事件搜索和 eclipse solver 内部减少同一 JD 的 Sun/Moon/frame 重复 eval；
- 对连续 JD 扫描增加局部 cursor，但 cursor 只保存 key/range，不保存可能被淘汰的裸 data 指针；
- 为特定格式引入 mmap 或持久 kernel handle，减少重复打开和解析源文件；
- 在保持 route rule、catalog、segment cache 分层清晰的前提下，补充更细的性能诊断工具；
- 为长区间搜索和批量星盘计算提供更明确的缓存容量配置建议。

## 相关文档

如果需要继续了解当前实现细节，可以阅读：

```text
ephemeris_runtime_architecture.md
catalog_cache_model.md
opc_catalog_format.md
```

本文用于解释设计背景和后续优化方向。具体运行时行为以上述当前文档和源码为准。
