# 恒星黄道与 Ayanamsha 模型

文档状态：当前说明

占星扩展把 ayanamsha（岁差偏移、恒星黄道零点差）视为一个有名字的恒星黄道
零点模型：

```text
恒星黄经 = normalize(热带黄经 - ayanamsha)
```

它不是星历路线，不是章动模型，也不会替代 `NativeCalcContext` 选择的天文学
岁差模型。

## 内置模型

调用参数 `ayanamsha_id` 当前支持：

- Fagan/Bradley
- Lahiri
- Raman
- Krishnamurti
- Galactic Center 0 Sagittarius，使用内置 Sgr A* 天体测量参数锚定
- True Chitra，使用内置 Spica 天体测量参数锚定

参考历元型模型会保留其历史参考岁差模型。恒星锚定型模型通过正常的固定星位置
链路传播内置 ICRF 天体测量参数，不依赖调用方安装的恒星表。

## 岁差策略 Flags

恒星黄道调用直接接收 native context、ayanamsha ID 与 `uint64_t flags`。历史定义
默认保留 native context 选择的岁差模型，并补偿模型口径差。两个高位 flags 可以覆盖
默认行为，且二者互斥：

- `TAIYIN_SIDEREAL_RAW_REFERENCE_OFFSET` 保留 native 岁差模型，但不做补偿。
- `TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION` 在模型条目声明了参考岁差模型时，
  使用其 `reference_precession_model_id`。

三种策略都不会修改进程全局的岁差配置。

## 自定义模型

自定义 ID 从 `TAIYIN_SIDEREAL_AYANAMSHA_CUSTOM_START` 开始。调用方用
`add_ayanamsha_model()` 注册 `AyanamshaModelEntry`。内置 ID 和已经注册的
自定义 ID 不能被覆盖；未知 ID 会明确报错，不会静默替换成另一个流派。

evaluator 收到 `AyanamshaDispatchData`，其中包含借用的 native context、ayanamsha
ID、TT 时刻、native position flags、sidereal flags 和条目的 `model_data`。注册表
不拥有 `model_data`，同一 evaluator 可能被多个线程并发调用；回调代码和
`model_data` 必须在进程剩余生命周期内保持已加载、有效且可安全并发读取。evaluator
返回 `Status`；成功且有限的结果会统一归一化到 `[0, 2*pi)`。

`reference_precession_model_id` 是可选元数据。负值表示即使选择
`TAIYIN_SIDEREAL_USE_REFERENCE_PRECESSION`，也继续使用 native context 的
岁差模型；非负值必须对应一个已经注册的岁差模型。

这个注册表是底层扩展点。以后可以在其上增加“用户给定参考历元与偏移”的强类型
便捷对象，而不需要引入可变的全局恒星黄道状态。

## 位置 API

`AstrologyContext` 持有一份配置后的 `NativeCalcContext`。调用方先用
`configure_astrology_context()` 选择 ayanamsha、参考面策略和可选参考历元；随后其
内嵌的 `native_context` 会把该恒星黄道面安装为
`TAIYIN_APPARENT_FRAME_CUSTOM`。普通 `calc_position_*()`、`calc_state_*()`
因而可以直接使用它，并和其他 native 参考面走同一条输出矩阵管线；月交点、月拱点等
已注册占星目标也适用。

配置时传入的低位 native-position flags 会固定恒星锚定 ayanamsha 模型采用的物理
修正口径；输出形状位在该模型求值时会被屏蔽。之后每次调用的 native flags 仍控制
目标本身的计算，但不会悄悄重定义已经配置好的参考面。

custom-frame evaluator 返回 ICRF 到恒星黄道面的旋转矩阵。速度和加速度所需的矩阵
导数由现有 apparent-position 管线统一求取，不再由 sidereal wrapper 手工
旋转。复制 `AstrologyContext` 时会自动修复 callback 指向自身的 data 指针。

`calc_ayanamsha_tt()` 遵循扩展 ayanamsha 语义：默认返回所选平 ayanamsha
加黄经章动 `dpsi`；加上 `TAIYIN_NATIVE_POSITION_NONUT` 则返回平
ayanamsha。恒星黄道位置内部仍使用平 ayanamsha，因为真热带黄经和
扩展 ayanamsha 中的章动应当相消，不能重复施加。

`calc_sidereal_position_tt()`、
`calc_sidereal_position_ut()` 和 `calc_sidereal_coordinates_*()` 仍作为便捷入口，
内部临时配置同样的 `AstrologyContext`；所以继续拒绝 XYZ 与赤道输出 flags。
默认当日黄道面上的 `SiderealPosition::tropical_longitude_rad` 遵循公开
ayanamsha 口径：默认返回视/真热带黄经，带 `NONUT` 时返回平热带黄经，因而
`sidereal_longitude_rad + calc_ayanamsha_tt()` 会还原该字段（模 `2*pi`）。固定
J2000、`ECL_T0` 或太阳系不变面上的该字段仍是对应参考面内的未偏移黄经，不是
当日热带黄经。

## 恒星黄道参考面

恒星黄道调用的低 32 位仍是普通 native-position flags；高 32 位专门控制参考面，
由同一个 resolver 检查冲突：

- 不设参考面位：默认当日平黄道；
- `TAIYIN_SIDEREAL_REFERENCE_J2000_ECLIPTIC`：固定 J2000.0 平黄道；
- `TAIYIN_SIDEREAL_REFERENCE_ECL_T0`：固定到 `reference_epoch_jd` 的平黄道；
- `TAIYIN_SIDEREAL_REFERENCE_SSY_PLANE`：太阳系不变面，并以
  `reference_epoch_jd` 定义零点方向；
- `TAIYIN_SIDEREAL_REFERENCE_EPOCH_UT1`：把该参考历元解释为 UT1；只允许和
  `ECL_T0` 或 `SSY_PLANE` 组合。

`ECL_T0` 和 `SSY_PLANE` 必须传有限的 split-JD `reference_epoch_jd`；其他
参考面不传参考历元。固定面与不变面是完整三维 ICRF 旋转，不是简单减去一个黄经偏移。
`EQUATORIAL` 是 native 输出形状选择：它会在该次调用中覆盖已配置的恒星黄道
自定义面并返回当日平/真赤道坐标；参考面和岁差策略仍保存在 astrology context
中，后续黄道输出继续使用。

C ABI 目前保留无状态便捷形式：使用同样的高位含义（名称带 `TAIYIN_C_` 前缀），
并在每个恒星黄道位置/坐标调用中传可选的 split-JD
`reference_epoch_jd` 指针；不使用参考历元时传空指针。
