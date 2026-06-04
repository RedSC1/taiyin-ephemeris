#ifndef TAIYIN_INTERNAL_DELTA_T_DATA_H
#define TAIYIN_INTERNAL_DELTA_T_DATA_H

namespace taiyin {
namespace internal {

struct DeltaTSplineSegment {
    double x0;
    double x1;
    double a3;
    double a2;
    double a1;
    double a0;
};

struct AnnualDeltaT {
    double year;
    double delta_t_seconds;
};

extern const DeltaTSplineSegment kDeltaTS15Spline[];
extern const int kDeltaTS15SplineCount;
extern const AnnualDeltaT kAnnualDeltaT[];
extern const int kAnnualDeltaTCount;

}  // namespace internal
}  // namespace taiyin

#endif
