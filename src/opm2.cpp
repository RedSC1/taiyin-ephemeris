#include "taiyin/internal/opm2.h"

#include "taiyin/chebyshev.h"
#include "taiyin/physical_constants.h"
#include "taiyin/time.h"
#include "taiyin/vector3.h"

#include <cstddef>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace taiyin {
namespace internal {
namespace {

const uint64_t UINT64_MAX_VALUE = std::numeric_limits<uint64_t>::max();

uint16_t read_u16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_u32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0])
        | (static_cast<uint32_t>(p[1]) << 8)
        | (static_cast<uint32_t>(p[2]) << 16)
        | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t read_u64(const uint8_t* p) noexcept {
    uint64_t value = 0;
    for (int i = 7; i >= 0; --i) {
        value = (value << 8) | static_cast<uint64_t>(p[i]);
    }
    return value;
}

int64_t read_i64(const uint8_t* p) noexcept {
    return static_cast<int64_t>(read_u64(p));
}

double read_f64(const uint8_t* p) noexcept {
    uint64_t bits = read_u64(p);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

bool checked_range(size_t total, uint64_t offset, uint64_t length, const uint8_t** out) noexcept {
    if (!out || offset > UINT64_MAX_VALUE - length) {
        return false;
    }
    const uint64_t end = offset + length;
    if (offset > static_cast<uint64_t>(total) || end > static_cast<uint64_t>(total)) {
        return false;
    }
    *out = 0;
    return true;
}

uint32_t fourcc(const char text[4]) noexcept {
    return static_cast<uint32_t>(static_cast<unsigned char>(text[0]))
        | (static_cast<uint32_t>(static_cast<unsigned char>(text[1])) << 8)
        | (static_cast<uint32_t>(static_cast<unsigned char>(text[2])) << 16)
        | (static_cast<uint32_t>(static_cast<unsigned char>(text[3])) << 24);
}

bool has_kind(const std::vector<Opm2SectionEntry>& sections, uint32_t kind) noexcept {
    for (size_t i = 0; i < sections.size(); ++i) {
        if (sections[i].kind == kind) {
            return true;
        }
    }
    return false;
}

const Opm2SectionEntry* find_section(const std::vector<Opm2SectionEntry>& sections, uint32_t kind) noexcept {
    for (size_t i = 0; i < sections.size(); ++i) {
        if (sections[i].kind == kind) {
            return &sections[i];
        }
    }
    return 0;
}

bool parse_section_table(
    const uint8_t* data,
    size_t byte_count,
    std::vector<Opm2SectionEntry>* out
) noexcept {
    if (!data || !out || byte_count < OPM2_HEADER_LEN || std::memcmp(data, "OPM2", 4) != 0) {
        return false;
    }
    const uint16_t header_len = read_u16(data + 4);
    const uint16_t version = read_u16(data + 6);
    const uint64_t table_offset = read_u64(data + 12);
    const uint32_t section_count = read_u32(data + 20);
    if (header_len < OPM2_HEADER_LEN || version != OPM2_CONTAINER_VERSION || section_count == 0) {
        return false;
    }
    if (table_offset < header_len || section_count > 1024) {
        return false;
    }
    const uint64_t table_size = static_cast<uint64_t>(section_count) * OPM2_SECTION_ENTRY_LEN;
    const uint8_t* ignored = 0;
    if (!checked_range(byte_count, table_offset, table_size, &ignored)) {
        return false;
    }

    try {
        out->clear();
        for (uint32_t i = 0; i < section_count; ++i) {
            const uint8_t* entry = data + table_offset + static_cast<uint64_t>(i) * OPM2_SECTION_ENTRY_LEN;
            Opm2SectionEntry parsed;
            parsed.kind = read_u32(entry);
            parsed.version = read_u16(entry + 4);
            parsed.flags = read_u16(entry + 6);
            parsed.offset = read_u64(entry + 8);
            parsed.length = read_u64(entry + 16);
            parsed.crc32 = read_u32(entry + 24);
            parsed.reserved = read_u32(entry + 28);
            if (!checked_range(byte_count, parsed.offset, parsed.length, &ignored) || has_kind(*out, parsed.kind)) {
                out->clear();
                return false;
            }
            out->push_back(parsed);
        }
    } catch (...) {
        out->clear();
        return false;
    }

    return has_kind(*out, OPM2_SEC_EPHE)
        && has_kind(*out, OPM2_SEC_GRID)
        && has_kind(*out, OPM2_SEC_DOMN)
        && has_kind(*out, OPM2_SEC_MODL)
        && has_kind(*out, OPM2_SEC_QNTB)
        && has_kind(*out, OPM2_SEC_RCOF);
}

bool section_payload(
    const uint8_t* data,
    const std::vector<Opm2SectionEntry>& sections,
    uint32_t kind,
    const uint8_t** out,
    size_t* out_size
) noexcept {
    if (!data || !out || !out_size) {
        return false;
    }
    const Opm2SectionEntry* section = find_section(sections, kind);
    if (!section || section->length > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        return false;
    }
    *out = data + section->offset;
    *out_size = static_cast<size_t>(section->length);
    return true;
}

bool parse_ephe_payload(const uint8_t* payload, size_t size, Opm2EpheSection* out) noexcept {
    if (!payload || !out || size != 48) {
        return false;
    }
    Opm2EpheSection ephe;
    ephe.target_id = static_cast<int>(read_u32(payload));
    ephe.center_id = static_cast<int>(read_u32(payload + 4));
    ephe.frame_id = read_u32(payload + 8);
    ephe.time_scale_id = read_u32(payload + 12);
    ephe.model_id = read_u32(payload + 16);
    ephe.storage_kind = read_u32(payload + 20);
    ephe.axis_count = read_u32(payload + 24);
    ephe.coverage_start_jd = read_f64(payload + 32);
    ephe.coverage_end_jd = read_f64(payload + 40);
    if (ephe.target_id == ephe.center_id
        || ephe.axis_count != OPM2_AXIS_COUNT
        || ephe.time_scale_id != OPM2_TIME_SCALE_TDB
        || ephe.frame_id != OPM2_FRAME_ECLIPTIC_J2000
        || !std::isfinite(ephe.coverage_start_jd)
        || !std::isfinite(ephe.coverage_end_jd)
        || ephe.coverage_end_jd <= ephe.coverage_start_jd) {
        return false;
    }
    *out = ephe;
    return true;
}

bool parse_grid_payload(const uint8_t* payload, size_t size, Opm2GridSection* out) noexcept {
    if (!payload || !out || size != 56) {
        return false;
    }
    Opm2GridSection grid;
    grid.grid_kind = read_u32(payload);
    grid.correction_kind = read_u32(payload + 4);
    grid.first_segment_index = read_i64(payload + 8);
    grid.segment_count = read_u64(payload + 16);
    grid.origin_jd = read_f64(payload + 24);
    grid.period_days = read_f64(payload + 32);
    grid.correction_table_offset = read_u64(payload + 40);
    grid.correction_table_size = read_u64(payload + 48);
    if (grid.correction_kind != OPM2_CORRECTION_NONE
        || grid.segment_count == 0
        || grid.segment_count > static_cast<uint64_t>(std::numeric_limits<int>::max())
        || !std::isfinite(grid.origin_jd)
        || !std::isfinite(grid.period_days)
        || grid.period_days <= 0.0) {
        return false;
    }
    *out = grid;
    return true;
}

bool parse_domain_payload(const uint8_t* payload, size_t size, Opm2DomainSection* out) noexcept {
    if (!payload || !out || size != 24) {
        return false;
    }
    Opm2DomainSection domain;
    domain.domain_kind = read_u32(payload);
    domain.left_expansion_days = read_f64(payload + 8);
    domain.right_expansion_days = read_f64(payload + 16);
    if (!std::isfinite(domain.left_expansion_days) || !std::isfinite(domain.right_expansion_days)) {
        return false;
    }
    if (domain.domain_kind == OPM2_DOMAIN_AFFINE_EXPANSION_DAYS) {
        if (domain.left_expansion_days < 0.0 || domain.right_expansion_days < 0.0) {
            return false;
        }
    } else if (domain.domain_kind == OPM2_DOMAIN_EXPANSION_FRACTION) {
        if (std::fabs(domain.left_expansion_days - domain.right_expansion_days) > 0.0) {
            return false;
        }
        domain.has_expansion_fraction = true;
        domain.expansion_fraction = domain.left_expansion_days;
        domain.left_expansion_days = 0.0;
        domain.right_expansion_days = 0.0;
        if (domain.expansion_fraction < 0.0) {
            return false;
        }
    } else {
        return false;
    }
    *out = domain;
    return true;
}

bool parse_qntb_payload(const uint8_t* payload, size_t size, std::vector<double>* out) noexcept {
    if (!payload || !out || size < 8) {
        return false;
    }
    const uint16_t axis_count = read_u16(payload);
    const uint16_t coeff_count = read_u16(payload + 2);
    const uint32_t flags = read_u32(payload + 4);
    if (axis_count != OPM2_AXIS_COUNT || coeff_count == 0 || (flags & OPM2_QNTB_SHARED_AXIS_STEPS) == 0) {
        return false;
    }
    const size_t expected = 8u + static_cast<size_t>(coeff_count) * sizeof(double);
    if (size != expected) {
        return false;
    }
    try {
        out->assign(coeff_count, 0.0);
        for (uint16_t i = 0; i < coeff_count; ++i) {
            (*out)[i] = read_f64(payload + 8u + static_cast<size_t>(i) * sizeof(double));
            if (!std::isfinite((*out)[i]) || (*out)[i] <= 0.0) {
                return false;
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

int64_t zigzag_decode(uint64_t value) noexcept {
    return (value & 1u) == 0u
        ? static_cast<int64_t>(value >> 1)
        : -static_cast<int64_t>((value >> 1) + 1u);
}

struct BitReader {
    const uint8_t* data;
    size_t size;
    size_t pos;
    uint64_t acc;
    int nbits;

    BitReader(const uint8_t* p, size_t s) noexcept : data(p), size(s), pos(0), acc(0), nbits(0) {}

    bool read(int width, uint64_t* out) noexcept {
        if (!out || width <= 0 || width > 63) {
            return false;
        }
        while (nbits < width) {
            if (pos >= size) {
                return false;
            }
            acc |= static_cast<uint64_t>(data[pos++]) << nbits;
            nbits += 8;
        }
        *out = acc & ((uint64_t(1) << width) - 1u);
        acc >>= width;
        nbits -= width;
        return true;
    }

    bool skip(uint64_t bit_count) noexcept {
        if (bit_count == 0) {
            return true;
        }

        if (nbits > 0) {
            const uint64_t consume = bit_count < static_cast<uint64_t>(nbits)
                ? bit_count
                : static_cast<uint64_t>(nbits);
            acc >>= consume;
            nbits -= static_cast<int>(consume);
            bit_count -= consume;
        }

        if (bit_count >= 8 && nbits == 0) {
            const uint64_t byte_count = bit_count / 8u;
            if (byte_count > static_cast<uint64_t>(size - pos)) {
                return false;
            }
            pos += static_cast<size_t>(byte_count);
            bit_count -= byte_count * 8u;
            acc = 0;
        }

        while (bit_count > 0) {
            if (nbits == 0) {
                if (pos >= size) {
                    return false;
                }
                acc = static_cast<uint64_t>(data[pos++]);
                nbits = 8;
            }
            const uint64_t consume = bit_count < static_cast<uint64_t>(nbits)
                ? bit_count
                : static_cast<uint64_t>(nbits);
            acc >>= consume;
            nbits -= static_cast<int>(consume);
            bit_count -= consume;
        }
        return true;
    }
};

bool parse_rcof_payload_range(
    const uint8_t* payload,
    size_t size,
    uint64_t segment_count,
    size_t expected_coeff_count,
    uint64_t first_segment,
    uint64_t selected_segment_count,
    std::vector<int64_t>* qcoeffs
) noexcept {
    if (!payload || !qcoeffs || size < 24 || segment_count == 0 || selected_segment_count == 0) {
        return false;
    }
    const uint16_t axis_count = read_u16(payload);
    const uint16_t coeff_count = read_u16(payload + 2);
    const uint32_t width_count = read_u32(payload + 4);
    const uint64_t width_offset = read_u64(payload + 8);
    const uint64_t payload_offset = read_u64(payload + 16);
    if (axis_count != OPM2_AXIS_COUNT
        || coeff_count == 0
        || coeff_count != expected_coeff_count
        || width_count != OPM2_AXIS_COUNT * static_cast<uint32_t>(coeff_count)
        || width_offset > payload_offset
        || payload_offset > static_cast<uint64_t>(size)
        || width_offset + width_count > payload_offset
        || first_segment > segment_count
        || selected_segment_count > segment_count - first_segment
        || selected_segment_count > static_cast<uint64_t>(std::numeric_limits<size_t>::max())
        || static_cast<uint64_t>(OPM2_AXIS_COUNT) * static_cast<uint64_t>(coeff_count)
            > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) / selected_segment_count) {
        return false;
    }
    const uint8_t* widths = payload + width_offset;
    const uint8_t* packed = payload + payload_offset;
    size_t cursor = 0;
    try {
        qcoeffs->assign(static_cast<size_t>(selected_segment_count) * OPM2_AXIS_COUNT * coeff_count, 0);
        for (int axis = 0; axis < OPM2_AXIS_COUNT; ++axis) {
            uint64_t bit_count = 0;
            for (uint16_t coeff = 0; coeff < coeff_count; ++coeff) {
                const uint8_t width = widths[axis * coeff_count + coeff];
                if (width == 0 || width > 63) {
                    return false;
                }
                bit_count += static_cast<uint64_t>(width) * segment_count;
            }
            const size_t byte_count = static_cast<size_t>((bit_count + 7u) / 8u);
            if (cursor + byte_count > size - static_cast<size_t>(payload_offset)) {
                return false;
            }
            BitReader reader(packed + cursor, byte_count);
            for (uint16_t coeff = 0; coeff < coeff_count; ++coeff) {
                const int width = widths[axis * coeff_count + coeff];
                if (!reader.skip(first_segment * static_cast<uint64_t>(width))) {
                    return false;
                }
                for (uint64_t segment = 0; segment < selected_segment_count; ++segment) {
                    uint64_t encoded = 0;
                    if (!reader.read(width, &encoded)) {
                        return false;
                    }
                    const size_t index = (static_cast<size_t>(segment) * OPM2_AXIS_COUNT + axis) * coeff_count + coeff;
                    (*qcoeffs)[index] = zigzag_decode(encoded);
                }
                const uint64_t trailing_segments = segment_count - first_segment - selected_segment_count;
                if (!reader.skip(trailing_segments * static_cast<uint64_t>(width))) {
                    return false;
                }
            }
            cursor += byte_count;
        }
    } catch (...) {
        return false;
    }
    return cursor == size - static_cast<size_t>(payload_offset);
}

bool parse_rcof_payload(
    const uint8_t* payload,
    size_t size,
    uint64_t segment_count,
    size_t expected_coeff_count,
    std::vector<int64_t>* qcoeffs
) noexcept {
    return parse_rcof_payload_range(
        payload,
        size,
        segment_count,
        expected_coeff_count,
        0,
        segment_count,
        qcoeffs);
}

bool split_standard_modl(
    const uint8_t* payload,
    size_t size,
    std::vector<double>* shape_x,
    std::vector<double>* shape_y,
    std::vector<double>* frame_coeffs,
    uint32_t* frame_rows
) noexcept {
    if (!payload || !shape_x || !shape_y || !frame_coeffs || !frame_rows || (size % sizeof(double)) != 0) {
        return false;
    }
    const size_t value_count = size / sizeof(double);
    std::vector<double> values;
    try {
        values.assign(value_count, 0.0);
        for (size_t i = 0; i < value_count; ++i) {
            values[i] = read_f64(payload + i * sizeof(double));
            if (!std::isfinite(values[i])) {
                return false;
            }
        }
        for (uint32_t rows = 4; rows >= 3; --rows) {
            const size_t tail = static_cast<size_t>(rows) * 2u;
            if (value_count >= tail && ((value_count - tail) % 2u) == 0u) {
                const size_t shape_count = (value_count - tail) / 2u;
                if (shape_count > 0) {
                    shape_x->assign(values.begin(), values.begin() + static_cast<long>(shape_count));
                    shape_y->assign(values.begin() + static_cast<long>(shape_count), values.begin() + static_cast<long>(2u * shape_count));
                    frame_coeffs->assign(values.begin() + static_cast<long>(2u * shape_count), values.end());
                    *frame_rows = rows;
                    return true;
                }
            }
            if (rows == 3) {
                break;
            }
        }
    } catch (...) {
        return false;
    }
    return false;
}

bool parse_lunar_modl(const uint8_t* payload, size_t size, Opm2LunarCoeffRefModel* out) noexcept {
    if (!payload || !out || size < 32 || ((size - 32u) % sizeof(double)) != 0) {
        return false;
    }
    Opm2LunarCoeffRefModel model;
    model.degree = read_u16(payload);
    model.axis_count = read_u16(payload + 2);
    model.frame_basis_kind = read_u16(payload + 4);
    model.frame_time_origin_jd = read_f64(payload + 8);
    model.frame_time_unit_days = read_f64(payload + 16);
    model.frame_period_years = read_f64(payload + 24);
    if (model.axis_count != OPM2_AXIS_COUNT
        || model.frame_basis_kind != OPM2_FRAME_BASIS_AFFINE_PLUS_SINCOS_PERIOD
        || !std::isfinite(model.frame_time_origin_jd)
        || !std::isfinite(model.frame_time_unit_days)
        || !std::isfinite(model.frame_period_years)
        || model.frame_time_unit_days == 0.0
        || model.frame_period_years == 0.0) {
        return false;
    }
    const size_t value_count = (size - 32u) / sizeof(double);
    const size_t ref_count = OPM2_AXIS_COUNT * (static_cast<size_t>(model.degree) + 1u);
    if (value_count != 12u + ref_count) {
        return false;
    }
    try {
        for (size_t i = 0; i < 4; ++i) {
            model.u_coeff[i] = read_f64(payload + 32u + i * sizeof(double));
            model.v_coeff[i] = read_f64(payload + 32u + (4u + i) * sizeof(double));
            model.angle_coeff[i] = read_f64(payload + 32u + (8u + i) * sizeof(double));
        }
        model.ref_coeffs.assign(ref_count, 0.0);
        for (size_t i = 0; i < ref_count; ++i) {
            model.ref_coeffs[i] = read_f64(payload + 32u + (12u + i) * sizeof(double));
        }
    } catch (...) {
        return false;
    }
    *out = model;
    return true;
}

double normalize_time(double jd, double start, double end) noexcept {
    return (2.0 * jd - start - end) / (end - start);
}

bool segment_bounds(const Opm2EphemerisData* data, size_t local_index, double* out_a, double* out_b) noexcept {
    if (!data || local_index >= data->grid.segment_count || !out_a || !out_b) {
        return false;
    }
    const int64_t global_index = data->grid.first_segment_index + static_cast<int64_t>(local_index);
    const double a = data->grid.origin_jd + static_cast<double>(global_index) * data->grid.period_days;
    const double b = a + data->grid.period_days;
    if (!std::isfinite(a) || !std::isfinite(b) || b <= a) {
        return false;
    }
    *out_a = a;
    *out_b = b;
    return true;
}

double days_between_canonical_split_jd(
    const SplitJulianDate& jd_a,
    const SplitJulianDate& jd_b
) noexcept {
    const bool forward = jd_b.day_number >= jd_a.day_number;
    const uint64_t whole_day_difference = forward
        ? static_cast<uint64_t>(jd_b.day_number) - static_cast<uint64_t>(jd_a.day_number)
        : static_cast<uint64_t>(jd_a.day_number) - static_cast<uint64_t>(jd_b.day_number);
    const long double day_difference = forward
        ? static_cast<long double>(whole_day_difference)
        : -static_cast<long double>(whole_day_difference);
    return static_cast<double>(
        day_difference + static_cast<long double>(jd_b.day_fraction)
        - static_cast<long double>(jd_a.day_fraction));
}

bool select_segment(
    const Opm2EphemerisData* data,
    const SplitJulianDate& jd,
    size_t* out_segment
) noexcept {
    if (!data || !out_segment || data->segment_start_jds.size() != data->grid.segment_count
        || data->grid.period_days <= 0.0) {
        return false;
    }
    const double from_start = days_between_canonical_split_jd(data->coverage_start_jd, jd);
    const double from_end = days_between_canonical_split_jd(data->coverage_end_jd, jd);
    if (!std::isfinite(from_start) || !std::isfinite(from_end)
        || from_start < -1.0e-9 || from_end > 1.0e-9) {
        return false;
    }
    const double raw_index = std::floor(
        days_between_canonical_split_jd(data->grid_origin_jd, jd) / data->grid.period_days);
    if (!std::isfinite(raw_index)
        || raw_index < static_cast<double>(std::numeric_limits<int64_t>::min())
        || raw_index > static_cast<double>(std::numeric_limits<int64_t>::max())) {
        return false;
    }
    const int64_t global_index = static_cast<int64_t>(raw_index);
    int64_t local_index = global_index - data->grid.first_segment_index;
    if (std::fabs(from_end) <= 1.0e-9) {
        local_index = static_cast<int64_t>(data->grid.segment_count) - 1;
    }
    if (local_index < 0 || local_index >= static_cast<int64_t>(data->grid.segment_count)) {
        return false;
    }
    *out_segment = static_cast<size_t>(local_index);
    return true;
}

bool normalize_domain_for_segment(
    const Opm2EphemerisData* data,
    size_t local_index,
    const SplitJulianDate& jd,
    double* tau,
    double* scale
) noexcept {
    if (!data || local_index >= data->segment_start_jds.size() || !tau || !scale) {
        return false;
    }
    double left_padding = data->domain.left_expansion_days;
    double right_padding = data->domain.right_expansion_days;
    if (data->domain.has_expansion_fraction) {
        const double pad = data->domain.expansion_fraction * data->grid.period_days;
        left_padding = pad;
        right_padding = pad;
    }
    const double domain_days = data->grid.period_days + left_padding + right_padding;
    if (!std::isfinite(left_padding) || !std::isfinite(right_padding)
        || !std::isfinite(domain_days) || domain_days <= 0.0) {
        return false;
    }
    const double offset_days = days_between_canonical_split_jd(
        data->segment_start_jds[local_index], jd) + left_padding;
    if (!std::isfinite(offset_days)) {
        return false;
    }
    *tau = 2.0 * offset_days / domain_days - 1.0;
    *scale = 2.0 / domain_days;
    return true;
}

size_t qcoeff_index(const Opm2EphemerisData* data, size_t segment, int axis, size_t coeff) noexcept {
    return (segment * OPM2_AXIS_COUNT + static_cast<size_t>(axis)) * data->quant_steps.size() + coeff;
}

bool segment_coeffs(const Opm2EphemerisData* data, size_t segment, double coeffs[OPM2_AXIS_COUNT][128]) noexcept {
    const size_t coeff_count = data->quant_steps.size();
    if (coeff_count == 0 || coeff_count > 128) {
        return false;
    }
    for (int axis = 0; axis < OPM2_AXIS_COUNT; ++axis) {
        for (size_t coeff = 0; coeff < coeff_count; ++coeff) {
            coeffs[axis][coeff] = static_cast<double>(data->qcoeffs[qcoeff_index(data, segment, axis, coeff)]) * data->quant_steps[coeff];
        }
    }
    return true;
}

Vector3 frame_column_from_normal_x(const Vector3& n) noexcept {
    if (n.z > -1.0 + 1.0e-12) {
        const double inv = 1.0 / (1.0 + n.z);
        return Vector3{1.0 - n.x * n.x * inv, -n.x * n.y * inv, -n.x};
    }
    return Vector3{1.0, 0.0, 0.0};
}

Vector3 frame_column_from_normal_y(const Vector3& n) noexcept {
    if (n.z > -1.0 + 1.0e-12) {
        const double inv = 1.0 / (1.0 + n.z);
        return Vector3{-n.x * n.y * inv, 1.0 - n.y * n.y * inv, -n.y};
    }
    return Vector3{0.0, -1.0, 0.0};
}

Vector3 plane_frame_x(double u, double v) noexcept {
    const double den_inv = 1.0 / (1.0 + u * u + v * v);
    return Vector3{(1.0 + v * v - u * u) * den_inv, 2.0 * v * u * den_inv, -2.0 * u * den_inv};
}

Vector3 plane_frame_y(double u, double v) noexcept {
    const double den_inv = 1.0 / (1.0 + u * u + v * v);
    return Vector3{2.0 * v * u * den_inv, (1.0 - v * v + u * u) * den_inv, 2.0 * v * den_inv};
}

Vector3 plane_frame_z(double u, double v) noexcept {
    const double den_inv = 1.0 / (1.0 + u * u + v * v);
    return Vector3{2.0 * u * den_inv, -2.0 * v * den_inv, (1.0 - u * u - v * v) * den_inv};
}

Vector3 apply_frame_columns(const Vector3& local, const Vector3& x_axis, const Vector3& y_axis, const Vector3& z_axis) noexcept {
    Vector3 out;
    out.x = local.x * x_axis.x + local.y * y_axis.x + local.z * z_axis.x;
    out.y = local.x * x_axis.y + local.y * y_axis.y + local.z * z_axis.y;
    out.z = local.x * x_axis.z + local.y * y_axis.z + local.z * z_axis.z;
    return out;
}

Vector3 unalign_regular(const Vector3& aligned, double u, double v, double angle) noexcept {
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const Vector3 local{c * aligned.x - s * aligned.y, s * aligned.x + c * aligned.y, aligned.z};
    return apply_frame_columns(local, plane_frame_x(u, v), plane_frame_y(u, v), plane_frame_z(u, v));
}

Vector3 unalign_normal(const Vector3& aligned, const Vector3& normal, double angle) noexcept {
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    const Vector3 local{c * aligned.x - s * aligned.y, s * aligned.x + c * aligned.y, aligned.z};
    return apply_frame_columns(local, frame_column_from_normal_x(normal), frame_column_from_normal_y(normal), normal);
}

bool eval_frame_params(const Opm2EphemerisData* data, size_t segment, double* out) noexcept {
    if (!data || !out || segment >= data->grid.segment_count || data->frame_rows == 0) {
        return false;
    }
    if (data->frame_coeffs.size() < static_cast<size_t>(data->frame_rows) * 2u
        || data->grid.period_days <= 0.0
        || data->grid.segment_count == 0) {
        return false;
    }

    const double first_mid = data->frame_first_mid_jd;
    const double last_mid = data->frame_last_mid_jd;
    const double current_mid = data->grid.origin_jd
        + (static_cast<double>(data->grid.first_segment_index)
              + static_cast<double>(segment) + 0.5)
            * data->grid.period_days;
    if (!std::isfinite(first_mid) || !std::isfinite(last_mid) || !std::isfinite(current_mid)) {
        return false;
    }

    const double t = first_mid == last_mid
        ? 0.0
        : normalize_time(current_mid, first_mid, last_mid);
    if (!std::isfinite(t)) {
        return false;
    }

    for (uint32_t i = 0; i < data->frame_rows; ++i) {
        const double* coeff = &data->frame_coeffs[i * 2u];
        out[i] = chebyshev_eval(coeff, 2, t);
    }
    if (data->frame_rows == 4) {
        Vector3 normal{out[0], out[1], out[2]};
        const double norm = vector3_norm(normal);
        if (norm <= 0.0) {
            return false;
        }
        normal = vector3_scale(normal, 1.0 / norm);
        out[0] = normal.x;
        out[1] = normal.y;
        out[2] = normal.z;
    }
    return true;
}

double eval_formula4(const double coeff[4], double t, double w) noexcept {
    return coeff[0] + coeff[1] * t + coeff[2] * std::cos(w * t) + coeff[3] * std::sin(w * t);
}

bool eval_lunar_params(const Opm2EphemerisData* data, size_t segment, double* out) noexcept {
    if (!data || !out || segment >= data->grid.segment_count) {
        return false;
    }
    if (data->lunar_model.frame_time_unit_days == 0.0
        || data->lunar_model.frame_period_years == 0.0) {
        return false;
    }
    double a = 0.0;
    double b = 0.0;
    if (!segment_bounds(data, segment, &a, &b)) {
        return false;
    }
    const double mid = 0.5 * (a + b);
    const double t = (mid - data->lunar_model.frame_time_origin_jd) / data->lunar_model.frame_time_unit_days;
    const double w = TAIYIN_TWO_PI / data->lunar_model.frame_period_years;
    if (!std::isfinite(t) || !std::isfinite(w)) {
        return false;
    }
    out[0] = eval_formula4(data->lunar_model.u_coeff, t, w);
    out[1] = eval_formula4(data->lunar_model.v_coeff, t, w);
    out[2] = eval_formula4(data->lunar_model.angle_coeff, t, w);
    return true;
}

bool state_from_position_derivatives(
    const Vector3& position_km,
    const Vector3& velocity_km_per_day,
    const Vector3& acceleration_km_per_day2,
    CartesianState* out
) noexcept {
    if (!out) {
        return false;
    }
    out->position_au = vector3_scale(position_km, 1.0 / TAIYIN_AU_KM);
    out->velocity_au_per_day = vector3_scale(velocity_km_per_day, 1.0 / TAIYIN_AU_KM);
    out->acceleration_au_per_day2 = vector3_scale(acceleration_km_per_day2, 1.0 / TAIYIN_AU_KM);
    return true;
}

struct Opm2StateKm {
    Vector3 position;
    Vector3 velocity;
    Vector3 acceleration;
};

Vector3 transform_regular_components(const Vector3& aligned, double u, double v, double angle) noexcept {
    return unalign_regular(aligned, u, v, angle);
}

Vector3 transform_normal_components(const Vector3& aligned, const Vector3& normal, double angle) noexcept {
    return unalign_normal(aligned, normal, angle);
}

bool eval_opm2_state_km(const SplitJulianDate& jd_tdb, const Opm2EphemerisData* data, Opm2StateKm* out) noexcept {
    SplitJulianDate jd;
    if (!data || !out || !normalize_split_julian_date(
            jd_tdb.day_number, jd_tdb.day_fraction, &jd)) {
        return false;
    }
    size_t segment = 0;
    if (!select_segment(data, jd, &segment)) {
        return false;
    }
    double tau = 0.0;
    double scale = 0.0;
    if (!normalize_domain_for_segment(data, segment, jd, &tau, &scale)) {
        return false;
    }

    double coeffs[OPM2_AXIS_COUNT][128];
    if (!segment_coeffs(data, segment, coeffs)) {
        return false;
    }
    const int coeff_count = static_cast<int>(data->quant_steps.size());

    Vector3 aligned_position;
    Vector3 aligned_velocity;
    Vector3 aligned_acceleration;

    if (data->ephe.model_id == OPM2_MODEL_RAW_XYZ_CHEB_V1) {
        const ChebyshevValue x = chebyshev_eval_with_derivative(coeffs[0], coeff_count, tau);
        const ChebyshevValue y = chebyshev_eval_with_derivative(coeffs[1], coeff_count, tau);
        const ChebyshevValue z = chebyshev_eval_with_derivative(coeffs[2], coeff_count, tau);
        aligned_position = Vector3{x.value, y.value, z.value};
        aligned_velocity = Vector3{x.derivative * scale, y.derivative * scale, z.derivative * scale};
        aligned_acceleration = Vector3{
            x.second_derivative * scale * scale,
            y.second_derivative * scale * scale,
            z.second_derivative * scale * scale};
        out->position = aligned_position;
        out->velocity = aligned_velocity;
        out->acceleration = aligned_acceleration;
        return true;
    }

    if (data->ephe.model_id == OPM2_MODEL_LUNAR_FIXED_COEFF_REF_V1) {
        if (data->lunar_model.ref_coeffs.size() != OPM2_AXIS_COUNT * data->quant_steps.size()) {
            return false;
        }
        double local_coeffs[OPM2_AXIS_COUNT][128];
        for (int axis = 0; axis < OPM2_AXIS_COUNT; ++axis) {
            for (size_t coeff = 0; coeff < data->quant_steps.size(); ++coeff) {
                local_coeffs[axis][coeff] =
                    data->lunar_model.ref_coeffs[axis * data->quant_steps.size() + coeff]
                    + coeffs[axis][coeff];
            }
        }
        const ChebyshevValue x = chebyshev_eval_with_derivative(local_coeffs[0], coeff_count, tau);
        const ChebyshevValue y = chebyshev_eval_with_derivative(local_coeffs[1], coeff_count, tau);
        const ChebyshevValue z = chebyshev_eval_with_derivative(local_coeffs[2], coeff_count, tau);
        aligned_position = Vector3{x.value, y.value, z.value};
        aligned_velocity = Vector3{x.derivative * scale, y.derivative * scale, z.derivative * scale};
        aligned_acceleration = Vector3{
            x.second_derivative * scale * scale,
            y.second_derivative * scale * scale,
            z.second_derivative * scale * scale};

        double lunar_params[3];
        if (!eval_lunar_params(data, segment, lunar_params)) {
            return false;
        }
        out->position = transform_regular_components(aligned_position, lunar_params[0], lunar_params[1], lunar_params[2]);
        out->velocity = transform_regular_components(aligned_velocity, lunar_params[0], lunar_params[1], lunar_params[2]);
        out->acceleration = transform_regular_components(aligned_acceleration, lunar_params[0], lunar_params[1], lunar_params[2]);
        return true;
    }

    if (data->shape_x.empty() || data->shape_y.empty() || data->frame_rows == 0) {
        return false;
    }

    const ChebyshevValue shape_x = chebyshev_eval_with_derivative(&data->shape_x[0], static_cast<int>(data->shape_x.size()), tau);
    const ChebyshevValue shape_y = chebyshev_eval_with_derivative(&data->shape_y[0], static_cast<int>(data->shape_y.size()), tau);
    const ChebyshevValue dx = chebyshev_eval_with_derivative(coeffs[0], coeff_count, tau);
    const ChebyshevValue dy = chebyshev_eval_with_derivative(coeffs[1], coeff_count, tau);
    const ChebyshevValue dz = chebyshev_eval_with_derivative(coeffs[2], coeff_count, tau);
    aligned_position = Vector3{
        shape_x.value + dx.value,
        shape_y.value + dy.value,
        dz.value};
    aligned_velocity = Vector3{
        (shape_x.derivative + dx.derivative) * scale,
        (shape_y.derivative + dy.derivative) * scale,
        dz.derivative * scale};
    aligned_acceleration = Vector3{
        (shape_x.second_derivative + dx.second_derivative) * scale * scale,
        (shape_y.second_derivative + dy.second_derivative) * scale * scale,
        dz.second_derivative * scale * scale};

    double params[4] = {0.0, 0.0, 0.0, 0.0};
    if (!eval_frame_params(data, segment, params)) {
        return false;
    }
    if (data->frame_rows == 4) {
        Vector3 normal{params[0], params[1], params[2]};
        const double norm = vector3_norm(normal);
        if (norm <= 0.0) {
            return false;
        }
        normal = vector3_scale(normal, 1.0 / norm);
        out->position = transform_normal_components(aligned_position, normal, params[3]);
        out->velocity = transform_normal_components(aligned_velocity, normal, params[3]);
        out->acceleration = transform_normal_components(aligned_acceleration, normal, params[3]);
    } else {
        out->position = transform_regular_components(aligned_position, params[0], params[1], params[2]);
        out->velocity = transform_regular_components(aligned_velocity, params[0], params[1], params[2]);
        out->acceleration = transform_regular_components(aligned_acceleration, params[0], params[1], params[2]);
    }
    return true;
}

// Component-aware mirror of eval_opm2_state_km that evaluates only the value
// and first derivative (position + velocity). Every branch, validation check,
// and frame/lunar-parameter call below must stay in lockstep with
// eval_opm2_state_km; changing the validation logic on one side without the
// other silently forks the two paths.
bool eval_opm2_position_velocity_km(
    const SplitJulianDate& jd_tdb,
    const Opm2EphemerisData* data,
    Vector3* out_position_km,
    Vector3* out_velocity_km_per_day
) noexcept {
    if (!data || !out_position_km || !out_velocity_km_per_day) {
        return false;
    }
    SplitJulianDate jd;
    if (!normalize_split_julian_date(
            jd_tdb.day_number, jd_tdb.day_fraction, &jd)) {
        return false;
    }
    size_t segment = 0;
    if (!select_segment(data, jd, &segment)) {
        return false;
    }
    double tau = 0.0;
    double scale = 0.0;
    if (!normalize_domain_for_segment(data, segment, jd, &tau, &scale)) {
        return false;
    }

    double coeffs[OPM2_AXIS_COUNT][128];
    if (!segment_coeffs(data, segment, coeffs)) {
        return false;
    }
    const int coeff_count = static_cast<int>(data->quant_steps.size());

    Vector3 aligned_position;
    Vector3 aligned_velocity;

    if (data->ephe.model_id == OPM2_MODEL_RAW_XYZ_CHEB_V1) {
        const ChebyshevValueWithDerivative x = chebyshev_eval_with_first_derivative(coeffs[0], coeff_count, tau);
        const ChebyshevValueWithDerivative y = chebyshev_eval_with_first_derivative(coeffs[1], coeff_count, tau);
        const ChebyshevValueWithDerivative z = chebyshev_eval_with_first_derivative(coeffs[2], coeff_count, tau);
        aligned_position = Vector3{x.value, y.value, z.value};
        aligned_velocity = Vector3{x.derivative * scale, y.derivative * scale, z.derivative * scale};
        *out_position_km = aligned_position;
        *out_velocity_km_per_day = aligned_velocity;
        return true;
    }

    if (data->ephe.model_id == OPM2_MODEL_LUNAR_FIXED_COEFF_REF_V1) {
        if (data->lunar_model.ref_coeffs.size() != OPM2_AXIS_COUNT * data->quant_steps.size()) {
            return false;
        }
        double local_coeffs[OPM2_AXIS_COUNT][128];
        for (int axis = 0; axis < OPM2_AXIS_COUNT; ++axis) {
            for (size_t coeff = 0; coeff < data->quant_steps.size(); ++coeff) {
                local_coeffs[axis][coeff] =
                    data->lunar_model.ref_coeffs[axis * data->quant_steps.size() + coeff]
                    + coeffs[axis][coeff];
            }
        }
        const ChebyshevValueWithDerivative x = chebyshev_eval_with_first_derivative(local_coeffs[0], coeff_count, tau);
        const ChebyshevValueWithDerivative y = chebyshev_eval_with_first_derivative(local_coeffs[1], coeff_count, tau);
        const ChebyshevValueWithDerivative z = chebyshev_eval_with_first_derivative(local_coeffs[2], coeff_count, tau);
        aligned_position = Vector3{x.value, y.value, z.value};
        aligned_velocity = Vector3{x.derivative * scale, y.derivative * scale, z.derivative * scale};

        double lunar_params[3];
        if (!eval_lunar_params(data, segment, lunar_params)) {
            return false;
        }
        *out_position_km = transform_regular_components(aligned_position, lunar_params[0], lunar_params[1], lunar_params[2]);
        *out_velocity_km_per_day = transform_regular_components(aligned_velocity, lunar_params[0], lunar_params[1], lunar_params[2]);
        return true;
    }

    if (data->shape_x.empty() || data->shape_y.empty() || data->frame_rows == 0) {
        return false;
    }

    const ChebyshevValueWithDerivative shape_x = chebyshev_eval_with_first_derivative(&data->shape_x[0], static_cast<int>(data->shape_x.size()), tau);
    const ChebyshevValueWithDerivative shape_y = chebyshev_eval_with_first_derivative(&data->shape_y[0], static_cast<int>(data->shape_y.size()), tau);
    const ChebyshevValueWithDerivative dx = chebyshev_eval_with_first_derivative(coeffs[0], coeff_count, tau);
    const ChebyshevValueWithDerivative dy = chebyshev_eval_with_first_derivative(coeffs[1], coeff_count, tau);
    const ChebyshevValueWithDerivative dz = chebyshev_eval_with_first_derivative(coeffs[2], coeff_count, tau);
    aligned_position = Vector3{
        shape_x.value + dx.value,
        shape_y.value + dy.value,
        dz.value};
    aligned_velocity = Vector3{
        (shape_x.derivative + dx.derivative) * scale,
        (shape_y.derivative + dy.derivative) * scale,
        dz.derivative * scale};

    double params[4] = {0.0, 0.0, 0.0, 0.0};
    if (!eval_frame_params(data, segment, params)) {
        return false;
    }
    if (data->frame_rows == 4) {
        Vector3 normal{params[0], params[1], params[2]};
        const double norm = vector3_norm(normal);
        if (norm <= 0.0) {
            return false;
        }
        normal = vector3_scale(normal, 1.0 / norm);
        *out_position_km = transform_normal_components(aligned_position, normal, params[3]);
        *out_velocity_km_per_day = transform_normal_components(aligned_velocity, normal, params[3]);
    } else {
        *out_position_km = transform_regular_components(aligned_position, params[0], params[1], params[2]);
        *out_velocity_km_per_day = transform_regular_components(aligned_velocity, params[0], params[1], params[2]);
    }
    return true;
}

bool calc_opm2_position_only(const SplitJulianDate& jd_tdb, const Opm2EphemerisData* data, Vector3* out_km) noexcept {
    SplitJulianDate jd;
    if (!data || !out_km || !normalize_split_julian_date(
            jd_tdb.day_number, jd_tdb.day_fraction, &jd)) {
        return false;
    }
    size_t segment = 0;
    if (!select_segment(data, jd, &segment)) {
        return false;
    }
    double tau = 0.0;
    double scale = 0.0;
    if (!normalize_domain_for_segment(data, segment, jd, &tau, &scale)) {
        return false;
    }

    double coeffs[OPM2_AXIS_COUNT][128];
    if (!segment_coeffs(data, segment, coeffs)) {
        return false;
    }
    const int coeff_count = static_cast<int>(data->quant_steps.size());
    Vector3 aligned;
    if (data->ephe.model_id == OPM2_MODEL_RAW_XYZ_CHEB_V1) {
        aligned.x = chebyshev_eval(coeffs[0], coeff_count, tau);
        aligned.y = chebyshev_eval(coeffs[1], coeff_count, tau);
        aligned.z = chebyshev_eval(coeffs[2], coeff_count, tau);
        *out_km = aligned;
        return true;
    }

    if (data->ephe.model_id == OPM2_MODEL_LUNAR_FIXED_COEFF_REF_V1) {
        if (data->lunar_model.ref_coeffs.size() != OPM2_AXIS_COUNT * data->quant_steps.size()) {
            return false;
        }
        double local_coeffs[OPM2_AXIS_COUNT][128];
        for (int axis = 0; axis < OPM2_AXIS_COUNT; ++axis) {
            for (size_t coeff = 0; coeff < data->quant_steps.size(); ++coeff) {
                local_coeffs[axis][coeff] = data->lunar_model.ref_coeffs[axis * data->quant_steps.size() + coeff] + coeffs[axis][coeff];
            }
        }
        Vector3 local_aligned;
        local_aligned.x = chebyshev_eval(local_coeffs[0], coeff_count, tau);
        local_aligned.y = chebyshev_eval(local_coeffs[1], coeff_count, tau);
        local_aligned.z = chebyshev_eval(local_coeffs[2], coeff_count, tau);
        double lunar_params[3];
        if (!eval_lunar_params(data, segment, lunar_params)) {
            return false;
        }
        const double c = std::cos(lunar_params[2]);
        const double s = std::sin(lunar_params[2]);
        const Vector3 local{c * local_aligned.x - s * local_aligned.y, s * local_aligned.x + c * local_aligned.y, local_aligned.z};
        *out_km = apply_frame_columns(local, plane_frame_x(lunar_params[0], lunar_params[1]), plane_frame_y(lunar_params[0], lunar_params[1]), plane_frame_z(lunar_params[0], lunar_params[1]));
        return true;
    }

    if (data->shape_x.empty() || data->shape_y.empty() || data->frame_rows == 0) {
        return false;
    }
    aligned.x = chebyshev_eval(&data->shape_x[0], static_cast<int>(data->shape_x.size()), tau) + chebyshev_eval(coeffs[0], coeff_count, tau);
    aligned.y = chebyshev_eval(&data->shape_y[0], static_cast<int>(data->shape_y.size()), tau) + chebyshev_eval(coeffs[1], coeff_count, tau);
    aligned.z = chebyshev_eval(coeffs[2], coeff_count, tau);
    double params[4] = {0.0, 0.0, 0.0, 0.0};
    if (!eval_frame_params(data, segment, params)) {
        return false;
    }
    if (data->frame_rows == 4) {
        Vector3 normal{params[0], params[1], params[2]};
        const double norm = vector3_norm(normal);
        if (norm <= 0.0) {
            return false;
        }
        normal = vector3_scale(normal, 1.0 / norm);
        *out_km = unalign_normal(aligned, normal, params[3]);
    } else {
        *out_km = unalign_regular(aligned, params[0], params[1], params[2]);
    }
    return true;
}

bool prepare_lazy_frame_params(Opm2EphemerisData* data) noexcept {
    if (!data || data->frame_rows == 0) {
        return true;
    }
    data->frame_params.clear();
    if (data->frame_coeffs.size() < static_cast<size_t>(data->frame_rows) * 2u
        || data->grid.segment_count == 0
        || data->grid.period_days <= 0.0
        || !std::isfinite(data->frame_first_mid_jd)
        || !std::isfinite(data->frame_last_mid_jd)
        || data->frame_last_mid_jd < data->frame_first_mid_jd) {
        return false;
    }
    return true;
}

bool prepare_lazy_lunar_params(Opm2EphemerisData* data) noexcept {
    if (!data || data->ephe.model_id != OPM2_MODEL_LUNAR_FIXED_COEFF_REF_V1) {
        return true;
    }
    data->lunar_params.clear();
    if (data->grid.segment_count == 0
        || data->grid.period_days <= 0.0
        || data->lunar_model.frame_time_unit_days == 0.0
        || data->lunar_model.frame_period_years == 0.0) {
        return false;
    }
    return true;
}

bool prepare_segment_time_cache(Opm2EphemerisData* data) noexcept {
    if (!data || data->grid.segment_count == 0
        || data->grid.period_days <= 0.0
        || data->grid.segment_count > static_cast<uint64_t>(std::numeric_limits<size_t>::max())
        || !split_julian_date_from_double(
            data->ephe.coverage_start_jd, &data->coverage_start_jd)
        || !split_julian_date_from_double(
            data->ephe.coverage_end_jd, &data->coverage_end_jd)
        || !split_julian_date_from_double(
            data->grid.origin_jd, &data->grid_origin_jd)) {
        return false;
    }
    try {
        std::vector<SplitJulianDate> starts;
        starts.reserve(static_cast<size_t>(data->grid.segment_count));
        for (size_t local_index = 0;
             local_index < static_cast<size_t>(data->grid.segment_count);
             ++local_index) {
            double start = 0.0;
            double end = 0.0;
            SplitJulianDate start_jd;
            if (!segment_bounds(data, local_index, &start, &end)
                || !split_julian_date_from_double(start, &start_jd)) {
                return false;
            }
            starts.push_back(start_jd);
        }
        data->segment_start_jds.swap(starts);
    } catch (...) {
        return false;
    }
    return true;
}

void update_data_bytes(Opm2EphemerisData* data, size_t byte_count) noexcept {
    if (!data) {
        return;
    }
    data->bytes = byte_count + data->qcoeffs.size() * sizeof(int64_t) + data->quant_steps.size() * sizeof(double)
        + data->shape_x.size() * sizeof(double) + data->shape_y.size() * sizeof(double)
        + data->frame_coeffs.size() * sizeof(double)
        + data->lunar_model.ref_coeffs.size() * sizeof(double)
        + data->segment_start_jds.size() * sizeof(SplitJulianDate);
}

bool slice_segments_for_range(Opm2EphemerisData* data, double jd_tdb_start, double jd_tdb_end) noexcept {
    if (!data || jd_tdb_end <= jd_tdb_start) {
        return true;
    }
    if (data->grid.segment_count == 0
        || data->grid.period_days <= 0.0
        || data->quant_steps.empty()) {
        return false;
    }

    const double first_double = std::floor((jd_tdb_start - data->grid.origin_jd) / data->grid.period_days);
    const double last_exclusive_double = std::ceil((jd_tdb_end - data->grid.origin_jd) / data->grid.period_days);
    if (!std::isfinite(first_double) || !std::isfinite(last_exclusive_double)) {
        return false;
    }

    const int64_t available_first = data->grid.first_segment_index;
    const int64_t available_last_exclusive =
        data->grid.first_segment_index + static_cast<int64_t>(data->grid.segment_count);
    int64_t first_global = static_cast<int64_t>(first_double);
    int64_t last_exclusive_global = static_cast<int64_t>(last_exclusive_double);
    if (first_global < available_first) {
        first_global = available_first;
    }
    if (last_exclusive_global > available_last_exclusive) {
        last_exclusive_global = available_last_exclusive;
    }
    if (last_exclusive_global <= first_global) {
        return false;
    }

    const size_t first_local = static_cast<size_t>(first_global - available_first);
    const size_t selected_count = static_cast<size_t>(last_exclusive_global - first_global);
    const size_t coeff_count = data->quant_steps.size();
    const size_t values_per_segment = OPM2_AXIS_COUNT * coeff_count;
    if (first_local > static_cast<size_t>(data->grid.segment_count)
        || selected_count > static_cast<size_t>(data->grid.segment_count)
        || first_local + selected_count > static_cast<size_t>(data->grid.segment_count)
        || values_per_segment == 0
        || data->qcoeffs.size() < static_cast<size_t>(data->grid.segment_count) * values_per_segment) {
        return false;
    }

    try {
        std::vector<int64_t> sliced(
            data->qcoeffs.begin() + static_cast<std::ptrdiff_t>(first_local * values_per_segment),
            data->qcoeffs.begin() + static_cast<std::ptrdiff_t>((first_local + selected_count) * values_per_segment));
        data->qcoeffs.swap(sliced);
        data->grid.first_segment_index = first_global;
        data->grid.segment_count = static_cast<uint64_t>(selected_count);
        data->ephe.coverage_start_jd = jd_tdb_start;
        data->ephe.coverage_end_jd = jd_tdb_end;
        data->frame_params.clear();
        data->lunar_params.clear();
    } catch (...) {
        return false;
    }

    return prepare_segment_time_cache(data)
        && prepare_lazy_frame_params(data)
        && prepare_lazy_lunar_params(data);
}

bool compute_segment_slice_for_range(
    const Opm2EphemerisData* data,
    double jd_tdb_start,
    double jd_tdb_end,
    int64_t* out_first_global,
    uint64_t* out_count
) noexcept {
    if (out_first_global) {
        *out_first_global = 0;
    }
    if (out_count) {
        *out_count = 0;
    }
    if (!data || !out_first_global || !out_count || jd_tdb_end <= jd_tdb_start) {
        return false;
    }
    if (data->grid.segment_count == 0
        || data->grid.period_days <= 0.0
        || jd_tdb_start < data->ephe.coverage_start_jd
        || jd_tdb_end > data->ephe.coverage_end_jd) {
        return false;
    }

    const double first_double = std::floor((jd_tdb_start - data->grid.origin_jd) / data->grid.period_days);
    const double last_exclusive_double = std::ceil((jd_tdb_end - data->grid.origin_jd) / data->grid.period_days);
    if (!std::isfinite(first_double) || !std::isfinite(last_exclusive_double)) {
        return false;
    }

    const int64_t available_first = data->grid.first_segment_index;
    const int64_t available_last_exclusive =
        data->grid.first_segment_index + static_cast<int64_t>(data->grid.segment_count);
    int64_t first_global = static_cast<int64_t>(first_double);
    int64_t last_exclusive_global = static_cast<int64_t>(last_exclusive_double);
    if (first_global < available_first) {
        first_global = available_first;
    }
    if (last_exclusive_global > available_last_exclusive) {
        last_exclusive_global = available_last_exclusive;
    }
    if (last_exclusive_global <= first_global) {
        return false;
    }

    *out_first_global = first_global;
    *out_count = static_cast<uint64_t>(last_exclusive_global - first_global);
    return true;
}

bool parse_all_for_range(
    const void* bytes,
    size_t byte_count,
    double jd_tdb_start,
    double jd_tdb_end,
    bool has_required_range,
    Opm2EphemerisData* data
) noexcept {
    if (!bytes || !data) {
        return false;
    }
    const uint8_t* raw = static_cast<const uint8_t*>(bytes);
    std::vector<Opm2SectionEntry> sections;
    if (!parse_section_table(raw, byte_count, &sections)) {
        return false;
    }
    const uint8_t* payload = 0;
    size_t size = 0;
    if (!section_payload(raw, sections, OPM2_SEC_EPHE, &payload, &size) || !parse_ephe_payload(payload, size, &data->ephe)) {
        return false;
    }
    if (!section_payload(raw, sections, OPM2_SEC_GRID, &payload, &size) || !parse_grid_payload(payload, size, &data->grid)) {
        return false;
    }
    data->frame_first_mid_jd = data->grid.origin_jd
        + (static_cast<double>(data->grid.first_segment_index) + 0.5) * data->grid.period_days;
    data->frame_last_mid_jd = data->grid.origin_jd
        + (static_cast<double>(data->grid.first_segment_index)
              + static_cast<double>(data->grid.segment_count) - 0.5)
            * data->grid.period_days;
    if (!std::isfinite(data->frame_first_mid_jd)
        || !std::isfinite(data->frame_last_mid_jd)
        || data->frame_last_mid_jd < data->frame_first_mid_jd) {
        return false;
    }
    if (!section_payload(raw, sections, OPM2_SEC_DOMN, &payload, &size) || !parse_domain_payload(payload, size, &data->domain)) {
        return false;
    }
    if (!section_payload(raw, sections, OPM2_SEC_QNTB, &payload, &size) || !parse_qntb_payload(payload, size, &data->quant_steps)) {
        return false;
    }

    const uint64_t original_segment_count = data->grid.segment_count;
    int64_t selected_first_global = data->grid.first_segment_index;
    uint64_t selected_count = data->grid.segment_count;
    if (has_required_range) {
        if (!compute_segment_slice_for_range(
                data,
                jd_tdb_start,
                jd_tdb_end,
                &selected_first_global,
                &selected_count)) {
            return false;
        }
    }
    const uint64_t first_local_segment = static_cast<uint64_t>(selected_first_global - data->grid.first_segment_index);
    if (!section_payload(raw, sections, OPM2_SEC_RCOF, &payload, &size)
        || !parse_rcof_payload_range(
            payload,
            size,
            original_segment_count,
            data->quant_steps.size(),
            first_local_segment,
            selected_count,
            &data->qcoeffs)) {
        return false;
    }
    if (has_required_range) {
        data->grid.first_segment_index = selected_first_global;
        data->grid.segment_count = selected_count;
        data->ephe.coverage_start_jd = jd_tdb_start;
        data->ephe.coverage_end_jd = jd_tdb_end;
    }
    if (!section_payload(raw, sections, OPM2_SEC_MODL, &payload, &size)) {
        return false;
    }
    if (data->ephe.model_id == OPM2_MODEL_RAW_XYZ_CHEB_V1) {
        if (size != 0) {
            return false;
        }
    } else if (data->ephe.model_id == OPM2_MODEL_LUNAR_FIXED_COEFF_REF_V1) {
        if (!parse_lunar_modl(payload, size, &data->lunar_model)) {
            return false;
        }
    } else if (data->ephe.model_id == OPM2_MODEL_FIXED_FRAME_SHARED_SHAPE_V1
        || data->ephe.model_id == OPM2_MODEL_MEAN_APSIS_SHARED_SHAPE_V1
        || data->ephe.model_id == OPM2_MODEL_LUNAR_APSIS_SHARED_SHAPE_V1) {
        if (!split_standard_modl(payload, size, &data->shape_x, &data->shape_y, &data->frame_coeffs, &data->frame_rows)) {
            return false;
        }
    } else {
        return false;
    }
    update_data_bytes(data, byte_count);
    return prepare_segment_time_cache(data)
        && prepare_lazy_frame_params(data)
        && prepare_lazy_lunar_params(data);
}

bool parse_all(const void* bytes, size_t byte_count, Opm2EphemerisData* data) noexcept {
    return parse_all_for_range(bytes, byte_count, 1.0, 0.0, false, data);
}

}  // namespace

Opm2SectionEntry::Opm2SectionEntry()
    : kind(0), version(0), flags(0), offset(0), length(0), crc32(0), reserved(0) {}

Opm2EpheSection::Opm2EpheSection()
    : target_id(0), center_id(0), frame_id(0), time_scale_id(0), model_id(0), storage_kind(0), axis_count(0), coverage_start_jd(0.0), coverage_end_jd(0.0) {}

Opm2GridSection::Opm2GridSection()
    : grid_kind(0), correction_kind(0), first_segment_index(0), segment_count(0), origin_jd(0.0), period_days(0.0), correction_table_offset(0), correction_table_size(0) {}

Opm2DomainSection::Opm2DomainSection()
    : domain_kind(0), left_expansion_days(0.0), right_expansion_days(0.0), has_expansion_fraction(false), expansion_fraction(0.0) {}

Opm2LunarCoeffRefModel::Opm2LunarCoeffRefModel()
    : degree(0), axis_count(0), frame_basis_kind(0), frame_time_origin_jd(0.0), frame_time_unit_days(0.0), frame_period_years(0.0), ref_coeffs() {
    for (int i = 0; i < 4; ++i) {
        u_coeff[i] = 0.0;
        v_coeff[i] = 0.0;
        angle_coeff[i] = 0.0;
    }
}

Opm2EphemerisData::Opm2EphemerisData()
    : ephe(),
      grid(),
      domain(),
      coverage_start_jd(),
      coverage_end_jd(),
      grid_origin_jd(),
      segment_start_jds(),
      quant_steps(),
      qcoeffs(),
      shape_x(),
      shape_y(),
      frame_coeffs(),
      frame_rows(0),
      frame_first_mid_jd(0.0),
      frame_last_mid_jd(0.0),
      frame_params(),
      lunar_model(),
      lunar_params(),
      bytes(sizeof(Opm2EphemerisData)) {}

bool parse_opm2_summary(
    const void* bytes,
    size_t byte_count,
    Opm2EpheSection* ephe,
    Opm2GridSection* grid,
    uint32_t* source_id
) noexcept {
    if (!bytes || !ephe || !grid) {
        return false;
    }
    const uint8_t* raw = static_cast<const uint8_t*>(bytes);
    std::vector<Opm2SectionEntry> sections;
    if (!parse_section_table(raw, byte_count, &sections)) {
        return false;
    }
    if (source_id) {
        *source_id = read_u32(raw + 24);
    }
    const uint8_t* payload = 0;
    size_t size = 0;
    return section_payload(raw, sections, OPM2_SEC_EPHE, &payload, &size)
        && parse_ephe_payload(payload, size, ephe)
        && section_payload(raw, sections, OPM2_SEC_GRID, &payload, &size)
        && parse_grid_payload(payload, size, grid);
}

bool compile_opm2_ephemeris_data_for_range(
    const void* bytes,
    size_t byte_count,
    double jd_tdb_start,
    double jd_tdb_end,
    Opm2EphemerisData** out
) noexcept {
    if (!out) {
        return false;
    }
    *out = 0;
    Opm2EphemerisData* data = new (std::nothrow) Opm2EphemerisData();
    if (!data) {
        return false;
    }
    const bool has_required_range = jd_tdb_end > jd_tdb_start;
    if (!parse_all_for_range(bytes, byte_count, jd_tdb_start, jd_tdb_end, has_required_range, data)) {
        delete data;
        return false;
    }
    update_data_bytes(data, byte_count);
    *out = data;
    return true;
}

bool compile_opm2_ephemeris_data(
    const void* bytes,
    size_t byte_count,
    Opm2EphemerisData** out
) noexcept {
    return compile_opm2_ephemeris_data_for_range(bytes, byte_count, 1.0, 0.0, out);
}

void opm2_ephemeris_data_destroy(Opm2EphemerisData* data) noexcept {
    delete data;
}

void opm2_ephemeris_data_destroy_void(void* data) noexcept {
    opm2_ephemeris_data_destroy(static_cast<Opm2EphemerisData*>(data));
}

bool calc_opm2_position(const SplitJulianDate& jd_tdb, const Opm2EphemerisData* data, Vector3* out_position_au) noexcept {
    if (!out_position_au) {
        return false;
    }
    Vector3 position_km;
    if (!calc_opm2_position_only(jd_tdb, data, &position_km)) {
        return false;
    }
    *out_position_au = vector3_scale(position_km, 1.0 / TAIYIN_AU_KM);
    return true;
}

bool calc_opm2_velocity(const SplitJulianDate& jd_tdb, const Opm2EphemerisData* data, Vector3* out_velocity_au_per_day) noexcept {
    if (!out_velocity_au_per_day) {
        return false;
    }
    Vector3 position_km;
    Vector3 velocity_km_per_day;
    if (!eval_opm2_position_velocity_km(jd_tdb, data, &position_km, &velocity_km_per_day)) {
        return false;
    }
    *out_velocity_au_per_day = vector3_scale(velocity_km_per_day, 1.0 / TAIYIN_AU_KM);
    return true;
}

bool calc_opm2_position_velocity(
    const SplitJulianDate& jd_tdb,
    const Opm2EphemerisData* data,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day
) noexcept {
    if (!out_position_au || !out_velocity_au_per_day) {
        return false;
    }
    Vector3 position_km;
    Vector3 velocity_km_per_day;
    if (!eval_opm2_position_velocity_km(jd_tdb, data, &position_km, &velocity_km_per_day)) {
        return false;
    }
    *out_position_au = vector3_scale(position_km, 1.0 / TAIYIN_AU_KM);
    *out_velocity_au_per_day = vector3_scale(velocity_km_per_day, 1.0 / TAIYIN_AU_KM);
    return true;
}

bool calc_opm2_acceleration(const SplitJulianDate& jd_tdb, const Opm2EphemerisData* data, Vector3* out_acceleration_au_per_day2) noexcept {
    if (!out_acceleration_au_per_day2) {
        return false;
    }
    Opm2StateKm state;
    if (!eval_opm2_state_km(jd_tdb, data, &state)) {
        return false;
    }
    *out_acceleration_au_per_day2 = vector3_scale(state.acceleration, 1.0 / TAIYIN_AU_KM);
    return true;
}

bool calc_opm2_position_void(const SplitJulianDate& jd_tdb, const void* data, Vector3* out_position_au) noexcept {
    return calc_opm2_position(jd_tdb, static_cast<const Opm2EphemerisData*>(data), out_position_au);
}

bool calc_opm2_position_velocity_void(
    const SplitJulianDate& jd_tdb,
    const void* data,
    Vector3* out_position_au,
    Vector3* out_velocity_au_per_day
) noexcept {
    return calc_opm2_position_velocity(
        jd_tdb,
        static_cast<const Opm2EphemerisData*>(data),
        out_position_au,
        out_velocity_au_per_day);
}

bool calc_opm2_velocity_void(const SplitJulianDate& jd_tdb, const void* data, Vector3* out_velocity_au_per_day) noexcept {
    return calc_opm2_velocity(jd_tdb, static_cast<const Opm2EphemerisData*>(data), out_velocity_au_per_day);
}

bool calc_opm2_acceleration_void(const SplitJulianDate& jd_tdb, const void* data, Vector3* out_acceleration_au_per_day2) noexcept {
    return calc_opm2_acceleration(jd_tdb, static_cast<const Opm2EphemerisData*>(data), out_acceleration_au_per_day2);
}

bool calc_opm2_state(const SplitJulianDate& jd_tdb, const Opm2EphemerisData* data, CartesianState* out) noexcept {
    if (!data || !out) {
        return false;
    }
    Opm2StateKm state;
    if (!eval_opm2_state_km(jd_tdb, data, &state)) {
        return false;
    }
    return state_from_position_derivatives(state.position, state.velocity, state.acceleration, out);
}

bool calc_opm2_state_void(const SplitJulianDate& jd_tdb, const void* data, CartesianState* out) noexcept {
    return calc_opm2_state(jd_tdb, static_cast<const Opm2EphemerisData*>(data), out);
}

}  // namespace internal
}  // namespace taiyin
