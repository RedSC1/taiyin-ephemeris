# 手动安星、改盘与报数／随机起盘

[English / 完整接口说明](manual-casting.md) · [可运行 C++ 示例](../examples/manual_casting.cpp)

这几种操作都使用现有的 TOML／JSON option 表，不增加一套安星算法。
普通出生盘仍走原来的历法转换；手动起盘不需要星历文件、历法 context 或出生日期。

## 随机和报数起盘

```cpp
using namespace taiyin;
using namespace taiyin::ziwei;

ZiweiDataCatalog catalog("ziwei_astrology/rules/default.toml");
const ZiweiContext ctx = catalog.create_context();
const CompiledRules& tables = ctx.compiled_tables();
CastingChart chart;
Status status = random_casting_chart(
    Gender::Male, ZiweiChartMode::TianPan, tables, &chart);
if (status == TAIYIN_STATUS_OK) {
    // 保存编号、性别、天地人盘模式、规则版本，之后可以复现。
    std::cout << chart.index << '\n';
}
```

报数改用 `casting_chart_from_number("123456", ..., &chart)`；复现编号用
`casting_chart_from_index(index, ..., &chart)`。完整可编译示例见上方链接。

- 固定性别、天地人盘和规则后，共 `60 × 12 × 30 × 12 = 259200` 种输入组合。
- 抽六十甲子年序，再拆成年干支；月 1～12、日 1～30、时支 0～11。
- 采用拒绝采样，避免直接取模产生偏差；最多尝试 128 次，失败报错。
- Windows 用 BCrypt，Unix 类平台读取 `/dev/urandom`；不静默退回伪随机。
- 可以传自己的同步 uint32 随机回调；库不保留回调。多个线程共用回调状态时由调用方同步。
- 报数沿用 JS 的 `number-v1` 算法，去掉前导零；`123456` 与 `000123456`
  都得到编号 **209225**。它是本库定义的映射，不宣称是哪派传统口诀，也允许碰撞。
- 输入组合均匀，不等于最终不同盘面均匀；性别与流派选择不参与随机。

`CastingChart` 是独立类型，不冒充 `NatalChart`，没有虚构的出生年月日，也没有实际日期的流运时间轴。

## 直接输入安星参数

`PlacementInput` 包含年干、年支、月、日、时支；`arrange_ziwei_stars()` 直接排星，
`make_casting_chart()` 生成带起盘记录的独立盘。可额外指定五行局。

这里的月份已经是安星有效月，不再处理闰月、早晚子或真太阳时。二月三十等组合可以使用，
因为不是在创建真实日期。年干支可各自选择，但不配对时不能算旬空；缺少真实日期时也不能
推导日干支、时干。依赖这些输入的星会列入 `omitted_placements`，星位为 `0xff`，
而不是硬塞到子宫。其他星正常计算。

## 修改出生盘

`modify_natal_chart()` 接受原盘、`PlacementPatch`、原排盘的 `AnchorOptions` 和同一套规则。
返回新盘，不改输入盘；补丁字段 `-1` 表示不改，连续调用累积覆盖。

| 项目 | 行为 |
| --- | --- |
| 原出生时间、历法事实、节气和农历四柱 | 保留 |
| 原命身宫、宫干、命主身主 | 保留 |
| 安星位置 | 按显式覆盖后的参数重新排 |
| 年干四化 | 显式改年干才改，否则沿用各自年界 |
| 自化、向心四化 | 新星位配原宫干重新算 |
| 五行局 | `update_bureau` 控制 |
| 流运顺逆及独立日期计算 | 仍用原出生事实 |

`update_bureau`：`-1` 沿用上次设置，`0` 保留最初原盘五行局，`1` 重新定局。
初始默认保留。**重新定局时，大限起岁及起止年份一起改**，没有另一套隐藏的起限局。
显式修改的年干支同时作用于农历和节气输入；没改的字段保留各自历法边界结果。
改安星生日只改日序，不伪造新的日干支；改时支仍用原日干推导时干。

`shift_natal_life_palace()` 只平移宫名与以命宫起的大限／童限宫位，星曜物理落宫、
身宫、宫干和日期不动。后续改盘仍以原命身宫为安星依据。
`reset_natal_chart()` 回到最初原盘，不是撤销最后一步。

流运继续传**原来的 `ResolvedBirth` + 修改后的 `NatalChart`**。
不要拿改后的锚点回写出生资料。C++ 的 `Chart` 替换本命盘后要清除旧 `flow_stack` 再重算；
C ABI 的修改／平移／重置入口会自动返回没有旧流运层的新句柄。

独立起盘对应 `modify_casting_chart()`、`shift_casting_life_palace()`、`reset_casting_chart()`，
盘面语义相同。重置恢复最初抽到的盘，不重新抽签。

核心只保存一个不可变的原盘快照，不持有星历／历法 context，也不形成不断增长的历史链。
不同输出对象可以并行使用同一规则快照；不要让多个线程同时写同一个输出对象。

## 验证与绑定

包含 427 组 JS 对照数据，覆盖六十甲子、男女、天地人盘、连续改盘、平移、重置和报数。
比较包含全部星位和十二层四化标记。编号解码全量检查，但不会在普通 CI 中全排几十万张盘。
另有真实历法流运、无效输入、随机回调、共享库和静态 C ABI 测试。

C ABI 已提供独立的 `taiyin_ziwei_casting_chart`、创建／查询／修改／销毁接口，
可以供 Python、Dart 等绑定接入；它们的高层方法需要各仓库另外更新。
