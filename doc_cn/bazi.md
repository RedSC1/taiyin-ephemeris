# 八字扩展

八字是建立在 Taiyin 天文、中国历法和干支计算之上的可选中国术数层。干支与
历法四柱属于基础 `taiyin` runtime；藏干、十神、运程、关系和神煞等解释规则
位于本扩展。

## 构建与动态库边界

必须同时开启两个选项：

```sh
cmake -S . -B build-bazi \
  -DTAIYIN_BUILD_CHINESE_METAPHYSICS_EXTENSIONS=ON \
  -DTAIYIN_BUILD_BAZI_EXTENSION=ON \
  -DTAIYIN_BUILD_MODULAR_C_API=ON
cmake --build build-bazi
ctest --test-dir build-bazi --output-on-failure
```

模块化安装中的 `taiyin` 包含核心天文、占星、中国历法和干支 API；启用八字
后才会增加依赖 `taiyin` 的可选 `taiyin_bazi` shared library。普通干支历法应用
只链接 `taiyin`，八字应用链接两者。

原生 C++ 头文件是 `bazi_astrology/include/taiyin/bazi/bazi.h`；稳定 C ABI
头文件是 `bazi_astrology/include/taiyin/c/bazi.h`。

## 计算流程与所有权

边界保持显式：

```text
NativeCalcContext
        |
        v
ChineseCalendarContext -- calculate_four_pillars() --> GanzhiFourPillars
        |                                                   |
        |                                                   v
        +----------- 节令查询 -------------------------> BaziContext
                                                            |
                                                            v
                                                        BaziChart
```

`BaziContext` 只持有八字配置，不拥有天文 context 或中国历 context。大多数规则
只需要已完成的命盘；起运和某一时刻的人元司令会显式接收
`ChineseCalendarContext`，从而沿用调用方选择的星历路线与民用日 profile。

正常调用顺序是：

1. 初始化 Taiyin runtime 和 native calculation context；
2. 用所需 UTC 时差或经线 profile 初始化 `ChineseCalendarContext`；
3. 计算出生时刻的 `GanzhiFourPillars`；
4. 初始化 `BaziContext`，调用 `calculate_chart()`；
5. 复用命盘计算关系、神煞、小运、起运和大运。

墙钟、时区以及真/平太阳时策略应在四柱计算前由调用方解析。八字不会根据经度
自行推断法定时区。

## 命盘与基础规则

`BaziChart` 包含：

- 年、月、日、时四柱；
- 命宫、身宫、胎元和胎息；
- 四柱藏干及数量；
- 明透与藏干十神 ID；
- 十二长生 ID 和纳音 ID。

纯规则入口还提供空亡、十神、藏干、天干/地支二元与三元关系、十二长生以及
流年、流月、流日、流时。原生层返回稳定数值 ID 与 mask；中文名称和显示格式
属于 binding 或应用层。

## 起运、大运与小运

当前起运方向按年干阴阳与性别决定。Context 可选择三种起运时间模型：

- 传统历法分量：三日折一年，再按 360 日年、30 日月分解；
- 连续 365.25 日儒略年；
- 连续 Taiyin 平回归年。

大运边界也可选择民用年、儒略年或回归年十年步长。`fill_dayun()` 的天文时间
边界为半开区间 `[start_jd_ut, end_jd_ut)`，虚岁字段仍保留传统显示所需的
含首尾范围。小运采用从 1 开始的虚岁，并显式传入顺逆方向。

## 关系、神煞与人元司令

`collect_chart_relations()` 按柱位 mask 和关系类型 mask 返回合并后的关系图；
四柱和四个附加宫位可分别选择。规则定义合化五行时，结果会携带合化五行 ID。

神煞结果是 66 个稳定 ID 的 bitset。需要传统性别相关规则时使用
`collect_target_shen_sha_with_gender()`；没有性别资料的调用方可使用中性入口。
完整稳定 ID 见 `bazi_astrology/shen_sha_ids.md`。

人元司令提供《三命通会》表与兼容 common 表。时间既可按上一节后连续经过的
24 小时日，也可按跨过的地方民用日计算；后一种会使用所传中国历 context 的
日界策略。

## C ABI 数组约定

变长 C 结果统一使用两次调用：第一次传 `out = NULL`、`capacity = 0` 获取
`out_count`，分配并初始化对应版本化结构后再次调用。小运、大运、关系、神煞
bitset word 和人元司令分段均遵循此约定。

## 验证范围

原生迁移由规则测试、C/C++ API 测试、模块化符号与安装 smoke、历法集成测试、
10,000 命盘 benchmark 和起运记录生成器共同覆盖。起运差分验证同时覆盖男女，
并比较方向、年龄分量、split-JD 与民用时间字段。归档 Pascal 实现保留在
`legacy/` 供维护时参考，不会链接进现行 runtime。
