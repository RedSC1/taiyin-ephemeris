# 八字节气边界模式

> 状态（2026-08-07）：八字模块当前**没有**历史节气模式。节令边界一律使用
> Taiyin 的精确天文定气。历史模式是日历模块 civil-day profile 的能力，八字
> 不消费。若未来需要"按当年颁行历法排盘"，再在此处补充。

## 当前行为

八字模块定位节令边界时使用 `getPrevJie` / `getNextJie`，且只读取返回事件
的 `jd_ut`（精确天文时刻）：

```cpp
double interval_days = birth_jd_ut - reference.jd_ut;        // 起运间隔
double day_coordinate = instant_jd_ut - previous_jie.jd_ut;  // 时柱/分段
```

民用日仅通过 `calendar_civil_day_number()` 用固定公式 `(jd_ut + offset + 0.5)`
计算，不读取 `SolarTermEvent::civil_day_number`，也不读取
`ChineseCalendarConfig::rule_mode`。

因此无论调用方传入 `historical_china_config()` 还是天文配置，八字得到的节令
边界都是同一套精确天文定气，与日历模块的历史 civil-day profile 完全解耦。

## 为什么现在不加

- 现代八字排盘普遍以定气（精确天文节气）定月柱边界，当前行为符合主流口径。
- 日历模块的历史 profile 只改写 `civil_day_number` 字段，八字不消费该字段，
  增加"历史节气模式"开关在当前架构下没有可作用的路径。

## 未来添加计划（暂未实现）

当出现"复刻古代历法"场景时——某个历史日期应按当年实际颁行历法的节气日
（如平气、或特定年代历书）划分月柱——需要新增八字侧的节令边界来源选择：

1. 新增节令边界来源开关，如 `定气`（当前）与 `历史民用日`。
2. 历史来源读取日历模块生成的 `SolarTermEvent::civil_day_number`，或直接查询
   historical profile 的节气民用日。
3. 明确该开关与日历 `rule_mode` 的关系：八字开关独立于日历配置，避免调用方
   必须同步两个 context。

此功能尚未排期。添加前需先确认流派需求（哪些年代、哪些流派依赖历史节气日），
避免为一个无实际消费方的特性增加配置面。
