# 日食原始 JSON 导出

`taiyin_solar_eclipse_export` 是一个位于 `tools/` 的命令行工具。它把
Taiyin runtime 已经计算出的全球日食事件、路线曲线和区域多边形写成
版本化 JSON，供地图、历书和静态站点生成程序继续处理。

它不是新的日食求解器，也不是网页产品层。工具不会重新计算中心线或
边界；所有数值都来自 `eclipse_search` 的公开搜索和 route-product API。

## 构建与调用

```bash
cmake -S . -B build
cmake --build build --target taiyin_solar_eclipse_export

./build/taiyin_solar_eclipse_export \
  --data-root data \
  --start 2026-01-01 \
  --end 2027-01-01 \
  --route-samples 400 \
  --output forecast.json
```

不传 `--output` 或传入 `--output -` 时，JSON 写到标准输出。
`--route-samples N` 控制导出的路线曲线和 polygon 点密度。默认是 `400`；
更大的值会让大比例尺地图路径更平滑，但 JSON 更大、导出更慢。它不是
星历或日食模型精度开关。

默认使用光滑平均月缘。需要把已挂载的 TLL1 月缘用于接触时刻和路线
边界时，显式传入模型文件：

```bash
./build/taiyin_solar_eclipse_export \
  --data-root data \
  --lunar-limb data/lunar-limb/kaguya_lalt_16ppd.tll1 \
  --start 2026-08-01 \
  --end 2026-09-01 \
  --output forecast-tll1.json
```

仅把 TLL1 文件放进数据目录不会改变结果；`--lunar-limb` 是显式开启
修正的选项。文件加载失败时工具返回错误，不会静默退回光滑月缘。

时间范围也可以直接使用 Julian day：

```bash
./build/taiyin_solar_eclipse_export \
  --data-root data \
  --start-jd-ut 2461041.5 \
  --end-jd-ut 2461406.5
```

起点包含在搜索范围内，终点不包含。日历日期严格接受
`YYYY-MM-DD`，范围是 `0001-01-01` 至 `9999-12-31`。历法与
`taiyin::julian_day()` 一致：1582-10-15 起使用格里历，更早日期使用
儒略历，并拒绝历法切换中不存在的 1582-10-05 至 1582-10-14。

## JSON 契约

顶层字段如下：

```json
{
  "schema": "taiyin.solar-eclipse-forecast",
  "schema_version": 1,
  "time_scale": {},
  "query": {},
  "models": {},
  "events": []
}
```

`schema_version` 控制字段契约。下游程序应先验证 `schema` 和
`schema_version`，不要根据某个示例文件猜测字段。

每个事件包含：

- 由最大食日历日期生成的稳定 `event_id`；
- `kind_flags` 和可读的 `kind` 名称；
- 最大食时刻、预测 Delta T 和最大食地点；
- P1、C1、食甚、C4、P4；
- 轴距、半影/本影半径和几何 margin；
- 中心线、本影/伪本影南北界、偏食始终接触线、晨昏食甚线、半影南北界和半食分线；
- core、penumbral 和 half-magnitude polygon；
- 路线计数，以及闭合 polygon 可用时的经纬度范围和反经线标志。

`route_product.available` 表示是否返回了路线曲线或 polygon；
`route_product.polygon_available` 单独表示是否存在闭合 polygon。

非中心日偏食会导出实际存在的一侧半影界；当食分达到 0.5 时，还会
导出相应的半食分界，因此 route 可用。另一侧边界由日出、日落和接触
时刻对应的地平线几何闭合，并不是第二条半影界。导出器会保留这条
sunrise/sunset maximum boundary，并用它闭合 penumbral polygon；达到
0.5 食分时也用对应晨昏边界闭合 half-magnitude polygon。浅偏食没有
0.5 食分等值线时，空的 half-magnitude 曲线和 polygon 是正常结果。

中心食会在对应边界取得足够点数时生成闭合的 core、penumbral 或
half-magnitude polygon；宽层缺少一侧物理极限界时使用晨昏食甚边界。
`polygon_available` 只表示至少生成了一层；
每一层是否可用应检查对应的 `*_polygon_point_count` 和 polygon 数组。
如果没有任何一层能够闭合，`polygon_available` 为 `false`，并由
`polygon_reason` 说明原因。

默认光滑中心食路线会在可解时生成 `curves.core_begin_horizon` 和
`curves.core_end_horizon`，core polygon 使用它们连接南北界；任一端无法
求解时则不导出 core polygon。Penumbral 和
half-magnitude polygon 使用各自的晨昏食甚线以及物理上存在的极限界
闭合，各分支在有解/无解切换处使用精修端点。

不适用于某种日食的时间或几何量写成 JSON `null`。输出不会包含
`NaN` 或 `Infinity`。

## 时间尺度

第一版工具显式使用估算 Delta T 的预测 UT1：

```json
"time_scale": {
  "name": "UT1_ESTIMATED"
}
```

`jd_ut` 和 `calendar_ut` 因此表示预测 UT1。未来 civil UTC 与 UT1
之间还存在尚未观测到的 DUT1；网页可以在秒级展示中近似标为 UTC，
但数据处理层不应把二者描述成完全相同。

## 模型边界

输出会明确声明实际使用的模型和 flags。默认模式为：

```json
"models": {
  "earth_surface": "wgs84_sea_level",
  "terrain": "none",
  "lunar_limb": "smooth_mean",
  "lunar_limb_correction_enabled": false,
  "lunar_limb_source_id": null,
  "lunar_limb_generation": null,
  "eclipse_search_flags": 8589934592,
  "eclipse_route_flags": 0,
  "route_sample_count": 400
}
```

传入 `--lunar-limb` 后，`lunar_limb` 为 `tll1`，修正开关为 `true`，
并写出 TLL1 header 中的 `source_id` 和 `generation`。搜索 flags 同时作用于
日食接触时刻，路线 flags 作用于中心线以外的各层边界和 polygon；中心线
由影轴定义，不因月缘模型改变。

这些字段表示导出工具使用 WGS84 海平面椭球，并按所选模式使用光滑平均
月缘或 TLL1 月缘：

- 不使用地球 DEM 或观测点真实海拔；
- 不使用山脉、建筑或真实地平线；
- 默认导出不使用 Kaguya TLL1 修正；
- 只有显式传入 `--lunar-limb` 时，接触时刻和路线边缘才统一启用 TLL1。

事件、曲线和 polygon 的基本组织方式不依赖具体月缘模型。

## 下游职责

建议由独立的历书或地图仓库处理：

- GeoJSON 转换和反经线切割；
- 地图投影、抽稀和样式；
- SVG/PNG/交互地图；
- 城市、行政区、人口和天气数据；
- GitHub Pages 与定时 Action。

下游不应重新实现日食几何。Taiyin 导出的原始 JSON 是天文计算结果，
地图项目只负责转换、展示和产品数据组合。
