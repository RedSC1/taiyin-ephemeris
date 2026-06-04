#include "taiyin/internal/opm_core.h"

#include "taiyin/chebyshev.h"
#include "taiyin/physical_constants.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace taiyin {
namespace internal {
namespace {

class ByteReader {
public:
    ByteReader(const void* bytes, size_t byte_count)
        : data_(static_cast<const uint8_t*>(bytes)), size_(byte_count), offset_(0) {}

    bool read_u8(uint8_t* out) noexcept {
        if (!out || remaining() < 1) return false;
        *out = data_[offset_++];
        return true;
    }

    bool read_i8(int8_t* out) noexcept {
        uint8_t value = 0;
        if (!read_u8(&value) || !out) return false;
        *out = static_cast<int8_t>(value);
        return true;
    }

    bool read_u16(uint16_t* out) noexcept {
        if (!out || remaining() < 2) return false;
        const uint16_t value = static_cast<uint16_t>(data_[offset_])
            | (static_cast<uint16_t>(data_[offset_ + 1]) << 8);
        offset_ += 2;
        *out = value;
        return true;
    }

    bool read_i16(int16_t* out) noexcept {
        uint16_t value = 0;
        if (!read_u16(&value) || !out) return false;
        *out = static_cast<int16_t>(value);
        return true;
    }

    bool read_u32(uint32_t* out) noexcept {
        if (!out || remaining() < 4) return false;
        const uint32_t value = static_cast<uint32_t>(data_[offset_])
            | (static_cast<uint32_t>(data_[offset_ + 1]) << 8)
            | (static_cast<uint32_t>(data_[offset_ + 2]) << 16)
            | (static_cast<uint32_t>(data_[offset_ + 3]) << 24);
        offset_ += 4;
        *out = value;
        return true;
    }

    bool read_i32(int32_t* out) noexcept {
        uint32_t value = 0;
        if (!read_u32(&value) || !out) return false;
        *out = static_cast<int32_t>(value);
        return true;
    }

    bool read_i24(int32_t* out) noexcept {
        if (!out || remaining() < 3) return false;
        int32_t value = static_cast<int32_t>(data_[offset_])
            | (static_cast<int32_t>(data_[offset_ + 1]) << 8)
            | (static_cast<int32_t>(data_[offset_ + 2]) << 16);
        offset_ += 3;
        if (value & 0x800000) value -= 0x1000000;
        *out = value;
        return true;
    }

    bool read_f64(double* out) noexcept {
        if (!out || remaining() < 8) return false;
        uint64_t bits = static_cast<uint64_t>(data_[offset_])
            | (static_cast<uint64_t>(data_[offset_ + 1]) << 8)
            | (static_cast<uint64_t>(data_[offset_ + 2]) << 16)
            | (static_cast<uint64_t>(data_[offset_ + 3]) << 24)
            | (static_cast<uint64_t>(data_[offset_ + 4]) << 32)
            | (static_cast<uint64_t>(data_[offset_ + 5]) << 40)
            | (static_cast<uint64_t>(data_[offset_ + 6]) << 48)
            | (static_cast<uint64_t>(data_[offset_ + 7]) << 56);
        offset_ += 8;
        std::memcpy(out, &bits, sizeof(bits));
        return true;
    }

    bool skip(size_t byte_count) noexcept {
        if (remaining() < byte_count) return false;
        offset_ += byte_count;
        return true;
    }

    bool seek(size_t offset) noexcept {
        if (offset > size_) return false;
        offset_ = offset;
        return true;
    }

    size_t position() const noexcept { return offset_; }
    bool is_at_end() const noexcept { return offset_ == size_; }

private:
    size_t remaining() const noexcept { return offset_ <= size_ ? size_ - offset_ : 0; }

    const uint8_t* data_;
    size_t size_;
    size_t offset_;
};

bool checked_add(size_t a, size_t b, size_t* out) noexcept {
    if (!out || a > std::numeric_limits<size_t>::max() - b) return false;
    *out = a + b;
    return true;
}

bool checked_mul(size_t a, size_t b, size_t* out) noexcept {
    if (!out || (a != 0 && b > std::numeric_limits<size_t>::max() / a)) return false;
    *out = a * b;
    return true;
}

Vector3 vector_add(const Vector3& a, const Vector3& b) noexcept {
    return Vector3{ a.x + b.x, a.y + b.y, a.z + b.z };
}

Vector3 vector_scale(const Vector3& v, double scale) noexcept {
    return Vector3{ v.x * scale, v.y * scale, v.z * scale };
}

Vector3 cross(const Vector3& a, const Vector3& b) noexcept {
    return Vector3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

double norm(const Vector3& v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vector3 normalize(const Vector3& v) noexcept {
    const double n = norm(v);
    return n == 0.0 ? Vector3{ 0.0, 0.0, 0.0 } : vector_scale(v, 1.0 / n);
}

bool decode_frame_angles(double node_lon, double node_lat, double in_plane_angle, Vector3* u, Vector3* v, Vector3* w) noexcept {
    if (!u || !v || !w) return false;

    const double cos_lat = std::cos(node_lat);
    const double sin_lat = std::sin(node_lat);
    const double cos_lon = std::cos(node_lon);
    const double sin_lon = std::sin(node_lon);

    const Vector3 local_w = { cos_lat * cos_lon, cos_lat * sin_lon, sin_lat };
    const bool use_z = std::fabs(local_w.z) < 0.9;
    const Vector3 ref = { 0.0, use_z ? 0.0 : 1.0, use_z ? 1.0 : 0.0 };

    const Vector3 bu = normalize(cross(ref, local_w));
    const Vector3 bv = normalize(cross(local_w, bu));
    if (norm(bu) == 0.0 || norm(bv) == 0.0) return false;

    const double c = std::cos(in_plane_angle);
    const double s = std::sin(in_plane_angle);
    const Vector3 local_u = normalize(vector_add(vector_scale(bu, c), vector_scale(bv, s)));
    const Vector3 local_v = normalize(cross(local_w, local_u));
    if (norm(local_u) == 0.0 || norm(local_v) == 0.0) return false;

    *u = local_u;
    *v = local_v;
    *w = local_w;
    return true;
}

bool decode_mixed_width_integers(ByteReader* reader, size_t count, int32_t* out) noexcept {
    if (!reader || (count > 0 && !out)) return false;

    const size_t width_byte_count = (count + 3) / 4;
    std::unique_ptr<uint8_t[]> width_bytes;
    if (width_byte_count > 0) {
        width_bytes.reset(new (std::nothrow) uint8_t[width_byte_count]);
        if (!width_bytes) return false;
    }

    for (size_t i = 0; i < width_byte_count; ++i) {
        if (!reader->read_u8(&width_bytes[i])) return false;
    }

    for (size_t i = 0; i < count; ++i) {
        const uint8_t width_code = static_cast<uint8_t>((width_bytes[i >> 2] >> ((i & 3) * 2)) & 0x03);
        if (width_code == 0) {
            int8_t value = 0;
            if (!reader->read_i8(&value)) return false;
            out[i] = value;
        } else if (width_code == 1) {
            int16_t value = 0;
            if (!reader->read_i16(&value)) return false;
            out[i] = value;
        } else if (width_code == 2) {
            if (!reader->read_i24(&out[i])) return false;
        } else {
            if (!reader->read_i32(&out[i])) return false;
        }
    }

    return true;
}

bool skip_mixed_width_integers(ByteReader* reader, size_t count) noexcept {
    if (!reader) return false;

    const size_t width_byte_count = (count + 3) / 4;
    std::unique_ptr<uint8_t[]> width_bytes;
    if (width_byte_count > 0) {
        width_bytes.reset(new (std::nothrow) uint8_t[width_byte_count]);
        if (!width_bytes) return false;
    }

    for (size_t i = 0; i < width_byte_count; ++i) {
        if (!reader->read_u8(&width_bytes[i])) return false;
    }

    for (size_t i = 0; i < count; ++i) {
        const uint8_t width_code = static_cast<uint8_t>((width_bytes[i >> 2] >> ((i & 3) * 2)) & 0x03);
        if (!reader->skip(static_cast<size_t>(width_code) + 1u)) return false;
    }
    return true;
}

const OpmSegment* find_segment(double jd_tdb, const OpmEphemerisData* data) noexcept {
    if (!data || data->segment_count == 0) return 0;

    const OpmSegment* segments = data->get_segments();
    size_t low = 0;
    size_t high = data->segment_count;
    while (low < high) {
        const size_t mid = low + (high - low) / 2;
        const OpmSegment& segment = segments[mid];
        if (jd_tdb < segment.jd_tdb_start) {
            high = mid;
        } else if (jd_tdb > segment.jd_tdb_end) {
            low = mid + 1;
        } else {
            return &segment;
        }
    }
    return 0;
}

bool coeff_range_is_valid(const OpmEphemerisData* data, const OpmSegment& segment) noexcept {
    const size_t n_xy = static_cast<size_t>(data->degree_xy) + 1;
    const size_t n_z = static_cast<size_t>(data->degree_z) + 1;
    size_t xy_twice = 0;
    size_t segment_coeffs = 0;
    size_t end_offset = 0;

    return checked_mul(n_xy, 2, &xy_twice)
        && checked_add(xy_twice, n_z, &segment_coeffs)
        && checked_add(segment.coeff_offset, segment_coeffs, &end_offset)
        && end_offset <= data->total_coeffs;
}

ChebyshevValue eval_scaled_i32_chebyshev(const int32_t* coefficients, const int32_t* reference, size_t count, double unit_km, double x) noexcept {
    if (!coefficients || count == 0) return ChebyshevValue{ 0.0, 0.0, 0.0 };

    double b_k_plus_1 = 0.0;
    double b_k_plus_2 = 0.0;
    double d_k_plus_1 = 0.0;
    double d_k_plus_2 = 0.0;
    double s_k_plus_1 = 0.0;
    double s_k_plus_2 = 0.0;

    for (size_t index = count - 1; index >= 1; --index) {
        const double coeff = (static_cast<double>(coefficients[index])
            + static_cast<double>(reference ? reference[index] : 0)) * unit_km;
        const double b_next = b_k_plus_1;
        const double d_next = d_k_plus_1;
        const double b_k = 2.0 * x * b_k_plus_1 - b_k_plus_2 + coeff;
        const double d_k = 2.0 * x * d_k_plus_1 - d_k_plus_2 + 2.0 * b_next;
        const double s_k = 2.0 * x * s_k_plus_1 - s_k_plus_2 + 4.0 * d_next;

        b_k_plus_2 = b_k_plus_1;
        b_k_plus_1 = b_k;
        d_k_plus_2 = d_k_plus_1;
        d_k_plus_1 = d_k;
        s_k_plus_2 = s_k_plus_1;
        s_k_plus_1 = s_k;
    }

    const double coeff_0 = (static_cast<double>(coefficients[0])
        + static_cast<double>(reference ? reference[0] : 0)) * unit_km;
    return ChebyshevValue{
        x * b_k_plus_1 - b_k_plus_2 + coeff_0,
        x * d_k_plus_1 - d_k_plus_2 + b_k_plus_1,
        x * s_k_plus_1 - s_k_plus_2 + 2.0 * d_k_plus_1,
    };
}

Vector3 basis_combine_km_to_au(const Vector3& u, const Vector3& v, const Vector3& w, double x, double y, double z) noexcept {
    return Vector3{
        (u.x * x + v.x * y + w.x * z) / TAIYIN_AU_KM,
        (u.y * x + v.y * y + w.y * z) / TAIYIN_AU_KM,
        (u.z * x + v.z * y + w.z * z) / TAIYIN_AU_KM,
    };
}

}  // namespace

bool opm_allocated_bytes(size_t segment_count, size_t total_coeffs, size_t* out_bytes) noexcept {
    if (!out_bytes) return false;

    size_t segment_bytes = 0;
    size_t coeff_bytes = 0;
    size_t header_and_segments = 0;
    size_t total = 0;
    if (!checked_mul(segment_count, sizeof(OpmSegment), &segment_bytes)
        || !checked_mul(total_coeffs, sizeof(int32_t), &coeff_bytes)
        || !checked_add(sizeof(OpmEphemerisData), segment_bytes, &header_and_segments)
        || !checked_add(header_and_segments, coeff_bytes, &total)) {
        *out_bytes = 0;
        return false;
    }
    *out_bytes = total;
    return true;
}

bool opm_reference_allocated_bytes(size_t coeff_count, size_t* out_bytes) noexcept {
    if (!out_bytes) return false;

    size_t coeffs_twice = 0;
    size_t coeff_bytes = 0;
    size_t total = 0;
    if (!checked_mul(coeff_count, 2, &coeffs_twice)
        || !checked_mul(coeffs_twice, sizeof(int32_t), &coeff_bytes)
        || !checked_add(sizeof(OpmReferenceData), coeff_bytes, &total)) {
        *out_bytes = 0;
        return false;
    }
    *out_bytes = total;
    return true;
}

OpmEphemerisData* opm_ephemeris_data_create(size_t segment_count, size_t total_coeffs) noexcept {
    size_t bytes = 0;
    if (!opm_allocated_bytes(segment_count, total_coeffs, &bytes)) return 0;

    void* memory = ::operator new(bytes, std::nothrow);
    if (!memory) return 0;

    OpmEphemerisData* data = new (memory) OpmEphemerisData();
    data->segment_count = segment_count;
    data->total_coeffs = total_coeffs;
    return data;
}

void opm_ephemeris_data_destroy(OpmEphemerisData* data) noexcept {
    if (!data) return;
    data->~OpmEphemerisData();
    ::operator delete(data);
}

void opm_ephemeris_data_destroy_void(void* data) noexcept {
    opm_ephemeris_data_destroy(static_cast<OpmEphemerisData*>(data));
}

OpmReferenceData* opm_reference_data_create(size_t coeff_count) noexcept {
    size_t bytes = 0;
    if (!opm_reference_allocated_bytes(coeff_count, &bytes)) return 0;

    void* memory = ::operator new(bytes, std::nothrow);
    if (!memory) return 0;

    OpmReferenceData* data = new (memory) OpmReferenceData();
    data->coeff_count = coeff_count;
    return data;
}

void opm_reference_data_destroy(OpmReferenceData* data) noexcept {
    if (!data) return;
    data->~OpmReferenceData();
    ::operator delete(data);
}

void opm_reference_data_destroy_void(void* data) noexcept {
    opm_reference_data_destroy(static_cast<OpmReferenceData*>(data));
}

bool compile_opm_segment_stream(
    const void* bytes,
    size_t byte_count,
    int body_id,
    double jd_tdb_start,
    double jd_tdb_end,
    double quant_unit_km,
    size_t segment_count,
    uint8_t degree_xy,
    uint8_t degree_z,
    const OpmCompileRange& range,
    OpmEphemerisData** out
) noexcept {
    if (!bytes || !out) return false;
    *out = 0;
    if (segment_count == 0 || jd_tdb_end <= jd_tdb_start || quant_unit_km <= 0.0
        || !std::isfinite(jd_tdb_start) || !std::isfinite(jd_tdb_end) || !std::isfinite(quant_unit_km)) {
        return false;
    }
    if (range.enabled
        && (range.jd_tdb_end < range.jd_tdb_start
            || range.jd_tdb_start < jd_tdb_start - 1e-9
            || range.jd_tdb_end > jd_tdb_end + 1e-9)) {
        return false;
    }

    ByteReader reader(bytes, byte_count);
    const size_t n_xy = static_cast<size_t>(degree_xy) + 1;
    const size_t n_z = static_cast<size_t>(degree_z) + 1;
    size_t coeffs_per_segment = 0;
    size_t xy_twice = 0;
    size_t total_coeffs = 0;
    if (!checked_mul(n_xy, 2, &xy_twice)
        || !checked_add(xy_twice, n_z, &coeffs_per_segment)
        || !checked_mul(segment_count, coeffs_per_segment, &total_coeffs)) {
        return false;
    }

    const size_t payload_offset = reader.position();
    size_t selected_segment_count = 0;
    double selected_start = 0.0;
    double selected_end = 0.0;

    for (size_t i = 0; i < segment_count; ++i) {
        OpmSegment segment;
        double node_lon = 0.0;
        double node_lat = 0.0;
        double in_plane_angle = 0.0;
        if (!reader.read_f64(&segment.jd_tdb_start)
            || !reader.read_f64(&segment.jd_tdb_end)
            || segment.jd_tdb_end <= segment.jd_tdb_start
            || segment.jd_tdb_start < jd_tdb_start - 1e-9
            || segment.jd_tdb_end > jd_tdb_end + 1e-9
            || !reader.read_f64(&node_lon)
            || !reader.read_f64(&node_lat)
            || !reader.read_f64(&in_plane_angle)
            || !decode_frame_angles(node_lon, node_lat, in_plane_angle, &segment.u, &segment.v, &segment.w)
            || !skip_mixed_width_integers(&reader, n_xy)
            || !skip_mixed_width_integers(&reader, n_xy)
            || !skip_mixed_width_integers(&reader, n_z)) {
            return false;
        }

        const bool keep = !range.enabled
            || !(segment.jd_tdb_end < range.jd_tdb_start || segment.jd_tdb_start > range.jd_tdb_end);
        if (keep) {
            if (selected_segment_count == 0) selected_start = segment.jd_tdb_start;
            selected_end = segment.jd_tdb_end;
            ++selected_segment_count;
        }
    }

    if (!reader.is_at_end() || selected_segment_count == 0) return false;
    if (range.enabled
        && (selected_start > range.jd_tdb_start + 1e-9
            || selected_end < range.jd_tdb_end - 1e-9)) {
        return false;
    }

    size_t selected_total_coeffs = 0;
    if (!checked_mul(selected_segment_count, coeffs_per_segment, &selected_total_coeffs)) return false;

    OpmEphemerisData* data = opm_ephemeris_data_create(selected_segment_count, selected_total_coeffs);
    if (!data) return false;

    data->body_id = body_id;
    data->jd_tdb_start = range.enabled ? range.jd_tdb_start : jd_tdb_start;
    data->jd_tdb_end = range.enabled ? range.jd_tdb_end : jd_tdb_end;
    data->quant_unit_km = quant_unit_km;
    data->degree_xy = degree_xy;
    data->degree_z = degree_z;

    OpmSegment* segments = data->get_segments();
    int32_t* coeff_pool = data->get_coeff_pool();

    if (!reader.seek(payload_offset)) {
        opm_ephemeris_data_destroy(data);
        return false;
    }

    size_t selected_index = 0;
    for (size_t i = 0; i < segment_count; ++i) {
        OpmSegment segment;
        double node_lon = 0.0;
        double node_lat = 0.0;
        double in_plane_angle = 0.0;
        if (!reader.read_f64(&segment.jd_tdb_start)
            || !reader.read_f64(&segment.jd_tdb_end)
            || segment.jd_tdb_end <= segment.jd_tdb_start
            || !reader.read_f64(&node_lon)
            || !reader.read_f64(&node_lat)
            || !reader.read_f64(&in_plane_angle)
            || !decode_frame_angles(node_lon, node_lat, in_plane_angle, &segment.u, &segment.v, &segment.w)) {
            opm_ephemeris_data_destroy(data);
            return false;
        }

        const bool keep = !range.enabled
            || !(segment.jd_tdb_end < range.jd_tdb_start || segment.jd_tdb_start > range.jd_tdb_end);
        if (!keep) {
            if (!skip_mixed_width_integers(&reader, n_xy)
                || !skip_mixed_width_integers(&reader, n_xy)
                || !skip_mixed_width_integers(&reader, n_z)) {
                opm_ephemeris_data_destroy(data);
                return false;
            }
            continue;
        }

        segments[selected_index] = segment;
        segments[selected_index].coeff_offset = selected_index * coeffs_per_segment;
        int32_t* coeff_x = coeff_pool + segments[selected_index].coeff_offset;
        int32_t* coeff_y = coeff_x + n_xy;
        int32_t* coeff_z = coeff_y + n_xy;
        if (!decode_mixed_width_integers(&reader, n_xy, coeff_x)
            || !decode_mixed_width_integers(&reader, n_xy, coeff_y)
            || !decode_mixed_width_integers(&reader, n_z, coeff_z)) {
            opm_ephemeris_data_destroy(data);
            return false;
        }
        ++selected_index;
    }

    if (selected_index != selected_segment_count || !reader.is_at_end()) {
        opm_ephemeris_data_destroy(data);
        return false;
    }

    *out = data;
    return true;
}

bool calc_opm_state(double jd_tdb, const OpmEphemerisData* data, const OpmReferenceData* reference, CartesianState* out) noexcept {
    if (!data || !out || jd_tdb < data->jd_tdb_start || jd_tdb > data->jd_tdb_end) return false;
    if (reference && reference->coeff_count < static_cast<size_t>(data->degree_xy) + 1) return false;
    if (reference && std::fabs(reference->quant_unit_km - data->quant_unit_km) > 1e-15) return false;

    const OpmSegment* segment = find_segment(jd_tdb, data);
    if (!segment || segment->jd_tdb_end <= segment->jd_tdb_start || !coeff_range_is_valid(data, *segment)) return false;

    const double span_days = segment->jd_tdb_end - segment->jd_tdb_start;
    const double normalized_time_raw = (2.0 * jd_tdb - segment->jd_tdb_start - segment->jd_tdb_end) / span_days;
    if (normalized_time_raw < -1.0 - 1e-12 || normalized_time_raw > 1.0 + 1e-12) return false;
    const double normalized_time = normalized_time_raw < -1.0
        ? -1.0
        : (normalized_time_raw > 1.0 ? 1.0 : normalized_time_raw);
    const double derivative_scale = 2.0 / span_days;
    const double second_derivative_scale = derivative_scale * derivative_scale;

    const size_t n_xy = static_cast<size_t>(data->degree_xy) + 1;
    const size_t n_z = static_cast<size_t>(data->degree_z) + 1;
    const int32_t* coeff_pool = data->get_coeff_pool();
    const int32_t* coeff_x = coeff_pool + segment->coeff_offset;
    const int32_t* coeff_y = coeff_x + n_xy;
    const int32_t* coeff_z = coeff_y + n_xy;
    const int32_t* ref_x = reference ? reference->get_ref_cx_int() : 0;
    const int32_t* ref_y = reference ? reference->get_ref_cy_int() : 0;
    const double unit_km = data->quant_unit_km;

    const ChebyshevValue x = eval_scaled_i32_chebyshev(coeff_x, ref_x, n_xy, unit_km, normalized_time);
    const ChebyshevValue y = eval_scaled_i32_chebyshev(coeff_y, ref_y, n_xy, unit_km, normalized_time);
    const ChebyshevValue z = eval_scaled_i32_chebyshev(coeff_z, 0, n_z, unit_km, normalized_time);

    out->position_au = basis_combine_km_to_au(segment->u, segment->v, segment->w, x.value, y.value, z.value);
    out->velocity_au_per_day = basis_combine_km_to_au(
        segment->u, segment->v, segment->w,
        x.derivative * derivative_scale,
        y.derivative * derivative_scale,
        z.derivative * derivative_scale);
    out->acceleration_au_per_day2 = basis_combine_km_to_au(
        segment->u, segment->v, segment->w,
        x.second_derivative * second_derivative_scale,
        y.second_derivative * second_derivative_scale,
        z.second_derivative * second_derivative_scale);

    return true;
}

bool calc_opm_state_void(double jd_tdb, const void* data, CartesianState* out) noexcept {
    return calc_opm_state(jd_tdb, static_cast<const OpmEphemerisData*>(data), 0, out);
}

bool calc_opm_eval_data_state(double jd_tdb, const OpmEvalData* data, CartesianState* out) noexcept {
    return data && calc_opm_state(jd_tdb, data->ephemeris, data->reference, out);
}

bool calc_opm_eval_data_state_void(double jd_tdb, const void* data, CartesianState* out) noexcept {
    return calc_opm_eval_data_state(jd_tdb, static_cast<const OpmEvalData*>(data), out);
}

}  // namespace internal
}  // namespace taiyin
