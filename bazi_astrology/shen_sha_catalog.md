# Shen Sha user modules

This API follows Ziwei's immutable user-module pattern. The new C ABI bridge
is intended for beta12; it does not change the original 66-bit Shen Sha results.
Include `<taiyin/bazi/shen_sha_catalog.h>` and link `taiyin_bazi_extension`.

```cpp
using namespace taiyin::bazi;
BaziShenShaCatalog defaults;
BaziShenShaModule school("my-school", {
    {"day-marker", "Day marker", [](const BaziShenShaInput& input) {
        return input.target_kind == BaziShenShaTargetDay;
    }}
});
auto catalog = defaults.add_module(school);
auto context = catalog.create_context();
// chart is a BaziChart produced by calculate_chart().
auto matches = context.evaluate(chart, chart.pillars.day,
                                BaziShenShaTargetDay, BaziGenderMale);
auto without_school = catalog.remove_module("my-school");
// context still contains my-school:day-marker.
```

## Definitions versus selections

- The catalog always retains the default 66 definitions (`builtin:0` through
  `builtin:65`). They cannot be replaced or removed. No `replace()` API exists.
- A user module has one unique label and unique local rule IDs. Its full IDs
  are `label:rule-id`. Keys contain no colon or whitespace; `builtin` and
  `option1` are reserved module labels. Display names can contain spaces.
- Duplicate labels/IDs are errors, not implicit overwrites. Unknown module
  removal is an error. `remove_module("my-school")` removes **all** rules from
  that module, not just one match or one target's rule.
- `BaziShenShaSelection.disabled_ids` disables selected entries in a new
  context without changing definitions. Unknown or repeated IDs are errors.
  An alternate school adds a new ID and disables the default entry in its
  selection; it never rewrites that default entry.
- Empty selection enables all default and added rules. A context owns a
  snapshot, independent of later catalog/selection changes and lifetimes.

## Evaluation and concurrency

Default matches are calculated once per target using the existing native
collector. A match has `id`, `name`, and `builtin_id`: the latter is -1 for
custom rules; built-in names are empty and callers use the stable enum ID for
localized display. Results are ordered by built-in ID, then module and rule
insertion order. Arbitrary user rules do not acquire default bitset positions.

Callbacks are synchronous predicates of a chart copy, target Ganzhi, target
kind, and gender (-1 means unspecified). With unspecified gender, an unknown hour (`0xff`) is
accepted as in the neutral collector; callbacks needing an hour must handle
that sentinel. Gender-dependent evaluation requires a valid hour. Year,
month, day and the requested target must remain valid in either mode.
Callbacks must not retain references to
that temporary input. Callback exceptions propagate; no partial result is
returned. Only rule membership/configuration is immutable: captured mutable
objects are not deep-copied. Concurrent evaluation requires pure/thread-safe
callbacks; do not mutate or assign the same catalog/context object during a
call. There is no global registry and no ephemeris ownership dependency.

## 中文说明

这套接口与紫微的“不可变用户模块 + 计算上下文快照”对齐，不复制 JS 的
`replace` 内置规则功能。默认 66 种永远保留；另一流派以新 ID 注册，在
`disabled_ids` 中停用不采用的默认判定即可。停用只是选择，不是删除定义。

`remove_module("my-school")` 会移除该用户模块的**全部神煞规则**，不分本命、
命身宫或流运目标；它返回新目录，已有上下文继续使用旧快照。删除不存在的
模块、删除内置命名空间或重复添加模块均报错。

新增 C ABI 和 Python/Dart 桥接正在为 beta12 准备，尚未发布。
原有默认神煞位集函数完全不受影响。

未指定性别时允许时柱未知（`0xff`），与原无性别接口一致；自定义回调需自行
处理未知时辰。指定性别时仍要求有效时柱，年月日和目标干支始终需要有效。

## C ABI ownership

Include `<taiyin/c/bazi.h>` and link the BaZi shared module. Catalog and context
handles are immutable snapshots. `taiyin_bazi_shen_sha_evaluate` returns an
owned matches handle; count/get only read it and never evaluate rules again.
Destroy every handle using its matching destroy function. Strings returned by
matches_get remain borrowed until matches_destroy.

Predicates receive borrowed four-pillar bytes, target, target kind and gender.
Return 0/1 for false/true, or -1 to report a callback failure (mapped to
TAIYIN_ERROR_INTERNAL). Other return values are invalid arguments. Exceptions
must never cross the C boundary. On failure no partial matches are returned.

The C caller owns callback code and user_data. Keep them alive until **all**
derived catalog/context handles are destroyed, including snapshots made before
removing a module. Do not destroy or assign handles during their own calls.
Predicates run synchronously on the calling thread. C callers are responsible
for callback thread safety; Python holds the GIL, and Dart uses isolate-local
callbacks with explicit close and shared-callback reentry protection.

C 调用方负责回调与 user_data 的生命周期；删除目录里的模块不代表旧上下文
已经不再引用该回调。求值返回独立结果句柄，读取结果不会重新执行回调。
Python 自动持有回调引用；Dart 应显式 close，不允许跨 isolate 共享回调句柄。
