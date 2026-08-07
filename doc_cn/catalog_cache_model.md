# Catalog 和 Segment Cache 模型

文档状态：当前说明
最后审阅：2026-07-01
主要头文件：
`include/taiyin/internal/ephemeris_catalog.h`,
`include/taiyin/internal/ephemeris_segment_cache.h`,
`include/taiyin/runtime/ephemeris_engine.h`

本文说明 Taiyin runtime 中 catalog 和 segment cache 的职责边界：

```text
Catalog            本机有什么数据，在哪，覆盖什么时间
SegmentCache       某个数据源的某个天然段是否已经编译进内存
```

```text
OPC / directory discovery  ->  EphemerisBlockCatalog
disk index or file scan        descriptor/path records

EphemerisBlockCatalog      ->  BodyRegistry
descriptor inventory           direct-capable body marks

NativeCalcContext          ->  EphemerisRouteRuleTable
route_rule_id                  resolved immutable route-rule table

EphemerisEngine           ->  Catalog method page       -> descriptor copy
request evaluation            route/method lookup          source inventory

EphemerisEngine           ->  EphemerisSegmentCache    -> compiled storage blocks
selected descriptor            fixed-entry cache          format artifacts

```

## Descriptor Catalog

`EphemerisBlockCatalog` 保存 source descriptor：

```text
target_id
center_id
method_id
frame
format
jd_tdb_start / jd_tdb_end
source_key
path
route_key
cache_policy
```

Catalog 是回到文件或注册源的 source-of-truth map。如果 runtime cached data 被换出，descriptor 仍然包含重新加载该 segment 所需的信息。

Catalog 内部还有两类索引：

```text
method_pages_:
  hash(target_id, center_id, frame, method_id)
    -> one or more MethodPage
       -> descriptor indexes

source_indexes_:
  source_key
    -> EphemerisSourceIndex(path / format / optional parsed payload)
```

`find_method_candidates()` 先用 method page 缩小候选范围，再按 JD 覆盖范围过滤，并返回 descriptor copy。调用方不会持有 catalog 内部 vector 的裸指针，因此 catalog 写入导致 vector 扩容时，不会让已经开始 eval 的 reader 读到悬空地址。

Catalog 顺序不是方法策略。当前拆分是：

```text
BodyRegistry             -> 哪些 body 能 direct，哪些需要 fallback/composite
EphemerisRouteRuleTable  -> direct method preference，可由 context 选择
Catalog                  -> 本机数据清单
SegmentCache             -> 已加载 runtime segments
```

设计背景可参考维护者文档 `runtime_cache_redesign.md`。

## Route Rule 和 Context

`NativeCalcContext` 保存 `route_rule_id` 和已经解析好的 `route_rules` 指针。Route-rule table 在 runtime 初始化或注册时建立，计算期间按不可变表使用。

内置 route 规则包括：

```text
AUTO     SPK -> OPM2 -> 内置半解析 -> TKC1 Kepler -> Taiyin Kepler file
OPM2     只尝试 OPM2
SPK      只尝试 SPK
SEMI     只尝试内置半解析模型
```

如果 context 没有指定 route rule，`EphemerisEngine` 使用 runtime 的默认 route-rule table。指定非 AUTO 规则时，只尝试该规则表里的 method，不做跨 method fallback。

## Segment Cache

`EphemerisSegmentCache` 保存已加载 runtime artifacts：

```text
EphemerisSegmentCacheKey {
  kind,
  target_id,
  center_id,
  method_id,
  frame,
  source_key,
  item_id
}
EphemerisSegmentCacheData { void* data, destroy_fn }
```

Cache 有固定最大条目数。它不追踪 byte budget、reload cost、frequency weight 或 route-rule priority。当前 eviction 使用简单 clock policy。

在 ephemeris engine 主线里，cache key 的 `kind` 来自 descriptor `format`，`item_id` 来自 bucket descriptor 的 `route_key.bucket_id`：

```text
CacheWholeEntry      bucket_id = 0
CacheNaturalSegment  bucket_id = natural segment index
CacheFixedSpan       bucket_id = floor((jd - origin) / span)
```

OPM2 使用 natural segment policy，对应 Chebyshev segment index。SPK、TKC1/Kepler、custom method 等格式按各自 discovery 生成的 `cache_policy` 决定 bucket。TSC1 star provider 也复用 `EphemerisSegmentCache` 类型，但它不走 ephemeris descriptor catalog 主线，而是用 provider 内部的 star runtime id 作为 cache identity。

调用方在计算 cached entry 时使用 `with_data()`。它会在 cached pointer 使用期间持有 read lock，避免数据在计算中被换出。

Segment cache miss 时，`EphemerisEngine` 的流程是：

```text
descriptor
  -> make_cache_bucket_descriptor_for_jd()
  -> make_method_cache_key()
  -> SegmentCache.with_data()
       hit: eval
       miss:
         RouteInflightMap acquire
           loader: load_descriptor_ephemeris_block()
                   SegmentCache.insert()
                   with_data() eval
           follower: wait / retry with_data()
```

`RouteInflightMap` 用来避免多个线程同时加载同一个 segment。它不改变 cache key，也不参与 route selection。

## 这里不缓存什么

SegmentCache 不拥有 persistent catalog、source path、OPC 内容或 discovery result。它只拥有可以销毁并从 descriptor 重新加载的 compiled runtime data。

Catalog 不缓存 compiled segment，也不缓存最终位置结果。它只描述“本机有哪些可用 source，以及如何回到这些 source”。
