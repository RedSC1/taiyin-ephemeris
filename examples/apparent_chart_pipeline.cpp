#include "taiyin/angle.h"
#include "taiyin/apparent_position.h"
#include "taiyin/body_id.h"
#include "taiyin/corrections.h"
#include "taiyin/dispatch.h"
#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/ephemeris_file_loader.h"
#include "taiyin/internal/opm4.h"
#include "taiyin/runtime/pipeline.h"
#include "taiyin/time.h"
#include "taiyin/vector3.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <iomanip>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

using namespace taiyin;
using namespace taiyin::internal;
using namespace taiyin::runtime;

const double SAMPLE_HALF_WINDOW_DAYS = 0.25;
const double MOON_EARTH_MASS_RATIO = 0.0123000371;

struct BodySpec {
    const char* name;
    const char* opm4_subdir;
    int target_id;
    int center_id;
};

struct Opm4FileEntry {
    std::string path;
    double jd_tdb_start;
    double jd_tdb_end;
};

struct ApparentChartBody {
    const char* name;
    int target_id;
    int observer_id;
    double longitude_deg;
    double latitude_deg;
    double distance_au;
    double light_time_days;
    std::string source_path;
};

struct ApparentChart {
    std::vector<ApparentChartBody> bodies;
};

struct ApparentChartScratch {
    std::vector<ApparentChartBody> bodies;
};

struct ComputeApparentStepData {
    const BodySpec* bodies;
    size_t body_count;
    std::string opm4_root;
    double jd_tdb;
};

struct WeightedBlockData {
    const CompiledEphemerisBlock* first;
    const CompiledEphemerisBlock* second;
    double first_weight;
    double second_weight;
};

bool directory_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string join_path(const std::string& lhs, const std::string& rhs) {
    if (lhs.empty()) return rhs;
    if (rhs.empty()) return lhs;
    return lhs[lhs.size() - 1] == '/' ? lhs + rhs : lhs + "/" + rhs;
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
    const double start = std::strtod(start_text.c_str(), &end_ptr);
    if (!end_ptr || *end_ptr != '\0') {
        return false;
    }
    const std::string end_text = name.substr(last_us + 1);
    const double end = std::strtod(end_text.c_str(), &end_ptr);
    if (!end_ptr || *end_ptr != '\0' || !(end > start)) {
        return false;
    }

    *out_start = start;
    *out_end = end;
    return true;
}

void collect_files_recursive(const std::string& root, std::vector<std::string>* out) {
    if (!out) return;
    DIR* dir = opendir(root.c_str());
    if (!dir) return;

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

bool build_opm4_index(const std::string& root, std::vector<Opm4FileEntry>* out) {
    if (!out) return false;
    out->clear();
    if (!directory_exists(root)) {
        return false;
    }

    std::vector<std::string> paths;
    collect_files_recursive(root, &paths);
    for (size_t i = 0; i < paths.size(); ++i) {
        const std::string& path = paths[i];
        if (!has_suffix(path, ".opm4") && !has_suffix(path, ".bin")) {
            continue;
        }
        if (basename_of(path).find("ref") != std::string::npos) {
            continue;
        }
        double start = 0.0;
        double end = 0.0;
        if (!parse_file_jd_range(path, &start, &end)) {
            continue;
        }
        Opm4FileEntry entry;
        entry.path = path;
        entry.jd_tdb_start = start;
        entry.jd_tdb_end = end;
        out->push_back(entry);
    }

    std::sort(out->begin(), out->end(), [](const Opm4FileEntry& a, const Opm4FileEntry& b) {
        if (a.jd_tdb_start != b.jd_tdb_start) return a.jd_tdb_start < b.jd_tdb_start;
        return a.path < b.path;
    });
    return !out->empty();
}

const Opm4FileEntry* find_opm4_file(const std::vector<Opm4FileEntry>& files, double jd_tdb) {
    const double required_start = jd_tdb - SAMPLE_HALF_WINDOW_DAYS;
    const double required_end = jd_tdb + SAMPLE_HALF_WINDOW_DAYS;
    for (size_t i = 0; i < files.size(); ++i) {
        const Opm4FileEntry& entry = files[i];
        if (entry.jd_tdb_start <= required_start + 1.0e-9
            && entry.jd_tdb_end >= required_end - 1.0e-9) {
            return &entry;
        }
    }
    return 0;
}

bool read_opm4_header(const std::string& path, OPM4Header* out) {
    std::vector<uint8_t> bytes;
    return out
        && load_ephemeris_file_bytes(path, &bytes)
        && !bytes.empty()
        && parse_opm4_header(&bytes[0], bytes.size(), out);
}

Vector3 zero_vector() {
    Vector3 out;
    out.x = 0.0;
    out.y = 0.0;
    out.z = 0.0;
    return out;
}

bool zero_position(double, const void*, Vector3* out) {
    if (!out) return false;
    *out = zero_vector();
    return true;
}

bool zero_velocity(double, const void*, Vector3* out) {
    if (!out) return false;
    *out = zero_vector();
    return true;
}

bool zero_acceleration(double, const void*, Vector3* out) {
    if (!out) return false;
    *out = zero_vector();
    return true;
}

bool weighted_position(double jd_tdb, const void* data, Vector3* out) {
    const WeightedBlockData* weighted = static_cast<const WeightedBlockData*>(data);
    if (!weighted || !weighted->first || !weighted->second || !out) {
        return false;
    }
    Vector3 first;
    Vector3 second;
    if (!eval_compiled_ephemeris_block_position(jd_tdb, weighted->first, &first)
        || !eval_compiled_ephemeris_block_position(jd_tdb, weighted->second, &second)) {
        return false;
    }
    *out = vector3_add(vector3_scale(first, weighted->first_weight), vector3_scale(second, weighted->second_weight));
    return true;
}

bool weighted_velocity(double jd_tdb, const void* data, Vector3* out) {
    const WeightedBlockData* weighted = static_cast<const WeightedBlockData*>(data);
    if (!weighted || !weighted->first || !weighted->second || !out) {
        return false;
    }
    Vector3 first;
    Vector3 second;
    if (!eval_compiled_ephemeris_block_velocity(jd_tdb, weighted->first, &first)
        || !eval_compiled_ephemeris_block_velocity(jd_tdb, weighted->second, &second)) {
        return false;
    }
    *out = vector3_add(vector3_scale(first, weighted->first_weight), vector3_scale(second, weighted->second_weight));
    return true;
}

bool weighted_acceleration(double jd_tdb, const void* data, Vector3* out) {
    const WeightedBlockData* weighted = static_cast<const WeightedBlockData*>(data);
    if (!weighted || !weighted->first || !weighted->second || !out) {
        return false;
    }
    Vector3 first;
    Vector3 second;
    if (!eval_compiled_ephemeris_block_acceleration(jd_tdb, weighted->first, &first)
        || !eval_compiled_ephemeris_block_acceleration(jd_tdb, weighted->second, &second)) {
        return false;
    }
    *out = vector3_add(vector3_scale(first, weighted->first_weight), vector3_scale(second, weighted->second_weight));
    return true;
}

CompiledEphemerisBlock make_zero_block() {
    static const int zero_block_data = 0;
    CompiledEphemerisBlock block;
    block.data = &zero_block_data;
    block.bytes = sizeof(zero_block_data);
    block.position = &zero_position;
    block.velocity = &zero_velocity;
    block.acceleration = &zero_acceleration;
    block.format = EphemerisBlockFormat::FormatUnknown;
    return block;
}

CompiledEphemerisBlock make_weighted_block(const WeightedBlockData* data) {
    CompiledEphemerisBlock block;
    block.data = data;
    block.bytes = sizeof(WeightedBlockData);
    block.position = &weighted_position;
    block.velocity = &weighted_velocity;
    block.acceleration = &weighted_acceleration;
    block.format = EphemerisBlockFormat::FormatUnknown;
    return block;
}

bool compile_opm4_block_for_sample(
    const std::vector<Opm4FileEntry>& files,
    double jd_tdb,
    StorageEphemerisBlock* storage,
    CompiledEphemerisBlock* block,
    OPM4Header* header,
    std::string* out_path
) {
    const Opm4FileEntry* file = find_opm4_file(files, jd_tdb);
    if (!file) {
        return false;
    }
    if (out_path) {
        *out_path = file->path;
    }
    if (header && !read_opm4_header(file->path, header)) {
        return false;
    }
    if (!compile_opm4_ephemeris_block_from_file(
            file->path,
            jd_tdb - SAMPLE_HALF_WINDOW_DAYS,
            jd_tdb + SAMPLE_HALF_WINDOW_DAYS,
            storage)) {
        return false;
    }
    return get_compiled_block_from_storage(storage, 0, block);
}

bool compute_opm4_apparent_true_ecliptic(
    const BodySpec& body,
    const std::string& opm4_root,
    double jd_tdb,
    ApparentChartBody* out,
    std::string* out_status
) {
    if (out_status) *out_status = "ok";

    std::vector<Opm4FileEntry> target_files;
    std::vector<Opm4FileEntry> emb_files;
    std::vector<Opm4FileEntry> moon_files;
    if (!build_opm4_index(join_path(opm4_root, body.opm4_subdir), &target_files)) {
        if (out_status) *out_status = "missing_target_index";
        return false;
    }
    if (!build_opm4_index(join_path(opm4_root, "ear"), &emb_files)) {
        if (out_status) *out_status = "missing_earth_index";
        return false;
    }
    if (!build_opm4_index(join_path(opm4_root, "moon"), &moon_files)) {
        if (out_status) *out_status = "missing_moon_index";
        return false;
    }

    StorageEphemerisBlock target_storage;
    StorageEphemerisBlock emb_storage;
    StorageEphemerisBlock moon_storage;
    CompiledEphemerisBlock target_opm;
    CompiledEphemerisBlock emb_opm;
    CompiledEphemerisBlock moon_geo_opm;
    OPM4Header target_header;
    OPM4Header emb_header;
    OPM4Header moon_header;
    std::string target_path;

    bool ok = compile_opm4_block_for_sample(target_files, jd_tdb, &target_storage, &target_opm, &target_header, &target_path)
        && compile_opm4_block_for_sample(emb_files, jd_tdb, &emb_storage, &emb_opm, &emb_header, 0)
        && compile_opm4_block_for_sample(moon_files, jd_tdb, &moon_storage, &moon_geo_opm, &moon_header, 0);
    if (!ok) {
        destroy_storage_ephemeris_block(&moon_storage);
        destroy_storage_ephemeris_block(&emb_storage);
        destroy_storage_ephemeris_block(&target_storage);
        if (out_status) *out_status = "compile_failed";
        return false;
    }

    if (target_header.target_id != body.target_id || target_header.center_id != body.center_id
        || moon_header.target_id != TAIYIN_BODY_MOON || moon_header.center_id != TAIYIN_BODY_EARTH) {
        destroy_storage_ephemeris_block(&moon_storage);
        destroy_storage_ephemeris_block(&emb_storage);
        destroy_storage_ephemeris_block(&target_storage);
        if (out_status) *out_status = "header_mismatch";
        return false;
    }

    WeightedBlockData earth_data;
    const CompiledEphemerisBlock* observer_block = &emb_opm;
    if (emb_header.target_id == TAIYIN_BODY_EMB && emb_header.center_id == TAIYIN_BODY_SUN) {
        earth_data.first = &emb_opm;
        earth_data.second = &moon_geo_opm;
        earth_data.first_weight = 1.0;
        earth_data.second_weight = -MOON_EARTH_MASS_RATIO / (1.0 + MOON_EARTH_MASS_RATIO);
        observer_block = 0;
    } else if (emb_header.target_id == TAIYIN_BODY_EARTH && emb_header.center_id == TAIYIN_BODY_SUN) {
        observer_block = &emb_opm;
    } else {
        destroy_storage_ephemeris_block(&moon_storage);
        destroy_storage_ephemeris_block(&emb_storage);
        destroy_storage_ephemeris_block(&target_storage);
        if (out_status) *out_status = "earth_header_mismatch";
        return false;
    }

    const CompiledEphemerisBlock earth_block = observer_block ? *observer_block : make_weighted_block(&earth_data);

    WeightedBlockData moon_heliocentric_data;
    const CompiledEphemerisBlock* target_block = &target_opm;
    CompiledEphemerisBlock moon_heliocentric_block;
    if (body.target_id == TAIYIN_BODY_MOON && body.center_id == TAIYIN_BODY_EARTH) {
        if (emb_header.target_id != TAIYIN_BODY_EMB || emb_header.center_id != TAIYIN_BODY_SUN) {
            destroy_storage_ephemeris_block(&moon_storage);
            destroy_storage_ephemeris_block(&emb_storage);
            destroy_storage_ephemeris_block(&target_storage);
            if (out_status) *out_status = "moon_needs_emb";
            return false;
        }
        moon_heliocentric_data.first = &emb_opm;
        moon_heliocentric_data.second = &moon_geo_opm;
        moon_heliocentric_data.first_weight = 1.0;
        moon_heliocentric_data.second_weight = 1.0 / (1.0 + MOON_EARTH_MASS_RATIO);
        moon_heliocentric_block = make_weighted_block(&moon_heliocentric_data);
        target_block = &moon_heliocentric_block;
    }

    const CompiledEphemerisBlock zero_sun_block = make_zero_block();
    const int deflector_ids[] = { TAIYIN_BODY_SUN };
    const CompiledEphemerisBlock* deflectors[] = { &zero_sun_block };
    const double schwarzschild[] = { TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU };
    const double deflection_limit[] = { TAIYIN_SOLAR_DEFLECTION_LIMIT };
    const uint32_t flags = TAIYIN_APPARENT_LIGHT_TIME
        | TAIYIN_APPARENT_ABERRATION
        | TAIYIN_APPARENT_DEFLECTION
        | TAIYIN_APPARENT_SPHERICAL;

    const double jd_tt = tdb_to_tt_jd(jd_tdb, TdbModel::FastPeriodic);
    double lon_rad = 0.0;
    double lat_rad = 0.0;
    double distance_au = 0.0;
    double light_time_days = 0.0;
    ok = taiyin_calc_apparent_flat(
        jd_tdb,
        jd_tt,
        body.target_id,
        target_block,
        TAIYIN_BODY_EARTH,
        &earth_block,
        0, 0, 0,
        1,
        0,
        deflector_ids,
        deflectors,
        schwarzschild,
        deflection_limit,
        flags,
        TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
        0, 0, 0, 0,
        dispatch::PRECESSION_IAU2006,
        dispatch::NUTATION_IAU2000B,
        0,
        8,
        1.0e-13,
        1.0e-3,
        0, 0, 0,
        0, 0, 0,
        0, 0, 0,
        0, 0, 0,
        0, 0, 0,
        &lon_rad,
        &lat_rad,
        &distance_au,
        0, 0, 0,
        0, 0, 0,
        &light_time_days,
        0, 0, 0);

    if (ok && out) {
        out->name = body.name;
        out->target_id = body.target_id;
        out->observer_id = TAIYIN_BODY_EARTH;
        out->longitude_deg = lon_rad * TAIYIN_RAD_TO_DEG;
        out->latitude_deg = lat_rad * TAIYIN_RAD_TO_DEG;
        out->distance_au = distance_au;
        out->light_time_days = light_time_days;
        out->source_path = target_path;
    }
    if (!ok && out_status) {
        *out_status = "apparent_failed";
    }

    destroy_storage_ephemeris_block(&moon_storage);
    destroy_storage_ephemeris_block(&emb_storage);
    destroy_storage_ephemeris_block(&target_storage);
    return ok;
}

bool compute_apparent_step(PipelineFrame* frame, void* step_data) {
    ApparentChartScratch* scratch = static_cast<ApparentChartScratch*>(frame ? frame->scratch : 0);
    const ComputeApparentStepData* data = static_cast<const ComputeApparentStepData*>(step_data);
    if (!scratch || !data || !data->bodies || data->body_count == 0 || data->opm4_root.empty()) {
        return false;
    }

    scratch->bodies.clear();
    for (size_t i = 0; i < data->body_count; ++i) {
        ApparentChartBody body;
        std::string status;
        if (!compute_opm4_apparent_true_ecliptic(data->bodies[i], data->opm4_root, data->jd_tdb, &body, &status)) {
            std::cerr << "warning: skipped " << data->bodies[i].name << ": " << status << "\n";
            continue;
        }
        scratch->bodies.push_back(body);
    }
    return !scratch->bodies.empty();
}

bool write_chart_step(PipelineFrame* frame, void*) {
    ApparentChart* chart = static_cast<ApparentChart*>(frame ? frame->chart : 0);
    ApparentChartScratch* scratch = static_cast<ApparentChartScratch*>(frame ? frame->scratch : 0);
    if (!chart || !scratch) {
        return false;
    }
    chart->bodies = scratch->bodies;
    return true;
}

const char* get_opm4_root(int argc, char** argv) {
    if (argc > 1 && argv[1] && argv[1][0] != '\0') {
        return argv[1];
    }
    return std::getenv("TAIYIN_OPM4_ROOT");
}

double get_jd_tdb(int argc, char** argv) {
    if (argc > 2 && argv[2] && argv[2][0] != '\0') {
        return std::strtod(argv[2], 0);
    }
    return 2460310.500800740905;
}

void print_usage(const char* program) {
    std::cout << "usage: " << program << " /path/to/data_integrated_opm4 [jd_tdb]\n"
              << "   or: TAIYIN_OPM4_ROOT=/path/to/data_integrated_opm4 " << program << " [ignored] [jd_tdb]\n";
}

}  // namespace

int main(int argc, char** argv) {
    const char* opm4_root = get_opm4_root(argc, argv);
    if (!opm4_root) {
        print_usage(argv[0]);
        return 0;
    }

    const double jd_tdb = get_jd_tdb(argc, argv);
    BodySpec bodies[] = {
        { "Mercury", "mer", TAIYIN_BODY_MERCURY_BARYCENTER, TAIYIN_BODY_SUN },
        { "Venus", "ven", TAIYIN_BODY_VENUS_BARYCENTER, TAIYIN_BODY_SUN },
        { "Mars", "mar", TAIYIN_BODY_MARS_BARYCENTER, TAIYIN_BODY_SUN },
        { "Jupiter", "jup", TAIYIN_BODY_JUPITER_BARYCENTER, TAIYIN_BODY_SUN },
        { "Saturn", "sat", TAIYIN_BODY_SATURN_BARYCENTER, TAIYIN_BODY_SUN },
        { "Uranus", "ura", TAIYIN_BODY_URANUS_BARYCENTER, TAIYIN_BODY_SUN },
        { "Neptune", "nep", TAIYIN_BODY_NEPTUNE_BARYCENTER, TAIYIN_BODY_SUN },
        { "Pluto", "plu", TAIYIN_BODY_PLUTO_BARYCENTER, TAIYIN_BODY_SUN },
        { "Moon", "moon", TAIYIN_BODY_MOON, TAIYIN_BODY_EARTH },
    };

    ComputeApparentStepData compute_data;
    compute_data.bodies = bodies;
    compute_data.body_count = sizeof(bodies) / sizeof(bodies[0]);
    compute_data.opm4_root = opm4_root;
    compute_data.jd_tdb = jd_tdb;

    Pipeline pipeline;
    if (!pipeline.add_step(PipelineStep("compute_apparent_true_ecliptic", compute_apparent_step, &compute_data))
        || !pipeline.add_step(PipelineStep("write_chart", write_chart_step, 0))) {
        std::cerr << "failed to build apparent chart pipeline\n";
        return 1;
    }

    ApparentChart chart;
    ApparentChartScratch scratch;
    PipelineFrame frame;
    frame.chart = &chart;
    frame.scratch = &scratch;

    PipelineRunResult result;
    if (!pipeline.run(&frame, &result)) {
        std::cerr << "apparent chart pipeline failed at step " << result.failed_step_index;
        if (result.failed_step_name) {
            std::cerr << " (" << result.failed_step_name << ")";
        }
        std::cerr << "\n";
        return 1;
    }

    std::cout << std::fixed << std::setprecision(9);
    std::cout << "body,target,observer,longitude_deg,latitude_deg,distance_au,light_time_days,source\n";
    for (size_t i = 0; i < chart.bodies.size(); ++i) {
        const ApparentChartBody& body = chart.bodies[i];
        std::cout << body.name << ","
                  << body.target_id << ","
                  << body.observer_id << ","
                  << body.longitude_deg << ","
                  << body.latitude_deg << ","
                  << body.distance_au << ","
                  << body.light_time_days << ","
                  << body.source_path << "\n";
    }

    return 0;
}
