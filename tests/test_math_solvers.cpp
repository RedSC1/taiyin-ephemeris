#include "taiyin/angle.h"
#include "taiyin/math_solvers.h"

#include <cmath>
#include <iostream>
#include <cassert>

namespace {


bool near(double actual, double expected, double tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

void expect_near(double actual, double expected, double tolerance, const char* message, int* failures) {
    if (!near(actual, expected, tolerance)) {
        std::cerr << "FAIL: " << message
                  << ": actual=" << actual
                  << " expected=" << expected
                  << " diff=" << std::fabs(actual - expected)
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

// 物理求解目标函数定义：求 x^2 - 4.0 的零点 (在 x = 2.0 处)
double target_quad_zero(double x, void* user_data) noexcept {
    (void)user_data; // 未使用
    return x * x - 4.0;
}

bool bracketed_quad_eval(double x, void*, taiyin::BracketedRootEval* out) noexcept {
    if (!out) {
        return false;
    }
    out->value = x * x - 4.0;
    out->derivative = 2.0 * x;
    return true;
}

bool bracketed_linear_eval(double x, void*, taiyin::BracketedRootEval* out) noexcept {
    if (!out) {
        return false;
    }
    out->value = x - 0.25;
    out->derivative = 0.0;
    return true;
}

} // namespace

int main() {
    int failures = 0;

    // ============================================================================
    // 测试一：refine_root_newton (一阶差分牛顿法)
    // ============================================================================
    {
        double t_seed = 1.5;
        double dt_seconds = 10.0;
        int max_iters = 10;
        double tolerance_days = 1e-12;
        
        double root = 0.0;
        bool ok = taiyin::refine_root_newton(
            t_seed,
            target_quad_zero,
            nullptr,
            dt_seconds,
            max_iters,
            tolerance_days,
            &root
        );
        
        expect_near(ok ? 1.0 : 0.0, 1.0, 0.0, "refine_root_newton success", &failures);
        expect_near(root, 2.0, 1e-9, "refine_root_newton quad zero", &failures);
    }

    // ============================================================================
    // 测试一-b：solve_bracketed_newton_bisection (Newton + 二分兜底)
    // ============================================================================
    {
        double root = 0.0;
        double value = 0.0;
        double derivative = 0.0;
        double last_evaluated = 0.0;
        double next = 0.0;
        int iteration_count = 0;
        int evaluation_count = 0;
        bool ok = taiyin::solve_bracketed_newton_bisection(
            bracketed_quad_eval,
            nullptr,
            0.0,
            -4.0,
            4.0,
            12.0,
            3.5,
            1e-12,
            1e-15,
            10.0,
            16,
            &root,
            &value,
            &derivative,
            &last_evaluated,
            &next,
            &iteration_count,
            &evaluation_count);
        expect_near(ok ? 1.0 : 0.0, 1.0, 0.0, "solve_bracketed_newton_bisection success", &failures);
        expect_near(root, 2.0, 1e-10, "solve_bracketed_newton_bisection root", &failures);

        root = 0.0;
        value = 0.0;
        derivative = 0.0;
        last_evaluated = 0.0;
        next = 0.0;
        iteration_count = 0;
        evaluation_count = 0;
        ok = taiyin::solve_bracketed_newton_bisection(
            bracketed_linear_eval,
            nullptr,
            0.0,
            -0.25,
            1.0,
            0.75,
            0.9,
            1e-12,
            1e-9,
            10.0,
            48,
            &root,
            &value,
            &derivative,
            &last_evaluated,
            &next,
            &iteration_count,
            &evaluation_count);
        expect_near(ok ? 1.0 : 0.0, 1.0, 0.0, "solve_bracketed_newton_bisection bisection fallback success", &failures);
        expect_near(root, 0.25, 1e-10, "solve_bracketed_newton_bisection bisection fallback root", &failures);
    }

    // ============================================================================
    // 测试二：solve_quadratic_contact (二次相切求根器)
    // ============================================================================
    {
        // 沿 X 轴以速度 1.0 匀速直线运动，从 x = -2.0 运动到 x = 2.0，圆半径 R = 1.0
        double t0 = 0.0;
        double x0 = -2.0;
        double y0 = 0.0;
        double vx = 1.0;
        double vy = 0.0;
        double contact_radius = 1.0;
        
        double t1 = 0.0;
        double t2 = 0.0;
        bool ok = taiyin::solve_quadratic_contact(t0, x0, y0, vx, vy, contact_radius, &t1, &t2);
        
        expect_near(ok ? 1.0 : 0.0, 1.0, 0.0, "solve_quadratic_contact success", &failures);
        expect_near(t1, 1.0, 1e-15, "solve_quadratic_contact t1", &failures);
        expect_near(t2, 3.0, 1e-15, "solve_quadratic_contact t2", &failures);
        
        // 擦边不相交情况 (y0 = 2.0, R = 1.0)
        double y_no_hit = 2.0;
        bool no_hit = taiyin::solve_quadratic_contact(t0, x0, y_no_hit, vx, vy, contact_radius, &t1, &t2);
        expect_near(no_hit ? 1.0 : 0.0, 0.0, 0.0, "solve_quadratic_contact no hit", &failures);
    }

    // ============================================================================
    // 测试三：intersect_line_spheroid (直线与地球椭球体解析相交)
    // ============================================================================
    {
        // 正球体 (e = 1.0, r = 1.0)
        taiyin::Vector3 p1 = {0.0, 0.0, 5.0};
        taiyin::Vector3 p2 = {0.0, 0.0, 0.0};
        double e = 1.0;
        double r = 1.0;
        
        taiyin::Vector3 o = {0.0, 0.0, 0.0};
        double d1 = 0.0, d2 = 0.0;
        
        bool hit = taiyin::intersect_line_spheroid(p1, p2, e, r, &o, &d1, &d2);
        
        expect_near(hit ? 1.0 : 0.0, 1.0, 0.0, "intersect_line_spheroid sphere hit", &failures);
        expect_near(o.x, 0.0, 1e-15, "intersect_line_spheroid ox", &failures);
        expect_near(o.y, 0.0, 1e-15, "intersect_line_spheroid oy", &failures);
        expect_near(o.z, 1.0, 1e-15, "intersect_line_spheroid oz", &failures);
        expect_near(d1, 4.0, 1e-15, "intersect_line_spheroid d1", &failures);
        expect_near(d2, 1.0, 1e-15, "intersect_line_spheroid d2", &failures);
    }

    // ============================================================================
    // 测试四：intersect_ellipse_circle (投影椭圆与影锥圆求交)
    // ============================================================================
    {
        // 投影椭圆退化为单位圆 (R = 1.0, ba = 1.0) 与圆半径 1.0，圆心在 (1.0, 0.0) 处相交
        double R = 1.0;
        double ba = 1.0;
        double R2 = 1.0;
        double x0 = 1.0;
        double y0 = 0.0;
        
        double x1 = 0.0, y1 = 0.0;
        double x2 = 0.0, y2 = 0.0;
        
        int pts = taiyin::intersect_ellipse_circle(R, ba, R2, x0, y0, &x1, &y1, &x2, &y2);
        
        expect_near(pts, 2.0, 0.0, "intersect_ellipse_circle count", &failures);
        expect_near(x1, 0.5, 1e-15, "intersect_ellipse_circle x1", &failures);
        expect_near(x2, 0.5, 1e-15, "intersect_ellipse_circle x2", &failures);
        expect_near(std::abs(y1), std::sqrt(0.75), 1e-15, "intersect_ellipse_circle y1", &failures);
        expect_near(std::abs(y2), std::sqrt(0.75), 1e-15, "intersect_ellipse_circle y2", &failures);

        // A grazing overlap can place both roots inside one coarse angular
        // sample cell.  The solver must split at the residual minimum rather
        // than relying only on endpoint sign changes.
        constexpr double shallow_ba = 0.99664719;
        const double shallow_angle = M_PI / 720.0;
        const double normal_x_unscaled = std::cos(shallow_angle);
        const double normal_y_unscaled = std::sin(shallow_angle) / shallow_ba;
        const double normal_scale = std::sqrt(
            normal_x_unscaled * normal_x_unscaled
            + normal_y_unscaled * normal_y_unscaled);
        constexpr double center_offset = 0.4737231662;
        constexpr double shallow_radius = center_offset + 1.0e-6;
        const double shallow_x = std::cos(shallow_angle)
            + center_offset * normal_x_unscaled / normal_scale;
        const double shallow_y = shallow_ba * std::sin(shallow_angle)
            + center_offset * normal_y_unscaled / normal_scale;
        pts = taiyin::intersect_ellipse_circle(
            1.0,
            shallow_ba,
            shallow_radius,
            shallow_x,
            shallow_y,
            &x1,
            &y1,
            &x2,
            &y2);
        expect_near(pts, 2.0, 0.0, "intersect_ellipse_circle shallow overlap count", &failures);
        expect_near(
            x1 * x1 + y1 * y1 / (shallow_ba * shallow_ba),
            1.0,
            2.0e-12,
            "intersect_ellipse_circle shallow overlap ellipse residual 1",
            &failures);
        expect_near(
            x2 * x2 + y2 * y2 / (shallow_ba * shallow_ba),
            1.0,
            2.0e-12,
            "intersect_ellipse_circle shallow overlap ellipse residual 2",
            &failures);
        expect_near(
            (x1 - shallow_x) * (x1 - shallow_x)
                + (y1 - shallow_y) * (y1 - shallow_y),
            shallow_radius * shallow_radius,
            2.0e-12,
            "intersect_ellipse_circle shallow overlap circle residual 1",
            &failures);
        expect_near(
            (x2 - shallow_x) * (x2 - shallow_x)
                + (y2 - shallow_y) * (y2 - shallow_y),
            shallow_radius * shallow_radius,
            2.0e-12,
            "intersect_ellipse_circle shallow overlap circle residual 2",
            &failures);

        // Root identities must remain stable when one intersection crosses
        // the zero-angle wrap.  For rotationally symmetric unit circles both
        // physical branches advance by exactly the center-angle increment.
        const auto solve_wrapped_roots = [](
            double center_angle,
            double* out_angle_a,
            double* out_angle_b
        ) {
            double ax = NAN;
            double ay = NAN;
            double bx = NAN;
            double by = NAN;
            const int count = taiyin::intersect_ellipse_circle(
                1.0,
                1.0,
                0.5,
                std::cos(center_angle),
                std::sin(center_angle),
                &ax,
                &ay,
                &bx,
                &by);
            *out_angle_a = std::atan2(ay, ax);
            *out_angle_b = std::atan2(by, bx);
            return count;
        };
        const auto signed_angle_delta = [](double actual, double expected) {
            double delta = actual - expected;
            while (delta > M_PI) delta -= 2.0 * M_PI;
            while (delta < -M_PI) delta += 2.0 * M_PI;
            return delta;
        };
        double before_a = NAN;
        double before_b = NAN;
        double after_a = NAN;
        double after_b = NAN;
        expect_near(
            solve_wrapped_roots(0.504, &before_a, &before_b),
            2.0,
            0.0,
            "intersect_ellipse_circle pre-wrap count",
            &failures);
        expect_near(
            solve_wrapped_roots(0.506, &after_a, &after_b),
            2.0,
            0.0,
            "intersect_ellipse_circle post-wrap count",
            &failures);
        expect_near(
            signed_angle_delta(after_a, before_a),
            0.002,
            2.0e-12,
            "intersect_ellipse_circle branch A wrap continuity",
            &failures);
        expect_near(
            signed_angle_delta(after_b, before_b),
            0.002,
            2.0e-12,
            "intersect_ellipse_circle branch B wrap continuity",
            &failures);
    }

    // ============================================================================
    // 测试五：project_bessel_to_geodetic (贝塞尔投影至地理坐标)
    // ============================================================================
    {
        // 切点处于 (1.0, 0.0, 0.0)，无旋转，RA=0, Dec=0, GST=0
        taiyin::Vector3 intersection = {1.0, 0.0, 0.0};
        double e = 1.0;
        double ra = 0.0;
        double dec = 0.0;
        double gst = 0.0;
        
        double lon = 0.0, lat = 0.0;
        bool ok = taiyin::project_bessel_to_geodetic(intersection, e, ra, dec, gst, &lon, &lat);
        
        expect_near(ok ? 1.0 : 0.0, 1.0, 0.0, "project_bessel_to_geodetic success", &failures);
        expect_near(lon, 0.0, 1e-15, "project_bessel_to_geodetic lon", &failures);
        expect_near(lat, 0.0, 1e-15, "project_bessel_to_geodetic lat", &failures);
    }

    // ============================================================================
    // 测试六：calculate_earth_shadow_radii (地球阴影半径 - 多模型校验)
    // ============================================================================
    {
        double moon_dist = 60.0; // 地月距离 (以地球赤道半径为单位)
        double sun_dist = 1.0;   // 日地距离 (AU)
        
        // 1. 传统 Chauvenet 模型 (紫金山天文台 / sxwnl 标准)
        {
            double earth_scale = 1.02 * 0.99834; // 1.02 大气放大 * 0.99834 平均子午地球半径比例
            double sun_scale = 1.02;
            double parallax_scale = 1.02;
            double umbra = 0.0, penumbra = 0.0;
            
            bool ok = taiyin::calculate_earth_shadow_radii(
                moon_dist, sun_dist, earth_scale, sun_scale, parallax_scale, &umbra, &penumbra
            );
            
            // 理论值: (1/60)*1.02*0.99834 - ((959.63 - 8.794)/206264.8)*1.02
            // = 0.01697178 - 0.00470198 = 0.0122698 弧度
            expect_near(ok ? 1.0 : 0.0, 1.0, 0.0, "calculate_earth_shadow_radii Chauvenet success", &failures);
            expect_near(umbra, 0.0122698, 1e-6, "calculate_earth_shadow_radii Chauvenet umbra", &failures);
        }
        
        // 2. NASA Danjon 官方模型 (Espenak / GSFC 推荐标准)
        {
            double earth_scale = 1.01; // NASA 放大系数
            double sun_scale = 1.0;
            double parallax_scale = 1.0;
            double umbra = 0.0, penumbra = 0.0;
            
            bool ok = taiyin::calculate_earth_shadow_radii(
                moon_dist, sun_dist, earth_scale, sun_scale, parallax_scale, &umbra, &penumbra
            );
            
            // 理论值: (1/60)*1.01 - (959.63 - 8.794)/206264.8
            // = 0.01683333 - 0.00460978 = 0.01222355 弧度
            expect_near(ok ? 1.0 : 0.0, 1.0, 0.0, "calculate_earth_shadow_radii NASA success", &failures);
            expect_near(umbra, 0.01222355, 1e-6, "calculate_earth_shadow_radii NASA umbra", &failures);
        }
    }

    // ============================================================================
    // 测试七：project_ecliptic_to_fundamental (黄道坐标根本面直角投影)
    // ============================================================================
    {
        // 月亮视黄经 90度，视黄纬 0.0。太阳视黄经 270度，视黄纬 0.0
        // 反日点黄经为 90度，相对黄经差 dlon = 0.0，x = 0.0, y = 0.0
        double moon_lon = taiyin::TAIYIN_PI / 2.0;
        double moon_lat = 0.0;
        double sun_lon = 3.0 * taiyin::TAIYIN_PI / 2.0;
        double sun_lat = 0.0;
        
        double x = 999.0, y = 999.0;
        bool ok = taiyin::project_ecliptic_to_fundamental(moon_lon, moon_lat, sun_lon, sun_lat, &x, &y);
        
        expect_near(ok ? 1.0 : 0.0, 1.0, 0.0, "project_ecliptic_to_fundamental success", &failures);
        expect_near(x, 0.0, 1e-15, "project_ecliptic_to_fundamental x", &failures);
        expect_near(y, 0.0, 1e-15, "project_ecliptic_to_fundamental y", &failures);
    }

    if (failures == 0) {
        std::cout << "All math_solvers tests PASSED!\n";
        return 0;
    } else {
        std::cerr << failures << " tests FAILED!\n";
        return 1;
    }
}
