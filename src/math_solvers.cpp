#include "taiyin/math_solvers.h"

#include "taiyin/angle.h"
#include "taiyin/time.h"

#include <cmath>
#include <algorithm>

namespace taiyin {

// ============================================================================
// 1. 数值差分与求根纯函数 (Math Solvers)
// ============================================================================

bool refine_root_newton(
    double t_seed, 
    TargetFunction f,
    void* user_data,
    double dt_seconds, 
    int max_iterations, 
    double tolerance_days,
    double* out_t
) noexcept {
    if (!f || !out_t) {
        return false;
    }

    double t = t_seed;
    double dt = dt_seconds / SECONDS_PER_DAY; // 将秒转换为儒略日天数
    bool converged = false;
    
    for (int iter = 0; iter < max_iterations; ++iter) {
        double y = f(t, user_data);
        double y_plus = f(t + dt, user_data);
        
        double dydt = (y_plus - y) / dt; // 瞬时相对速度 (数值一阶差分)
        if (std::abs(dydt) < 1e-15) {
            break; // 避免除以零
        }
        
        double delta_t = -y / dydt;
        t += delta_t;
        
        if (std::abs(delta_t) < tolerance_days) {
            converged = true;
            break; // 满足时间容许偏差，提前收敛
        }
    }
    *out_t = t;
    return converged;
}

bool solve_quadratic_contact(
    double t0,
    double x0,
    double y0,
    double vx,
    double vy,
    double contact_radius,
    double* out_t1,
    double* out_t2
) noexcept {
    if (!out_t1 || !out_t2) {
        return false;
    }

    double A = vx * vx + vy * vy;
    if (A < 1e-15) {
        return false; // 速度为 0，无法求解
    }
    double B = x0 * vx + y0 * vy;
    double C = x0 * x0 + y0 * y0 - contact_radius * contact_radius;
    
    double disc = B * B - A * C; // 判别式
    if (disc < 0.0) {
        return false; // 无解 (影子没有碰到天面)
    }
    
    double D = std::sqrt(disc);
    *out_t1 = t0 + (-B - D) / A;
    *out_t2 = t0 + (-B + D) / A;
    return true;
}


// ============================================================================
// 2. 空间与平面解析几何求交纯函数 (Geometry Intersections)
// ============================================================================

bool intersect_line_spheroid(
    const Vector3& p1,
    const Vector3& p2,
    double axis_ratio,
    double equator_radius,
    Vector3* out_intersection,
    double* out_dist1,
    double* out_dist2
) noexcept {
    if (!out_intersection || !out_dist1 || !out_dist2) {
        return false;
    }

    double dx = p2.x - p1.x;
    double dy = p2.y - p1.y;
    double dz = p2.z - p1.z;
    double e2 = axis_ratio * axis_ratio;
    if (e2 < 1e-15) {
        return false;
    }
    
    // Z 轴按极赤比缩放，影轴线与椭球体表面空间交点二次方程参数
    double A = dx * dx + dy * dy + (dz * dz) / e2;
    double B = p1.x * dx + p1.y * dy + (p1.z * dz) / e2;
    double C = p1.x * p1.x + p1.y * p1.y + (p1.z * p1.z) / e2 - equator_radius * equator_radius;
    
    double disc = B * B - A * C;
    if (disc < 0.0) {
        return false; // 不与地球相交 (影子掠过太空)
    }
    
    double D = std::sqrt(disc);
    if (B < 0.0) {
        D = -D; // 只求靠近起点 p1 (月面端) 的交点
    }
    double t = (-B + D) / A;
    
    out_intersection->x = p1.x + dx * t;
    out_intersection->y = p1.y + dy * t;
    out_intersection->z = p1.z + dz * t;
    
    double R = std::sqrt(dx * dx + dy * dy + dz * dz);
    *out_dist1 = R * std::abs(t);
    *out_dist2 = R * std::abs(t - 1.0);
    return true;
}

int intersect_ellipse_circle(
    double ellipse_major_axis,
    double axis_ratio,
    double circle_radius,
    double circle_x,
    double circle_y,
    double* out_x1, double* out_y1,
    double* out_x2, double* out_y2
) noexcept {
    if (!out_x1 || !out_y1 || !out_x2 || !out_y2) {
        return 0;
    }

    double d = std::sqrt(circle_x * circle_x + circle_y * circle_y);
    if (d < 1e-15) {
        return 0; // 避免除以零
    }
    double sinB = circle_y / d;
    double cosB = circle_x / d;
    
    // 粗估相交角 A
    double cosA = (ellipse_major_axis * ellipse_major_axis + d * d - circle_radius * circle_radius) / (2.0 * d * ellipse_major_axis);
    if (std::abs(cosA) > 1.0) {
        return 0; // 无交点
    }
    double sinA = std::sqrt(1.0 - cosA * cosA);
    
    double ba2 = axis_ratio * axis_ratio;
    if (ba2 < 1e-15) {
        return 0;
    }
    double x_out[2] = {0.0, 0.0};
    double y_out[2] = {0.0, 0.0};
    int count = 0;
    
    // 精密微小偏心率修正
    for (int k = -1; k < 2; k += 2) {
        double S = cosA * sinB + sinA * cosB * k;
        double g = ellipse_major_axis - S * S * (1.0 / ba2 - 1.0) / 2.0;
        
        double cosA_g = (g * g + d * d - circle_radius * circle_radius) / (2.0 * d * g);
        if (std::abs(cosA_g) > 1.0) {
            return 0; // 无交点
        }
        double sinA_g = std::sqrt(1.0 - cosA_g * cosA_g);
        
        double C = cosA_g * cosB - sinA_g * sinB * k;
        double S_g = cosA_g * sinB + sinA_g * cosB * k;
        
        x_out[count] = g * C;
        y_out[count] = g * S_g;
        count++;
    }
    
    *out_x1 = x_out[0];
    *out_y1 = y_out[0];
    *out_x2 = x_out[1];
    *out_y2 = y_out[1];
    return count;
}

bool project_bessel_to_geodetic(
    const Vector3& intersection,
    double axis_ratio,
    double bessel_ra,
    double bessel_dec,
    double gst,
    double* out_lon,
    double* out_lat
) noexcept {
    if (!out_lon || !out_lat) {
        return false;
    }

    // 1. 绕 X 轴旋转角度 bessel_dec (影轴赤纬 d)
    double P = std::cos(bessel_dec);
    double Q = std::sin(bessel_dec);
    
    double rx = intersection.x;
    double ry = P * intersection.y - Q * intersection.z;
    double rz = Q * intersection.y + P * intersection.z;
    
    // 2. 地理纬度计算：
    // phi = atan( rz / (e * e * sqrt(rx*rx + ry*ry)) )
    double denom = axis_ratio * axis_ratio * std::sqrt(rx * rx + ry * ry);
    if (denom < 1e-15) {
        return false; // 交点在极轴附近产生极小分母
    }
    *out_lat = std::atan(rz / denom);
    
    // 3. 地理经度计算：
    // lambda = atan2(ry, rx) + bessel_ra - gst
    double lon = std::atan2(ry, rx) + bessel_ra - gst;
    
    // 角度规整到 [-PI, PI]
    while (lon > TAIYIN_PI) lon -= TAIYIN_TWO_PI;
    while (lon < -TAIYIN_PI) lon += TAIYIN_TWO_PI;
    
    *out_lon = lon;
    return true;
}


// ============================================================================
// 3. 天体物理与相对坐标变换纯函数 (Physical Formulations)
// ============================================================================

bool calculate_earth_shadow_radii(
    double moon_distance_equatorial_radii,
    double sun_distance_au,
    double earth_scale,
    double sun_scale,
    double parallax_scale,
    double* out_umbra_rad,
    double* out_penumbra_rad
) noexcept {
    if (!out_umbra_rad || !out_penumbra_rad || moon_distance_equatorial_radii <= 1e-6 || sun_distance_au <= 1e-6) {
        return false;
    }

    // 太阳视半径与太阳视差常数 (在 1 AU 处的角秒值)
    const double sun_radius_1au_arcsec = 959.63;
    const double solar_parallax_1au_arcsec = 8.794;
    
    // 每弧度对应的角秒数比例因子
    // 月亮视差 (以地球赤道半径为单位直接取倒数)
    double moon_parallax_rad = 1.0 / moon_distance_equatorial_radii;

    // 太阳角半径与太阳视差 (在当前距离下的弧度值)
    double sun_radius_rad = (sun_radius_1au_arcsec / sun_distance_au) * TAIYIN_ARCSEC_TO_RAD;
    double sun_parallax_rad = (solar_parallax_1au_arcsec / sun_distance_au) * TAIYIN_ARCSEC_TO_RAD;
    
    // 应用各自的比例放大因子进行影子半径求解
    *out_umbra_rad = moon_parallax_rad * earth_scale - (sun_radius_rad * sun_scale - sun_parallax_rad * parallax_scale);
    *out_penumbra_rad = moon_parallax_rad * earth_scale + (sun_radius_rad * sun_scale + sun_parallax_rad * parallax_scale);
    return true;
}

bool project_ecliptic_to_fundamental(
    double moon_longitude_rad,
    double moon_latitude_rad,
    double sun_longitude_rad,
    double sun_latitude_rad,
    double* out_fundamental_x,
    double* out_fundamental_y
) noexcept {
    if (!out_fundamental_x || !out_fundamental_y) {
        return false;
    }

    // 计算月球相对日地反日点 (Anti-sun point) 的黄经差
    double dlon = moon_longitude_rad + TAIYIN_PI - sun_longitude_rad;
    
    // 经度差规整到 [-PI, PI]
    while (dlon > TAIYIN_PI) dlon -= TAIYIN_TWO_PI;
    while (dlon < -TAIYIN_PI) dlon += TAIYIN_TWO_PI;
    
    // 根本平面的直角平面投影计算，包含平均纬度的余弦缩放因子
    double avg_lat = (moon_latitude_rad - sun_latitude_rad) / 2.0;
    
    *out_fundamental_x = dlon * std::cos(avg_lat);
    *out_fundamental_y = moon_latitude_rad + sun_latitude_rad;
    return true;
}

} // namespace taiyin
