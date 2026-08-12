# Occultation Search

文档状态：当前说明
最后审阅：2026-07-05

主要头文件：`include/taiyin/runtime/occultation_search.h`

当前 occultation 模块提供月掩恒星和月掩太阳系天体搜索。它面向“从某个时间开始，寻找下一次或上一次月亮掩指定目标”的工作流，接口使用 Taiyin 自己的 context、flag 和结果结构约定。

## 当前公开入口

```cpp
Status search_next_geocentric_lunar_star_occultation_ut(
    const NativeCalcContext* context,
    const char* star_key,
    double jd_start_ut,
    uint64_t flags,
    LunarStarOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status search_next_local_lunar_star_occultation_ut(
    const NativeCalcContext* context,
    const char* star_key,
    double jd_start_ut,
    uint64_t flags,
    LunarStarOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status search_next_geocentric_lunar_body_occultation_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_radius_km,
    double jd_start_ut,
    uint64_t flags,
    LunarBodyOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status search_next_local_lunar_body_occultation_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_radius_km,
    double jd_start_ut,
    uint64_t flags,
    LunarBodyOccultationSearchResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status compute_lunar_star_occultation_local_visibility_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const LunarStarOccultationSearchResult* occultation,
    uint64_t visibility_flags,
    LunarOccultationLocalVisibility* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status compute_lunar_body_occultation_local_visibility_ut(
    const NativeCalcContext* context,
    int body_id,
    const LunarBodyOccultationSearchResult* occultation,
    uint64_t visibility_flags,
    LunarOccultationLocalVisibility* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status compute_lunar_star_occultation_where_ut(
    const NativeCalcContext* context,
    const char* star_key,
    const LunarStarOccultationSearchResult* occultation,
    uint64_t flags,
    LunarOccultationWhereResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;

Status compute_lunar_body_occultation_where_ut(
    const NativeCalcContext* context,
    int body_id,
    double target_radius_km,
    const LunarBodyOccultationSearchResult* occultation,
    uint64_t flags,
    LunarOccultationWhereResult* out,
    EphemerisEvalDiagnostic* diagnostic = nullptr
) noexcept;
```

`star_key` 使用已加载的 TSC1/TSF1 star catalog 查询。应用应先调用 `add_global_tsc1_star_catalog()` 或 `add_global_tsf1_star_catalog()` 加载恒星 catalog。

`body_id` 是 Taiyin/NAIF 风格的太阳系目标 ID；search 和 `where` 入口的 `target_radius_km` 是显式传入的圆盘半径，半径为 `0` 明确表示点目标。core runtime 不会根据绝对星等或反照率猜测小天体直径。local visibility 只采样地平坐标，所以只需要 `body_id`，不需要半径。月亮、地球、EMB、太阳和 SSB 不是有效的月掩 body target；太阳遮掩属于日食 API，不走这里。

这四个 body 月掩入口也保留不带 `target_radius_km` 的便捷重载：已知主天体从 Taiyin 的共享平均物理半径表取值，未知 body ID 仍是点目标。新接入方如果已有目标的物理尺寸，应直接传入半径。

`search_next_geocentric_lunar_star_occultation_ut()` 和 `search_next_geocentric_lunar_body_occultation_ut()` 使用地心口径判断月亮和目标的 apparent 方向是否相交。

`search_next_local_lunar_star_occultation_ut()` 和 `search_next_local_lunar_body_occultation_ut()` 使用 `NativeCalcContext` 内的 observer/topocentric 信息做最终地表判定；如果 context 没有 observer location 或 topocentric offset，会返回 `TAIYIN_ERROR_INVALID_ARGUMENT`。新 API 不再额外传经纬度参数。

`compute_lunar_*_occultation_local_visibility_ut()` 不重新搜索事件。它接收已经算出的月掩结果，在 C1/C2/最大掩/C3/C4 这些时刻采样本地地平坐标，并返回 Moon、target、Sun 的高度/方位和可见性 bit。这个入口同样要求 context 已经设置 observer。

`compute_lunar_*_occultation_where_ut()` 也不重新搜索事件。它接收已经算出的月掩事件；该事件可以来自 geocentric search，也可以来自 local/topocentric search，因为 `where` 入口只使用事件类型和最大掩时刻。它使用 Taiyin 的解析直线—扁地球椭球求交，把 Moon-target 中心线投影到地球表面。中心线命中地球时返回中心线落点、中心线采样路径、外接触边界带和闭合 polygon；中心线没有打到地球时，返回非中心掩的第一版最佳观测点。

## 能力边界

当前模块优先支持“指定目标月掩”工作流，而不是做整张星表的批量天象表。

对应到 Taiyin，已经有的部分是：

- 指定固定星或太阳系 body target，从给定 UT 起点搜索下一场或上一场地心月掩；
- 指定固定星或太阳系 body target，从给定 UT 起点搜索下一场或上一场地方/topocentric 月掩；
- 返回最大掩/最近角距时刻和接触时刻；
- 返回基于最大掩几何和中心线几何的掩食类型 bit，例如全掩、偏掩、切掩、中心掩或非中心掩；
- 对已有地方月掩结果采样接触点和最大掩时刻的 Moon/target/Sun 高度方位；
- 对已有月掩事件计算第一版 `where` 中心线/最佳观测点；中心线未命中地球的非中心掩会返回最佳观测点。

尚未补完的方向主要是：

- 公告级 `where` 地表区域，例如月缘地形/地形高程修正后的南北界；
- 更完整的地方可见性产品，例如把升落事件关联到具体可见区间、生成面向 almanac 的文字摘要；
- 更多公开月掩预报或外部参考 oracle 覆盖，尤其是边界和擦边 case。

扫描整张固定星表并列出所有月掩星更像 almanac/catalog 层功能，不是当前 core runtime 的目标。以后如果要做，应先设计星表预筛、时间窗口管理和输出格式，而不是直接把当前 next-search 对每颗星循环调用。

## Flags

月掩 API 使用 64-bit flags。低 32 位是和 `calc_position_*` 共享的 native position 修饰位，高 32 位是月掩搜索或可见性选项：

```cpp
TAIYIN_OCCULTATION_POSITION_FLAGS_MASK
TAIYIN_OCCULTATION_OPTION_FLAGS_MASK

TAIYIN_OCCULTATION_SEARCH_TRUEPOS
TAIYIN_OCCULTATION_SEARCH_BACKWARD
TAIYIN_OCCULTATION_SEARCH_ONE_CANDIDATE
TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION

TAIYIN_OCCULTATION_FILTER_PARTIAL
TAIYIN_OCCULTATION_FILTER_TOTAL
TAIYIN_OCCULTATION_FILTER_GRAZING
TAIYIN_OCCULTATION_FILTER_CENTRAL
TAIYIN_OCCULTATION_FILTER_NONCENTRAL

TAIYIN_OCCULTATION_VISIBILITY_REFRACTION
```

默认向未来搜索。设置 `TAIYIN_OCCULTATION_SEARCH_BACKWARD` 后从 `jd_start_ut` 向过去找上一场。

`TAIYIN_OCCULTATION_SEARCH_TRUEPOS` 是低 32 位 native `TAIYIN_NATIVE_POSITION_TRUEPOS` 的兼容别名。设置后，搜索使用 true/geometric position 口径，关闭 light-time、aberration、deflection 等 apparent 修正。这个 flag 会同时影响 seed 候选和最终 separation 判定。搜索入口也接受 `TAIYIN_NATIVE_POSITION_ASTROMETRIC`、`TAIYIN_NATIVE_POSITION_NO_ABERR` 和 `TAIYIN_NATIVE_POSITION_NO_GDEFL`。`XYZ`、`EQUATORIAL`、`RADIANS`、`SPEED`、`TOPOCENTRIC` 这类输出形态 native flag 会被拒绝，因为月掩 search result 的输出形态是固定的。

设置 `TAIYIN_OCCULTATION_SEARCH_ONE_CANDIDATE` 后，函数只检查 `jd_start_ut` 附近的第一个月亮经过候选。如果这个候选不是月掩，或者不匹配指定 filter，函数返回 `TAIYIN_EVENT_ERROR_NOT_FOUND`，并填充 `candidate_jd_ut`、`next_search_jd_ut` 和 `candidate_count`，方便调用方自己继续扫描。

设置 `TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION` 后，最终事件分类和接触时刻会用方向相关 TLL1 轮廓替换光滑月球半径。候选生成和最近角距搜索仍使用光滑模型；光滑事件解作为修正接触扫描的 seed，随后按 TLL1 余量建立根区间。有限差分牛顿步只有在留在区间内且降低余量时才会采用，否则退回受保护的割线/二分。该选项要求全局 runtime 已加载 TLL1，否则返回 `TAIYIN_ERROR_UNSUPPORTED`。只加载模型而不设置 flag 不会改变结果。

类型过滤 bit 用来限制接受的事件分类。不设置 filter 时，任何月掩类型都可返回；设置一个或多个 filter 后，只有最终 `type_flags` 与 filter 相交的事件会被接受，否则普通搜索继续尝试下一个候选。在 one-candidate 模式下，filter 不匹配会在第一个候选后停止。

`TAIYIN_OCCULTATION_SEARCH_BACKWARD` 和 `TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION` 只用于 `search_next_*` 入口。`compute_lunar_*_occultation_where_ut()` 是给定事件时刻的地表几何计算，目前还没有把 TLL1 接入其边界，因此只接受 `TAIYIN_OCCULTATION_SEARCH_TRUEPOS` 和 `TAIYIN_OCCULTATION_VISIBILITY_REFRACTION`。

`TAIYIN_OCCULTATION_VISIBILITY_REFRACTION` 只用于 local visibility summary。默认 visibility 采样使用几何地平高度；设置该位后，返回的高度/方位来自折射后的 horizontal 坐标。调用方需要先在 context 里设置 atmosphere，否则底层 observed/refraction 链路会按现有规则报错。

## 返回结果

```cpp
struct LunarStarOccultationSearchResult {
    int kind;
    uint32_t type_flags;
    double jd_ut;
    double begin_jd_ut;
    double end_jd_ut;
    double first_contact_jd_ut;
    double second_contact_jd_ut;
    double third_contact_jd_ut;
    double fourth_contact_jd_ut;
    double separation_rad;
    double moon_radius_rad;
    double target_radius_rad;
    double margin_rad;
    double candidate_jd_ut;
    double next_search_jd_ut;
    int candidate_count;
    int iteration_count;
    int evaluation_count;
};
```

`jd_ut` 是月亮和目标天球角距最小的时刻，也就是最大掩/最近角距时刻。

`first_contact_jd_ut` 到 `fourth_contact_jd_ut` 是 C1-C4 接触时刻。C1/C4 解的是 `moon_radius_rad + target_radius_rad - separation_rad = 0`，C2/C3 解的是 `abs(moon_radius_rad - target_radius_rad) - separation_rad = 0`。`begin_jd_ut` 是 `first_contact_jd_ut` 的兼容别名，`end_jd_ut` 是 `fourth_contact_jd_ut` 的兼容别名。恒星按点目标处理，所以 `target_radius_rad = 0`，C2/C3 保持 NaN；太阳系 body 使用平均物理半径和当时 observer-target 距离计算目标视半径。

`margin_rad = moon_radius_rad + target_radius_rad - separation_rad`。当它非负时，月亮视圆面与目标视圆面相交，搜索返回 `TAIYIN_STATUS_OK`。没有找到事件时返回 `TAIYIN_EVENT_ERROR_NOT_FOUND`，`kind` 保持 `TAIYIN_OCCULTATION_KIND_NONE`。

`candidate_jd_ut` 记录本次调用最后一次精修过的月亮经过候选；`next_search_jd_ut` 是建议调用方继续扫描时使用的下一个起点；`candidate_count` 是本次调用实际 probe 的候选数量。这几个字段主要服务 `TAIYIN_OCCULTATION_SEARCH_ONE_CANDIDATE` 和以后更高层的批量扫描。

`type_flags` 是传统事件分类 bit：

```cpp
TAIYIN_OCCULTATION_TYPE_PARTIAL
TAIYIN_OCCULTATION_TYPE_TOTAL
TAIYIN_OCCULTATION_TYPE_ANNULAR
TAIYIN_OCCULTATION_TYPE_GRAZING
TAIYIN_OCCULTATION_TYPE_CENTRAL
TAIYIN_OCCULTATION_TYPE_NONCENTRAL
TAIYIN_OCCULTATION_TYPE_CENTRALITY_UNAVAILABLE
```

普通月掩恒星/body search 结果根据最大掩时刻的视半径和最小角距填充 `PARTIAL`、`TOTAL`、`GRAZING`，并在同一最大掩时刻尽量用中心线与地球相交几何填充 `CENTRAL` 或 `NONCENTRAL`。`TAIYIN_OCCULTATION_TYPE_ANNULAR` 仍然作为保留 type bit 存在，但当前非太阳月掩搜索不会返回它。中心线分类依赖当前模型上下文可计算 GAST；如果该模型组合不支持，search 仍会返回已求出的事件和接触时刻，并设置 `CENTRALITY_UNAVAILABLE`，而不是把整个事件判失败。`compute_lunar_*_occultation_where_ut()` 使用同一套中心线几何，并额外返回中心线/最佳观测点经纬度；作为专门的 where 入口，它在中心线几何不可用时会返回错误。固定星按点目标处理，通常会分类为 `TOTAL` 或切掩。

## 本地可见性摘要

```cpp
struct LunarOccultationLocalVisibilitySample {
    int valid;
    double jd_ut;
    double moon_altitude_rad;
    double moon_azimuth_rad;
    double target_altitude_rad;
    double target_azimuth_rad;
    double sun_altitude_rad;
    double sun_azimuth_rad;
    uint32_t visibility_flags;
};

struct LunarOccultationPhenomena {
    double angular_distance_rad;
    double diameter_ratio;
    double magnitude;
    double obscuration;
    double occulted_fraction;
};

struct LunarOccultationVisibilityInterval {
    int valid;
    double begin_jd_ut;
    double end_jd_ut;
};

struct LunarOccultationLocalVisibility {
    LunarOccultationLocalVisibilitySample first_contact;
    LunarOccultationLocalVisibilitySample second_contact;
    LunarOccultationLocalVisibilitySample maximum;
    LunarOccultationLocalVisibilitySample third_contact;
    LunarOccultationLocalVisibilitySample fourth_contact;
    double target_rise_jd_ut;
    double target_set_jd_ut;
    double visible_begin_jd_ut;
    double visible_end_jd_ut;
    double dark_visible_begin_jd_ut;
    double dark_visible_end_jd_ut;
    int visible_interval_count;
    LunarOccultationVisibilityInterval visible_intervals[TAIYIN_OCCULTATION_MAX_VISIBILITY_INTERVALS];
    int dark_visible_interval_count;
    LunarOccultationVisibilityInterval dark_visible_intervals[TAIYIN_OCCULTATION_MAX_VISIBILITY_INTERVALS];
    uint32_t visibility_flags;
};

struct LunarOccultationWherePathPoint {
    int valid;
    double jd_ut;
    double longitude_deg;
    double latitude_deg;
    double height_m;
};

struct LunarOccultationWhereResult {
    int center_line_hits_earth;
    uint32_t type_flags;
    double jd_ut;
    double center_line_begin_jd_ut;
    double center_line_end_jd_ut;
    int center_line_path_count;
    LunarOccultationWherePathPoint center_line_path[TAIYIN_OCCULTATION_WHERE_MAX_PATH_POINTS];
    double center_line_min_longitude_deg;
    double center_line_max_longitude_deg;
    double center_line_min_latitude_deg;
    double center_line_max_latitude_deg;
    double center_line_path_distance_km;
    int outer_limit_path_count;
    LunarOccultationWherePathPoint outer_north_path[TAIYIN_OCCULTATION_WHERE_MAX_PATH_POINTS];
    LunarOccultationWherePathPoint outer_south_path[TAIYIN_OCCULTATION_WHERE_MAX_PATH_POINTS];
    double outer_limit_mean_width_km;
    double outer_limit_max_width_km;
    int visible_region_polygon_count;
    LunarOccultationWherePathPoint visible_region_polygon[TAIYIN_OCCULTATION_WHERE_MAX_POLYGON_POINTS];
    double visible_region_min_longitude_deg;
    double visible_region_max_longitude_deg;
    double visible_region_min_latitude_deg;
    double visible_region_max_latitude_deg;
    double longitude_deg;
    double latitude_deg;
    double height_m;
    double separation_rad;
    double moon_radius_rad;
    double target_radius_rad;
    double margin_rad;
    LunarOccultationPhenomena phenomena;
    LunarOccultationLocalVisibilitySample local_sample;
    uint32_t visibility_flags;
};
```

`center_line_begin_jd_ut` 和 `center_line_end_jd_ut`
是第一版中心线命中地球的起止时间：它们只描述 `jd_ut`
附近 Moon-target 中心线与地球相交的时间范围。如果当前事件是非中心掩，或者当前窗口无法 bracket
到边界，字段保持 `NaN`。

`center_line_path` 会在 `center_line_begin_jd_ut` 到
`center_line_end_jd_ut` 之间采样同一条中心线，返回固定容量的路径摘要。
`center_line_*_longitude_deg`、`center_line_*_latitude_deg` 和
`center_line_path_distance_km` 是这条采样中心线的包络和近似长度。它是中心线
path 产品，不是完整可见区域多边形或南北界产品。非中心掩没有中心线地表路径，`center_line_path_count` 保持 `0`，`longitude_deg` / `latitude_deg` 返回最佳观测点。

`outer_north_path` 和 `outer_south_path` 是第一版外接触边界带摘要：对中心线
path 的每个采样点，在当前 Taiyin topocentric 几何下沿局部法线求
`margin_rad = 0` 的两侧地表点。`outer_limit_mean_width_km` 和
`outer_limit_max_width_km` 是这些成对边界点之间的近似宽度统计。它只在中心线命中地球时生成；非中心掩的 `outer_limit_path_count` 保持 `0`。这不是月缘地形修正后的精密南北界，也不是完整 polygon。

`visible_region_polygon` 会把 `outer_north_path` 和反向的
`outer_south_path` 闭合成第一版可见区域 polygon。经度会按相邻点展开，所以跨
180° 经线时 polygon 包络不会被 `-180/180` 跳变撕开；调用方如果需要标准经度，可自行归一化每个点。这个 polygon 仍然来自上面的外接触边界带，不包含月缘地形和地形高程。

`LunarOccultationPhenomena` 是 attr-style 现象指标整理层：
`angular_distance_rad` 是 Moon-target 中心角距。`diameter_ratio`、`magnitude`
和 `obscuration` 尽量沿用传统月掩现象指标口径：
`diameter_ratio` 是月亮视直径/目标视直径，`magnitude` 是目标直径被月亮覆盖的比例，
`obscuration` 是对应的双圆面积量。对很小的行星盘面，这些值可以大于 1。
恒星按点目标处理。`occulted_fraction` 是 Taiyin 额外提供的 0..1 归一化被掩比例，方便只需要有界值的调用方使用。

`LunarOccultationWhereResult::height_m` 当前固定为 `0.0`。这表示中心线/最佳观测点按海平面处理；当前 `where` 几何不查询或估算地形高程。

每个 sample 的 `visibility_flags` 使用下面的 bit：

```cpp
TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_MOON_ABOVE_HORIZON
TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_TARGET_ABOVE_HORIZON
TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_SUN_BELOW_HORIZON
```

`TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_GEOMETRICALLY_VISIBLE` 是 Moon 和 target 都在地平线以上的组合 mask；`TAIYIN_OCCULTATION_VISIBILITY_SAMPLE_DARK_SKY_VISIBLE` 在此基础上还要求 Sun 在地平线以下。

汇总结果的 `visibility_flags` 使用下面的 bit：

```cpp
TAIYIN_OCCULTATION_VISIBILITY_HAS_VISIBLE_SAMPLE
TAIYIN_OCCULTATION_VISIBILITY_MAXIMUM_VISIBLE
TAIYIN_OCCULTATION_VISIBILITY_HAS_DARK_SAMPLE
TAIYIN_OCCULTATION_VISIBILITY_MAXIMUM_DARK
TAIYIN_OCCULTATION_VISIBILITY_HAS_VISIBLE_INTERVAL
TAIYIN_OCCULTATION_VISIBILITY_HAS_DARK_INTERVAL
```

恒星按点目标处理，所以 C2/C3 没有物理意义，对应 sample 的 `valid` 为 0。body 目标如果存在内接触，会返回 C1-C4 全部 sample。

`target_rise_jd_ut` 和 `target_set_jd_ut` 是掩星区间内目标高度穿越地平线的
时刻；如果目标在整个事件期间一直在地平线上方或下方，对应字段保持 `NaN`。
`visible_begin_jd_ut` / `visible_end_jd_ut` 表示 Moon 和 target 都在地平线以上的
第一段连续时间。`dark_visible_begin_jd_ut` / `dark_visible_end_jd_ut`
在此基础上还要求 Sun 位于地平线以下。这些地方可见性区间使用
`TAIYIN_OCCULTATION_VISIBILITY_REFRACTION` 选择的同一套水平坐标。
`visible_intervals[]` 和 `dark_visible_intervals[]` 保存完整扫描到的多段区间；
旧的 begin/end 字段始终镜像第一段，方便简单调用方使用。

## 搜索算法

当前实现不是固定小步长扫完整时间窗口，也不是对未来几十年逐时/逐分硬搜。公开入口是 next-event search，内部先生成少量候选，再对候选附近做精修：

1. 用地心真黄道坐标计算起点附近的月亮黄经和目标黄经。
2. 用月亮相对目标的黄经速度估计下一次黄经相合 seed。
3. 对 seed 做几步黄经相合修正。每处理完一个候选后，会从新的 probe epoch 重新计算月亮和目标黄经/速度，再估计下一个 seed；它不是用起点的一次平均周期一直往后加。
4. 用月亮和恒星黄纬差做快速过滤。
5. 通过过滤后，在 seed 附近窗口内最小化月亮-目标三维天球角距。
6. 如果最小角距小于等于月亮视半径和目标视半径之和，则在最大掩两侧解外接触根；如果 body 目标在最大掩时完全进入月亮视圆内，再解内接触根，并返回这次月掩事件。

local 入口的 seed 候选仍使用地心黄道坐标生成，然后用较宽的黄纬 gate 覆盖地表视差可能带来的候选偏移；最终结果用 local/topocentric observed direction 重新计算和判定。

这种设计让 seed/gate 只负责“找候选”，不把最终几何结果绑死在粗筛模型上。

### 为什么不是硬搜还可能漏？

这里的“硬搜”只发生在候选 seed 附近：一旦候选通过黄纬 gate，代码会在 seed 前后窗口内做三维角距最小化，并用二分解接触点。它没有对整个搜索范围做密集扫描。

因此可能漏的不是候选窗口里的最小化，而是候选生成阶段：

- 月掩事件的最近角距可能和“月亮黄经等于目标黄经”的时刻有偏移；local/topocentric 事件还会被月球地平视差推开。
- 如果 seed 离真实最近角距太远，固定窗口可能没有覆盖真正的最小值。
- 如果黄纬 gate 在候选时刻过早判定“不可能”，但目标在附近时间因地表视差、行星运动或恒星自行进入可掩范围，也可能漏掉。
- 恒星有自行。TSC1/TSF1 位置评估会在每个 sample epoch 传播自行，但 seed/gate 设计仍必须避免只用 start epoch 做长期排除。

所以当前实现适合近现代、指定目标的 next-search 和回归测试；长期、大跨度搜索仍需要更强的候选层，例如类似日月食/凌日的 `k` 候选、保守 F/黄纬筛选、以及与真实星历硬扫对照过的 bracket 策略。批量星表扫描另属上层 catalog/almanac 功能，不作为这个 API 的下一步。

## 当前测试覆盖

`test_occultation_search` 覆盖：

- synthetic TSF1 恒星：把恒星放在指定时刻的月亮方向上，确认可搜索到月掩事件；
- 多日期 synthetic grid：在多个不同日期生成“月亮路径恒星”，从事件前 20 天搜索，确认 seed 候选没有退回密集扫描；
- packaged TSC1 smoke：加载 `data/stars/catalogs/stars-fixed-traditional.tsc1`，在几颗低黄纬亮星中寻找真实 catalog 链路能跑通的月掩事件；
- 外部参考 oracle：Antares、Spica、Regulus、Aldebaran 的地方月掩恒星 maximum、入掩和出掩基准，以及 Mercury/Venus/Mars/Jupiter/Saturn 地方月掩 body maximum 和 C1-C4 基准；其中 Jupiter/Saturn 覆盖 COB 本体组合路线，Mars 使用 Mars barycenter 近似，因为当前 packaged COB 没有 Mars offset 文件；
- `where` 覆盖：Antares 和 Mercury 的中心线落点、外接触边界带、闭合 polygon，以及 Spica 和 Saturn 的非中心最佳观测点，并与外部参考 fixture 对比；
- local visibility summary：对 Antares 和 Mercury 的地方月掩结果采样 C1/最大掩/C4 或 C1-C4，确认本地高度/方位有限、多段可见区间填充、observer 缺失会报错；
- major-body smoke：在 Mercury/Venus/Mars/Jupiter/Saturn 中寻找一个真实月掩 body 事件，并验证 Moon/Sun 等无效 target 会被拒绝；
- Mercury/Venus 月掩 body seed 边界：从最大掩前一瞬、入掩后、事件后反向搜索等起点确认能找回同一场；
- missing star 错误传播；
- local API 必须从 `NativeCalcContext` 读取 observer/topocentric 信息。
- TLL1 可选月缘接触修正：覆盖地心月掩行星和地方月掩恒星，并验证缺模型报错与未开启 flag 时结果不变。

## 当前限制

当前第一版已经返回月掩恒星/月掩太阳系天体的最大掩/最近角距时刻；body 目标会返回 C1-C4，恒星点目标返回 C1/C4。它还会返回基础掩食类型 bit，并提供 `where` 中心线/最佳观测点：中心掩返回中心线落点和采样路径，非中心掩返回最佳观测点。它尚未提供：

- 行星椭球、土星环或恒星角直径模型；当前 body API 使用平均物理半径作为目标视半径；
- 星等限制、太阳距限制或月亮高度限制；
- 公告级 `where` 地表区域；当前 polygon 是外接触边界带闭合，尚未把 TLL1 月缘地形、地形高程或高阶边界平滑接入边界；
- 地方可见性区间已返回多段列表，但还没有把升落事件关联到具体区间编号，也没有做面向 almanac 的文字摘要；
- 批量扫描整张星表并列出所有月掩星；这个能力后置到 catalog/almanac 层设计；
- 行星互掩、卫星凌日或一般 occultation/appulse 框架；
- 外部公告级月掩星 oracle 的秒级对照。

这些能力应在当前 next-event API 稳定后继续补充。短期优先级是继续补指定目标月掩能力和 oracle；批量星表搜索后置。
