# 第三方软件与数据来源

状态：当前说明

Taiyin 包含项目原创代码，也使用了少量第三方源码、公开天文模型、生成数据和
外部参考结果。本文面向用户与集成者，提供一份便于查阅的总览。具有法律效力的
署名与再分发条款以仓库根目录的 [`NOTICE`](../NOTICE)、
[`LICENSE`](../LICENSE)、源码内保留的声明，以及各数据产品旁的 README 或
manifest 为准。

## 软件与算法来源

| 组件 | 在 Taiyin 中的用途 | 位置与条款 |
| --- | --- | --- |
| ERFA / IAU SOFA | CIRS 与天体测量相关函数的精简子集 | [`src/internal/erfa_cirs_subset.cpp`](../src/internal/erfa_cirs_subset.cpp) 保留 ERFA/SOFA 声明和 BSD-3-Clause 再分发条款。 |
| miniz | 运行时数据容器使用的 ZIP/deflate 支持 | [`src/third_party/miniz/`](../src/third_party/miniz/) 中拆分的 C 源码保留 MIT 声明；`miniz.c` 还完整保留上游 Unlicense/public-domain dedication。 |
| Astronomy Engine | 紧凑 L1.2 木星伽利略卫星系数子集 | [`src/third_party/astronomy_engine/`](../src/third_party/astronomy_engine/) 保留改编后的系数子集和 Don Cross 的 MIT 许可证；Taiyin 自行实现状态微分与参考架集成。 |
| 寿星天文历（sxwnl） | 归档的日月食实现、中国历法行为参考及回归 oracle | 归档源码集中在 [`legacy/sxwnl/`](../legacy/sxwnl/)，不链入生产 runtime；完整署名和上游声明收录在 [`NOTICE`](../NOTICE)。 |

Taiyin 曾使用 sxwnl 的部分日食和月食几何。这些移植以及固定路线/oracle
材料现已统一归档到 `legacy/sxwnl/`，只用于对照。生产日月食和掩星代码使用
Taiyin 独立维护的三维影轴、椭球求交、路线和地方观测几何。中国历法独立实现
冬至年编排和无中气置闰规则，sxwnl 作为其中一个行为 oracle；历史中国历法
profile 包含由 sxwnl/SSQ 生成的固定民用日结果。部分农历和日月食回归
fixture 同样保留 sxwnl 结果。

## 天文模型与拟合参考

| 来源 | 在 Taiyin 中的用途 | 分发说明 |
| --- | --- | --- |
| VSOP2013 | 水星、金星、物理地心和火星 L/B/R 表的理论来源 | 运行时只包含离线转换、筛选并按 DE441 校准后的最终系数，不包含完整上游发行包。 |
| TOP2013 | 木星、土星、天王星和海王星 L/B/R 表的理论来源 | 运行时包含由公开 L/B/R 表筛选并按 DE441 校准后的最终系数。 |
| ELP/MPP02 月球理论 | 内置全年代月球模型的频率基底 | 运行时包含由 DE405 常数组 ELP/MPP02 表派生、使用 763 个共享参数的 1,241 个折叠项；完整 ELP 表只用于再生成，不随仓库分发。 |
| NASA/JPL DE441 | 月球与行星半解析模型、事件 seed 模型和打包星历的拟合/验证参考 | DE441 BSP 不随仓库分发，仓库只包含生成后的 Taiyin 系数和数据产品。 |

VSOP2013 与 TOP2013 的发行包和文档由 IMCCE 发布，见
<https://ftp.imcce.fr/pub/ephem/planets/>。Taiyin 离线把公开理论／表转换成统一的
`T^n A cos(phase + frequency*T)` 运行时形式，并把另行拟合的 DE441 残差系数折入
生成表。准确范围和验证结果见根目录 NOTICE 与
[`semi_analytic_ephemeris.md`](semi_analytic_ephemeris.md)。

ELP 月球理论由 Michelle Chapront-Touzé 与 Jean Chapront 建立；ELP/MPP02
是 Jean Chapront 与 Gérard Francou 完成的修订，并纳入 P. Bidart 的 MPP01
行星摄动。修订论文为 *The lunar theory ELP revisited. Introduction of new
planetary perturbations*, Astronomy & Astrophysics 404 (2003), 735–742。
DOI：`10.1051/0004-6361:20030529`。

35,901 项候选池由公开的 `ELP_MAIN.S1`–`S3` 和 `ELP_PERT.S1`–`S3` 六个
文件按 DE405 模式转换得到。准确的转换输入校验和、来源 revision 和处理链保留在
私有维护 provenance 记录中；这六个完整上游系数文件不随 Taiyin 分发。读取 JPL
BSP 的再生成与验证工具会选用 NumPy、jplephem、PyERFA 等 Python 包；它们不是
运行时依赖，也不属于公开源码快照。

## 随包提供和可选的数据

| 产品 | 上游来源 | 本地来源说明 |
| --- | --- | --- |
| 主天体、卫星系统质心和小天体 OPM2 数据 | NASA/JPL 行星、卫星和小天体 SPK/Horizons 数据；部分小天体远期区间使用文档注明的自行积分扩展 | [`data/ephemerides/opm2/`](../data/ephemerides/opm2/) 下的 README 与 `MANIFEST.json` |
| Kepler/TKC1 小天体包 | NASA/JPL Small-Body Database Query API | [`data/kepler/sbdb/manifest.json`](../data/kepler/sbdb/manifest.json) |
| TSC1 恒星表 | Gaia DR3、ESA Hipparcos、Yale Bright Star Catalogue/BSC5、项目维护的特殊方向，以及 Stellarium sky-cultures 的中国星官/西方星座连线选星和名称（CC BY-SA） | [`tsc1_v1_known_limitations.md`](tsc1_v1_known_limitations.md) 说明来源优先级和记录数量；[`data/stars/catalogs/lite/required_stars.json`](../data/stars/catalogs/lite/required_stars.json) 固定 Stellarium 来源 revision。 |
| TLL1 月缘表 | ISAS/JAXA SELENE Data Archive 发布的 SELENE（Kaguya）LALT 全球地形 | [`data/lunar-limb/README.md`](../data/lunar-limb/README.md) 记录源产品、致谢要求、生成方法和校验和。 |

OPM2、TKC1、TSC1 和 TLL1 是 Taiyin 的运行时格式。把外部数据转换为这些
格式不会替代原数据提供方的条款。再分发数据包时，应同时保留相邻的 README
和 manifest 来源元数据。

## 外部校验参考

测试和技术文档还会把部分结果与 NASA 日食/凌日表、JPL Horizons、紫金山
天文台资料、Swiss Ephemeris、SOFA/ERFA 和 sxwnl 对比。这些属于参考结果或
行为 oracle；除非某项测试明确启用了相应外部程序或数据，否则它们不是运行时
依赖。
