#include "taiyin/internal/spk.h"

#include "taiyin/physical_constants.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <limits>
#include <new>

namespace taiyin {
namespace internal {
namespace {

const int DAF_RECORD_BYTES = 1024;
const double SPK_ACCELERATION_STEP_DAYS = 1e-3;
const int SPK_TYPE21_MAX_TERMS = 25;

struct SpkChebyshevDirectory {
    double init_et_seconds;
    double interval_seconds;
    int record_size_doubles;
    int record_count;

    SpkChebyshevDirectory()
        : init_et_seconds(0.0),
          interval_seconds(0.0),
          record_size_doubles(0),
          record_count(0) {}
};

struct SpkType20Directory {
    double distance_scale_km;
    double time_scale_seconds;
    double init_et_seconds;
    double interval_seconds;
    int record_size_doubles;
    int record_count;

    SpkType20Directory()
        : distance_scale_km(0.0),
          time_scale_seconds(0.0),
          init_et_seconds(0.0),
          interval_seconds(0.0),
          record_size_doubles(0),
          record_count(0) {}
};

struct SpkPathStep {
    int node_id;
    int parent_index;
    int edge_target_id;
    int edge_center_id;
    bool edge_reversed;

    SpkPathStep()
        : node_id(0),
          parent_index(-1),
          edge_target_id(0),
          edge_center_id(0),
          edge_reversed(false) {}
};

struct SpkFileSource {
    std::string path;
};

struct SpkMemorySource {
    const uint8_t* bytes;
    size_t byte_count;

    SpkMemorySource()
        : bytes(0),
          byte_count(0) {}
};

bool checked_source_range(uint64_t source_size, uint64_t offset, size_t byte_count) noexcept {
    return offset <= source_size && static_cast<uint64_t>(byte_count) <= source_size - offset;
}

bool read_file_source_range(const void* user_data, uint64_t offset, void* out, size_t byte_count) noexcept {
    const SpkFileSource* source = static_cast<const SpkFileSource*>(user_data);
    if (!source || source->path.empty() || !out || byte_count == 0) {
        return false;
    }
    std::ifstream file(source->path.c_str(), std::ios::binary);
    if (!file || offset > static_cast<uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        return false;
    }
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file) {
        return false;
    }
    file.read(static_cast<char*>(out), static_cast<std::streamsize>(byte_count));
    return static_cast<bool>(file);
}

bool read_memory_source_range(const void* user_data, uint64_t offset, void* out, size_t byte_count) noexcept {
    const SpkMemorySource* source = static_cast<const SpkMemorySource*>(user_data);
    if (!source || !source->bytes || !out || !checked_source_range(source->byte_count, offset, byte_count)) {
        return false;
    }
    std::memcpy(out, source->bytes + static_cast<size_t>(offset), byte_count);
    return true;
}

void destroy_file_source(void* user_data) noexcept {
    delete static_cast<SpkFileSource*>(user_data);
}

void destroy_memory_source(void* user_data) noexcept {
    delete static_cast<SpkMemorySource*>(user_data);
}

bool file_size_bytes(const std::string& path, uint64_t* out) noexcept {
    if (path.empty() || !out) {
        return false;
    }
    std::ifstream file(path.c_str(), std::ios::binary | std::ios::ate);
    if (!file) {
        return false;
    }
    const std::ifstream::pos_type end_pos = file.tellg();
    if (end_pos < 0) {
        return false;
    }
    *out = static_cast<uint64_t>(end_pos);
    return true;
}

bool read_spk_range(const SpkKernel& kernel, uint64_t offset, void* out, size_t byte_count) noexcept {
    if (!out || byte_count == 0 || !kernel.source.read || !checked_source_range(kernel.source.byte_count, offset, byte_count)) {
        return false;
    }
    return kernel.source.read(kernel.source.user_data, offset, out, byte_count);
}

void destroy_spk_source(SpkByteSource* source) noexcept {
    if (!source) {
        return;
    }
    if (source->user_data && source->destroy) {
        source->destroy(source->user_data);
    }
    *source = SpkByteSource();
}

bool can_read_record(const uint8_t* data, size_t size, size_t offset, size_t byte_count) noexcept {
    return data && offset <= size && byte_count <= size - offset;
}

bool read_i32_le(const uint8_t* data, size_t size, size_t offset, int32_t* out) noexcept {
    if (!out || !can_read_record(data, size, offset, 4)) {
        return false;
    }
    const uint32_t value = static_cast<uint32_t>(data[offset])
        | (static_cast<uint32_t>(data[offset + 1]) << 8)
        | (static_cast<uint32_t>(data[offset + 2]) << 16)
        | (static_cast<uint32_t>(data[offset + 3]) << 24);
    *out = static_cast<int32_t>(value);
    return true;
}

bool read_u64_le(const uint8_t* data, size_t size, size_t offset, uint64_t* out) noexcept {
    if (!out || !can_read_record(data, size, offset, 8)) {
        return false;
    }
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(data[offset + i]) << (8 * i);
    }
    *out = value;
    return true;
}

bool read_f64_le(const uint8_t* data, size_t size, size_t offset, double* out) noexcept {
    if (!out) {
        return false;
    }
    uint64_t bits = 0;
    if (!read_u64_le(data, size, offset, &bits)) {
        return false;
    }
    std::memcpy(out, &bits, sizeof(bits));
    return true;
}

bool read_daf_double_at_address(const SpkKernel& kernel, int address, double* out) noexcept {
    if (!out || address <= 0) {
        return false;
    }
    const uint64_t offset = static_cast<uint64_t>(address - 1) * 8u;
    if (!checked_source_range(kernel.source.byte_count, offset, 8)) {
        return false;
    }
    uint8_t bytes[8];
    return read_spk_range(kernel, offset, bytes, sizeof(bytes))
        && read_f64_le(bytes, sizeof(bytes), 0, out);
}

bool read_daf_doubles_at_address(
    const SpkKernel& kernel,
    int start_address,
    size_t count,
    std::vector<double>* out
) noexcept {
    if (!out || start_address <= 0 || count == 0) {
        return false;
    }
    const uint64_t offset = static_cast<uint64_t>(start_address - 1) * 8u;
    const size_t byte_count = count * sizeof(double);
    if (count > std::numeric_limits<size_t>::max() / sizeof(double)
        || !checked_source_range(kernel.source.byte_count, offset, byte_count)) {
        return false;
    }

    std::vector<uint8_t> bytes(byte_count);
    if (!read_spk_range(kernel, offset, &bytes[0], bytes.size())) {
        return false;
    }

    out->resize(count);
    for (size_t i = 0; i < count; ++i) {
        if (!read_f64_le(&bytes[0], bytes.size(), i * sizeof(double), &(*out)[i])) {
            return false;
        }
    }
    return true;
}

bool double_to_nonnegative_int(double value, int* out) noexcept {
    if (!out || !std::isfinite(value) || value < 0.0 || value > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }
    const double rounded = std::floor(value + 0.5);
    if (std::fabs(value - rounded) > 1e-9) {
        return false;
    }
    *out = static_cast<int>(rounded);
    return true;
}

bool double_to_positive_int(double value, int* out) noexcept {
    return double_to_nonnegative_int(value, out) && *out > 0;
}

bool record_number_in_source(int record_number, uint64_t source_size) noexcept {
    if (record_number <= 0 || source_size < DAF_RECORD_BYTES) {
        return false;
    }
    const uint64_t offset = static_cast<uint64_t>(record_number - 1) * DAF_RECORD_BYTES;
    return checked_source_range(source_size, offset, DAF_RECORD_BYTES);
}

bool read_record(const SpkKernel& kernel, int record_number, uint8_t out[DAF_RECORD_BYTES]) noexcept {
    if (!record_number_in_source(record_number, kernel.source.byte_count)) {
        return false;
    }
    const uint64_t offset = static_cast<uint64_t>(record_number - 1) * DAF_RECORD_BYTES;
    return read_spk_range(kernel, offset, out, DAF_RECORD_BYTES);
}

int clamp_int(int value, int low, int high) noexcept {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

int chebyshev_component_count_for_type(int spk_type) noexcept {
    if (spk_type == 2) {
        return 3;
    }
    if (spk_type == 3) {
        return 6;
    }
    return 0;
}

bool valid_chebyshev_directory_shape(const SpkChebyshevDirectory& directory, int spk_type) noexcept {
    if (!std::isfinite(directory.init_et_seconds)
        || !std::isfinite(directory.interval_seconds)
        || directory.interval_seconds <= 0.0
        || directory.record_size_doubles <= 2
        || directory.record_count <= 0) {
        return false;
    }
    const int component_count = chebyshev_component_count_for_type(spk_type);
    if (component_count == 0) {
        return false;
    }
    const int coefficient_count = (directory.record_size_doubles - 2) / component_count;
    return coefficient_count > 0 && 2 + component_count * coefficient_count == directory.record_size_doubles;
}

bool valid_chebyshev_directory_for_segment(const SpkSegment& segment, const SpkChebyshevDirectory& directory) noexcept {
    if (!valid_chebyshev_directory_shape(directory, segment.spk_type)) {
        return false;
    }
    const int64_t payload_doubles = static_cast<int64_t>(directory.record_size_doubles) * directory.record_count;
    const int64_t available_doubles = static_cast<int64_t>(segment.end_address) - segment.start_address + 1 - 4;
    return payload_doubles > 0 && payload_doubles <= available_doubles;
}

bool read_spk_chebyshev_directory(const SpkKernel& kernel, const SpkSegment& segment, SpkChebyshevDirectory* out) noexcept {
    if (!out || chebyshev_component_count_for_type(segment.spk_type) == 0 || segment.end_address - segment.start_address + 1 < 5) {
        return false;
    }

    SpkChebyshevDirectory directory;
    double record_size = 0.0;
    double record_count = 0.0;
    if (!read_daf_double_at_address(kernel, segment.end_address - 3, &directory.init_et_seconds)
        || !read_daf_double_at_address(kernel, segment.end_address - 2, &directory.interval_seconds)
        || !read_daf_double_at_address(kernel, segment.end_address - 1, &record_size)
        || !read_daf_double_at_address(kernel, segment.end_address, &record_count)
        || !double_to_positive_int(record_size, &directory.record_size_doubles)
        || !double_to_positive_int(record_count, &directory.record_count)
        || !valid_chebyshev_directory_for_segment(segment, directory)) {
        return false;
    }

    *out = directory;
    return true;
}

bool valid_type20_directory_for_segment(const SpkSegment& segment, const SpkType20Directory& directory) noexcept {
    if (!std::isfinite(directory.distance_scale_km)
        || !std::isfinite(directory.time_scale_seconds)
        || !std::isfinite(directory.init_et_seconds)
        || !std::isfinite(directory.interval_seconds)
        || directory.distance_scale_km <= 0.0
        || directory.time_scale_seconds <= 0.0
        || directory.interval_seconds <= 0.0
        || directory.record_size_doubles <= 3
        || directory.record_count <= 0) {
        return false;
    }
    const int coefficient_count = (directory.record_size_doubles - 3) / 3;
    if (coefficient_count <= 0 || coefficient_count > 128 || 3 + 3 * coefficient_count != directory.record_size_doubles) {
        return false;
    }
    const int64_t payload_doubles = static_cast<int64_t>(directory.record_size_doubles) * directory.record_count;
    const int64_t available_doubles = static_cast<int64_t>(segment.end_address) - segment.start_address + 1 - 7;
    return payload_doubles > 0 && payload_doubles <= available_doubles;
}

bool read_spk_type20_directory(const SpkKernel& kernel, const SpkSegment& segment, SpkType20Directory* out) noexcept {
    if (!out || segment.spk_type != 20 || segment.end_address - segment.start_address + 1 < 8) {
        return false;
    }

    SpkType20Directory directory;
    double init_jd_integer = 0.0;
    double init_jd_fraction = 0.0;
    double interval_days = 0.0;
    double record_size = 0.0;
    double record_count = 0.0;
    if (!read_daf_double_at_address(kernel, segment.end_address - 6, &directory.distance_scale_km)
        || !read_daf_double_at_address(kernel, segment.end_address - 5, &directory.time_scale_seconds)
        || !read_daf_double_at_address(kernel, segment.end_address - 4, &init_jd_integer)
        || !read_daf_double_at_address(kernel, segment.end_address - 3, &init_jd_fraction)
        || !read_daf_double_at_address(kernel, segment.end_address - 2, &interval_days)
        || !read_daf_double_at_address(kernel, segment.end_address - 1, &record_size)
        || !read_daf_double_at_address(kernel, segment.end_address, &record_count)
        || !double_to_positive_int(record_size, &directory.record_size_doubles)
        || !double_to_positive_int(record_count, &directory.record_count)) {
        return false;
    }

    directory.init_et_seconds = (init_jd_integer + init_jd_fraction - JD_J2000) * SECONDS_PER_DAY;
    directory.interval_seconds = interval_days * SECONDS_PER_DAY;
    if (!valid_type20_directory_for_segment(segment, directory)) {
        return false;
    }

    *out = directory;
    return true;
}

bool eval_chebyshev_record(
    int spk_type,
    const uint8_t* record_data,
    size_t size_bytes,
    double et_seconds,
    bool compute_velocity,
    CartesianState* out
) noexcept {
    if (!record_data || !out || size_bytes < 24 || size_bytes % 8 != 0) {
        return false;
    }

    const int component_count = chebyshev_component_count_for_type(spk_type);
    const int record_size_doubles = static_cast<int>(size_bytes / 8);
    const int coefficient_count = component_count == 0 ? 0 : (record_size_doubles - 2) / component_count;
    if (component_count == 0
        || coefficient_count <= 0
        || 2 + component_count * coefficient_count != record_size_doubles
        || coefficient_count > 128) {
        return false;
    }

    double midpoint = 0.0;
    double radius = 0.0;
    if (!read_f64_le(record_data, size_bytes, 0, &midpoint)
        || !read_f64_le(record_data, size_bytes, 8, &radius)
        || !std::isfinite(midpoint)
        || !std::isfinite(radius)
        || radius <= 0.0) {
        return false;
    }

    const double scaled_time = (et_seconds - midpoint) / radius;
    if (!std::isfinite(scaled_time) || scaled_time < -1.0 - 1e-12 || scaled_time > 1.0 + 1e-12) {
        return false;
    }

    double p_terms[128];
    double v_terms[128];
    double a_terms[128];
    p_terms[0] = 1.0;
    v_terms[0] = 0.0;
    a_terms[0] = 0.0;
    if (coefficient_count > 1) {
        p_terms[1] = scaled_time;
        v_terms[1] = 1.0;
        a_terms[1] = 0.0;
    }
    const double doubled_time = 2.0 * scaled_time;
    for (int i = 2; i < coefficient_count; ++i) {
        p_terms[i] = doubled_time * p_terms[i - 1] - p_terms[i - 2];
        v_terms[i] = 2.0 * p_terms[i - 1] + doubled_time * v_terms[i - 1] - v_terms[i - 2];
        a_terms[i] = 4.0 * v_terms[i - 1] + doubled_time * a_terms[i - 1] - a_terms[i - 2];
    }

    double components[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    double derivatives[6] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    double second_derivatives[3] = { 0.0, 0.0, 0.0 };
    const size_t coefficients_offset = 16;
    for (int component = 0; component < component_count; ++component) {
        double position_sum = 0.0;
        double velocity_sum = 0.0;
        double acceleration_sum = 0.0;
        const size_t axis_offset = coefficients_offset + static_cast<size_t>(component) * coefficient_count * 8;
        for (int i = 0; i < coefficient_count; ++i) {
            double coefficient = 0.0;
            if (!read_f64_le(record_data, size_bytes, axis_offset + static_cast<size_t>(i) * 8, &coefficient)) {
                return false;
            }
            position_sum += coefficient * p_terms[i];
            velocity_sum += coefficient * v_terms[i];
            acceleration_sum += coefficient * a_terms[i];
        }
        components[component] = position_sum;
        derivatives[component] = velocity_sum / radius;
        if (component < 3) {
            second_derivatives[component] = acceleration_sum / (radius * radius);
        }
    }

    out->position_au.x = components[0] / TAIYIN_AU_KM;
    out->position_au.y = components[1] / TAIYIN_AU_KM;
    out->position_au.z = components[2] / TAIYIN_AU_KM;
    if (compute_velocity) {
        if (spk_type == 3) {
            out->velocity_au_per_day.x = components[3] * SECONDS_PER_DAY / TAIYIN_AU_KM;
            out->velocity_au_per_day.y = components[4] * SECONDS_PER_DAY / TAIYIN_AU_KM;
            out->velocity_au_per_day.z = components[5] * SECONDS_PER_DAY / TAIYIN_AU_KM;
        } else {
            out->velocity_au_per_day.x = derivatives[0] * SECONDS_PER_DAY / TAIYIN_AU_KM;
            out->velocity_au_per_day.y = derivatives[1] * SECONDS_PER_DAY / TAIYIN_AU_KM;
            out->velocity_au_per_day.z = derivatives[2] * SECONDS_PER_DAY / TAIYIN_AU_KM;
        }
    } else {
        out->velocity_au_per_day.x = 0.0;
        out->velocity_au_per_day.y = 0.0;
        out->velocity_au_per_day.z = 0.0;
    }
    if (spk_type == 3) {
        out->acceleration_au_per_day2.x = derivatives[3] * SECONDS_PER_DAY * SECONDS_PER_DAY / TAIYIN_AU_KM;
        out->acceleration_au_per_day2.y = derivatives[4] * SECONDS_PER_DAY * SECONDS_PER_DAY / TAIYIN_AU_KM;
        out->acceleration_au_per_day2.z = derivatives[5] * SECONDS_PER_DAY * SECONDS_PER_DAY / TAIYIN_AU_KM;
    } else {
        out->acceleration_au_per_day2.x = second_derivatives[0] * SECONDS_PER_DAY * SECONDS_PER_DAY / TAIYIN_AU_KM;
        out->acceleration_au_per_day2.y = second_derivatives[1] * SECONDS_PER_DAY * SECONDS_PER_DAY / TAIYIN_AU_KM;
        out->acceleration_au_per_day2.z = second_derivatives[2] * SECONDS_PER_DAY * SECONDS_PER_DAY / TAIYIN_AU_KM;
    }
    return true;
}

bool eval_type20_record(
    const uint8_t* record_data,
    size_t size_bytes,
    double et_seconds,
    const SpkType20Directory& directory,
    int record_index,
    bool compute_velocity,
    CartesianState* out
) noexcept {
    if (!record_data || !out || size_bytes < 48 || size_bytes % 8 != 0 || record_index < 0) {
        return false;
    }

    const int record_size_doubles = static_cast<int>(size_bytes / 8);
    const int coefficient_count = (record_size_doubles - 3) / 3;
    if (record_size_doubles != directory.record_size_doubles
        || coefficient_count <= 0
        || coefficient_count > 128
        || 3 + 3 * coefficient_count != record_size_doubles) {
        return false;
    }

    const double midpoint_et_seconds =
        directory.init_et_seconds
        + (static_cast<double>(record_index) + 0.5) * directory.interval_seconds;
    const double radius_seconds = 0.5 * directory.interval_seconds;
    const double scaled_time = (et_seconds - midpoint_et_seconds) / radius_seconds;
    if (!std::isfinite(scaled_time) || scaled_time < -1.0 - 1e-12 || scaled_time > 1.0 + 1e-12) {
        return false;
    }

    double terms[129];
    terms[0] = 1.0;
    if (coefficient_count + 1 > 1) {
        terms[1] = scaled_time;
    }
    const double doubled_time = 2.0 * scaled_time;
    for (int i = 2; i <= coefficient_count; ++i) {
        terms[i] = doubled_time * terms[i - 1] - terms[i - 2];
    }

    double derivative_terms[128];
    derivative_terms[0] = 0.0;
    if (coefficient_count > 1) {
        derivative_terms[1] = 1.0;
    }
    for (int i = 2; i < coefficient_count; ++i) {
        derivative_terms[i] = 2.0 * terms[i - 1] + doubled_time * derivative_terms[i - 1] - derivative_terms[i - 2];
    }

    double terms_at_zero[129];
    terms_at_zero[0] = 1.0;
    if (coefficient_count + 1 > 1) {
        terms_at_zero[1] = 0.0;
    }
    for (int i = 2; i <= coefficient_count; ++i) {
        terms_at_zero[i] = -terms_at_zero[i - 2];
    }

    double integrals[128];
    integrals[0] = scaled_time;
    if (coefficient_count > 1) {
        integrals[1] = 0.5 * scaled_time * scaled_time;
    }
    for (int i = 2; i < coefficient_count; ++i) {
        const double antiderivative =
            terms[i + 1] / (2.0 * static_cast<double>(i + 1))
            - terms[i - 1] / (2.0 * static_cast<double>(i - 1));
        const double antiderivative_at_zero =
            terms_at_zero[i + 1] / (2.0 * static_cast<double>(i + 1))
            - terms_at_zero[i - 1] / (2.0 * static_cast<double>(i - 1));
        integrals[i] = antiderivative - antiderivative_at_zero;
    }

    const double velocity_scale_km_per_second = directory.distance_scale_km / directory.time_scale_seconds;
    double position_km[3] = { 0.0, 0.0, 0.0 };
    double velocity_km_per_second[3] = { 0.0, 0.0, 0.0 };
    double acceleration_km_per_second2[3] = { 0.0, 0.0, 0.0 };
    for (int axis = 0; axis < 3; ++axis) {
        const size_t coefficients_offset = static_cast<size_t>(axis) * coefficient_count * 8u;
        double velocity_sum = 0.0;
        double acceleration_sum = 0.0;
        double position_integral_sum = 0.0;
        for (int i = 0; i < coefficient_count; ++i) {
            double coefficient = 0.0;
            if (!read_f64_le(record_data, size_bytes, coefficients_offset + static_cast<size_t>(i) * 8u, &coefficient)) {
                return false;
            }
            velocity_sum += coefficient * terms[i];
            acceleration_sum += coefficient * derivative_terms[i];
            position_integral_sum += coefficient * integrals[i];
        }

        double midpoint_position_scaled = 0.0;
        const size_t midpoint_offset = static_cast<size_t>(3 * coefficient_count + axis) * 8u;
        if (!read_f64_le(record_data, size_bytes, midpoint_offset, &midpoint_position_scaled)) {
            return false;
        }
        velocity_km_per_second[axis] = velocity_sum * velocity_scale_km_per_second;
        acceleration_km_per_second2[axis] = acceleration_sum * velocity_scale_km_per_second / radius_seconds;
        position_km[axis] =
            midpoint_position_scaled * directory.distance_scale_km
            + radius_seconds * velocity_scale_km_per_second * position_integral_sum;
    }

    out->position_au.x = position_km[0] / TAIYIN_AU_KM;
    out->position_au.y = position_km[1] / TAIYIN_AU_KM;
    out->position_au.z = position_km[2] / TAIYIN_AU_KM;
    if (compute_velocity) {
        out->velocity_au_per_day.x = velocity_km_per_second[0] * SECONDS_PER_DAY / TAIYIN_AU_KM;
        out->velocity_au_per_day.y = velocity_km_per_second[1] * SECONDS_PER_DAY / TAIYIN_AU_KM;
        out->velocity_au_per_day.z = velocity_km_per_second[2] * SECONDS_PER_DAY / TAIYIN_AU_KM;
    } else {
        out->velocity_au_per_day.x = 0.0;
        out->velocity_au_per_day.y = 0.0;
        out->velocity_au_per_day.z = 0.0;
    }
    out->acceleration_au_per_day2.x = acceleration_km_per_second2[0] * SECONDS_PER_DAY * SECONDS_PER_DAY / TAIYIN_AU_KM;
    out->acceleration_au_per_day2.y = acceleration_km_per_second2[1] * SECONDS_PER_DAY * SECONDS_PER_DAY / TAIYIN_AU_KM;
    out->acceleration_au_per_day2.z = acceleration_km_per_second2[2] * SECONDS_PER_DAY * SECONDS_PER_DAY / TAIYIN_AU_KM;
    return true;
}

bool eval_type21_record(double et_seconds, const std::vector<double>& record, int maxdim, CartesianState* out) noexcept {
    if (!out || maxdim <= 0 || maxdim > SPK_TYPE21_MAX_TERMS || record.size() < static_cast<size_t>(4 * maxdim + 11)) {
        return false;
    }

    const double tl = record[0];
    const double delta = et_seconds - tl;
    double g[SPK_TYPE21_MAX_TERMS] = { 0.0 };
    double refpos[3] = { 0.0, 0.0, 0.0 };
    double refvel[3] = { 0.0, 0.0, 0.0 };
    double dt[SPK_TYPE21_MAX_TERMS][3];
    int kq[3] = { 0, 0, 0 };

    for (int i = 0; i < maxdim; ++i) {
        g[i] = record[1 + i];
    }
    refpos[0] = record[maxdim + 1];
    refvel[0] = record[maxdim + 2];
    refpos[1] = record[maxdim + 3];
    refvel[1] = record[maxdim + 4];
    refpos[2] = record[maxdim + 5];
    refvel[2] = record[maxdim + 6];

    for (int axis = 0; axis < 3; ++axis) {
        for (int j = 0; j < maxdim; ++j) {
            dt[j][axis] = record[maxdim + 7 + j + axis * maxdim];
        }
    }

    const int kqmax1 = static_cast<int>(std::floor(record[4 * maxdim + 7] + 0.5));
    kq[0] = static_cast<int>(std::floor(record[4 * maxdim + 8] + 0.5));
    kq[1] = static_cast<int>(std::floor(record[4 * maxdim + 9] + 0.5));
    kq[2] = static_cast<int>(std::floor(record[4 * maxdim + 10] + 0.5));
    if (kqmax1 < 2 || kqmax1 > SPK_TYPE21_MAX_TERMS + 1) {
        return false;
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (kq[axis] <= 0 || kq[axis] > maxdim) {
            return false;
        }
    }

    double fc[SPK_TYPE21_MAX_TERMS + 2] = { 0.0 };
    double wc[SPK_TYPE21_MAX_TERMS + 2] = { 0.0 };
    double w[SPK_TYPE21_MAX_TERMS + 3] = { 0.0 };
    fc[0] = 1.0;

    double tp = delta;
    const int mq2 = kqmax1 - 2;
    int ks = kqmax1 - 1;
    for (int j = 1; j <= mq2; ++j) {
        if (g[j - 1] == 0.0) {
            return false;
        }
        fc[j] = tp / g[j - 1];
        wc[j - 1] = delta / g[j - 1];
        tp = delta + g[j - 1];
    }

    for (int j = 1; j <= kqmax1; ++j) {
        w[j - 1] = 1.0 / static_cast<double>(j);
    }

    int jx = 0;
    int ks1 = ks - 1;
    while (ks >= 2) {
        ++jx;
        for (int j = 1; j <= jx; ++j) {
            w[j + ks - 1] = fc[j] * w[j + ks1 - 1] - wc[j - 1] * w[j + ks - 1];
        }
        ks = ks1;
        ks1 = ks1 - 1;
    }

    double position_km[3] = { 0.0, 0.0, 0.0 };
    double velocity_km_per_second[3] = { 0.0, 0.0, 0.0 };
    for (int axis = 0; axis < 3; ++axis) {
        double sum = 0.0;
        for (int j = kq[axis]; j >= 1; --j) {
            sum += dt[j - 1][axis] * w[j + ks - 1];
        }
        position_km[axis] = refpos[axis] + delta * (refvel[axis] + delta * sum);
    }

    for (int j = 1; j <= jx; ++j) {
        w[j + ks - 1] = fc[j] * w[j + ks1 - 1] - wc[j - 1] * w[j + ks - 1];
    }
    ks = ks - 1;

    for (int axis = 0; axis < 3; ++axis) {
        double sum = 0.0;
        for (int j = kq[axis]; j >= 1; --j) {
            sum += dt[j - 1][axis] * w[j + ks - 1];
        }
        velocity_km_per_second[axis] = refvel[axis] + delta * sum;
    }

    out->position_au.x = position_km[0] / TAIYIN_AU_KM;
    out->position_au.y = position_km[1] / TAIYIN_AU_KM;
    out->position_au.z = position_km[2] / TAIYIN_AU_KM;
    out->velocity_au_per_day.x = velocity_km_per_second[0] * SECONDS_PER_DAY / TAIYIN_AU_KM;
    out->velocity_au_per_day.y = velocity_km_per_second[1] * SECONDS_PER_DAY / TAIYIN_AU_KM;
    out->velocity_au_per_day.z = velocity_km_per_second[2] * SECONDS_PER_DAY / TAIYIN_AU_KM;
    out->acceleration_au_per_day2.x = 0.0;
    out->acceleration_au_per_day2.y = 0.0;
    out->acceleration_au_per_day2.z = 0.0;
    return true;
}

bool eval_spk_segment_type21(
    const SpkKernel& kernel,
    const SpkSegment& segment,
    double et_seconds,
    bool compute_velocity,
    CartesianState* out
) noexcept {
    if (!out || segment.spk_type != 21 || et_seconds < segment.start_et_seconds || et_seconds > segment.end_et_seconds) {
        return false;
    }

    double maxdim_double = 0.0;
    double entry_count_double = 0.0;
    if (!read_daf_double_at_address(kernel, segment.end_address - 1, &maxdim_double)
        || !read_daf_double_at_address(kernel, segment.end_address, &entry_count_double)) {
        return false;
    }
    int maxdim = 0;
    int entry_count = 0;
    if (!double_to_positive_int(maxdim_double, &maxdim)
        || !double_to_positive_int(entry_count_double, &entry_count)
        || maxdim > SPK_TYPE21_MAX_TERMS) {
        return false;
    }

    const int dlsize = 4 * maxdim + 11;
    const int epoch_dir_count = entry_count / 100;
    int search_first = 1;
    int search_last = entry_count;
    if (epoch_dir_count >= 1) {
        std::vector<double> epoch_dir;
        if (!read_daf_doubles_at_address(kernel, segment.end_address - epoch_dir_count - 1, epoch_dir_count, &epoch_dir)) {
            return false;
        }
        bool found = false;
        int i = 1;
        for (; i <= epoch_dir_count; ++i) {
            if (epoch_dir[static_cast<size_t>(i - 1)] > et_seconds) {
                found = true;
                break;
            }
        }
        if (found) {
            search_last = i * 100;
            search_first = (i - 1) * 100 + 1;
        } else {
            search_first = epoch_dir_count * 100 + 1;
        }
        if (search_last > entry_count) {
            search_last = entry_count;
        }
    }

    std::vector<double> epoch_table;
    if (!read_daf_doubles_at_address(kernel, segment.start_address + entry_count * dlsize, entry_count, &epoch_table)) {
        return false;
    }

    int record_index = search_last;
    bool found = false;
    for (int i = search_first; i <= search_last; ++i) {
        if (epoch_table[static_cast<size_t>(i - 1)] > et_seconds) {
            record_index = i;
            found = true;
            break;
        }
    }
    if (!found) {
        record_index = search_last;
    }

    std::vector<double> record;
    const int record_start = segment.start_address + (record_index - 1) * dlsize;
    if (!read_daf_doubles_at_address(kernel, record_start, dlsize, &record)) {
        return false;
    }

    CartesianState state;
    if (!eval_type21_record(et_seconds, record, maxdim, &state)) {
        return false;
    }
    if (!compute_velocity) {
        state.velocity_au_per_day.x = 0.0;
        state.velocity_au_per_day.y = 0.0;
        state.velocity_au_per_day.z = 0.0;
    }
    *out = state;
    return true;
}

void subtract_state(const CartesianState& lhs, const CartesianState& rhs, CartesianState* out) noexcept {
    out->position_au.x = lhs.position_au.x - rhs.position_au.x;
    out->position_au.y = lhs.position_au.y - rhs.position_au.y;
    out->position_au.z = lhs.position_au.z - rhs.position_au.z;
    out->velocity_au_per_day.x = lhs.velocity_au_per_day.x - rhs.velocity_au_per_day.x;
    out->velocity_au_per_day.y = lhs.velocity_au_per_day.y - rhs.velocity_au_per_day.y;
    out->velocity_au_per_day.z = lhs.velocity_au_per_day.z - rhs.velocity_au_per_day.z;
    out->acceleration_au_per_day2.x = lhs.acceleration_au_per_day2.x - rhs.acceleration_au_per_day2.x;
    out->acceleration_au_per_day2.y = lhs.acceleration_au_per_day2.y - rhs.acceleration_au_per_day2.y;
    out->acceleration_au_per_day2.z = lhs.acceleration_au_per_day2.z - rhs.acceleration_au_per_day2.z;
}

void add_state_in_place(CartesianState* lhs, const CartesianState& rhs) noexcept {
    lhs->position_au.x += rhs.position_au.x;
    lhs->position_au.y += rhs.position_au.y;
    lhs->position_au.z += rhs.position_au.z;
    lhs->velocity_au_per_day.x += rhs.velocity_au_per_day.x;
    lhs->velocity_au_per_day.y += rhs.velocity_au_per_day.y;
    lhs->velocity_au_per_day.z += rhs.velocity_au_per_day.z;
    lhs->acceleration_au_per_day2.x += rhs.acceleration_au_per_day2.x;
    lhs->acceleration_au_per_day2.y += rhs.acceleration_au_per_day2.y;
    lhs->acceleration_au_per_day2.z += rhs.acceleration_au_per_day2.z;
}

void subtract_state_in_place(CartesianState* lhs, const CartesianState& rhs) noexcept {
    lhs->position_au.x -= rhs.position_au.x;
    lhs->position_au.y -= rhs.position_au.y;
    lhs->position_au.z -= rhs.position_au.z;
    lhs->velocity_au_per_day.x -= rhs.velocity_au_per_day.x;
    lhs->velocity_au_per_day.y -= rhs.velocity_au_per_day.y;
    lhs->velocity_au_per_day.z -= rhs.velocity_au_per_day.z;
    lhs->acceleration_au_per_day2.x -= rhs.acceleration_au_per_day2.x;
    lhs->acceleration_au_per_day2.y -= rhs.acceleration_au_per_day2.y;
    lhs->acceleration_au_per_day2.z -= rhs.acceleration_au_per_day2.z;
}

bool eval_direct_state(
    const SpkKernel& kernel,
    int target_id,
    int center_id,
    double et_seconds,
    bool compute_velocity,
    CartesianState* out
) noexcept {
    const SpkSegment* segment = find_spk_segment(kernel, target_id, center_id, et_seconds);
    return segment && eval_spk_segment(kernel, *segment, et_seconds, compute_velocity, out);
}

bool spk_segment_is_supported(const SpkSegment& segment) noexcept {
    return segment.spk_type == 2 || segment.spk_type == 3 || segment.spk_type == 20 || segment.spk_type == 21;
}

bool spk_direct_covers_et_range(
    const SpkKernel& kernel,
    int target_id,
    int center_id,
    double start_et_seconds,
    double end_et_seconds
) noexcept {
    if (!std::isfinite(start_et_seconds)
        || !std::isfinite(end_et_seconds)
        || end_et_seconds < start_et_seconds) {
        return false;
    }
    if (target_id == center_id) {
        return true;
    }

    const double epsilon_seconds = 1e-6;
    double covered_until = start_et_seconds;
    for (size_t guard = 0; guard <= kernel.index.segments.size(); ++guard) {
        double best_end = covered_until;
        for (size_t i = 0; i < kernel.index.segments.size(); ++i) {
            const SpkSegment& segment = kernel.index.segments[i];
            if (segment.target_id == target_id
                && segment.center_id == center_id
                && spk_segment_is_supported(segment)
                && segment.start_et_seconds <= covered_until + epsilon_seconds
                && segment.end_et_seconds > best_end) {
                best_end = segment.end_et_seconds;
            }
        }
        if (best_end >= end_et_seconds - epsilon_seconds) {
            return true;
        }
        if (best_end <= covered_until + epsilon_seconds) {
            return false;
        }
        covered_until = best_end;
    }
    return false;
}

bool find_spk_pair_path_for_et_range(
    const SpkKernel& kernel,
    int target_id,
    int center_id,
    double start_et_seconds,
    double end_et_seconds,
    std::vector<SpkPathStep>* out
) noexcept;

bool spk_relative_covers_jd_range(
    const SpkKernel& kernel,
    int target_id,
    int center_id,
    const SpkCompileRange& range
) noexcept {
    if (!range.enabled) {
        return true;
    }
    if (!std::isfinite(range.jd_tdb_start)
        || !std::isfinite(range.jd_tdb_end)
        || range.jd_tdb_end < range.jd_tdb_start) {
        return false;
    }

    const double start_et_seconds = (range.jd_tdb_start - JD_J2000) * SECONDS_PER_DAY;
    const double end_et_seconds = (range.jd_tdb_end - JD_J2000) * SECONDS_PER_DAY;
    std::vector<SpkPathStep> path;
    return find_spk_pair_path_for_et_range(kernel, target_id, center_id, start_et_seconds, end_et_seconds, &path);
}

bool spk_path_contains_node(const std::vector<SpkPathStep>& steps, int node_id) noexcept {
    for (size_t i = 0; i < steps.size(); ++i) {
        if (steps[i].node_id == node_id) {
            return true;
        }
    }
    return false;
}

bool spk_pair_in_path(const std::vector<SpkPathStep>& steps, int target_id, int center_id) noexcept {
    for (size_t i = 0; i < steps.size(); ++i) {
        if (steps[i].parent_index >= 0
            && steps[i].edge_target_id == target_id
            && steps[i].edge_center_id == center_id) {
            return true;
        }
    }
    return false;
}

bool finite_ordered_range(double start, double end) noexcept {
    return std::isfinite(start) && std::isfinite(end) && end >= start;
}

bool find_spk_pair_path_for_et_range(
    const SpkKernel& kernel,
    int target_id,
    int center_id,
    double start_et_seconds,
    double end_et_seconds,
    std::vector<SpkPathStep>* out
) noexcept {
    if (!out
        || !finite_ordered_range(start_et_seconds, end_et_seconds)
        || target_id == center_id) {
        if (out) {
            out->clear();
        }
        return out != 0 && target_id == center_id;
    }

    std::vector<SpkPathStep> queue;
    SpkPathStep root;
    root.node_id = target_id;
    queue.push_back(root);

    for (size_t cursor = 0; cursor < queue.size(); ++cursor) {
        const int current_id = queue[cursor].node_id;
        for (size_t i = 0; i < kernel.index.segments.size(); ++i) {
            const SpkSegment& segment = kernel.index.segments[i];
            if (!spk_segment_is_supported(segment)
                || !spk_direct_covers_et_range(
                    kernel,
                    segment.target_id,
                    segment.center_id,
                    start_et_seconds,
                    end_et_seconds)) {
                continue;
            }

            bool reversed = false;
            int next_id = 0;
            if (segment.target_id == current_id) {
                next_id = segment.center_id;
                reversed = false;
            } else if (segment.center_id == current_id) {
                next_id = segment.target_id;
                reversed = true;
            } else {
                continue;
            }

            if (spk_path_contains_node(queue, next_id)
                || spk_pair_in_path(queue, segment.target_id, segment.center_id)) {
                continue;
            }

            SpkPathStep step;
            step.node_id = next_id;
            step.parent_index = static_cast<int>(cursor);
            step.edge_target_id = segment.target_id;
            step.edge_center_id = segment.center_id;
            step.edge_reversed = reversed;
            queue.push_back(step);

            if (next_id == center_id) {
                std::vector<SpkPathStep> path;
                int index = static_cast<int>(queue.size()) - 1;
                while (index >= 0) {
                    path.push_back(queue[static_cast<size_t>(index)]);
                    index = queue[static_cast<size_t>(index)].parent_index;
                }
                out->clear();
                for (std::vector<SpkPathStep>::reverse_iterator it = path.rbegin(); it != path.rend(); ++it) {
                    out->push_back(*it);
                }
                return true;
            }
        }
    }

    out->clear();
    return false;
}

double jd_tdb_to_et_seconds(double jd_tdb) noexcept {
    return (jd_tdb - JD_J2000) * SECONDS_PER_DAY;
}

bool intervals_overlap(double a_start, double a_end, double b_start, double b_end) noexcept {
    return a_start <= b_end && b_start <= a_end;
}

bool append_daf_doubles(
    const SpkKernel& kernel,
    int start_address,
    size_t count,
    std::vector<double>* pool,
    size_t* out_offset
) noexcept {
    if (!pool || !out_offset) {
        return false;
    }
    std::vector<double> values;
    if (!read_daf_doubles_at_address(kernel, start_address, count, &values)) {
        return false;
    }
    *out_offset = pool->size();
    pool->insert(pool->end(), values.begin(), values.end());
    return true;
}

bool pool_has_range(const SpkEphemerisData& data, size_t offset, size_t count) noexcept {
    return offset <= data.double_pool.size() && count <= data.double_pool.size() - offset;
}

bool select_fixed_record_window(
    double init_et_seconds,
    double interval_seconds,
    int record_count,
    double start_et_seconds,
    double end_et_seconds,
    int* first_record_index,
    int* selected_record_count
) noexcept {
    if (!first_record_index
        || !selected_record_count
        || !std::isfinite(init_et_seconds)
        || !std::isfinite(interval_seconds)
        || interval_seconds <= 0.0
        || record_count <= 0
        || !finite_ordered_range(start_et_seconds, end_et_seconds)) {
        return false;
    }

    int first = static_cast<int>(std::floor((start_et_seconds - init_et_seconds) / interval_seconds));
    int last = static_cast<int>(std::floor((end_et_seconds - init_et_seconds) / interval_seconds));
    first = clamp_int(first, 0, record_count - 1);
    last = clamp_int(last, 0, record_count - 1);
    if (last < first) {
        return false;
    }
    *first_record_index = first;
    *selected_record_count = last - first + 1;
    return true;
}

int find_type21_record_index(const double* epoch_table, int entry_count, double et_seconds) noexcept {
    if (!epoch_table || entry_count <= 0) {
        return 0;
    }
    for (int i = 1; i <= entry_count; ++i) {
        if (epoch_table[static_cast<size_t>(i - 1)] > et_seconds) {
            return i;
        }
    }
    return entry_count;
}

bool compile_spk_segment_type2_or_3(
    const SpkKernel& kernel,
    const SpkSegment& segment,
    double required_start_et_seconds,
    double required_end_et_seconds,
    SpkEphemerisData* out
) noexcept {
    SpkChebyshevDirectory directory;
    if (!read_spk_chebyshev_directory(kernel, segment, &directory)) {
        return false;
    }

    const double start_et_seconds =
        segment.start_et_seconds > required_start_et_seconds ? segment.start_et_seconds : required_start_et_seconds;
    const double end_et_seconds =
        segment.end_et_seconds < required_end_et_seconds ? segment.end_et_seconds : required_end_et_seconds;

    int first_record_index = 0;
    int selected_record_count = 0;
    if (!select_fixed_record_window(
            directory.init_et_seconds,
            directory.interval_seconds,
            directory.record_count,
            start_et_seconds,
            end_et_seconds,
            &first_record_index,
            &selected_record_count)) {
        return false;
    }

    SpkCompiledSegment compiled;
    compiled.start_et_seconds = directory.init_et_seconds
        + static_cast<double>(first_record_index) * directory.interval_seconds;
    compiled.end_et_seconds = directory.init_et_seconds
        + static_cast<double>(first_record_index + selected_record_count) * directory.interval_seconds;
    compiled.target_id = segment.target_id;
    compiled.center_id = segment.center_id;
    compiled.frame_id = segment.frame_id;
    compiled.spk_type = segment.spk_type;
    compiled.record_count = selected_record_count;
    compiled.record_size_doubles = directory.record_size_doubles;
    compiled.first_record_index = first_record_index;
    compiled.init_et_seconds = directory.init_et_seconds;
    compiled.interval_seconds = directory.interval_seconds;

    const int start_address = segment.start_address + first_record_index * directory.record_size_doubles;
    const size_t copy_count =
        static_cast<size_t>(selected_record_count) * static_cast<size_t>(directory.record_size_doubles);
    if (!append_daf_doubles(kernel, start_address, copy_count, &out->double_pool, &compiled.record_offset)) {
        return false;
    }

    out->segments.push_back(compiled);
    return true;
}

bool compile_spk_segment_type20(
    const SpkKernel& kernel,
    const SpkSegment& segment,
    double required_start_et_seconds,
    double required_end_et_seconds,
    SpkEphemerisData* out
) noexcept {
    SpkType20Directory directory;
    if (!read_spk_type20_directory(kernel, segment, &directory)) {
        return false;
    }

    const double start_et_seconds =
        segment.start_et_seconds > required_start_et_seconds ? segment.start_et_seconds : required_start_et_seconds;
    const double end_et_seconds =
        segment.end_et_seconds < required_end_et_seconds ? segment.end_et_seconds : required_end_et_seconds;

    int first_record_index = 0;
    int selected_record_count = 0;
    if (!select_fixed_record_window(
            directory.init_et_seconds,
            directory.interval_seconds,
            directory.record_count,
            start_et_seconds,
            end_et_seconds,
            &first_record_index,
            &selected_record_count)) {
        return false;
    }

    SpkCompiledSegment compiled;
    compiled.start_et_seconds = directory.init_et_seconds
        + static_cast<double>(first_record_index) * directory.interval_seconds;
    compiled.end_et_seconds = directory.init_et_seconds
        + static_cast<double>(first_record_index + selected_record_count) * directory.interval_seconds;
    compiled.target_id = segment.target_id;
    compiled.center_id = segment.center_id;
    compiled.frame_id = segment.frame_id;
    compiled.spk_type = segment.spk_type;
    compiled.record_count = selected_record_count;
    compiled.record_size_doubles = directory.record_size_doubles;
    compiled.first_record_index = first_record_index;
    compiled.init_et_seconds = directory.init_et_seconds;
    compiled.interval_seconds = directory.interval_seconds;
    compiled.distance_scale_km = directory.distance_scale_km;
    compiled.time_scale_seconds = directory.time_scale_seconds;

    const int start_address = segment.start_address + first_record_index * directory.record_size_doubles;
    const size_t copy_count =
        static_cast<size_t>(selected_record_count) * static_cast<size_t>(directory.record_size_doubles);
    if (!append_daf_doubles(kernel, start_address, copy_count, &out->double_pool, &compiled.record_offset)) {
        return false;
    }

    out->segments.push_back(compiled);
    return true;
}

bool compile_spk_segment_type21(
    const SpkKernel& kernel,
    const SpkSegment& segment,
    double required_start_et_seconds,
    double required_end_et_seconds,
    SpkEphemerisData* out
) noexcept {
    double maxdim_double = 0.0;
    double entry_count_double = 0.0;
    if (!read_daf_double_at_address(kernel, segment.end_address - 1, &maxdim_double)
        || !read_daf_double_at_address(kernel, segment.end_address, &entry_count_double)) {
        return false;
    }

    int maxdim = 0;
    int entry_count = 0;
    if (!double_to_positive_int(maxdim_double, &maxdim)
        || !double_to_positive_int(entry_count_double, &entry_count)
        || maxdim > SPK_TYPE21_MAX_TERMS) {
        return false;
    }

    const int dlsize = 4 * maxdim + 11;
    std::vector<double> epoch_table;
    if (!read_daf_doubles_at_address(kernel, segment.start_address + entry_count * dlsize, entry_count, &epoch_table)) {
        return false;
    }

    const double start_et_seconds =
        segment.start_et_seconds > required_start_et_seconds ? segment.start_et_seconds : required_start_et_seconds;
    const double end_et_seconds =
        segment.end_et_seconds < required_end_et_seconds ? segment.end_et_seconds : required_end_et_seconds;
    int first_record_index = find_type21_record_index(&epoch_table[0], entry_count, start_et_seconds);
    int last_record_index = find_type21_record_index(&epoch_table[0], entry_count, end_et_seconds);
    if (first_record_index <= 0 || last_record_index <= 0) {
        return false;
    }
    if (last_record_index < first_record_index) {
        last_record_index = first_record_index;
    }

    SpkCompiledSegment compiled;
    compiled.start_et_seconds = segment.start_et_seconds;
    compiled.end_et_seconds = segment.end_et_seconds;
    compiled.target_id = segment.target_id;
    compiled.center_id = segment.center_id;
    compiled.frame_id = segment.frame_id;
    compiled.spk_type = segment.spk_type;
    compiled.record_count = last_record_index - first_record_index + 1;
    compiled.record_size_doubles = dlsize;
    compiled.first_record_index = first_record_index - 1;
    compiled.type21_maxdim = maxdim;
    compiled.type21_entry_count = entry_count;

    const int record_start_address = segment.start_address + (first_record_index - 1) * dlsize;
    const size_t record_doubles =
        static_cast<size_t>(compiled.record_count) * static_cast<size_t>(compiled.record_size_doubles);
    if (!append_daf_doubles(kernel, record_start_address, record_doubles, &out->double_pool, &compiled.record_offset)) {
        return false;
    }

    compiled.type21_epoch_table_offset = out->double_pool.size();
    out->double_pool.insert(out->double_pool.end(), epoch_table.begin(), epoch_table.end());

    compiled.type21_epoch_dir_count = entry_count / 100;
    if (compiled.type21_epoch_dir_count > 0) {
        if (!append_daf_doubles(
                kernel,
                segment.end_address - compiled.type21_epoch_dir_count - 1,
                static_cast<size_t>(compiled.type21_epoch_dir_count),
                &out->double_pool,
                &compiled.type21_epoch_dir_offset)) {
            return false;
        }
    }

    out->segments.push_back(compiled);
    return true;
}

bool compile_spk_segment_for_range(
    const SpkKernel& kernel,
    const SpkSegment& segment,
    double required_start_et_seconds,
    double required_end_et_seconds,
    SpkEphemerisData* out
) noexcept {
    if (!out
        || !spk_segment_is_supported(segment)
        || !intervals_overlap(
            segment.start_et_seconds,
            segment.end_et_seconds,
            required_start_et_seconds,
            required_end_et_seconds)) {
        return true;
    }
    if (segment.spk_type == 2 || segment.spk_type == 3) {
        return compile_spk_segment_type2_or_3(kernel, segment, required_start_et_seconds, required_end_et_seconds, out);
    }
    if (segment.spk_type == 20) {
        return compile_spk_segment_type20(kernel, segment, required_start_et_seconds, required_end_et_seconds, out);
    }
    if (segment.spk_type == 21) {
        return compile_spk_segment_type21(kernel, segment, required_start_et_seconds, required_end_et_seconds, out);
    }
    return false;
}

bool compile_spk_pair_for_range(
    const SpkKernel& kernel,
    int target_id,
    int center_id,
    double required_start_et_seconds,
    double required_end_et_seconds,
    SpkEphemerisData* out
) noexcept {
    for (size_t i = 0; i < kernel.index.segments.size(); ++i) {
        const SpkSegment& segment = kernel.index.segments[i];
        if (segment.target_id == target_id
            && segment.center_id == center_id
            && !compile_spk_segment_for_range(
                kernel,
                segment,
                required_start_et_seconds,
                required_end_et_seconds,
                out)) {
            return false;
        }
    }
    return true;
}

bool compile_spk_path_for_range(
    const SpkKernel& kernel,
    const std::vector<SpkPathStep>& path,
    double required_start_et_seconds,
    double required_end_et_seconds,
    SpkEphemerisData* out
) noexcept {
    for (size_t i = 0; i < path.size(); ++i) {
        const SpkPathStep& step = path[i];
        if (step.parent_index < 0) {
            continue;
        }
        if (!compile_spk_pair_for_range(
                kernel,
                step.edge_target_id,
                step.edge_center_id,
                required_start_et_seconds,
                required_end_et_seconds,
                out)) {
            return false;
        }
    }
    return true;
}

const SpkCompiledSegment* find_compiled_spk_segment(
    const SpkEphemerisData& data,
    int target_id,
    int center_id,
    double et_seconds
) noexcept {
    if (!std::isfinite(et_seconds)) {
        return 0;
    }
    for (size_t i = 0; i < data.segments.size(); ++i) {
        const SpkCompiledSegment& segment = data.segments[i];
        if (segment.target_id == target_id
            && segment.center_id == center_id
            && et_seconds >= segment.start_et_seconds
            && et_seconds <= segment.end_et_seconds) {
            return &segment;
        }
    }
    return 0;
}

bool eval_compiled_spk_segment(
    const SpkEphemerisData& data,
    const SpkCompiledSegment& segment,
    double et_seconds,
    bool compute_velocity,
    CartesianState* out
) noexcept {
    if (!out || et_seconds < segment.start_et_seconds || et_seconds > segment.end_et_seconds) {
        return false;
    }

    if (segment.spk_type == 21) {
        if (!pool_has_range(data, segment.type21_epoch_table_offset, static_cast<size_t>(segment.type21_entry_count))) {
            return false;
        }
        const double* epoch_table = &data.double_pool[segment.type21_epoch_table_offset];
        const int record_index = find_type21_record_index(epoch_table, segment.type21_entry_count, et_seconds);
        const int local_index = record_index - 1 - segment.first_record_index;
        if (record_index <= 0 || local_index < 0 || local_index >= segment.record_count) {
            return false;
        }
        const size_t record_offset =
            segment.record_offset + static_cast<size_t>(local_index) * static_cast<size_t>(segment.record_size_doubles);
        if (!pool_has_range(data, record_offset, static_cast<size_t>(segment.record_size_doubles))) {
            return false;
        }
        std::vector<double> record(
            data.double_pool.begin() + static_cast<std::vector<double>::difference_type>(record_offset),
            data.double_pool.begin() + static_cast<std::vector<double>::difference_type>(record_offset + segment.record_size_doubles));
        CartesianState state;
        if (!eval_type21_record(et_seconds, record, segment.type21_maxdim, &state)) {
            return false;
        }
        if (!compute_velocity) {
            state.velocity_au_per_day.x = 0.0;
            state.velocity_au_per_day.y = 0.0;
            state.velocity_au_per_day.z = 0.0;
        }
        *out = state;
        return true;
    }

    if (segment.spk_type != 2 && segment.spk_type != 3 && segment.spk_type != 20) {
        return false;
    }
    if (segment.interval_seconds <= 0.0 || segment.record_count <= 0 || segment.record_size_doubles <= 0) {
        return false;
    }

    int record_index = static_cast<int>(std::floor((et_seconds - segment.init_et_seconds) / segment.interval_seconds));
    record_index = clamp_int(record_index, segment.first_record_index, segment.first_record_index + segment.record_count - 1);
    const int local_index = record_index - segment.first_record_index;
    const size_t record_offset =
        segment.record_offset + static_cast<size_t>(local_index) * static_cast<size_t>(segment.record_size_doubles);
    const size_t record_doubles = static_cast<size_t>(segment.record_size_doubles);
    if (!pool_has_range(data, record_offset, record_doubles)) {
        return false;
    }

    std::vector<uint8_t> record(record_doubles * sizeof(double));
    std::memcpy(&record[0], &data.double_pool[record_offset], record.size());

    if (segment.spk_type == 20) {
        SpkType20Directory directory;
        directory.distance_scale_km = segment.distance_scale_km;
        directory.time_scale_seconds = segment.time_scale_seconds;
        directory.init_et_seconds = segment.init_et_seconds;
        directory.interval_seconds = segment.interval_seconds;
        directory.record_size_doubles = segment.record_size_doubles;
        directory.record_count = segment.first_record_index + segment.record_count;
        return eval_type20_record(&record[0], record.size(), et_seconds, directory, record_index, compute_velocity, out);
    }

    return eval_chebyshev_record(segment.spk_type, &record[0], record.size(), et_seconds, compute_velocity, out);
}

bool eval_compiled_direct_state(
    const SpkEphemerisData& data,
    int target_id,
    int center_id,
    double et_seconds,
    bool compute_velocity,
    CartesianState* out
) noexcept {
    const SpkCompiledSegment* segment = find_compiled_spk_segment(data, target_id, center_id, et_seconds);
    return segment && eval_compiled_spk_segment(data, *segment, et_seconds, compute_velocity, out);
}

bool compiled_path_contains_node(const std::vector<SpkPathStep>& steps, int node_id) noexcept {
    for (size_t i = 0; i < steps.size(); ++i) {
        if (steps[i].node_id == node_id) {
            return true;
        }
    }
    return false;
}

bool find_compiled_spk_path(
    const SpkEphemerisData& data,
    int target_id,
    int center_id,
    double et_seconds,
    std::vector<SpkPathStep>* out
) noexcept {
    if (!out) {
        return false;
    }
    out->clear();
    if (target_id == center_id) {
        SpkPathStep root;
        root.node_id = target_id;
        out->push_back(root);
        return true;
    }

    std::vector<SpkPathStep> queue;
    SpkPathStep root;
    root.node_id = target_id;
    queue.push_back(root);

    for (size_t cursor = 0; cursor < queue.size(); ++cursor) {
        const int current_id = queue[cursor].node_id;
        for (size_t i = 0; i < data.segments.size(); ++i) {
            const SpkCompiledSegment& segment = data.segments[i];
            if (et_seconds < segment.start_et_seconds || et_seconds > segment.end_et_seconds) {
                continue;
            }

            bool reversed = false;
            int next_id = 0;
            if (segment.target_id == current_id) {
                next_id = segment.center_id;
                reversed = false;
            } else if (segment.center_id == current_id) {
                next_id = segment.target_id;
                reversed = true;
            } else {
                continue;
            }
            if (compiled_path_contains_node(queue, next_id)) {
                continue;
            }

            SpkPathStep step;
            step.node_id = next_id;
            step.parent_index = static_cast<int>(cursor);
            step.edge_target_id = segment.target_id;
            step.edge_center_id = segment.center_id;
            step.edge_reversed = reversed;
            queue.push_back(step);

            if (next_id == center_id) {
                std::vector<SpkPathStep> path;
                int index = static_cast<int>(queue.size()) - 1;
                while (index >= 0) {
                    path.push_back(queue[static_cast<size_t>(index)]);
                    index = queue[static_cast<size_t>(index)].parent_index;
                }
                for (std::vector<SpkPathStep>::reverse_iterator it = path.rbegin(); it != path.rend(); ++it) {
                    out->push_back(*it);
                }
                return true;
            }
        }
    }
    return false;
}

bool eval_compiled_relative_state(
    const SpkEphemerisData& data,
    int target_id,
    int center_id,
    double et_seconds,
    bool compute_velocity,
    CartesianState* out
) noexcept {
    if (!out) {
        return false;
    }
    if (target_id == center_id) {
        *out = CartesianState();
        return true;
    }

    std::vector<SpkPathStep> path;
    if (!find_compiled_spk_path(data, target_id, center_id, et_seconds, &path)) {
        return false;
    }

    CartesianState result = CartesianState();
    for (size_t i = 1; i < path.size(); ++i) {
        const SpkPathStep& step = path[i];
        CartesianState edge_state;
        if (!eval_compiled_direct_state(
                data,
                step.edge_target_id,
                step.edge_center_id,
                et_seconds,
                compute_velocity,
                &edge_state)) {
            return false;
        }
        if (step.edge_reversed) {
            subtract_state_in_place(&result, edge_state);
        } else {
            add_state_in_place(&result, edge_state);
        }
    }

    *out = result;
    return true;
}

bool finite_difference_spk_acceleration(
    const SpkEphemerisData& data,
    double jd_tdb,
    Vector3* out
) noexcept {
    if (!out || !std::isfinite(jd_tdb) || jd_tdb < data.jd_tdb_start || jd_tdb > data.jd_tdb_end) {
        return false;
    }
    if (data.target_id == data.center_id) {
        *out = Vector3();
        return true;
    }

    double dt = SPK_ACCELERATION_STEP_DAYS;
    const double available_before = jd_tdb - data.jd_tdb_start;
    const double available_after = data.jd_tdb_end - jd_tdb;
    if (available_before >= dt && available_after >= dt) {
        CartesianState before;
        CartesianState after;
        if (!eval_compiled_relative_state(
                data,
                data.target_id,
                data.center_id,
                jd_tdb_to_et_seconds(jd_tdb - dt),
                true,
                &before)
            || !eval_compiled_relative_state(
                data,
                data.target_id,
                data.center_id,
                jd_tdb_to_et_seconds(jd_tdb + dt),
                true,
                &after)) {
            return false;
        }
        const double scale = 1.0 / (2.0 * dt);
        out->x = (after.velocity_au_per_day.x - before.velocity_au_per_day.x) * scale;
        out->y = (after.velocity_au_per_day.y - before.velocity_au_per_day.y) * scale;
        out->z = (after.velocity_au_per_day.z - before.velocity_au_per_day.z) * scale;
        return true;
    }

    if (available_after > 0.0 && available_before <= available_after) {
        dt = std::min(dt, available_after);
        CartesianState current;
        CartesianState after;
        if (dt <= 0.0
            || !eval_compiled_relative_state(
                data,
                data.target_id,
                data.center_id,
                jd_tdb_to_et_seconds(jd_tdb),
                true,
                &current)
            || !eval_compiled_relative_state(
                data,
                data.target_id,
                data.center_id,
                jd_tdb_to_et_seconds(jd_tdb + dt),
                true,
                &after)) {
            return false;
        }
        const double scale = 1.0 / dt;
        out->x = (after.velocity_au_per_day.x - current.velocity_au_per_day.x) * scale;
        out->y = (after.velocity_au_per_day.y - current.velocity_au_per_day.y) * scale;
        out->z = (after.velocity_au_per_day.z - current.velocity_au_per_day.z) * scale;
        return true;
    }

    if (available_before > 0.0) {
        dt = std::min(dt, available_before);
        CartesianState before;
        CartesianState current;
        if (dt <= 0.0
            || !eval_compiled_relative_state(
                data,
                data.target_id,
                data.center_id,
                jd_tdb_to_et_seconds(jd_tdb - dt),
                true,
                &before)
            || !eval_compiled_relative_state(
                data,
                data.target_id,
                data.center_id,
                jd_tdb_to_et_seconds(jd_tdb),
                true,
                &current)) {
            return false;
        }
        const double scale = 1.0 / dt;
        out->x = (current.velocity_au_per_day.x - before.velocity_au_per_day.x) * scale;
        out->y = (current.velocity_au_per_day.y - before.velocity_au_per_day.y) * scale;
        out->z = (current.velocity_au_per_day.z - before.velocity_au_per_day.z) * scale;
        return true;
    }

    return false;
}

}  // namespace

bool load_spk_kernel_from_source(SpkByteSource source, SpkKernel* out) noexcept {
    if (!out || !source.read || source.byte_count < DAF_RECORD_BYTES) {
        return false;
    }

    SpkKernel parsed;
    parsed.source = source;

    uint8_t first_record[DAF_RECORD_BYTES];
    if (!read_record(parsed, 1, first_record)) {
        return false;
    }

    if (std::memcmp(first_record, "DAF/SPK", 7) != 0) {
        return false;
    }

    int32_t nd = 0;
    int32_t ni = 0;
    int32_t first_summary_record = 0;
    int32_t last_summary_record = 0;
    int32_t free_address = 0;
    if (!read_i32_le(first_record, sizeof(first_record), 8, &nd)
        || !read_i32_le(first_record, sizeof(first_record), 12, &ni)
        || !read_i32_le(first_record, sizeof(first_record), 76, &first_summary_record)
        || !read_i32_le(first_record, sizeof(first_record), 80, &last_summary_record)
        || !read_i32_le(first_record, sizeof(first_record), 84, &free_address)) {
        return false;
    }
    if (nd != 2 || ni != 6 || first_summary_record <= 0 || last_summary_record <= 0 || free_address <= 0) {
        return false;
    }
    if (!record_number_in_source(first_summary_record, source.byte_count)
        || !record_number_in_source(last_summary_record, source.byte_count)) {
        return false;
    }

    char binary_format[9] = { 0 };
    std::memcpy(binary_format, first_record + 88, 8);
    if (std::memcmp(binary_format, "BIG-IEEE", 8) == 0) {
        return false;
    }

    parsed.index.little_endian = true;
    parsed.index.first_summary_record = first_summary_record;
    parsed.index.last_summary_record = last_summary_record;
    parsed.index.free_address = free_address;

    const int integer_word_count = (ni + 1) / 2;
    const int summary_size_bytes = (nd + integer_word_count) * 8;
    if (summary_size_bytes <= 0 || summary_size_bytes > DAF_RECORD_BYTES - 24) {
        return false;
    }

    int record_number = first_summary_record;
    int guard = 0;
    while (record_number != 0) {
        if (!record_number_in_source(record_number, source.byte_count)
            || ++guard > static_cast<int>(source.byte_count / DAF_RECORD_BYTES) + 1) {
            return false;
        }

        uint8_t record[DAF_RECORD_BYTES];
        if (!read_record(parsed, record_number, record)) {
            return false;
        }

        double next_record_double = 0.0;
        double summary_count_double = 0.0;
        if (!read_f64_le(record, sizeof(record), 0, &next_record_double)
            || !read_f64_le(record, sizeof(record), 16, &summary_count_double)) {
            return false;
        }

        int next_record = 0;
        int summary_count = 0;
        if (!double_to_nonnegative_int(summary_count_double, &summary_count)
            || !double_to_nonnegative_int(next_record_double, &next_record)
            || summary_count < 0
            || 24 + summary_count * summary_size_bytes > DAF_RECORD_BYTES) {
            return false;
        }

        for (int summary_index = 0; summary_index < summary_count; ++summary_index) {
            const size_t offset = 24 + static_cast<size_t>(summary_index) * summary_size_bytes;
            SpkSegment segment;
            int32_t target_id = 0;
            int32_t center_id = 0;
            int32_t frame_id = 0;
            int32_t spk_type = 0;
            int32_t start_address = 0;
            int32_t end_address = 0;
            if (!read_f64_le(record, sizeof(record), offset, &segment.start_et_seconds)
                || !read_f64_le(record, sizeof(record), offset + 8, &segment.end_et_seconds)
                || !read_i32_le(record, sizeof(record), offset + 16, &target_id)
                || !read_i32_le(record, sizeof(record), offset + 20, &center_id)
                || !read_i32_le(record, sizeof(record), offset + 24, &frame_id)
                || !read_i32_le(record, sizeof(record), offset + 28, &spk_type)
                || !read_i32_le(record, sizeof(record), offset + 32, &start_address)
                || !read_i32_le(record, sizeof(record), offset + 36, &end_address)) {
                return false;
            }

            segment.target_id = target_id;
            segment.center_id = center_id;
            segment.frame_id = frame_id;
            segment.spk_type = spk_type;
            segment.start_address = start_address;
            segment.end_address = end_address;
            segment.summary_record = record_number;
            segment.summary_index = summary_index;

            if (!std::isfinite(segment.start_et_seconds)
                || !std::isfinite(segment.end_et_seconds)
                || segment.end_et_seconds < segment.start_et_seconds
                || segment.start_address <= 0
                || segment.end_address < segment.start_address) {
                return false;
            }
            const uint64_t segment_offset = static_cast<uint64_t>(segment.start_address - 1) * 8u;
            const uint64_t segment_bytes = static_cast<uint64_t>(segment.end_address - segment.start_address + 1) * 8u;
            if (segment_bytes > static_cast<uint64_t>(std::numeric_limits<size_t>::max())
                || !checked_source_range(source.byte_count, segment_offset, static_cast<size_t>(segment_bytes))) {
                return false;
            }
            parsed.index.segments.push_back(segment);
        }

        record_number = next_record;
    }

    *out = parsed;
    return true;
}

SpkByteSource make_file_source(const std::string& path) noexcept {
    SpkByteSource source;
    uint64_t size = 0;
    if (path.empty() || !file_size_bytes(path, &size)) {
        return source;
    }

    SpkFileSource* file_source = new (std::nothrow) SpkFileSource();
    if (!file_source) {
        return source;
    }
    file_source->path = path;

    source.user_data = file_source;
    source.byte_count = size;
    source.read = read_file_source_range;
    source.destroy = destroy_file_source;
    return source;
}

SpkByteSource make_memory_source(const void* bytes, size_t byte_count) noexcept {
    SpkByteSource source;
    if (!bytes || byte_count == 0) {
        return source;
    }

    SpkMemorySource* memory_source = new (std::nothrow) SpkMemorySource();
    if (!memory_source) {
        return source;
    }
    memory_source->bytes = static_cast<const uint8_t*>(bytes);
    memory_source->byte_count = byte_count;

    source.user_data = memory_source;
    source.byte_count = byte_count;
    source.read = read_memory_source_range;
    source.destroy = destroy_memory_source;
    return source;
}

bool load_spk_kernel(const std::string& path, SpkKernel* out) noexcept {
    if (!out) {
        return false;
    }
    destroy_spk_source(&out->source);
    *out = SpkKernel();

    SpkByteSource source = make_file_source(path);
    if (!source.read) {
        return false;
    }
    if (!load_spk_kernel_from_source(source, out)) {
        destroy_spk_source(&source);
        return false;
    }
    return true;
}

bool compile_spk_kernel(SpkByteSource source, SpkKernel** out) noexcept {
    if (!out) {
        destroy_spk_source(&source);
        return false;
    }
    *out = 0;

    SpkKernel* kernel = new (std::nothrow) SpkKernel();
    if (!kernel) {
        destroy_spk_source(&source);
        return false;
    }
    if (!load_spk_kernel_from_source(source, kernel)) {
        destroy_spk_source(&source);
        delete kernel;
        return false;
    }
    *out = kernel;
    return true;
}

bool compile_spk_kernel_from_file(const std::string& path, SpkKernel** out) noexcept {
    return compile_spk_kernel(make_file_source(path), out);
}

bool compile_spk_kernel_from_memory(const void* bytes, size_t byte_count, SpkKernel** out) noexcept {
    return compile_spk_kernel(make_memory_source(bytes, byte_count), out);
}

void spk_kernel_destroy(SpkKernel* data) noexcept {
    if (data) {
        destroy_spk_source(&data->source);
    }
    delete data;
}

void spk_kernel_destroy_void(void* data) noexcept {
    spk_kernel_destroy(static_cast<SpkKernel*>(data));
}

bool compile_spk_ephemeris_data_from_source(
    SpkByteSource source,
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    SpkEphemerisData** out
) noexcept {
    if (!out) {
        destroy_spk_source(&source);
        return false;
    }
    *out = 0;
    if (!finite_ordered_range(jd_tdb_start, jd_tdb_end)) {
        destroy_spk_source(&source);
        return false;
    }

    SpkKernel* kernel = 0;
    if (!compile_spk_kernel(source, &kernel)) {
        return false;
    }
    const SpkCompileRange range(jd_tdb_start, jd_tdb_end);
    if (!spk_relative_covers_jd_range(*kernel, target_id, center_id, range)) {
        spk_kernel_destroy(kernel);
        return false;
    }

    SpkEphemerisData* data = new (std::nothrow) SpkEphemerisData();
    if (!data) {
        spk_kernel_destroy(kernel);
        return false;
    }
    data->target_id = target_id;
    data->center_id = center_id;
    data->jd_tdb_start = jd_tdb_start;
    data->jd_tdb_end = jd_tdb_end;

    const double start_et_seconds = jd_tdb_to_et_seconds(jd_tdb_start);
    const double end_et_seconds = jd_tdb_to_et_seconds(jd_tdb_end);
    std::vector<SpkPathStep> path;
    const bool ok =
        find_spk_pair_path_for_et_range(*kernel, target_id, center_id, start_et_seconds, end_et_seconds, &path)
        && compile_spk_path_for_range(*kernel, path, start_et_seconds, end_et_seconds, data);
    spk_kernel_destroy(kernel);

    if (!ok || (target_id != center_id && data->segments.empty())) {
        delete data;
        return false;
    }

    *out = data;
    return true;
}

bool compile_spk_ephemeris_data_from_file(
    const std::string& path,
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    SpkEphemerisData** out
) noexcept {
    if (!out) {
        return false;
    }
    return compile_spk_ephemeris_data_from_source(
        make_file_source(path),
        target_id,
        center_id,
        jd_tdb_start,
        jd_tdb_end,
        out);
}

bool calc_spk_position_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    CartesianState state;
    if (!calc_spk_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.position_au;
    return true;
}

bool calc_spk_velocity_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    if (!out) {
        return false;
    }
    CartesianState state;
    if (!calc_spk_state_void(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.velocity_au_per_day;
    return true;
}

bool calc_spk_acceleration_void(double jd_tdb, const void* data, Vector3* out) noexcept {
    const SpkEphemerisData* spk_data = static_cast<const SpkEphemerisData*>(data);
    return spk_data && finite_difference_spk_acceleration(*spk_data, jd_tdb, out);
}

bool compiled_spk_data_supports_acceleration(const SpkEphemerisData* data) noexcept {
    if (!data) {
        return false;
    }
    return true;
}

bool compile_spk_ephemeris_block_from_source(
    SpkByteSource source,
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept {
    if (!out) {
        destroy_spk_source(&source);
        return false;
    }
    *out = StorageEphemerisBlock();

    SpkEphemerisData* data = 0;
    if (!compile_spk_ephemeris_data_from_source(
            source,
            target_id,
            center_id,
            jd_tdb_start,
            jd_tdb_end,
            &data)) {
        return false;
    }

    out->cache_id = 0;
    out->format = EphemerisBlockFormat::Spk;
    out->position = calc_spk_position_void;
    out->velocity = calc_spk_velocity_void;
    out->acceleration = compiled_spk_data_supports_acceleration(data)
        ? calc_spk_acceleration_void
        : 0;
    out->destroy_element = spk_ephemeris_data_destroy_void;
    out->data_vector.push_back(data);
    out->total_bytes = sizeof(SpkEphemerisData)
        + data->segments.capacity() * sizeof(SpkCompiledSegment)
        + data->double_pool.capacity() * sizeof(double);
    return true;
}

bool compile_spk_ephemeris_block_from_file(
    const std::string& path,
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept {
    if (!out) {
        return false;
    }
    return compile_spk_ephemeris_block_from_source(
        make_file_source(path),
        target_id,
        center_id,
        jd_tdb_start,
        jd_tdb_end,
        out);
}

bool calc_spk_state(double jd_tdb, const SpkEphemerisData* data, CartesianState* out) noexcept {
    if (!data || !out || !std::isfinite(jd_tdb)) {
        return false;
    }
    if (jd_tdb < data->jd_tdb_start || jd_tdb > data->jd_tdb_end) {
        return false;
    }
    if (data->target_id == data->center_id) {
        *out = CartesianState();
        return true;
    }

    const double et_seconds = jd_tdb_to_et_seconds(jd_tdb);
    if (!eval_compiled_relative_state(*data, data->target_id, data->center_id, et_seconds, true, out)) {
        return false;
    }
    finite_difference_spk_acceleration(*data, jd_tdb, &out->acceleration_au_per_day2);
    return true;
}

bool calc_spk_state_void(double jd_tdb, const void* data, CartesianState* out) noexcept {
    return calc_spk_state(jd_tdb, static_cast<const SpkEphemerisData*>(data), out);
}

void spk_ephemeris_data_destroy(SpkEphemerisData* data) noexcept {
    delete data;
}

void spk_ephemeris_data_destroy_void(void* data) noexcept {
    spk_ephemeris_data_destroy(static_cast<SpkEphemerisData*>(data));
}

const SpkSegment* find_spk_segment(
    const SpkKernel& kernel,
    int target_id,
    int center_id,
    double et_seconds
) noexcept {
    if (!std::isfinite(et_seconds)) {
        return 0;
    }
    for (size_t i = 0; i < kernel.index.segments.size(); ++i) {
        const SpkSegment& segment = kernel.index.segments[i];
        if (segment.target_id == target_id
            && segment.center_id == center_id
            && spk_segment_is_supported(segment)
            && et_seconds >= segment.start_et_seconds
            && et_seconds <= segment.end_et_seconds) {
            return &segment;
        }
    }
    return 0;
}

bool eval_spk_segment_type2(
    const SpkKernel& kernel,
    const SpkSegment& segment,
    double et_seconds,
    bool compute_velocity,
    CartesianState* out
) noexcept {
    if (!out || segment.spk_type != 2 || et_seconds < segment.start_et_seconds || et_seconds > segment.end_et_seconds) {
        return false;
    }

    return eval_spk_segment(kernel, segment, et_seconds, compute_velocity, out);
}

bool eval_spk_segment(
    const SpkKernel& kernel,
    const SpkSegment& segment,
    double et_seconds,
    bool compute_velocity,
    CartesianState* out
) noexcept {
    if (!out || et_seconds < segment.start_et_seconds || et_seconds > segment.end_et_seconds) {
        return false;
    }
    if (segment.spk_type == 21) {
        return eval_spk_segment_type21(kernel, segment, et_seconds, compute_velocity, out);
    }
    if (segment.spk_type == 20) {
        SpkType20Directory directory;
        if (!read_spk_type20_directory(kernel, segment, &directory)) {
            return false;
        }
        if (et_seconds < directory.init_et_seconds
            || et_seconds > directory.init_et_seconds + directory.interval_seconds * directory.record_count) {
            return false;
        }

        int record_index = static_cast<int>(std::floor((et_seconds - directory.init_et_seconds) / directory.interval_seconds));
        record_index = clamp_int(record_index, 0, directory.record_count - 1);
        const int record_start_address = segment.start_address + record_index * directory.record_size_doubles;
        const uint64_t record_offset = static_cast<uint64_t>(record_start_address - 1) * 8u;
        const size_t record_byte_count = static_cast<size_t>(directory.record_size_doubles) * 8u;
        if (!checked_source_range(kernel.source.byte_count, record_offset, record_byte_count)) {
            return false;
        }

        std::vector<uint8_t> record(record_byte_count);
        if (!read_spk_range(kernel, record_offset, &record[0], record.size())) {
            return false;
        }
        return eval_type20_record(&record[0], record.size(), et_seconds, directory, record_index, compute_velocity, out);
    }
    if (segment.spk_type != 2 && segment.spk_type != 3) {
        return false;
    }

    SpkChebyshevDirectory directory;
    if (!read_spk_chebyshev_directory(kernel, segment, &directory)) {
        return false;
    }
    if (et_seconds < directory.init_et_seconds
        || et_seconds > directory.init_et_seconds + directory.interval_seconds * directory.record_count) {
        return false;
    }

    int record_index = static_cast<int>(std::floor((et_seconds - directory.init_et_seconds) / directory.interval_seconds));
    record_index = clamp_int(record_index, 0, directory.record_count - 1);
    const int record_start_address = segment.start_address + record_index * directory.record_size_doubles;
    const uint64_t record_offset = static_cast<uint64_t>(record_start_address - 1) * 8u;
    const size_t record_byte_count = static_cast<size_t>(directory.record_size_doubles) * 8u;
    if (!checked_source_range(kernel.source.byte_count, record_offset, record_byte_count)) {
        return false;
    }

    std::vector<uint8_t> record(record_byte_count);
    if (!read_spk_range(kernel, record_offset, &record[0], record.size())) {
        return false;
    }
    return eval_chebyshev_record(segment.spk_type, &record[0], record.size(), et_seconds, compute_velocity, out);
}

bool eval_spk_relative_state(
    const SpkKernel& kernel,
    int target_id,
    int center_id,
    double jd_tdb,
    bool compute_velocity,
    CartesianState* out
) noexcept {
    if (!out || !std::isfinite(jd_tdb)) {
        return false;
    }
    if (target_id == center_id) {
        *out = CartesianState();
        return true;
    }

    const double et_seconds = (jd_tdb - JD_J2000) * SECONDS_PER_DAY;
    if (eval_direct_state(kernel, target_id, center_id, et_seconds, compute_velocity, out)) {
        return true;
    }

    CartesianState target_state;
    CartesianState center_state;
    if (eval_direct_state(kernel, target_id, 0, et_seconds, compute_velocity, &target_state)
        && eval_direct_state(kernel, center_id, 0, et_seconds, compute_velocity, &center_state)) {
        subtract_state(target_state, center_state, out);
        return true;
    }

    return false;
}

}  // namespace internal
}  // namespace taiyin
