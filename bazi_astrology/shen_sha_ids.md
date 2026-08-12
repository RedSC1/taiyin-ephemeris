# BaZi Shen Sha ID Registry

This document assigns stable numeric IDs to the Shen Sha catalog that is
currently present in `bazi_core`.

The IDs are wire-format and bitset positions. Once published, an ID must never
be reused or reordered. A removed entry becomes a tombstone. New entries are
appended after the highest allocated ID.

The legacy Dart implementation explicitly labels its rules as experimental,
AI-assisted, and not systematically verified. Therefore this registry freezes
identities only. It does not declare the legacy rule values authoritative.

## Source and method audit

This catalog records selected traditional rule profiles, not one universally
accepted Shen Sha standard. The relevant traditional material is distributed
across works such as *San Ming Tong Hui* (including the chapters on Tian Yi,
San Qi, Tian/Yue De, Xue Tang/Ci Guan, Jie Sha/Wang Shen, Yang Ren/Kong Wang,
Yuan Chen, Gou Jiao, Gu Chen/Gua Su, and Tian Luo Di Wang) and *Yuan Hai Zi
Ping*. The online text used for the initial cross-check is the Chinese Text
Project edition of *San Ming Tong Hui*:
<https://ctext.org/wiki.pl?chapter=117077&if=en>.

The source names above are provenance anchors only. They do not imply that the
modern transmitted tables, or this implementation, are the sole or original
form of each rule. Where transmissions disagree, the implementation must
expose a profile/variant instead of silently presenting one table as canonical.

Confirmed comment-level decisions for the current draft:

- `TIAN_YI_MEDICINE` uses the common modern lookup `month_branch - 1`
  (for example, Yin month -> Chou). Historical almanac traditions contain
  different Tian Yi tables, so this is a named profile.
- `YANG_REN` uses the five-yang-stem table plus the legacy Dart project's
  reverse yin-stem Sheng/Wang variant. This is not the only Yang Ren lineage;
  it must not be described as the universal classical table.
- `SAN_QI_*` currently means set membership of the three required stems. It
  does not enforce order, adjacency, or a particular natal-pillar sequence.
- `TIAN_LUO_DI_WANG`, `TONG_ZI`, `DI_ZHUAN`, and `TIAN_ZHUAN` are explicitly
  variant profiles because target-pillar scope, gender conditions, and even
  Tian/Ti names differ between transmitted tables.
- `ZHENG_XUE_TANG` and `ZHENG_CI_GUAN` currently use the year-pillar Nayin
  only, although some traditions also inspect the day pillar.
- The fixed Ganzhi lists (`SELF60`) are adopted tables. Their exact source
  edition is not yet pinned, so they are not labelled as an original text.

Cross-check references used during this comment audit:

| Topic | Reference | How it is used here |
|---|---|---|
| Tian Yi | [common modern lookup](https://www.deeporacle.ai/bazi/glossary/tian-yi-star) | Supports `month_branch - 1`; historical tables remain a variant concern. |
| Yang Ren | [San Ming Tong Hui, Yang Ren discussion](https://ctext.org/wiki.pl?chapter=864968&if=gb&remap=gb) | Documents the five-yang-stem lineage; it is why the legacy yin-stem table is labelled a variant. |
| Tian De / Yue De | [San Ming Tong Hui, Tian/Yue De discussion](https://www.luckclub.cn/bazi/005/046/) | Cross-checks the month-to-stem mappings and the existence of transmitted variants. |
| Tian She | [Yuan Ding Xie Ji Bian Fang Shu](https://www.shidianguji.com/book/SK1618/chapter/1jurstsete3ux) | Cross-checks the four seasonal Ganzhi pairs. |
| Tian Luo Di Wang | [San Ming Tong Hui, Tian Luo Di Wang](https://www.qj.hk/mobile/jingdian/book/443/13140.html) | Supports the year-Nayin fire/water-earth lineage; gender and target-scope choices remain profile data. |
| Gu Chen / Gua Su | [San Ming Tong Hui, Gu Chen Gua Su](https://www.shidianguji.com/zh/book/SK1610/chapter/1kutiho2qwtn7) | Cross-checks the three-branch group mapping and notes exceptions in the source tradition. |
| Gong Lu / Gong Gui | [San Ming Tong Hui, Gong Lu Gong Gui](https://www.zggdwx.com/sanming/126.html) | Cross-checks the day-hour same-stem and bridging-pillar structure. |

These references are not a claim that every fixed list below has been
critically edited against one edition. They document why a comment says
"selected profile" instead of "the original rule".

## Bitset layout

- IDs `0..65` are allocated below.
- IDs `66..255` are reserved for future audited rules and profile variants.
- The initial storage target is a 256-bit mask represented by four `uint64`
  words.
- Bit `n` corresponds exactly to Shen Sha ID `n`.

## Pillar notation

The draft source and target columns describe the behavior of the legacy Dart
implementation and are inputs to the later rule audit.

- `Y`: natal year pillar
- `M`: natal month pillar
- `D`: natal day pillar
- `H`: natal hour pillar
- `ALL`: original four pillars, Ming Gong, Shen Gong, Tai Yuan, Tai Xi, decade,
  flow year, flow month, flow day, and flow hour
- `CHART`: a relationship involving multiple natal pillars

The final rule format should keep source scope and target scope separate. For
example, Tian Yi Gui Ren may use `Y|D` as sources while matching a target in any
supported natal or transit slot.

## Runtime representation

Rules do not share one global `12 x 12` matrix and are not interpreted by a
runtime rule language. Each simple rule keeps its smallest natural table:

- stem-to-branch rules use ten 12-bit masks;
- branch-to-branch rules use twelve 12-bit masks;
- fixed Ganzhi rules use one 60-bit mask;
- season-dependent Ganzhi rules use four 60-bit masks;
- formula and compound rules remain explicit C++ evaluators.

`data/shen_sha_simple_rules.json` stores Ganzhi values as `[stem_id, branch_id]`
pairs. `tools/generate_shen_sha_tables.py` validates stem/branch parity and
computes the sexagenary index; generated indices are never entered by hand.

`collect_target_shen_sha()` returns an ID-indexed bitset. Storage is supplied by
the C++ or C caller as `uint64_t* + word_capacity`; the C++ evaluator writes
that storage directly. There is no dynamic result allocation or second result
copy at the ABI boundary. A null output with zero capacity is a count-only query.

All IDs `0..65` are implemented. The original entry point intentionally remains
gender-neutral for callers that only need rules independent of gender.
`collect_target_shen_sha_with_gender()` takes `BaziGenderFemale` or
`BaziGenderMale` and additionally evaluates Gou Sha, Jiao Sha, Yuan Chen, Jin
Shen, Tong Zi, San Qi, Tian Luo Di Wang, Gong Lu, and Gong Gui. Both entry points
use the same two-word bitset and target-pillar restrictions.

`tools/shen_sha_dart_oracle.dart` enumerates the complete valid natal chart
space used by the legacy project: 60 year pillars, 12 valid month pillars for
each year stem, 60 day pillars, and 12 valid hour pillars for each day stem,
for 518,400 charts per profile. It evaluates all four natal targets and emits
stable FNV-1a fingerprints for the gender-neutral, male, and female profiles.
The committed C++ test repeats the same enumeration through the native C++
evaluator and locks the gender-neutral fingerprint to `f786d8f1fe672575`, the
male gender-aware fingerprint to `eadbdb6530c916a5`, and the female
gender-aware fingerprint to `15f9f46ec22459ed`.

This exhaustive oracle proves faithful migration of the implemented legacy
rules. It does not turn the legacy project's experimental rule catalog into an
audited historical authority; provenance and variant review remain separate
work.

## Rule-family notation

| Code | Rule shape |
|---|---|
| `S2B` | Source stem selects one or more target branches |
| `B2B` | Source branch selects one or more target branches |
| `M2X` | Month branch selects a target stem, branch, or Ganzhi |
| `SELF60` | Target pillar Ganzhi belongs to a fixed 60-Ganzhi set |
| `SEASON60` | Month season plus target Ganzhi |
| `XUN` | Xun, Kong Wang, or same-xun relationship |
| `NAYIN` | Nayin-derived rule |
| `DAY_STEM` | Day-stem-derived rule |
| `YEAR_GENDER` | Year pillar plus gender or yin-yang direction |
| `CHART` | Multi-pillar relationship |
| `MIXED` | More than one of the above; requires a dedicated evaluator or a generated compound table |

## Stable IDs

| ID | Symbol | Name | Draft family | Draft source | Legacy target restriction |
|---:|---|---|---|---|---|
| 0 | `TIAN_YI_GUI_REN` | 天乙贵人 | `S2B` | `Y\|D` | `ALL` |
| 1 | `YI_MA` | 驿马 | `B2B` | `Y\|D` | `ALL` |
| 2 | `XIAN_CHI_TAO_HUA` | 咸池（桃花） | `B2B` | `Y\|D` | `ALL` |
| 3 | `HONG_LUAN` | 红鸾 | `B2B` | `Y` | `ALL` |
| 4 | `TIAN_XI` | 天喜 | `B2B` | `Y` | `ALL` |
| 5 | `YANG_REN` | 羊刃 | `S2B` | `D` | `ALL` |
| 6 | `FEI_REN` | 飞刃 | `S2B` | `D` | `ALL` |
| 7 | `FU_XING_GUI_REN` | 福星贵人 | `S2B` | `Y\|D` | `ALL` |
| 8 | `ZAI_SHA` | 灾煞 | `B2B` | `Y\|D` | `ALL` |
| 9 | `JIE_SHA` | 劫煞 | `B2B` | `Y\|D` | `ALL` |
| 10 | `WANG_SHEN` | 亡神 | `B2B` | `Y\|D` | `ALL` |
| 11 | `KONG_WANG` | 空亡 | `XUN` | `Y\|D` | `ALL` |
| 12 | `TIAN_CHU_GUI_REN_XUN` | 天厨贵人（本旬） | `XUN` | `Y\|D` | `ALL` |
| 13 | `TIAN_CHU_GUI_REN` | 天厨贵人 | `S2B` | `Y\|D` | `ALL` |
| 14 | `DE_XIU_GUI_REN` | 德秀贵人 | `M2X` | `M` | `ALL` |
| 15 | `TIAN_YI_MEDICINE` | 天医 | `M2X` | `M` | `ALL` |
| 16 | `XUE_REN` | 血刃 | `B2B` | `M` | `ALL` |
| 17 | `YUE_DE_HE` | 月德合 | `M2X` | `M` | `ALL` |
| 18 | `GOU_SHA` | 勾煞 | `YEAR_GENDER` | `Y` | `ALL` |
| 19 | `JIAO_SHA` | 绞煞 | `YEAR_GENDER` | `Y` | `ALL` |
| 20 | `YUAN_CHEN` | 元辰 | `YEAR_GENDER` | `Y` | `ALL` |
| 21 | `GU_CHEN` | 孤辰 | `B2B` | `Y` | `ALL` |
| 22 | `GUA_SU` | 寡宿 | `B2B` | `Y` | `ALL` |
| 23 | `HONG_YAN_SHA` | 红艳煞 | `S2B` | `D` | `ALL` |
| 24 | `JIN_YU` | 金舆 | `S2B` | `D` | `ALL` |
| 25 | `JIN_SHEN` | 金神 | `CHART` | `D` | `H` |
| 26 | `TIAN_SHE_DAY` | 天赦日 | `SEASON60` | `M` | `D` |
| 27 | `LIU_XIA` | 流霞 | `S2B` | `D` | `ALL` |
| 28 | `SANG_MEN` | 丧门 | `B2B` | `Y` | `ALL` |
| 29 | `DIAO_KE` | 吊客 | `B2B` | `Y` | `ALL` |
| 30 | `PI_MA` | 披麻 | `B2B` | `Y` | `ALL` |
| 31 | `TONG_ZI` | 童子 | `MIXED` | `Y\|M` | `D\|H` |
| 32 | `TIAN_DE_HE` | 天德合 | `M2X` | `M` | `ALL` |
| 33 | `SAN_QI_TIAN` | 三奇贵人（天） | `CHART` | `CHART` | `D` |
| 34 | `SAN_QI_DI` | 三奇贵人（地） | `CHART` | `CHART` | `D` |
| 35 | `SAN_QI_REN` | 三奇贵人（人） | `CHART` | `CHART` | `D` |
| 36 | `JIANG_XING` | 将星 | `B2B` | `Y\|D` | `ALL` |
| 37 | `HUA_GAI` | 华盖 | `B2B` | `Y\|D` | `ALL` |
| 38 | `KUI_GANG` | 魁罡 | `SELF60` | none | `D` |
| 39 | `SHI_LING_DAY` | 十灵日 | `SELF60` | none | `D` |
| 40 | `BA_ZHUAN_DAY` | 八专日 | `SELF60` | none | `D` |
| 41 | `LIU_XIU_DAY` | 六秀日 | `SELF60` | none | `D` |
| 42 | `JIU_CHOU_DAY` | 九丑日 | `SELF60` | none | `D` |
| 43 | `SI_FEI_DAY` | 四废日 | `SEASON60` | `M` | `D` |
| 44 | `SHI_E_DA_BAI` | 十恶大败 | `SELF60` | none | `D` |
| 45 | `TIAN_LUO_DI_WANG` | 天罗地网 | `MIXED` | `Y\|CHART` | `ALL` |
| 46 | `YIN_CHA_YANG_CUO` | 阴差阳错 | `SELF60` | none | `D` |
| 47 | `GU_LUAN_SHA` | 孤鸾煞 | `SELF60` | none | `D` |
| 48 | `GONG_LU` | 拱禄 | `CHART` | `D\|H` | `D` |
| 49 | `GONG_GUI` | 拱贵 | `CHART` | `D\|H` | `D` |
| 50 | `DI_ZHUAN` | 地转 | `SEASON60` | `M` | `ALL` |
| 51 | `TIAN_ZHUAN` | 天转 | `SEASON60` | `M` | `ALL` |
| 52 | `TAI_JI_GUI_REN` | 太极贵人 | `S2B` | `Y\|D` | `ALL` |
| 53 | `WEN_CHANG_GUI_REN` | 文昌贵人 | `S2B` | `Y\|D` | `ALL` |
| 54 | `GUO_YIN_GUI_REN` | 国印贵人 | `S2B` | `Y\|D` | `ALL` |
| 55 | `TIAN_DE_GUI_REN` | 天德贵人 | `M2X` | `M` | `ALL` |
| 56 | `YUE_DE_GUI_REN` | 月德贵人 | `M2X` | `M` | `ALL` |
| 57 | `LU_SHEN` | 禄神 | `S2B` | `D` | `ALL` |
| 58 | `RI_GAN_XUE_TANG` | 日干学堂 | `S2B` | `D` | `ALL` |
| 59 | `RI_GAN_CI_GUAN` | 日干词馆 | `S2B` | `D` | `ALL` |
| 60 | `ZHENG_XUE_TANG` | 正学堂 | `NAYIN` | `Y` | `ALL` |
| 61 | `ZHENG_CI_GUAN` | 正词馆 | `NAYIN` | `Y` | `ALL` |
| 62 | `GUAN_GUI_XUE_TANG` | 官贵学堂 | `DAY_STEM` | `D` | `ALL` |
| 63 | `GUAN_GUI_CI_GUAN` | 官贵词馆 | `DAY_STEM` | `D` | `ALL` |
| 64 | `GUAN_XING_XUE_TANG` | 官星学堂 | `DAY_STEM` | `D` | `ALL` |
| 65 | `XUE_TANG_HUI_GUI` | 学堂会贵 | `MIXED` | `Y\|D` | `ALL` |

## Audit notes

Before generating C++ tables, every entry must receive:

1. A cited source and edition.
2. A precise rule statement.
3. A source-pillar scope.
4. A target-pillar scope.
5. A profile or lineage identifier when variants disagree.
6. Positive and negative test vectors.

The following legacy behaviors deserve early review:

- `SAN_QI_*` currently checks set membership but not ordering or adjacency.
- `DI_ZHUAN` and `TIAN_ZHUAN` currently apply to every target slot even though
  the comments describe day-based rules.
- `ZHENG_XUE_TANG` and `ZHENG_CI_GUAN` comments mention year/day variants, while
  the implementation uses only the year pillar.
- `YANG_REN` includes yin-stem mappings; this differs among lineages.
- Generic rules currently apply to all extra and transit pillars unless a rule
  explicitly restricts the target.
- `TIAN_LUO_DI_WANG`, `TONG_ZI`, and the Xue Tang/Ci Guan family combine
  multiple traditions and should not be frozen as a single universal profile.
