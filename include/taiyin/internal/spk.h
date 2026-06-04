#ifndef TAIYIN_INTERNAL_SPK_H
#define TAIYIN_INTERNAL_SPK_H

#include "../state.h"
#include "ephemeris_block.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace taiyin {
namespace internal {

typedef bool (*SpkSourceReadFn)(
    const void* user_data,
    uint64_t offset,
    void* out,
    size_t byte_count
);

typedef void (*SpkSourceDestroyFn)(void* user_data);

struct SpkByteSource {
    void* user_data;
    uint64_t byte_count;
    SpkSourceReadFn read;
    SpkSourceDestroyFn destroy;

    SpkByteSource()
        : user_data(0),
          byte_count(0),
          read(0),
          destroy(0) {}
};

struct SpkCompileRange {
    bool enabled;
    double jd_tdb_start;
    double jd_tdb_end;

    SpkCompileRange()
        : enabled(false),
          jd_tdb_start(0.0),
          jd_tdb_end(0.0) {}

    SpkCompileRange(double start, double end)
        : enabled(true),
          jd_tdb_start(start),
          jd_tdb_end(end) {}
};

struct SpkSegment {
    double start_et_seconds;
    double end_et_seconds;
    int target_id;
    int center_id;
    int frame_id;
    int spk_type;
    int start_address;
    int end_address;
    int summary_record;
    int summary_index;

    SpkSegment()
        : start_et_seconds(0.0),
          end_et_seconds(0.0),
          target_id(0),
          center_id(0),
          frame_id(0),
          spk_type(0),
          start_address(0),
          end_address(0),
          summary_record(0),
          summary_index(0) {}
};

struct SpkIndex {
    std::vector<SpkSegment> segments;
    bool little_endian;
    int first_summary_record;
    int last_summary_record;
    int free_address;

    SpkIndex()
        : segments(),
          little_endian(true),
          first_summary_record(0),
          last_summary_record(0),
          free_address(0) {}
};

struct SpkKernel {
    SpkByteSource source;
    SpkIndex index;

    SpkKernel()
        : source(),
          index() {}
};

struct SpkCompiledSegment {
    double start_et_seconds;
    double end_et_seconds;
    int target_id;
    int center_id;
    int frame_id;
    int spk_type;

    size_t record_offset;
    int record_count;
    int record_size_doubles;
    int first_record_index;

    double init_et_seconds;
    double interval_seconds;
    double distance_scale_km;
    double time_scale_seconds;

    int type21_maxdim;
    int type21_entry_count;
    size_t type21_epoch_table_offset;
    size_t type21_epoch_dir_offset;
    int type21_epoch_dir_count;

    SpkCompiledSegment()
        : start_et_seconds(0.0),
          end_et_seconds(0.0),
          target_id(0),
          center_id(0),
          frame_id(0),
          spk_type(0),
          record_offset(0),
          record_count(0),
          record_size_doubles(0),
          first_record_index(0),
          init_et_seconds(0.0),
          interval_seconds(0.0),
          distance_scale_km(0.0),
          time_scale_seconds(0.0),
          type21_maxdim(0),
          type21_entry_count(0),
          type21_epoch_table_offset(0),
          type21_epoch_dir_offset(0),
          type21_epoch_dir_count(0) {}
};

struct SpkEphemerisData {
    int target_id;
    int center_id;
    double jd_tdb_start;
    double jd_tdb_end;
    std::vector<SpkCompiledSegment> segments;
    std::vector<double> double_pool;

    SpkEphemerisData()
        : target_id(0),
          center_id(0),
          jd_tdb_start(0.0),
          jd_tdb_end(0.0),
          segments(),
          double_pool() {}
};

bool compile_spk_kernel(SpkByteSource source, SpkKernel** out) noexcept;
bool load_spk_kernel(const std::string& path, SpkKernel* out) noexcept;
bool compile_spk_kernel_from_file(const std::string& path, SpkKernel** out) noexcept;
bool compile_spk_kernel_from_memory(const void* bytes, size_t byte_count, SpkKernel** out) noexcept;
void spk_kernel_destroy(SpkKernel* data) noexcept;
void spk_kernel_destroy_void(void* data) noexcept;

bool compile_spk_ephemeris_data_from_source(
    SpkByteSource source,
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    SpkEphemerisData** out
) noexcept;

bool compile_spk_ephemeris_data_from_file(
    const std::string& path,
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    SpkEphemerisData** out
) noexcept;

bool compile_spk_ephemeris_block_from_source(
    SpkByteSource source,
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept;

bool compile_spk_ephemeris_block_from_file(
    const std::string& path,
    int target_id,
    int center_id,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept;

bool calc_spk_state(
    double jd_tdb,
    const SpkEphemerisData* data,
    CartesianState* out
) noexcept;

bool calc_spk_state_void(
    double jd_tdb,
    const void* data,
    CartesianState* out
) noexcept;

void spk_ephemeris_data_destroy(SpkEphemerisData* data) noexcept;
void spk_ephemeris_data_destroy_void(void* data) noexcept;

const SpkSegment* find_spk_segment(
    const SpkKernel& kernel,
    int target_id,
    int center_id,
    double et_seconds
) noexcept;

bool eval_spk_segment_type2(
    const SpkKernel& kernel,
    const SpkSegment& segment,
    double et_seconds,
    bool compute_velocity,
    CartesianState* out
) noexcept;

bool eval_spk_segment(
    const SpkKernel& kernel,
    const SpkSegment& segment,
    double et_seconds,
    bool compute_velocity,
    CartesianState* out
) noexcept;

bool eval_spk_relative_state(
    const SpkKernel& kernel,
    int target_id,
    int center_id,
    double jd_tdb,
    bool compute_velocity,
    CartesianState* out
) noexcept;

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_SPK_H
