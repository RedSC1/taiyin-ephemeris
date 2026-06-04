#include "taiyin/internal/ephemeris_file_loader.h"
#include "taiyin/internal/opm4.h"
#include "taiyin/internal/spk.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <limits>
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

const char* kDefaultPrivateRoot = "";
const char* kDefaultNasaRoot = "";
const double kJ2000 = 2451545.0;
const double kJulianCenturyDays = 36525.0;
const double kSecondsPerDay = 86400.0;
const double kArcsecPerRadian = 206264.8062470963552;
const double kAuKm = 149597870.7;
const double kSampleHalfWindowDays = 0.01;

struct TargetSpec {
    const char* label;
    const char* private_subdir;
    const char* spk_subpath;
    int spk_target;
    int spk_center;
};

struct PrivateFileEntry {
    std::string path;
    double jd_tdb_start;
    double jd_tdb_end;
};

struct ErrorMetrics {
    double position_norm_au;
    double position_norm_km;
    double velocity_norm_au_per_day;
    double angular_arcsec;
    double position_error_arcsec_at_1au;
    double range_au;
};

struct TargetStats {
    int nasa_centuries;
    int ok_count;
    int missing_private_count;
    int missing_nasa_count;
    int eval_failed_count;
    std::vector<double> position_km;
    std::vector<double> angular_arcsec;
    std::vector<double> position_error_arcsec_at_1au;

    TargetStats()
        : nasa_centuries(0),
          ok_count(0),
          missing_private_count(0),
          missing_nasa_count(0),
          eval_failed_count(0),
          position_km(),
          angular_arcsec(),
          position_error_arcsec_at_1au() {}
};

enum SampleStatus {
    SampleOk,
    MissingPrivate,
    MissingNasa,
    EvalFailed,
};

struct SpkPathStep {
    int node_id;
    int parent_index;
    int edge_target_id;
    int edge_center_id;

    SpkPathStep()
        : node_id(0),
          parent_index(-1),
          edge_target_id(0),
          edge_center_id(0) {}
};

bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool directory_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string join_path(const std::string& lhs, const std::string& rhs) {
    if (lhs.empty()) {
        return rhs;
    }
    if (rhs.empty()) {
        return lhs;
    }
    if (lhs[lhs.size() - 1] == '/') {
        return lhs + rhs;
    }
    return lhs + "/" + rhs;
}

bool has_suffix(const std::string& value, const char* suffix) {
    const size_t suffix_len = std::strlen(suffix);
    return value.size() >= suffix_len
        && value.compare(value.size() - suffix_len, suffix_len, suffix) == 0;
}

std::string basename_of(const std::string& path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string strip_known_suffixes(std::string name) {
    if (has_suffix(name, ".gz")) {
        name.resize(name.size() - 3);
    }
    if (has_suffix(name, ".bin")) {
        name.resize(name.size() - 4);
    }
    if (has_suffix(name, ".opm4")) {
        name.resize(name.size() - 5);
    }
    return name;
}

bool parse_file_jd_range(const std::string& path, double* out_start, double* out_end) {
    if (!out_start || !out_end) {
        return false;
    }

    const std::string name = strip_known_suffixes(basename_of(path));
    const size_t last_us = name.find_last_of('_');
    if (last_us == std::string::npos || last_us + 1 >= name.size()) {
        return false;
    }
    const size_t prev_us = name.find_last_of('_', last_us - 1);
    if (prev_us == std::string::npos || prev_us + 1 >= last_us) {
        return false;
    }

    char* end_ptr = 0;
    const std::string start_text = name.substr(prev_us + 1, last_us - prev_us - 1);
    const std::string end_text = name.substr(last_us + 1);
    const double start = std::strtod(start_text.c_str(), &end_ptr);
    if (!end_ptr || *end_ptr != '\0') {
        return false;
    }
    const double end = std::strtod(end_text.c_str(), &end_ptr);
    if (!end_ptr || *end_ptr != '\0' || !(end > start)) {
        return false;
    }

    *out_start = start;
    *out_end = end;
    return true;
}

void collect_files_recursive(const std::string& root, std::vector<std::string>* out) {
    if (!out) {
        return;
    }
    DIR* dir = opendir(root.c_str());
    if (!dir) {
        return;
    }

    while (dirent* entry = readdir(dir)) {
        const char* name = entry->d_name;
        if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0) {
            continue;
        }

        const std::string path = join_path(root, name);
        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            collect_files_recursive(path, out);
        } else if (S_ISREG(st.st_mode)) {
            out->push_back(path);
        }
    }

    closedir(dir);
}

bool build_private_index(const std::string& root, std::vector<PrivateFileEntry>* out) {
    if (!out) {
        return false;
    }
    out->clear();
    if (!directory_exists(root)) {
        return false;
    }

    std::vector<std::string> paths;
    collect_files_recursive(root, &paths);
    for (size_t i = 0; i < paths.size(); ++i) {
        const std::string& path = paths[i];
        if (!has_suffix(path, ".bin") && !has_suffix(path, ".opm4")) {
            continue;
        }
        if (basename_of(path).find("ref") != std::string::npos) {
            continue;
        }

        double jd_start = 0.0;
        double jd_end = 0.0;
        if (!parse_file_jd_range(path, &jd_start, &jd_end)) {
            continue;
        }

        PrivateFileEntry entry;
        entry.path = path;
        entry.jd_tdb_start = jd_start;
        entry.jd_tdb_end = jd_end;
        out->push_back(entry);
    }

    std::sort(out->begin(), out->end(), [](const PrivateFileEntry& a, const PrivateFileEntry& b) {
        if (a.jd_tdb_start != b.jd_tdb_start) {
            return a.jd_tdb_start < b.jd_tdb_start;
        }
        return a.path < b.path;
    });
    return !out->empty();
}

const PrivateFileEntry* find_private_file(
    const std::vector<PrivateFileEntry>& files,
    double jd_tdb_start,
    double jd_tdb_end
) {
    for (size_t i = 0; i < files.size(); ++i) {
        const PrivateFileEntry& entry = files[i];
        if (entry.jd_tdb_start <= jd_tdb_start + 1e-9 && entry.jd_tdb_end >= jd_tdb_end - 1e-9) {
            return &entry;
        }
    }
    return 0;
}

double jd_tdb_to_et_seconds(double jd_tdb) {
    return (jd_tdb - kJ2000) * kSecondsPerDay;
}

double et_seconds_to_jd_tdb(double et_seconds) {
    return kJ2000 + et_seconds / kSecondsPerDay;
}

bool spk_segment_is_supported(const taiyin::internal::SpkSegment& segment) {
    return segment.spk_type == 2 || segment.spk_type == 3 || segment.spk_type == 20 || segment.spk_type == 21;
}

bool spk_direct_covers_et_range(
    const taiyin::internal::SpkKernel& kernel,
    int target_id,
    int center_id,
    double start_et_seconds,
    double end_et_seconds
) {
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
            const taiyin::internal::SpkSegment& segment = kernel.index.segments[i];
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

bool spk_path_contains_node(const std::vector<SpkPathStep>& steps, int node_id) {
    for (size_t i = 0; i < steps.size(); ++i) {
        if (steps[i].node_id == node_id) {
            return true;
        }
    }
    return false;
}

bool spk_pair_in_path(const std::vector<SpkPathStep>& steps, int target_id, int center_id) {
    for (size_t i = 0; i < steps.size(); ++i) {
        if (steps[i].parent_index >= 0
            && steps[i].edge_target_id == target_id
            && steps[i].edge_center_id == center_id) {
            return true;
        }
    }
    return false;
}

bool spk_relative_covers_et_range(
    const taiyin::internal::SpkKernel& kernel,
    int target_id,
    int center_id,
    double start_et_seconds,
    double end_et_seconds
) {
    if (!std::isfinite(start_et_seconds)
        || !std::isfinite(end_et_seconds)
        || end_et_seconds < start_et_seconds) {
        return false;
    }
    if (target_id == center_id) {
        return true;
    }

    std::vector<SpkPathStep> queue;
    SpkPathStep root;
    root.node_id = target_id;
    queue.push_back(root);

    for (size_t cursor = 0; cursor < queue.size(); ++cursor) {
        const int current_id = queue[cursor].node_id;
        for (size_t i = 0; i < kernel.index.segments.size(); ++i) {
            const taiyin::internal::SpkSegment& segment = kernel.index.segments[i];
            if (!spk_segment_is_supported(segment)
                || !spk_direct_covers_et_range(
                    kernel,
                    segment.target_id,
                    segment.center_id,
                    start_et_seconds,
                    end_et_seconds)) {
                continue;
            }

            int next_id = 0;
            if (segment.target_id == current_id) {
                next_id = segment.center_id;
            } else if (segment.center_id == current_id) {
                next_id = segment.target_id;
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
            queue.push_back(step);

            if (next_id == center_id) {
                return true;
            }
        }
    }

    return false;
}

bool spk_relative_covers_jd_tdb_sample(
    const taiyin::internal::SpkKernel& kernel,
    int target_id,
    int center_id,
    double jd_tdb
) {
    const double start_tdb = jd_tdb - kSampleHalfWindowDays;
    const double end_tdb = jd_tdb + kSampleHalfWindowDays;
    return spk_relative_covers_et_range(
        kernel,
        target_id,
        center_id,
        jd_tdb_to_et_seconds(start_tdb),
        jd_tdb_to_et_seconds(end_tdb));
}

bool kernel_supported_range(const taiyin::internal::SpkKernel& kernel, double* out_start_jd, double* out_end_jd) {
    if (!out_start_jd || !out_end_jd) {
        return false;
    }
    double start_et = std::numeric_limits<double>::infinity();
    double end_et = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < kernel.index.segments.size(); ++i) {
        const taiyin::internal::SpkSegment& segment = kernel.index.segments[i];
        if (!spk_segment_is_supported(segment)) {
            continue;
        }
        start_et = std::min(start_et, segment.start_et_seconds);
        end_et = std::max(end_et, segment.end_et_seconds);
    }
    if (!std::isfinite(start_et) || !std::isfinite(end_et) || end_et <= start_et) {
        return false;
    }
    *out_start_jd = et_seconds_to_jd_tdb(start_et);
    *out_end_jd = et_seconds_to_jd_tdb(end_et);
    return true;
}

bool choose_nasa_century_sample(
    const taiyin::internal::SpkKernel& kernel,
    int target_id,
    int center_id,
    double century_start_tdb,
    double century_end_tdb,
    double* out_jd_tdb
) {
    if (!out_jd_tdb) {
        return false;
    }

    const double span = century_end_tdb - century_start_tdb;
    const double probes[] = {
        century_start_tdb + span * 0.5,
        century_start_tdb + span * 0.25,
        century_start_tdb + span * 0.75,
        century_start_tdb + 5.0,
        century_end_tdb - 5.0,
        century_start_tdb + 0.05,
        century_end_tdb - 0.05,
    };
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        const double jd = probes[i];
        if (jd > century_start_tdb && jd < century_end_tdb
            && spk_relative_covers_jd_tdb_sample(kernel, target_id, center_id, jd)) {
            *out_jd_tdb = jd;
            return true;
        }
    }
    return false;
}

double vector_norm(double x, double y, double z) {
    return std::sqrt(x * x + y * y + z * z);
}

ErrorMetrics compare_states(const taiyin::CartesianState& spk, const taiyin::CartesianState& opm4) {
    ErrorMetrics metrics;
    const double dx = spk.position_au.x - opm4.position_au.x;
    const double dy = spk.position_au.y - opm4.position_au.y;
    const double dz = spk.position_au.z - opm4.position_au.z;
    const double dvx = spk.velocity_au_per_day.x - opm4.velocity_au_per_day.x;
    const double dvy = spk.velocity_au_per_day.y - opm4.velocity_au_per_day.y;
    const double dvz = spk.velocity_au_per_day.z - opm4.velocity_au_per_day.z;

    metrics.position_norm_au = vector_norm(dx, dy, dz);
    metrics.position_norm_km = metrics.position_norm_au * kAuKm;
    metrics.velocity_norm_au_per_day = vector_norm(dvx, dvy, dvz);
    metrics.range_au = vector_norm(spk.position_au.x, spk.position_au.y, spk.position_au.z);
    metrics.position_error_arcsec_at_1au = metrics.position_norm_au * kArcsecPerRadian;

    const double an = vector_norm(spk.position_au.x, spk.position_au.y, spk.position_au.z);
    const double bn = vector_norm(opm4.position_au.x, opm4.position_au.y, opm4.position_au.z);
    if (an == 0.0 || bn == 0.0) {
        metrics.angular_arcsec = 0.0;
    } else {
        const double cx = spk.position_au.y * opm4.position_au.z - spk.position_au.z * opm4.position_au.y;
        const double cy = spk.position_au.z * opm4.position_au.x - spk.position_au.x * opm4.position_au.z;
        const double cz = spk.position_au.x * opm4.position_au.y - spk.position_au.y * opm4.position_au.x;
        const double cross_norm = vector_norm(cx, cy, cz);
        const double dot =
            spk.position_au.x * opm4.position_au.x
            + spk.position_au.y * opm4.position_au.y
            + spk.position_au.z * opm4.position_au.z;
        metrics.angular_arcsec = std::atan2(cross_norm, dot) * kArcsecPerRadian;
    }

    return metrics;
}

double percentile(std::vector<double> values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const double raw = p * static_cast<double>(values.size() - 1);
    const size_t lo = static_cast<size_t>(std::floor(raw));
    const size_t hi = static_cast<size_t>(std::ceil(raw));
    if (lo == hi) {
        return values[lo];
    }
    const double frac = raw - static_cast<double>(lo);
    return values[lo] * (1.0 - frac) + values[hi] * frac;
}

SampleStatus validate_one_sample(
    const TargetSpec& spec,
    const std::string& spk_path,
    const std::vector<PrivateFileEntry>& private_files,
    double jd_tdb,
    const PrivateFileEntry** out_private_file,
    ErrorMetrics* out_metrics
) {
    using taiyin::CartesianState;
    using taiyin::internal::CompiledEphemerisBlock;
    using taiyin::internal::StorageEphemerisBlock;
    using taiyin::internal::compile_opm4_ephemeris_block_from_file;
    using taiyin::internal::compile_spk_ephemeris_block_from_file;
    using taiyin::internal::destroy_storage_ephemeris_block;
    using taiyin::internal::get_compiled_block_from_storage;
    using taiyin::internal::eval_compiled_ephemeris_block;

    if (out_private_file) {
        *out_private_file = 0;
    }
    if (!out_metrics) {
        return SampleStatus::EvalFailed;
    }

    const double jd_tdb_start = jd_tdb - kSampleHalfWindowDays;
    const double jd_tdb_end = jd_tdb + kSampleHalfWindowDays;
    const PrivateFileEntry* private_file = find_private_file(private_files, jd_tdb_start, jd_tdb_end);
    if (!private_file) {
        return SampleStatus::MissingPrivate;
    }
    if (out_private_file) {
        *out_private_file = private_file;
    }

    std::vector<uint8_t> private_bytes;
    taiyin::internal::OPM4Header private_header;
    if (!taiyin::internal::load_ephemeris_file_bytes(private_file->path, &private_bytes)
        || private_bytes.empty()
        || !taiyin::internal::parse_opm4_header(&private_bytes[0], private_bytes.size(), &private_header)
        || private_header.target_id != spec.spk_target
        || private_header.center_id != spec.spk_center) {
        return SampleStatus::EvalFailed;
    }

    StorageEphemerisBlock spk_storage;
    if (!compile_spk_ephemeris_block_from_file(
            spk_path,
            spec.spk_target,
            spec.spk_center,
            jd_tdb_start,
            jd_tdb_end,
            &spk_storage)) {
        return SampleStatus::MissingNasa;
    }
    CompiledEphemerisBlock spk_block;
    get_compiled_block_from_storage(&spk_storage, 0, &spk_block);

    StorageEphemerisBlock storage;
    if (!compile_opm4_ephemeris_block_from_file(
            private_file->path,
            jd_tdb_start,
            jd_tdb_end,
            &storage)) {
        destroy_storage_ephemeris_block(&spk_storage);
        return SampleStatus::MissingPrivate;
    }
    CompiledEphemerisBlock block;
    get_compiled_block_from_storage(&storage, 0, &block);

    CartesianState spk_state;
    CartesianState opm4_state;
    const bool ok =
        eval_compiled_ephemeris_block(jd_tdb, &spk_block, &spk_state)
        && eval_compiled_ephemeris_block(jd_tdb, &block, &opm4_state);
    if (ok) {
        *out_metrics = compare_states(spk_state, opm4_state);
    }

    destroy_storage_ephemeris_block(&storage);
    destroy_storage_ephemeris_block(&spk_storage);
    return ok ? SampleOk : EvalFailed;
}

void print_csv_header() {
    std::printf(
        "status,label,century_index,jd_tdb,spk_target,spk_center,position_km,position_au,velocity_au_day,angular_arcsec,position_error_arcsec_at_1au,range_au,private_file,spk_file\n");
}

void print_sample_row(
    const char* status,
    const TargetSpec& spec,
    int century_index,
    double jd_tdb,
    const ErrorMetrics* metrics,
    const PrivateFileEntry* private_file,
    const std::string& spk_path
) {
    if (metrics) {
        std::printf(
            "%s,%s,%d,%.9f,%d,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%s,%s\n",
            status,
            spec.label,
            century_index,
            jd_tdb,
            spec.spk_target,
            spec.spk_center,
            metrics->position_norm_km,
            metrics->position_norm_au,
            metrics->velocity_norm_au_per_day,
            metrics->angular_arcsec,
            metrics->position_error_arcsec_at_1au,
            metrics->range_au,
            private_file ? private_file->path.c_str() : "",
            spk_path.c_str());
    } else {
        std::printf(
            "%s,%s,%d,%.9f,%d,%d,,,,,,,%s,%s\n",
            status,
            spec.label,
            century_index,
            jd_tdb,
            spec.spk_target,
            spec.spk_center,
            private_file ? private_file->path.c_str() : "",
            spk_path.c_str());
    }
}

TargetStats validate_target(
    const TargetSpec& spec,
    const std::string& private_root,
    const std::string& nasa_root
) {
    using taiyin::internal::SpkKernel;
    using taiyin::internal::compile_spk_kernel_from_file;
    using taiyin::internal::spk_kernel_destroy;

    TargetStats stats;
    const std::string private_dir = join_path(private_root, spec.private_subdir);
    const std::string spk_path = join_path(nasa_root, spec.spk_subpath);

    if (!file_exists(spk_path)) {
        std::fprintf(stderr, "skip %s: missing SPK %s\n", spec.label, spk_path.c_str());
        return stats;
    }

    std::vector<PrivateFileEntry> private_files;
    if (!build_private_index(private_dir, &private_files)) {
        std::fprintf(stderr, "skip %s: no private OPM4 files in %s\n", spec.label, private_dir.c_str());
        return stats;
    }


    SpkKernel* kernel = 0;
    if (!compile_spk_kernel_from_file(spk_path, &kernel)) {
        std::fprintf(stderr, "skip %s: cannot read SPK index %s\n", spec.label, spk_path.c_str());
        return stats;
    }

    double nasa_start_jd = 0.0;
    double nasa_end_jd = 0.0;
    if (!kernel_supported_range(*kernel, &nasa_start_jd, &nasa_end_jd)) {
        std::fprintf(stderr, "skip %s: SPK has no supported segments\n", spec.label);
        spk_kernel_destroy(kernel);
        return stats;
    }

    const int start_century = static_cast<int>(std::floor((nasa_start_jd - kJ2000) / kJulianCenturyDays));
    const int end_century = static_cast<int>(std::floor((nasa_end_jd - kJ2000 - 1e-9) / kJulianCenturyDays));

    for (int century = start_century; century <= end_century; ++century) {
        const double century_start = kJ2000 + static_cast<double>(century) * kJulianCenturyDays;
        const double century_end = century_start + kJulianCenturyDays;
        double sample_jd_tdb = 0.0;
        if (!choose_nasa_century_sample(
                *kernel,
                spec.spk_target,
                spec.spk_center,
                century_start,
                century_end,
                &sample_jd_tdb)) {
            continue;
        }

        ++stats.nasa_centuries;
        const PrivateFileEntry* private_file = 0;
        const double jd_tdb_start = sample_jd_tdb - kSampleHalfWindowDays;
        const double jd_tdb_end = sample_jd_tdb + kSampleHalfWindowDays;
        private_file = find_private_file(private_files, jd_tdb_start, jd_tdb_end);
        if (!private_file) {
            ++stats.missing_private_count;
            print_sample_row("missing_private", spec, century, sample_jd_tdb, 0, 0, spk_path);
            continue;
        }

        ErrorMetrics metrics;
        const SampleStatus status = validate_one_sample(
                spec,
                spk_path,
                private_files,
                sample_jd_tdb,
                &private_file,
                &metrics);
        if (status == SampleStatus::MissingPrivate) {
            ++stats.missing_private_count;
            print_sample_row("missing_private", spec, century, sample_jd_tdb, 0, private_file, spk_path);
            continue;
        }
        if (status == SampleStatus::MissingNasa) {
            ++stats.missing_nasa_count;
            print_sample_row("missing_nasa", spec, century, sample_jd_tdb, 0, private_file, spk_path);
            continue;
        }
        if (status == SampleStatus::EvalFailed) {
            ++stats.eval_failed_count;
            print_sample_row("eval_failed", spec, century, sample_jd_tdb, 0, private_file, spk_path);
            continue;
        }

        ++stats.ok_count;
        stats.position_km.push_back(metrics.position_norm_km);
        stats.angular_arcsec.push_back(metrics.angular_arcsec);
        stats.position_error_arcsec_at_1au.push_back(metrics.position_error_arcsec_at_1au);
        print_sample_row("ok", spec, century, sample_jd_tdb, &metrics, private_file, spk_path);
    }

    spk_kernel_destroy(kernel);
    return stats;
}

std::vector<TargetSpec> make_targets() {
    std::vector<TargetSpec> targets;
    targets.push_back(TargetSpec{ "mercury", "mer", "planetary/de441.bsp", 1, 10 });
    targets.push_back(TargetSpec{ "venus", "ven", "planetary/de441.bsp", 2, 10 });
    targets.push_back(TargetSpec{ "emb", "ear", "planetary/de441.bsp", 3, 10 });
    targets.push_back(TargetSpec{ "mars", "mar", "planetary/de441.bsp", 4, 10 });
    targets.push_back(TargetSpec{ "jupiter", "jup", "planetary/de441.bsp", 5, 10 });
    targets.push_back(TargetSpec{ "saturn", "sat", "planetary/de441.bsp", 6, 10 });
    targets.push_back(TargetSpec{ "uranus", "ura", "planetary/de441.bsp", 7, 10 });
    targets.push_back(TargetSpec{ "neptune", "nep", "planetary/de441.bsp", 8, 10 });
    targets.push_back(TargetSpec{ "pluto", "plu", "planetary/de441.bsp", 9, 10 });
    targets.push_back(TargetSpec{ "moon_geocentric", "moon", "planetary/de441.bsp", 301, 399 });
    targets.push_back(TargetSpec{ "ceres", "ceres", "asteroids/sb441-n16.bsp", 2000001, 10 });
    targets.push_back(TargetSpec{ "pallas", "pallas", "asteroids/sb441-n16.bsp", 2000002, 10 });
    targets.push_back(TargetSpec{ "juno", "juno", "asteroids/sb441-n16.bsp", 2000003, 10 });
    targets.push_back(TargetSpec{ "vesta", "vesta", "asteroids/sb441-n16.bsp", 2000004, 10 });
    targets.push_back(TargetSpec{ "eros", "eros", "asteroids/sb441-n373s.bsp", 2000433, 10 });
    targets.push_back(TargetSpec{ "chiron", "chiron", "asteroids/chiron.bsp", 20002060, 10 });
    targets.push_back(TargetSpec{ "pholus", "pholus", "asteroids/pholus.bsp", 20005145, 10 });
    targets.push_back(TargetSpec{ "nessus", "nessus", "asteroids/nessus.bsp", 20007066, 10 });
    targets.push_back(TargetSpec{ "lilith", "lilith", "asteroids/lilith_1181.bsp", 20001181, 10 });
    targets.push_back(TargetSpec{ "jupiter_cob", "cob/jupiter", "satellites/jup365.bsp", 599, 5 });
    targets.push_back(TargetSpec{ "saturn_cob", "cob/saturn", "satellites/sat441.bsp", 699, 6 });
    targets.push_back(TargetSpec{ "uranus_cob", "cob/uranus", "satellites/ura111xl-799.bsp", 799, 7 });
    targets.push_back(TargetSpec{ "neptune_cob", "cob/neptune", "satellites/nep097.bsp", 899, 8 });
    targets.push_back(TargetSpec{ "pluto_cob", "cob/pluto", "satellites/plu060.bsp", 999, 9 });
    return targets;
}

}  // namespace

int main(int argc, char** argv) {
    const char* private_root_env = std::getenv("TAIYIN_OPM4_ROOT");
    const char* nasa_root_env = std::getenv("TAIYIN_NASA_BSP_ROOT");
    const std::string private_root = argc >= 2 ? argv[1] : (private_root_env ? private_root_env : kDefaultPrivateRoot);
    const std::string nasa_root = argc >= 3 ? argv[2] : (nasa_root_env ? nasa_root_env : kDefaultNasaRoot);


    print_csv_header();

    std::vector<TargetSpec> targets = make_targets();
    std::map<std::string, TargetStats> all_stats;
    TargetStats total;
    for (size_t i = 0; i < targets.size(); ++i) {
        const TargetStats stats = validate_target(targets[i], private_root, nasa_root);
        all_stats[targets[i].label] = stats;
        total.nasa_centuries += stats.nasa_centuries;
        total.ok_count += stats.ok_count;
        total.missing_private_count += stats.missing_private_count;
        total.missing_nasa_count += stats.missing_nasa_count;
        total.eval_failed_count += stats.eval_failed_count;
        total.position_km.insert(total.position_km.end(), stats.position_km.begin(), stats.position_km.end());
        total.angular_arcsec.insert(total.angular_arcsec.end(), stats.angular_arcsec.begin(), stats.angular_arcsec.end());
        total.position_error_arcsec_at_1au.insert(total.position_error_arcsec_at_1au.end(), stats.position_error_arcsec_at_1au.begin(), stats.position_error_arcsec_at_1au.end());
    }

    std::fprintf(stderr, "\nsummary,label,nasa_centuries,ok,missing_private,missing_nasa,eval_failed,max_position_km,p95_position_km,p99_position_km,max_angular_arcsec,p95_angular_arcsec,p99_angular_arcsec,max_position_error_arcsec_at_1au,p95_position_error_arcsec_at_1au,p99_position_error_arcsec_at_1au\n");
    for (size_t i = 0; i < targets.size(); ++i) {
        const TargetStats& stats = all_stats[targets[i].label];
        const double max_position_km = stats.position_km.empty() ? 0.0 : *std::max_element(stats.position_km.begin(), stats.position_km.end());
        const double max_angular = stats.angular_arcsec.empty() ? 0.0 : *std::max_element(stats.angular_arcsec.begin(), stats.angular_arcsec.end());
        const double max_position_error_arcsec_at_1au = stats.position_error_arcsec_at_1au.empty() ? 0.0 : *std::max_element(stats.position_error_arcsec_at_1au.begin(), stats.position_error_arcsec_at_1au.end());
        std::fprintf(
            stderr,
            "summary,%s,%d,%d,%d,%d,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n",
            targets[i].label,
            stats.nasa_centuries,
            stats.ok_count,
            stats.missing_private_count,
            stats.missing_nasa_count,
            stats.eval_failed_count,
            max_position_km,
            percentile(stats.position_km, 0.95),
            percentile(stats.position_km, 0.99),
            max_angular,
            percentile(stats.angular_arcsec, 0.95),
            percentile(stats.angular_arcsec, 0.99),
            max_position_error_arcsec_at_1au,
            percentile(stats.position_error_arcsec_at_1au, 0.95),
            percentile(stats.position_error_arcsec_at_1au, 0.99));
    }

    std::fprintf(
        stderr,
        "summary,total,%d,%d,%d,%d,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n",
        total.nasa_centuries,
        total.ok_count,
        total.missing_private_count,
        total.missing_nasa_count,
        total.eval_failed_count,
        total.position_km.empty() ? 0.0 : *std::max_element(total.position_km.begin(), total.position_km.end()),
        percentile(total.position_km, 0.95),
        percentile(total.position_km, 0.99),
        total.angular_arcsec.empty() ? 0.0 : *std::max_element(total.angular_arcsec.begin(), total.angular_arcsec.end()),
        percentile(total.angular_arcsec, 0.95),
        percentile(total.angular_arcsec, 0.99),
        total.position_error_arcsec_at_1au.empty() ? 0.0 : *std::max_element(total.position_error_arcsec_at_1au.begin(), total.position_error_arcsec_at_1au.end()),
        percentile(total.position_error_arcsec_at_1au, 0.95),
        percentile(total.position_error_arcsec_at_1au, 0.99));

    return total.eval_failed_count == 0 ? 0 : 1;
}
