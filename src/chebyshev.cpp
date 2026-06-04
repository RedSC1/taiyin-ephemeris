#include "taiyin/chebyshev.h"

namespace taiyin {

double chebyshev_eval(const double* coefficients, int count, double x) {
    if (!coefficients || count <= 0) {
        return 0.0;
    }

    // Clenshaw 递推变量：b_{k+1} 和 b_{k+2}
    double b_k_plus_1 = 0.0;
    double b_k_plus_2 = 0.0;

    // 从后往前逆向迭代递推系数
    // 递推公式：b_k = 2 * x * b_{k+1} - b_{k+2} + c_k
    for (int i = count - 1; i >= 1; --i) {
        const double b_k = 2.0 * x * b_k_plus_1 - b_k_plus_2 + coefficients[i];
        b_k_plus_2 = b_k_plus_1;
        b_k_plus_1 = b_k;
    }

    // 最后一项计算：f(x) = x * b_1 - b_2 + c_0
    return x * b_k_plus_1 - b_k_plus_2 + coefficients[0];
}

double chebyshev_derivative_eval(const double* coefficients, int count, double x) {
    return chebyshev_eval_with_derivative(coefficients, count, x).derivative;
}

ChebyshevValue chebyshev_eval_with_derivative(const double* coefficients, int count, double x) {
    if (!coefficients || count <= 0) {
        ChebyshevValue zero = { 0.0, 0.0, 0.0 };
        return zero;
    }

    // b: 切比雪夫多项式值递推变量
    // d: 一阶导数递推变量 (d_k = 2 * x * d_{k+1} - d_{k+2} + 2 * b_{k+1})
    // s: 二阶导数递推变量 (s_k = 2 * x * s_{k+1} - s_{k+2} + 4 * d_{k+1})
    double b_k_plus_1 = 0.0;
    double b_k_plus_2 = 0.0;
    double d_k_plus_1 = 0.0;
    double d_k_plus_2 = 0.0;
    double s_k_plus_1 = 0.0;
    double s_k_plus_2 = 0.0;

    for (int i = count - 1; i >= 1; --i) {
        const double b_next = b_k_plus_1;
        const double d_next = d_k_plus_1;
        
        // 1. 递推多项式值
        const double b_k = 2.0 * x * b_k_plus_1 - b_k_plus_2 + coefficients[i];
        // 2. 递推一阶导数
        const double d_k = 2.0 * x * d_k_plus_1 - d_k_plus_2 + 2.0 * b_next;
        // 3. 递推二阶导数
        const double s_k = 2.0 * x * s_k_plus_1 - s_k_plus_2 + 4.0 * d_next;

        b_k_plus_2 = b_k_plus_1;
        b_k_plus_1 = b_k;
        d_k_plus_2 = d_k_plus_1;
        d_k_plus_1 = d_k;
        s_k_plus_2 = s_k_plus_1;
        s_k_plus_1 = s_k;
    }

    // 组合计算最终值、一阶导数和二阶导数
    ChebyshevValue result = {
        x * b_k_plus_1 - b_k_plus_2 + coefficients[0],
        x * d_k_plus_1 - d_k_plus_2 + b_k_plus_1,
        x * s_k_plus_1 - s_k_plus_2 + 2.0 * d_k_plus_1,
    };
    return result;
}

}  // namespace taiyin
