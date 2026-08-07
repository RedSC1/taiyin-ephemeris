#include "taiyin/lunar_limb_tll1.h"

#include <cmath>
#include <cstring>
#include <limits>

namespace taiyin {
namespace {

const char TLL1_MAGIC[4] = { 'T', 'L', 'L', '1' };
const uint32_t TLL1_REQUIRED_FLAGS =
    TLL1_FLAG_LITTLE_ENDIAN | TLL1_FLAG_SIGNED_INT16_OFFSETS;

bool is_native_little_endian() noexcept {
    const uint16_t value = 1;
    return *reinterpret_cast<const uint8_t*>(&value) == 1;
}

bool checked_payload_size(const Tll1Header& header, size_t size) noexcept {
    const uint64_t longitude_count = header.longitude_count;
    const uint64_t latitude_count = header.latitude_count;
    const uint64_t angle_count = header.position_angle_count;
    if (longitude_count == 0 || latitude_count == 0 || angle_count < 2) {
        return false;
    }
    if (longitude_count > std::numeric_limits<uint64_t>::max() / latitude_count) {
        return false;
    }
    const uint64_t plane_count = longitude_count * latitude_count;
    if (plane_count > std::numeric_limits<uint64_t>::max() / angle_count) {
        return false;
    }
    const uint64_t sample_count = plane_count * angle_count;
    if (sample_count > std::numeric_limits<uint64_t>::max() / sizeof(int16_t)) {
        return false;
    }
    const uint64_t expected_size = sample_count * sizeof(int16_t);
    if (header.payload_size != expected_size
        || header.payload_offset < header.header_size
        || header.payload_offset > static_cast<uint64_t>(size)
        || header.payload_size > static_cast<uint64_t>(size) - header.payload_offset) {
        return false;
    }
    return true;
}

bool finite_positive(double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

bool validate_model(
    Tll1LunarLimbModel* model,
    const uint8_t* data,
    size_t size
) noexcept {
    if (!model || !data || size < sizeof(Tll1Header) || !is_native_little_endian()) {
        return false;
    }
    const Tll1Header* header = reinterpret_cast<const Tll1Header*>(data);
    if (std::memcmp(header->magic, TLL1_MAGIC, sizeof(TLL1_MAGIC)) != 0
        || header->version != TLL1_VERSION
        || header->header_size != sizeof(Tll1Header)
        || (header->flags & TLL1_REQUIRED_FLAGS) != TLL1_REQUIRED_FLAGS
        || !finite_positive(header->longitude_step_deg)
        || !finite_positive(header->latitude_step_deg)
        || !finite_positive(header->position_angle_step_deg)
        || !std::isfinite(header->longitude_start_deg)
        || !std::isfinite(header->latitude_start_deg)
        || !std::isfinite(header->position_angle_start_deg)
        || !finite_positive(header->reference_radius_m)
        || !finite_positive(header->mean_distance_m)
        || !finite_positive(header->offset_scale_m)
        || !checked_payload_size(*header, size)) {
        return false;
    }
    const uint8_t* payload = data + static_cast<size_t>(header->payload_offset);
    if ((reinterpret_cast<uintptr_t>(payload) % alignof(int16_t)) != 0u) {
        return false;
    }

    const double longitude_end = header->longitude_start_deg
        + static_cast<double>(header->longitude_count - 1) * header->longitude_step_deg;
    const double latitude_end = header->latitude_start_deg
        + static_cast<double>(header->latitude_count - 1) * header->latitude_step_deg;
    const double angle_span = static_cast<double>(header->position_angle_count)
        * header->position_angle_step_deg;
    if (!std::isfinite(longitude_end)
        || !std::isfinite(latitude_end)
        || !std::isfinite(angle_span)
        || longitude_end < header->longitude_start_deg
        || latitude_end < header->latitude_start_deg
        || std::fabs(angle_span - 360.0) > 1.0e-9) {
        return false;
    }

    model->data = data;
    model->byte_count = size;
    model->header = header;
    model->offsets = reinterpret_cast<const int16_t*>(payload);
    return true;
}

double normalize_degrees(double value) noexcept {
    value = std::fmod(value, 360.0);
    if (value < 0.0) value += 360.0;
    return value;
}

bool interpolation_coordinate(
    double value,
    double start,
    double step,
    uint32_t count,
    uint32_t* out_lower,
    uint32_t* out_upper,
    double* out_fraction
) noexcept {
    if (!out_lower || !out_upper || !out_fraction || count == 0) {
        return false;
    }
    const double end = start + static_cast<double>(count - 1) * step;
    const double tolerance = step * 1.0e-10;
    if (value < start - tolerance || value > end + tolerance) {
        return false;
    }
    if (value <= start) {
        *out_lower = 0;
        *out_upper = count > 1 ? 1 : 0;
        *out_fraction = 0.0;
        return true;
    }
    if (value >= end) {
        *out_lower = count - 1;
        *out_upper = count - 1;
        *out_fraction = 0.0;
        return true;
    }
    const double coordinate = (value - start) / step;
    const uint32_t lower = static_cast<uint32_t>(std::floor(coordinate));
    *out_lower = lower;
    *out_upper = lower + 1;
    *out_fraction = coordinate - static_cast<double>(lower);
    return true;
}

size_t sample_index(
    const Tll1Header& header,
    uint32_t longitude_index,
    uint32_t latitude_index,
    uint32_t angle_index
) noexcept {
    return (static_cast<size_t>(latitude_index) * header.longitude_count + longitude_index)
        * header.position_angle_count + angle_index;
}

double lerp(double a, double b, double fraction) noexcept {
    return a + (b - a) * fraction;
}

}  // namespace

static_assert(sizeof(Tll1Header) == 192, "Tll1Header size must match TLL1 v1");

Tll1LunarLimbModel::Tll1LunarLimbModel() noexcept
    : data(0), byte_count(0), header(0), offsets(0), file() {}

Status tll1_lunar_limb_load_from_memory(
    Tll1LunarLimbModel* model,
    const uint8_t* data,
    size_t size
) noexcept {
    if (!model || !data) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    tll1_lunar_limb_destroy(model);
    return validate_model(model, data, size)
        ? TAIYIN_STATUS_OK
        : TAIYIN_FILE_ERROR_BAD_FORMAT;
}

Status tll1_lunar_limb_load_from_file(
    Tll1LunarLimbModel* model,
    const std::string& path
) noexcept {
    if (!model || path.empty()) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    tll1_lunar_limb_destroy(model);
    if (!model->file.open_readonly(path)) {
        return TAIYIN_FILE_ERROR_NOT_FOUND;
    }
    if (!validate_model(model, model->file.data(), model->file.size())) {
        tll1_lunar_limb_destroy(model);
        return TAIYIN_FILE_ERROR_BAD_FORMAT;
    }
    return TAIYIN_STATUS_OK;
}

void tll1_lunar_limb_destroy(Tll1LunarLimbModel* model) noexcept {
    if (!model) return;
    model->data = 0;
    model->byte_count = 0;
    model->header = 0;
    model->offsets = 0;
    model->file.close();
}

Status tll1_lunar_limb_offset_m(
    const Tll1LunarLimbModel* model,
    double libration_longitude_deg,
    double libration_latitude_deg,
    double position_angle_deg,
    double* out_offset_m
) noexcept {
    if (out_offset_m) *out_offset_m = std::nan("");
    if (!model || !model->header || !model->offsets || !out_offset_m
        || !std::isfinite(libration_longitude_deg)
        || !std::isfinite(libration_latitude_deg)
        || !std::isfinite(position_angle_deg)) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }

    const Tll1Header& header = *model->header;
    uint32_t longitude_0 = 0;
    uint32_t longitude_1 = 0;
    uint32_t latitude_0 = 0;
    uint32_t latitude_1 = 0;
    double longitude_fraction = 0.0;
    double latitude_fraction = 0.0;
    if (!interpolation_coordinate(
            libration_longitude_deg,
            header.longitude_start_deg,
            header.longitude_step_deg,
            header.longitude_count,
            &longitude_0,
            &longitude_1,
            &longitude_fraction)
        || !interpolation_coordinate(
            libration_latitude_deg,
            header.latitude_start_deg,
            header.latitude_step_deg,
            header.latitude_count,
            &latitude_0,
            &latitude_1,
            &latitude_fraction)) {
        return TAIYIN_EPHEMERIS_ERROR_COVERAGE_GAP;
    }

    const double angle = normalize_degrees(
        normalize_degrees(position_angle_deg)
        - normalize_degrees(header.position_angle_start_deg));
    const double angle_coordinate = angle / header.position_angle_step_deg;
    const double angle_floor = std::floor(angle_coordinate);
    double wrapped_angle_index = std::fmod(
        angle_floor, static_cast<double>(header.position_angle_count));
    if (!std::isfinite(angle_coordinate)
        || !std::isfinite(angle_floor)
        || !std::isfinite(wrapped_angle_index)) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    if (wrapped_angle_index < 0.0) {
        wrapped_angle_index += static_cast<double>(header.position_angle_count);
    }
    if (!(wrapped_angle_index >= 0.0)
        || !(wrapped_angle_index < static_cast<double>(header.position_angle_count))) {
        return TAIYIN_EPHEMERIS_ERROR_EVAL_FAILED;
    }
    const uint32_t angle_0 = static_cast<uint32_t>(wrapped_angle_index);
    const uint32_t angle_1 = (angle_0 + 1) % header.position_angle_count;
    const double angle_fraction = angle_coordinate - angle_floor;

    const uint32_t longitude_indices[2] = { longitude_0, longitude_1 };
    const uint32_t latitude_indices[2] = { latitude_0, latitude_1 };
    double corners[2][2] = {};
    for (size_t latitude_side = 0; latitude_side < 2; ++latitude_side) {
        for (size_t longitude_side = 0; longitude_side < 2; ++longitude_side) {
            const size_t index_0 = sample_index(
                header,
                longitude_indices[longitude_side],
                latitude_indices[latitude_side],
                angle_0);
            const size_t index_1 = sample_index(
                header,
                longitude_indices[longitude_side],
                latitude_indices[latitude_side],
                angle_1);
            const double value_0 = static_cast<double>(model->offsets[index_0]);
            const double value_1 = static_cast<double>(model->offsets[index_1]);
            corners[latitude_side][longitude_side] = lerp(
                value_0, value_1, angle_fraction);
        }
    }

    const double latitude_0_value = lerp(
        corners[0][0], corners[0][1], longitude_fraction);
    const double latitude_1_value = lerp(
        corners[1][0], corners[1][1], longitude_fraction);
    *out_offset_m = lerp(
        latitude_0_value, latitude_1_value, latitude_fraction)
        * header.offset_scale_m;
    return TAIYIN_STATUS_OK;
}

Status tll1_lunar_limb_radius_m(
    const Tll1LunarLimbModel* model,
    double libration_longitude_deg,
    double libration_latitude_deg,
    double position_angle_deg,
    double* out_radius_m
) noexcept {
    if (out_radius_m) *out_radius_m = std::nan("");
    if (!out_radius_m) {
        return TAIYIN_ERROR_INVALID_ARGUMENT;
    }
    double offset_m = 0.0;
    const Status status = tll1_lunar_limb_offset_m(
        model,
        libration_longitude_deg,
        libration_latitude_deg,
        position_angle_deg,
        &offset_m);
    if (status != TAIYIN_STATUS_OK) return status;
    *out_radius_m = model->header->reference_radius_m + offset_m;
    return TAIYIN_STATUS_OK;
}

}  // namespace taiyin
