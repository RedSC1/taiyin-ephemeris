#ifndef TAIYIN_INTERNAL_COORDINATE_MODEL_DATA_H
#define TAIYIN_INTERNAL_COORDINATE_MODEL_DATA_H

namespace taiyin {
namespace internal {

struct VondrakPeriodicTerm {
    double period_centuries;
    double cos_0_arcsec;
    double cos_1_arcsec;
    double sin_0_arcsec;
    double sin_1_arcsec;
};

struct IAU2000BNutationTerm {
    int l;
    int lp;
    int f;
    int d;
    int om;
    double ps;
    double pst;
    double pc;
    double ec;
    double ect;
    double es;
};

extern const double kVondrakEclipticPolynomialPa[4];
extern const double kVondrakEclipticPolynomialQa[4];
extern const VondrakPeriodicTerm kVondrakEclipticPeriodic[];
extern const int kVondrakEclipticPeriodicCount;

extern const double kVondrakEquatorPolynomialXa[4];
extern const double kVondrakEquatorPolynomialYa[4];
extern const VondrakPeriodicTerm kVondrakEquatorPeriodic[];
extern const int kVondrakEquatorPeriodicCount;

extern const IAU2000BNutationTerm kIAU2000BNutationTerms[];
extern const int kIAU2000BNutationTermCount;

}  // namespace internal
}  // namespace taiyin

#endif
