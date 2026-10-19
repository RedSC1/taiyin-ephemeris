#ifndef TAIYIN_WASM_TRANSPORT_INTERNAL_H
#define TAIYIN_WASM_TRANSPORT_INTERNAL_H

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {
// All integer wire fields obey the same contract as AstrReader. Validate
// before conversion, including byte-sized fields, capacities and flag words.
template<class T> T web_integer(double value) {
    static_assert(std::numeric_limits<T>::is_integer, "integer transport type required");
    if (!std::isfinite(value) || value != std::floor(value)
        || (sizeof(T) >= 8 && std::fabs(value) > 9007199254740991.0)
        || static_cast<long double>(value) < static_cast<long double>((std::numeric_limits<T>::min)())
        || static_cast<long double>(value) > static_cast<long double>((std::numeric_limits<T>::max)()))
        throw std::invalid_argument("integer transport value out of range");
    return static_cast<T>(value);
}
// Same integer contract as AstrReader: a Number must denote an exact integer.
// This safe range is strictly inside int64_t, including on wasm32 where
// long double does not provide extra precision for checking INT64_MAX.
bool web_valid_jd_day(double day) {
    return std::isfinite(day) && day == std::floor(day)
        && std::fabs(day) <= 9007199254740991.0;
}
int64_t web_jd_day(double day) {
    return web_integer<int64_t>(day);
}
}
#endif
