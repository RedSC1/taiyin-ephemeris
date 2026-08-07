# OPC1 Catalog 格式

文档状态：当前说明
最后审阅：2026-07-01
主要头文件：`include/taiyin/internal/opc_catalog_persistent.h`

`OPC1` 是 Taiyin 的持久化星历源索引格式。它的文件 magic 是 `OPC1`，当前 schema version 是 `OPC_VERSION = 2`。

OPC catalog 的作用是在启动或添加数据目录时加速 discovery。真实星历数据仍然保存在源文件里，例如：

```text
*.opm2
*.bsp / *.spk
*.tke1
*.tkc1
```

OPC 文件只保存这些源文件对应的 runtime descriptor metadata。它不是星历数据文件，也不是第二份 source of truth。

## 运行时角色

运行时加载数据目录时可以走两条路径：

```text
load index.opc
  |
  +-- valid   -> 直接恢复 EphemerisBlockDescriptor 列表
  |
  +-- invalid -> 递归扫描源文件并重新生成 index.opc
```

因此 OPC 是可删除、可重建的索引。源文件新增、删除、相对路径变化或文件大小变化后，fingerprint 不匹配，旧 OPC 会被拒绝，然后 runtime fallback 到目录 discovery。

## 文件布局

`OPC1` 是 packed little-endian binary：

```text
[OpcHeader]
[OpcDescriptorRecord * N]
[string table]
```

当前实现只在本机 little-endian 环境加载该格式。

## Header

`OpcHeader` 固定 128 bytes：

```text
magic = "OPC1"
version = OPC_VERSION
flags
descriptor_count
descriptor_records_offset
string_table_offset
string_table_size
fingerprint
source_id
source_version
generation
reserved
```

当前 writer 使用：

```text
source_id = 0
source_version = OPC_VERSION
generation = OPC_VERSION
```

## Descriptor Record

`OpcDescriptorRecord` 当前固定 128 bytes。每条 record 对应一个 runtime ephemeris descriptor。一个源文件可以贡献多条 record；例如 `TKC1` catalog 会为多个小天体对象生成多个 descriptor，SPK kernel 也可能暴露多个 target/center route。

字段：

```text
target_id
center_id
method_id
frame_id
jd_tdb_start
jd_tdb_end
source_id
block_id
generation
purpose
bucket_id
format
path_offset
cache_policy_kind
file_size
file_mtime_sec
file_mtime_nsec
cache_origin_jd
cache_span_days
cache_first_index
cache_count
```

其中 `source_id/block_id/generation/purpose` 恢复为 `EphemerisBlockKey`，用于定位源文件 payload；`target_id/center_id/method_id/bucket_id` 恢复为 `EphemerisRouteKey`，用于 route lookup 和 segment cache key。

`cache_policy_*` 是 v2 记录的关键字段。它描述 source descriptor 如何切成可加载 bucket：

```text
CacheWholeEntry      整个 descriptor 作为一个 cache entry
CacheFixedSpan       按固定时间跨度切 bucket
CacheNaturalSegment  按源格式自然 segment 切 bucket
```

OPM2 通常使用 natural Chebyshev segment grid。SPK、TKC1 和 custom Kepler file 由各自 discoverer 写入合适的 cache policy。

## String Table 和路径

string table 第一个字节必须是 `NUL`。每个 descriptor 的 `path_offset` 指向一个 `NUL` 结尾的相对路径。

OPC 不写绝对路径。Loader 会把相对路径和 catalog root 拼接：

```text
major-bodies/600y/mars.opm2
asteroids/600y/ceres.opm2
cob/full/jupiter_cob.opm2
kepler/sbdb/sbdb-tier0-core.tkc1
```

绝对路径、空路径和越界字符串都会导致 catalog load 失败。

## Fingerprint

Fingerprint 用 indexed source files 计算。当前 indexed source 后缀是：

```text
.opm2
.bsp
.spk
.tke1
.tkc1
```

hash 输入包括：

```text
relative path
file size
```

路径会排序后再 hash。mtime 会写入 record 供诊断使用，但不参与 fingerprint。这样源码 checkout、压缩包解压或安装器复制导致 mtime 改变时，不会让 packaged catalog 无意义失效。

当前 fingerprint 不是内容 hash。同一路径、同大小的源文件被替换时，OPC fingerprint 不保证失效；这种场景需要重新生成 OPC，或在未来引入内容级校验后再自动检测。

## 校验规则

只有满足以下条件时，OPC load 才成功：

- magic 是 `OPC1`；
- version 等于当前 `OPC_VERSION`；
- header offset/range 合法；
- descriptor count 非零；
- fingerprint 匹配当前 root 下的 indexed source files；
- 每条 record 的 target、center、method、frame、时间范围合法；
- format 是持久化 runtime ephemeris 格式；
- source key 非零且 generation 非零；
- cache policy 合法；
- path 是非空相对路径。

当前持久化 ephemeris descriptor format 包括：

```text
OPM2
SPK
Kepler/TKE1
TKC1
```

TSC1/TSF1 恒星 catalog 不进入这条 ephemeris descriptor OPC 路径。恒星数据由 star provider/store 单独加载。

## Loader 行为

核心入口：

```cpp
collect_ephemeris_descriptors_from_catalog_or_directory(
    root,
    catalog_path,
    discoverers,
    options,
    out);
```

行为：

1. 如果 `catalog_path` 非空，先尝试 `load_opc_persistent_catalog(catalog_path, root, out)`。
2. 如果 OPC 缺失、过期或无效，则调用 directory discovery。
3. 如果 directory discovery 成功，并且提供了 `catalog_path`，则尝试重写 OPC。

重写 OPC 失败不会改变 discovery 结果；它只影响下一次加载速度。

## 打包数据中的 OPC

当前仓库里有两个打包索引：

```text
data/index.opc
data/ephemerides/opm2/index.opc
```

`data/index.opc` 是顶层 packaged data index，覆盖打包 OPM2 数据和 `data/kepler/sbdb/` 下的 TKC1 catalog。默认 packaged runtime 会优先使用它。

`data/ephemerides/opm2/index.opc` 是 OPM2 子树 index，方便只把 `data/ephemerides/opm2` 作为 data root 使用。

Runtime 会对 final path component 为 `data`、`opm2` 或 `sbdb` 的 root 尝试使用 `index.opc`；如果不存在或无效，就扫描目录。

## 生成工具

生成命令：

```text
generate_ephemeris_catalog <ephemeris-root> [catalog-path]
```

如果省略 `catalog-path`，工具会写入：

```text
<ephemeris-root>/index.opc
```

工具会先做目录 discovery，写 OPC，然后重新 load 一次验证 descriptor 数量。

## 相关文件

- `include/taiyin/internal/opc_catalog_persistent.h`
- `src/opc_catalog_persistent.cpp`
- `tests/test_opc_catalog_persistent.cpp`
- `tools/generate_ephemeris_catalog.cpp`
