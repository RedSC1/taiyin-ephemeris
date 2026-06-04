#include "taiyin/internal/eop.h"
#include "taiyin/angle.h"
#include "taiyin/time.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <new>
#include <vector>

namespace taiyin {
namespace internal {
namespace {

const double MJD_TO_JD_OFFSET = 2400000.5;
const double LOD_MILLISECONDS_TO_SECONDS = 0.001;
const double SP_ARCSEC_PER_CENTURY = -47.0e-6;

bool parse_double_field(const char* line, int line_size, int offset, int length, double* value) noexcept {
    if (!value || offset < 0 || length <= 0 || offset + length > line_size) {
        return false;
    }
    char field[32];
    if (length >= static_cast<int>(sizeof(field))) {
        return false;
    }
    std::memcpy(field, line + offset, static_cast<size_t>(length));
    field[length] = '\0';
    char* end = 0;
    errno = 0;
    const double parsed = std::strtod(field, &end);
    if (field == end || errno == ERANGE) {
        return false;
    }
    while (*end != '\0') {
        if (*end != ' ') {
            return false;
        }
        ++end;
    }
    *value = parsed;
    return true;
}

bool parse_finals2000a_line(const char* line, int line_size, EarthOrientationSample* sample) noexcept {
    if (!sample || line_size < 86) {
        return false;
    }
    double mjd = 0.0;
    double xp_arcsec = 0.0;
    double yp_arcsec = 0.0;
    double dut1_seconds = 0.0;
    double lod_milliseconds = 0.0;
    if (!parse_double_field(line, line_size, 7, 8, &mjd)
        || !parse_double_field(line, line_size, 18, 9, &xp_arcsec)
        || !parse_double_field(line, line_size, 37, 9, &yp_arcsec)
        || !parse_double_field(line, line_size, 58, 10, &dut1_seconds)
        || !parse_double_field(line, line_size, 79, 7, &lod_milliseconds)) {
        return false;
    }
    double dx_mas = 0.0;
    double dy_mas = 0.0;
    if (line_size >= 156) {
        parse_double_field(line, line_size, 136, 10, &dx_mas);
        parse_double_field(line, line_size, 146, 10, &dy_mas);
    }
    sample->jd_utc = mjd + MJD_TO_JD_OFFSET;
    sample->dut1_seconds = dut1_seconds;
    sample->xp_rad = xp_arcsec * TAIYIN_ARCSEC_TO_RAD;
    sample->yp_rad = yp_arcsec * TAIYIN_ARCSEC_TO_RAD;
    sample->sp_rad = sp_rad_for_jd(sample->jd_utc);
    sample->lod_seconds = lod_milliseconds * LOD_MILLISECONDS_TO_SECONDS;
    sample->dx_rad = dx_mas * TAIYIN_ARCSEC_TO_RAD / 1000.0;
    sample->dy_rad = dy_mas * TAIYIN_ARCSEC_TO_RAD / 1000.0;
    return true;
}

EarthOrientationSample interpolate_two_samples(
    const EarthOrientationSample& a,
    const EarthOrientationSample& b,
    double jd_utc
) noexcept {
    if (b.jd_utc == a.jd_utc) {
        return a;
    }
    const double t = (jd_utc - a.jd_utc) / (b.jd_utc - a.jd_utc);
    EarthOrientationSample sample;
    sample.jd_utc = jd_utc;
    sample.dut1_seconds = a.dut1_seconds + (b.dut1_seconds - a.dut1_seconds) * t;
    sample.xp_rad = a.xp_rad + (b.xp_rad - a.xp_rad) * t;
    sample.yp_rad = a.yp_rad + (b.yp_rad - a.yp_rad) * t;
    sample.sp_rad = sp_rad_for_jd(jd_utc);
    sample.lod_seconds = a.lod_seconds + (b.lod_seconds - a.lod_seconds) * t;
    sample.dx_rad = a.dx_rad + (b.dx_rad - a.dx_rad) * t;
    sample.dy_rad = a.dy_rad + (b.dy_rad - a.dy_rad) * t;
    return sample;
}

bool read_file_bytes(const char* path, std::vector<char>* out) noexcept {
    if (!path || !out) {
        return false;
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const std::ifstream::pos_type end_pos = file.tellg();
    if (end_pos <= 0 || end_pos > static_cast<std::ifstream::pos_type>(std::numeric_limits<int>::max())) {
        return false;
    }
    const int size = static_cast<int>(end_pos);
    std::vector<char> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!file.read(&bytes[0], size)) {
        return false;
    }
    *out = bytes;
    return true;
}

}  // namespace

double sp_rad_for_jd(double jd) noexcept {
    return SP_ARCSEC_PER_CENTURY * julian_centuries_from_j2000(jd) * TAIYIN_ARCSEC_TO_RAD;
}

bool interpolate_earth_orientation(
    const EarthOrientationTable* table,
    double jd_utc,
    EarthOrientationSample* out
) noexcept {
    if (!out || !table || table->count == 0) {
        return false;
    }
    if (jd_utc < table->samples[0].jd_utc || jd_utc > table->samples[table->count - 1].jd_utc) {
        return false;
    }
    size_t upper = 0;
    size_t lower = 0;
    while (upper < table->count && table->samples[upper].jd_utc < jd_utc) {
        ++upper;
    }
    if (upper == table->count) {
        *out = table->samples[table->count - 1];
        return true;
    }
    if (table->samples[upper].jd_utc == jd_utc || upper == 0) {
        *out = table->samples[upper];
        return true;
    }
    lower = upper - 1;
    *out = interpolate_two_samples(table->samples[lower], table->samples[upper], jd_utc);
    return true;
}

bool derive_earth_orientation_rates(
    const EarthOrientationTable* table,
    double jd_utc,
    EarthOrientationRates* out_rates,
    EarthRotationDerivatives* out_derivatives
) noexcept {
    if (!out_rates || !out_derivatives || !table || table->count < 2) {
        return false;
    }
    if (jd_utc < table->samples[0].jd_utc || jd_utc > table->samples[table->count - 1].jd_utc) {
        return false;
    }
    size_t upper = 0;
    while (upper < table->count && table->samples[upper].jd_utc < jd_utc) {
        ++upper;
    }
    size_t a_idx;
    size_t b_idx;
    if (upper == table->count) {
        b_idx = table->count - 1;
        a_idx = b_idx - 1;
    } else if (upper == 0) {
        a_idx = 0;
        b_idx = 1;
    } else if (table->samples[upper].jd_utc == jd_utc && upper + 1 < table->count) {
        a_idx = upper;
        b_idx = upper + 1;
    } else {
        b_idx = upper;
        a_idx = upper - 1;
    }
    const double dt_days = table->samples[b_idx].jd_utc - table->samples[a_idx].jd_utc;
    if (dt_days == 0.0) {
        return false;
    }
    EarthOrientationSample interpolated;
    if (!interpolate_earth_orientation(table, jd_utc, &interpolated)) {
        return false;
    }
    const EarthOrientationSample& sa = table->samples[a_idx];
    const EarthOrientationSample& sb = table->samples[b_idx];
    out_rates->xp_rate_rad_per_day = (sb.xp_rad - sa.xp_rad) / dt_days;
    out_rates->yp_rate_rad_per_day = (sb.yp_rad - sa.yp_rad) / dt_days;
    out_rates->sp_rate_rad_per_day = (sb.sp_rad - sa.sp_rad) / dt_days;
    out_rates->dx_rate_rad_per_day = (sb.dx_rad - sa.dx_rad) / dt_days;
    out_rates->dy_rate_rad_per_day = (sb.dy_rad - sa.dy_rad) / dt_days;
    out_derivatives->dut1_rate_seconds_per_day = (sb.dut1_seconds - sa.dut1_seconds) / dt_days;
    out_derivatives->lod_seconds = interpolated.lod_seconds;
    out_derivatives->lod_rate_seconds_per_day = (sb.lod_seconds - sa.lod_seconds) / dt_days;
    return true;
}

bool parse_finals2000a_table(
    const char* data,
    int size,
    EarthOrientationSample* out_samples,
    size_t max_count,
    size_t* out_count
) noexcept {
    if (!data || size <= 0 || !out_samples || max_count == 0 || !out_count) {
        return false;
    }
    *out_count = 0;
    int line_start = 0;
    while (line_start < size) {
        int line_end = line_start;
        while (line_end < size && data[line_end] != '\n' && data[line_end] != '\r') {
            ++line_end;
        }
        if (line_end > line_start) {
            EarthOrientationSample sample;
            if (parse_finals2000a_line(data + line_start, line_end - line_start, &sample)) {
                if (*out_count < max_count) {
                    out_samples[*out_count] = sample;
                    ++(*out_count);
                }
            }
        }
        line_start = line_end + 1;
        while (line_start < size && (data[line_start] == '\n' || data[line_start] == '\r')) {
            ++line_start;
        }
    }
    if (*out_count == 0) {
        return false;
    }
    std::sort(out_samples, out_samples + *out_count, [](const EarthOrientationSample& a, const EarthOrientationSample& b) {
        return a.jd_utc < b.jd_utc;
    });
    size_t write = 0;
    for (size_t read = 0; read < *out_count; ++read) {
        if (write == 0 || out_samples[read].jd_utc != out_samples[write - 1].jd_utc) {
            out_samples[write] = out_samples[read];
            ++write;
        }
    }
    *out_count = write;
    return true;
}

bool load_finals2000a_file(
    const char* path,
    EarthOrientationTable* out
) noexcept {
    if (!path || !out) {
        return false;
    }
    std::vector<char> bytes;
    if (!read_file_bytes(path, &bytes)) {
        return false;
    }
    size_t estimated_count = static_cast<size_t>(bytes.size()) / 80;
    if (estimated_count < 1) {
        estimated_count = 1;
    }
    EarthOrientationSample* samples = static_cast<EarthOrientationSample*>(
        ::operator new(estimated_count * sizeof(EarthOrientationSample), std::nothrow));
    if (!samples) {
        return false;
    }
    size_t count = 0;
    if (!parse_finals2000a_table(&bytes[0], static_cast<int>(bytes.size()), samples, estimated_count, &count)) {
        ::operator delete(samples);
        return false;
    }
    out->samples = samples;
    out->count = count;
    return true;
}

void destroy_earth_orientation_table(EarthOrientationTable* table) noexcept {
    if (!table || !table->samples) {
        return;
    }
    ::operator delete(const_cast<EarthOrientationSample*>(table->samples));
    table->samples = 0;
    table->count = 0;
}

}  // namespace internal
}  // namespace taiyin