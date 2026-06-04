#ifndef TAIYIN_MATH_SOLVERS_H
#define TAIYIN_MATH_SOLVERS_H

#include "vector3.h"

namespace taiyin {

// 目标求解函数指针定义：接收时间 JD (TT/UT) 与用户数据上下文口袋，返回目标差值（如黄经差、视差距离等）
typedef double (*TargetFunction)(double jd, void* user_data);

/**
 * @brief 通用一阶牛顿差分极值精修器 (Greatest Eclipse / Syzygy Refiner)
 * 
 * 传入估算时间 t_seed，物理目标函数指针 f，上下文口袋 user_data，执行 1~2 次牛顿极值差分收敛。
 * 
 * @param t_seed 估算时间起点 JD
 * @param f 目标求解函数指针
 * @param user_data 随同传递的用户上下文口袋
 * @param dt_seconds 差分微调扰动步长 (秒)
 * @param max_iterations 最大迭代步数
 * @param tolerance_days 容许的时间收敛偏差 (JD 天)
 * @param out_t 输出的极值/零点收敛时间 JD
 * @return true 成功收敛至偏差内，false 导数为 0 或者是未在迭代步数内收敛
 */
bool refine_root_newton(
    double t_seed, 
    TargetFunction f,
    void* user_data,
    double dt_seconds, 
    int max_iterations, 
    double tolerance_days,
    double* out_t
) noexcept;

/**
 * @brief 寿星万年历经典二次相切求根器 (lineT)
 * 
 * 将局部轨道运动做直线性化，直接以一元二次方程解析求解圆盘（影子与月盘）相切的接触时间。
 * 
 * @param t0 极值基准时间 JD
 * @param x0 极值时间 t0 处的相对平面 x 坐标
 * @param y0 极值时间 t0 处的相对平面 y 坐标
 * @param vx 极值瞬时相对速度 x (per day)
 * @param vy 极值瞬时相对速度 y (per day)
 * @param contact_radius 影子视半径与天体视半径之和/差 (R_shadow ± R_moon)
 * @param out_t1 输出初亏/食既 JD
 * @param out_t2 输出生光/复圆 JD
 * @return true 成功求解 (存在实数根)，false 速度为 0 或判别式小于 0 (不相交)
 */
bool solve_quadratic_contact(
    double t0,
    double x0,
    double y0,
    double vx,
    double vy,
    double contact_radius,
    double* out_t1,
    double* out_t2
) noexcept;

/**
 * @brief 空间射线与地球椭球体解析求交 (lineEll)
 * 
 * 将 Z 轴按极赤比缩放，利用二次方程解析求解影轴线与椭球体表面空间 ITRF 交点。无任何数值迭代，绝对精确！
 * 
 * @param p1 直线起点 3D 空间坐标
 * @param p2 直线终点 3D 空间坐标
 * @param axis_ratio 扁球体极赤比因子 b/a (对于 WGS84 约为 0.99664719)
 * @param equator_radius 扁球体赤道半径
 * @param out_intersection 输出的空间坐标交点
 * @param out_dist1 起点到交点距离
 * @param out_dist2 终点到交点距离
 * @return true 有交点，false 无交点 (射线掠过地球)
 */
bool intersect_line_spheroid(
    const Vector3& p1,
    const Vector3& p2,
    double axis_ratio,
    double equator_radius,
    Vector3* out_intersection,
    double* out_dist1,
    double* out_dist2
) noexcept;

/**
 * @brief 圆与地平边缘投影椭圆解析求交 (cirOvl)
 * 
 * 用于求解全球日食界线在根本平面上，圆心影锥（圆）与扁地球投影边缘（椭圆）的交点。无任何数值迭代！
 * 
 * @param ellipse_major_axis 投影椭圆长半轴 (地球赤道半径 1.0)
 * @param axis_ratio 投影椭圆极赤比 b/a
 * @param circle_radius 圆半径 (影锥半径 l1/l2)
 * @param circle_x 圆心坐标 x (影子在根本平面的位置)
 * @param circle_y 圆心坐标 y
 * @param out_x1 输出交点 1 x 坐标
 * @param out_y1 输出交点 1 y 坐标
 * @param out_x2 输出交点 2 x 坐标
 * @param out_y2 输出交点 2 y 坐标
 * @return int 返回交点个数 (0, 1, 2)
 */
int intersect_ellipse_circle(
    double ellipse_major_axis,
    double axis_ratio,
    double circle_radius,
    double circle_x,
    double circle_y,
    double* out_x1,
    double* out_y1,
    double* out_x2,
    double* out_y2
) noexcept;

/**
 * @brief 贝塞尔交点至地球地理经纬度投影 (lineEar2)
 * 
 * 利用影轴瞬时赤纬 d、时角 mu 及真恒星时，通过旋转解析投影出扁球体表面的地理经纬度。
 * 
 * @param intersection 空间交点 3D 坐标
 * @param axis_ratio 地球极赤比 b/a
 * @param bessel_ra 影轴交点赤经 (弧度)
 * @param bessel_dec 影轴交点赤纬 (弧度)
 * @param gst 时刻下的 Greenwich 真恒星时 (弧度)
 * @param out_lon 输出地理经度 (弧度，东经为正)
 * @param out_lat 输出地理纬度 (弧度)
 * @return true 成功投影，false 极点附近除以零失败
 */
bool project_bessel_to_geodetic(
    const Vector3& intersection,
    double axis_ratio,
    double bessel_ra,
    double bessel_dec,
    double gst,
    double* out_lon,
    double* out_lat
) noexcept;

/**
 * @brief 地球物理投影阴影半径大气补偿计算 (ysPL.lecXY)
 * 
 * 输入日地/地月距离与地/日/视差各自的放大系数，求解月球处的本影和半影实际物理视半径。
 * 
 * @param moon_distance_equatorial_radii 地月质心距 (地球赤道半径为单位)
 * @param sun_distance_au 日地质心距 (AU)
 * @param earth_scale 地球阴影半径放大系数 (如 1.01 或 1.02 * 0.99834)
 * @param sun_scale 太阳半径放大系数 (如 1.0 或 1.02)
 * @param parallax_scale 视差放大系数 (如 1.0 或 1.02)
 * @param out_umbra_rad 输出本影视半径 (弧度)
 * @param out_penumbra_rad 输出半影视半径 (弧度)
 * @return true 成功，false 输入数据无效 (如距离为非正数)
 */
bool calculate_earth_shadow_radii(
    double moon_distance_equatorial_radii,
    double sun_distance_au,
    double earth_scale,
    double sun_scale,
    double parallax_scale,
    double* out_umbra_rad,
    double* out_penumbra_rad
) noexcept;

/**
 * @brief 日月黄道视差相对平面直角坐标投影 (ysPL.lecXY)
 * 
 * 输入时刻 t 处计算出的太阳、月亮视黄经和视黄纬，投影映射出在根本影面上的相对直角相对坐标 x, y。
 * 
 * @param moon_longitude_rad 月亮视黄经
 * @param moon_latitude_rad 月亮视黄纬
 * @param sun_longitude_rad 太阳视黄经
 * @param sun_latitude_rad 太阳视黄纬
 * @param out_fundamental_x 输出根本平面直角 x 坐标
 * @param out_fundamental_y 输出根本平面直角 y 坐标
 * @return true 成功，false 失败 (参数无效)
 */
bool project_ecliptic_to_fundamental(
    double moon_longitude_rad,
    double moon_latitude_rad,
    double sun_longitude_rad,
    double sun_latitude_rad,
    double* out_fundamental_x,
    double* out_fundamental_y
) noexcept;

} // namespace taiyin

#endif // TAIYIN_MATH_SOLVERS_H
