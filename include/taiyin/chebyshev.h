#ifndef TAIYIN_CHEBYSHEV_H
#define TAIYIN_CHEBYSHEV_H

namespace taiyin {

/**
 * @brief 存放切比雪夫多项式求值结果的结构体
 */
struct ChebyshevValue {
    double value;             ///< 对应 x 的多项式计算值 (通常为位置)
    double derivative;        ///< 对应 x 的多项式一阶导数 (通常为速度，未做时间缩放)
    double second_derivative; ///< 对应 x 的多项式二阶导数 (通常为加速度，未做时间缩放)
};

/**
 * @brief 使用 Clenshaw 递推算法计算切比雪夫多项式的值
 * 
 * @param coefficients 切比雪夫多项式系数数组的首地址
 * @param count 系数个数 (即多项式阶数 Degree + 1)
 * @param x 归一化时间参数，取值范围必须在 [-1, 1] 之间
 * @return double 计算得到的多项式值
 */
double chebyshev_eval(const double* coefficients, int count, double x);

/**
 * @brief 计算切比雪夫多项式的一阶导数值
 * 
 * @param coefficients 切比雪夫多项式系数数组的首地址
 * @param count 系数个数 (即多项式阶数 Degree + 1)
 * @param x 归一化时间参数，取值范围必须在 [-1, 1] 之间
 * @return double 计算得到的一阶导数 (未做时间缩放)
 */
double chebyshev_derivative_eval(const double* coefficients, int count, double x);

/**
 * @brief 一次性计算切比雪夫多项式的值、一阶导数和二阶导数
 * 
 * @param coefficients 切比雪夫多项式系数数组的首地址
 * @param count 系数个数 (即多项式阶数 Degree + 1)
 * @param x 归一化时间参数，取值范围必须在 [-1, 1] 之间
 * @return ChebyshevValue 包含值、一阶导数和二阶导数的结构体
 */
ChebyshevValue chebyshev_eval_with_derivative(const double* coefficients, int count, double x);

}  // namespace taiyin

#endif  // TAIYIN_CHEBYSHEV_H
