# 星历数据包

文档状态：当前说明  
最后审阅：2026-08-24

Taiyin 将大型星历产品与 native 库、语言绑定分离。普通现代计算所需的小型数据
仍可随源码或 wrapper wheel 提供；长年代产品作为独立的 GitHub Release 资产发布。

## 当前主星产品

| 产品 | 分发方式 | 公共覆盖 | 主要用途 |
| --- | --- | --- | --- |
| DE441 来源 600 年 OPM2 | 源码仓库与 wrapper 数据包 | 1800-01-01 至 2400-01-01 | 现代区间兼容 |
| DE442 来源 OPM2 | 源码仓库与 wrapper 数据包 | 约 1550 至 2650 | 默认现代主星来源 |
| 全量 DE441 来源 OPM2 | 可选 GitHub Release 资产 | JD `-3096455.499990447` 至 `7996074.500009106` | 长年代计算和历史压力测试 |
| NASA/JPL 原始 SPK | 用户自行提供 | 取决于 kernel | 直接使用 JPL 参考数据，或补充 OPM2 未包含的目标 |

全量压缩包名为 `taiyin-opm2-de441-full-v1.zip`，约 85.3 MiB；其中 OPM2
净数据为 87.98 MiB，另有 manifest 和 OPC catalog。它包含 51 个约 600 年分片，
共 561 个 OPM2 文件，覆盖太阳、月球、水星、金星、地月质心、火星、木星、
土星、天王星、海王星和冥王星。

请从项目的 [GitHub Releases](https://github.com/RedSC1/taiyin-ephemeris/releases)
页面下载。单独提供的 `.zip.sha256` 用于校验整个压缩包；包内的 `SHA256SUMS`
用于校验每个解压文件。

## 压缩包结构

```text
taiyin-opm2-de441-full-v1/
├── README.md
├── README_CN.md
├── LICENSE
├── NOTICE
├── MANIFEST.json
├── VALIDATION.json
├── SHA256SUMS
└── opm2/
    ├── index.opc
    └── shards/
        ├── shard-000-jd-m3096455/
        │   ├── MANIFEST.json
        │   └── *.opm2
        └── ...
```

把 `data_root` 指向解压后的 `opm2` 目录。它的最后一级目录名正是 `opm2`，
runtime 会优先加载 `index.opc`；只有 catalog 缺失或校验失败时才递归扫描文件。

```cpp
taiyin::runtime::EphemerisRuntimeConfig config;
config.data_root = "/data/taiyin-opm2-de441-full-v1/opm2";

if (!taiyin::runtime::initialize_global_ephemeris_runtime(config)) {
    return 1;
}
```

C 应用把相同路径写入 `taiyin_runtime_config.data_root`。Python 和 Dart 也提供
对应的 `data_root` 构造参数。显式配置的 data root 先于 fallback 的随包目录加载，
因此全量包既可覆盖原 600 年 DE441 数据，也可提供更远年代。

全量 DE441 使用 OPM2 source id `1`，和随包 600 年数据属于同一个 DE441 来源
产品身份。DE442 来源 OPM2 使用 source id `2`；两套产品都作为显式来源注册时，
AUTO 通常优先选择 DE442。需要严格复现 route 时，建议只配置一个主要 data root。

## 相对 DE441 的全量精度验证

发布流程分别验证 51 个分片。每个拟合段采样 512 个 Chebyshev 节点，并把重建的
地心方向与 NASA/JPL DE441 对比。下表给出 51 份分片报告中最大的 p99，以及全量
样本中实际观测到的最大误差。“最差分片 p99”不是把全部样本混合后计算的全局 p99。

| 天体 | 样本数 | 最差分片 p99（角秒） | 观测最大值（角秒） |
| --- | ---: | ---: | ---: |
| 太阳 | 31,547,904 | 0.000538 | 0.000770 |
| 月球 | 206,114,304 | 0.001266 | 0.001677 |
| 水星 | 64,542,208 | 0.000649 | 0.001370 |
| 金星 | 25,273,344 | 0.001755 | 0.003545 |
| 火星 | 8,267,264 | 0.001309 | 0.002787 |
| 木星 | 1,893,376 | 0.000510 | 0.000705 |
| 土星 | 1,623,040 | 0.000523 | 0.000842 |
| 天王星 | 1,420,288 | 0.000345 | 0.000430 |
| 海王星 | 1,420,288 | 0.000325 | 0.000381 |
| 冥王星 | 1,420,288 | 0.000321 | 0.000419 |
| **合计** | **343,522,304** | **0.001755** | **0.003545** |

地月质心是重建地心位置时使用的依赖量，不是一个地心天球方向，因此没有放进这张
角误差表；native state validator 仍会检查它生成后的状态。

最终 ZIP 还按用户实际使用方式重新解压验证：包内 SHA-256 全部通过，`index.opc`
成功重载 561 个 descriptor，并对 11 个存储天体在所有分片边界前后共执行 1,122 次
runtime 求值，没有 route 或 coverage 失败。

以上数值衡量的是 OPM2 相对 DE441 的重建误差。最终 apparent、站心、升落、
日月食或历法结果还会受到时间尺度、观测者几何和所选改正模型影响。
