#include "transport_internal.h"
#include <cstdlib>
#include <iostream>

void check(bool value) { if (!value) std::abort(); }
template<class T> void rejects(double value) {
    try { (void)web_integer<T>(value); }
    catch (const std::invalid_argument&) { return; }
    std::cerr << "accepted malformed integer: " << value << '\n';
    std::abort();
}
int main() {
    check(web_integer<int32_t>(-2147483648.0)==(std::numeric_limits<int32_t>::min)());
    check(web_integer<int32_t>(2147483647.0)==(std::numeric_limits<int32_t>::max)());
    check(web_integer<uint32_t>(4294967295.0)==(std::numeric_limits<uint32_t>::max)());
    check(web_integer<uint8_t>(255.0)==255);
    check(web_integer<uint16_t>(65535.0)==65535);
    check(web_integer<int64_t>(9007199254740991.0)==INT64_C(9007199254740991));
    check(web_jd_day(-9007199254740991.0)==-INT64_C(9007199254740991));
    check(web_integer<uint32_t>(-0.0)==0);
    for (double v : {0.5, std::numeric_limits<double>::quiet_NaN(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}) {
        rejects<uint8_t>(v); rejects<uint16_t>(v); rejects<uint32_t>(v);
        rejects<int32_t>(v); rejects<int64_t>(v); rejects<size_t>(v);
    }
    rejects<uint8_t>(256); rejects<uint8_t>(-1);
    rejects<uint16_t>(65536); rejects<uint32_t>(4294967296.0);
    rejects<int32_t>(2147483648.0); rejects<int32_t>(-2147483649.0);
    rejects<int64_t>(9007199254740992.0); rejects<int64_t>(-9007199254740992.0);
    rejects<uint64_t>(-1); rejects<uint64_t>(18446744073709551616.0);
    check(web_valid_jd_day(2460310.0)); check(!web_valid_jd_day(2460310.5));
    std::cout << "transport integer width/safe-range checks passed\n";
}
