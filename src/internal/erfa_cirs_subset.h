#ifndef TAIYIN_INTERNAL_ERFA_CIRS_SUBSET_H
#define TAIYIN_INTERNAL_ERFA_CIRS_SUBSET_H

namespace taiyin {
namespace internal {

void erfa_nut00a(double jd_tt, double* dpsi, double* deps);
void erfa_nut06a(double jd_tt, double* dpsi, double* deps);
void erfa_xys06a(double jd_tt, double* x, double* y, double* s);
void erfa_pfw06(double jd_tt, double* gamb, double* phib, double* psib, double* epsa);
void erfa_fw2m(double gamb, double phib, double psi, double eps, double rm[3][3]);

}  // namespace internal
}  // namespace taiyin

#endif
