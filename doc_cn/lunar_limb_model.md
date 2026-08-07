# 月缘模型

文档状态：当前说明

Taiyin 可以使用由 Kaguya/SELENE LALT 地形生成的方向相关月球轮廓，精修
日月食和月掩接触时刻。模型是可选能力，而且必须显式开启；只加载模型不会
改变原有计算结果。

## 加载与使用

```cpp
#include "taiyin/runtime/eclipse_search.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/occultation_search.h"
#include "taiyin/runtime/runtime.h"

taiyin::runtime::EphemerisRuntimeConfig config;
config.data_root = "data";
config.lunar_limb_path = "data/lunar-limb/kaguya_lalt_16ppd.tll1";
taiyin::runtime::initialize_global_ephemeris_runtime(config);

taiyin::runtime::NativeCalcContext context;

const uint64_t flags =
    taiyin::runtime::TAIYIN_ECLIPSE_INCLUDE_CONTACTS |
    taiyin::runtime::TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION;

const uint64_t occultation_flags =
    taiyin::runtime::TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION;
```

全局 runtime 拥有 TLL1 mmap 及其生命周期。用户 ctx 不保存模型指针，也不
负责 unmap。runtime 初始化完成后，模型在所有计算期间保持只读；不得在并发
计算时重新初始化或替换全局数据。

设置 `TAIYIN_ECLIPSE_LUNAR_LIMB_CORRECTION` 却没有全局加载模型时，计算返回
`TAIYIN_ERROR_UNSUPPORTED`，不会静默退回圆月。没有设置该 flag 时，即使
runtime 已加载模型，结果也保持原有圆月语义。

月掩使用 `TAIYIN_OCCULTATION_LUNAR_LIMB_CORRECTION`。月掩恒星和月掩 body
的 `search_next_*` 入口会继续用光滑几何定位事件，再用 TLL1 接触余量建立
根区间并做带保护的牛顿/割线/二分精修。月掩 `where` 的地表边界目前尚未
接入这个 flag。

## 修正几何

原来的光滑圆月解继续负责定位事件和生成接触种子，随后才按实际位置角与
光学天平动查询月缘并精修接触根：

- 地方日食 C1/C4 使用朝向太阳一侧的月缘；
- 日全食 C2/C3 使用朝向太阳一侧的月缘；
- 日环食 C2/C3 使用背向太阳一侧的月缘；
- 全球日食 P1/P4 使用朝向地球切点观测者一侧的月缘；
- 月食 P1/P4、U1/U4 使用朝向地影中心一侧的月缘；
- 月食 U2/U3 使用背向地影中心一侧的月缘。

全球日食 P1/P4 不是先固定一个地球切点再查询月缘。每个候选时刻都会在
WGS84 椭球面上联立寻找半影余量最小点；该余量同时包含表面点到影轴的距离、
该观测方向的 TLL1 月缘半径和影锥扩张。外层再用带区间保护的割线/二分法求
最小余量为零的时刻。月球姿态矩阵在每个候选时刻只准备一次，内层椭球面
迭代复用该查询状态。

日食中心线开始/结束是影轴与地球的交点，不依赖月球半径。Route API 同样
接收 `uint64_t flags`：开启月缘修正后，南北核心界、半影界、半食分界和由
这些曲线闭合的 polygon 都会按各自观测方向迭代 TLL1 月缘；路线中心的全食或
环食时长会以光滑解为 seed，再按同一月缘模型精修 C2/C3。中心线保持不变。
不传该 flag 时，路线仍使用 ctx 选择的光滑月球半径模型。

## 随附 TLL1 数据

`kaguya_lalt_16ppd.tll1` 由官方 Kaguya LALT 1/16 度全球 DEM 生成，采用
Mean Earth/Polar Axis 月固坐标系，保存相对 1737.4 km 参考球的整米轮廓偏移：

| 维度 | 覆盖范围 | 步长 |
| --- | ---: | ---: |
| 天平动经度 | -9 到 +9 度 | 0.5 度 |
| 天平动纬度 | -8 到 +8 度 | 0.5 度 |
| 位置角 | 0 到 359.8 度 | 0.2 度 |

文件大小为 4,395,792 字节，运行时使用 mmap。每次查询读取 8 个 int16 样本并
进行三线性插值。固定随机种子的 250 点原始 DEM 直接对照结果为：RMS
100.4 m、绝对误差 P95 209.5 m、最大绝对误差 376.9 m。它适合亚秒到数秒级
接触修正，但不是专门的贝利珠高密度轮廓。

数据来源、署名与重新生成命令见 `data/lunar-limb/README.md`。
