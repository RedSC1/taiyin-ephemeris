#include "taiyin/angle.h"
#include "taiyin/apparent_position.h"
#include "taiyin/corrections.h"
#include "taiyin/dispatch.h"
#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/ephemeris_file_loader.h"
#include "taiyin/internal/opm4.h"
#include "taiyin/runtime/pipeline.h"
#include "taiyin/time.h"
#include "taiyin/vector3.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

namespace {

using namespace taiyin;
using namespace taiyin::internal;
using namespace taiyin::runtime;

const char* kDefaultOpm4Root = "";
const char* kDefaultSwissPython = "";
const char* kDefaultSwissEphePath = "";

const double kSampleHalfWindowDays = 0.25;
const double kRadToDeg = taiyin::TAIYIN_RAD_TO_DEG;
const double kMoonEarthMassRatio = 0.0123000371;

struct BodySpec {
    const char* label;
    const char* opm4_subdir;
    int target_id;
    int center_id;
    int swiss_id;
};

struct Opm4FileEntry {
    std::string path;
    double jd_tdb_start;
    double jd_tdb_end;
};

struct ChartBody {
    const char* label;
    double lon_deg;
    double lat_deg;
    double distance_au;
    std::string opm_file;
    bool ok;

    ChartBody()
        : label(0), lon_deg(0.0), lat_deg(0.0), distance_au(0.0), opm_file(), ok(false) {}
};

struct BareChart {
    std::vector<ChartBody> bodies;
};

struct BareChartScratch {
    std::vector<ChartBody> bodies;
};

struct ApparentPosition {
    double lon_deg;
    double lat_deg;
    double distance_au;

    ApparentPosition()
        : lon_deg(0.0), lat_deg(0.0), distance_au(0.0) {}
};

struct WeightedBlockData {
    const CompiledEphemerisBlock* first;
    const CompiledEphemerisBlock* second;
    double first_weight;
    double second_weight;
};

struct ComputeApparentStepData {
    const std::vector<BodySpec>* bodies;
    const std::string* opm4_root;
    double jd_tdb;
};

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_size(size_t actual, size_t expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (std::fabs(actual - expected) > tolerance) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

bool file_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool directory_exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string getenv_or_default(const char* name, const char* default_value) {
    const char* value = std::getenv(name);
    return value && value[0] ? std::string(value) : std::string(default_value);
}

std::string join_path(const std::string& lhs, const std::string& rhs) {
    if (lhs.empty()) return rhs;
    if (rhs.empty()) return lhs;
    if (lhs[lhs.size() - 1] == '/') return lhs + rhs;
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

const Opm4FileEntry* find_opm4_file(
    const std::vector<Opm4FileEntry>& files,
    double jd_tdb_start,
    double jd_tdb_end
) {
    for (size_t i = 0; i < files.size(); ++i) {
        const Opm4FileEntry& entry = files[i];
        if (entry.jd_tdb_start <= jd_tdb_start + 1e-9
            && entry.jd_tdb_end >= jd_tdb_end - 1e-9) {
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

Vector3 local_zero_vector() {
    Vector3 out;
    out.x = 0.0;
    out.y = 0.0;
    out.z = 0.0;
    return out;
}

bool zero_position(double, const void*, Vector3* out) {
    if (!out) return false;
    *out = local_zero_vector();
    return true;
}

bool zero_velocity(double, const void*, Vector3* out) {
    if (!out) return false;
    *out = local_zero_vector();
    return true;
}

bool zero_acceleration(double, const void*, Vector3* out) {
    if (!out) return false;
    *out = local_zero_vector();
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
    const Opm4FileEntry* file = find_opm4_file(
        files,
        jd_tdb - kSampleHalfWindowDays,
        jd_tdb + kSampleHalfWindowDays);
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
            jd_tdb - kSampleHalfWindowDays,
            jd_tdb + kSampleHalfWindowDays,
            storage)) {
        return false;
    }
    return get_compiled_block_from_storage(storage, 0, block);
}

bool compute_opm4_apparent(
    const BodySpec& body,
    const std::string& opm4_root,
    double jd_tdb,
    ApparentPosition* out,
    std::string* out_status,
    std::string* out_target_file
) {
    if (out_status) *out_status = "ok";
    if (out_target_file) out_target_file->clear();

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
    if (out_target_file) {
        *out_target_file = target_path;
    }

    if (target_header.target_id != body.target_id || target_header.center_id != body.center_id
        || moon_header.target_id != 301 || moon_header.center_id != 399) {
        destroy_storage_ephemeris_block(&moon_storage);
        destroy_storage_ephemeris_block(&emb_storage);
        destroy_storage_ephemeris_block(&target_storage);
        if (out_status) *out_status = "header_mismatch";
        return false;
    }

    WeightedBlockData earth_data;
    const CompiledEphemerisBlock* observer_block = &emb_opm;
    if (emb_header.target_id == 3 && emb_header.center_id == 10) {
        earth_data.first = &emb_opm;
        earth_data.second = &moon_geo_opm;
        earth_data.first_weight = 1.0;
        earth_data.second_weight = -kMoonEarthMassRatio / (1.0 + kMoonEarthMassRatio);
        observer_block = 0;
    } else if (emb_header.target_id == 399 && emb_header.center_id == 10) {
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
    if (body.target_id == 301 && body.center_id == 399) {
        if (emb_header.target_id != 3 || emb_header.center_id != 10) {
            destroy_storage_ephemeris_block(&moon_storage);
            destroy_storage_ephemeris_block(&emb_storage);
            destroy_storage_ephemeris_block(&target_storage);
            if (out_status) *out_status = "moon_needs_emb";
            return false;
        }
        moon_heliocentric_data.first = &emb_opm;
        moon_heliocentric_data.second = &moon_geo_opm;
        moon_heliocentric_data.first_weight = 1.0;
        moon_heliocentric_data.second_weight = 1.0 / (1.0 + kMoonEarthMassRatio);
        moon_heliocentric_block = make_weighted_block(&moon_heliocentric_data);
        target_block = &moon_heliocentric_block;
    }

    const CompiledEphemerisBlock zero_sun_block = make_zero_block();
    const int deflector_ids[] = { 10 };
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
    ok = taiyin_calc_apparent_flat(
        jd_tdb,
        jd_tt,
        body.target_id,
        target_block,
        399,
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
        0, 0, 0, 0);

    if (ok && out) {
        out->lon_deg = lon_rad * kRadToDeg;
        out->lat_deg = lat_rad * kRadToDeg;
        out->distance_au = distance_au;
    }
    if (!ok && out_status) {
        *out_status = "apparent_failed";
    }

    destroy_storage_ephemeris_block(&moon_storage);
    destroy_storage_ephemeris_block(&emb_storage);
    destroy_storage_ephemeris_block(&target_storage);
    return ok;
}

std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\'') {
            out += "'\\''";
        } else {
            out += value[i];
        }
    }
    out += "'";
    return out;
}

std::string read_command_stdout(const std::string& command) {
    std::string output;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        return output;
    }
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }
    pclose(pipe);
    return output;
}

std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == ',') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(line[i]);
        }
    }
    fields.push_back(current);
    return fields;
}

std::string trim(const std::string& value) {
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

bool parse_double_field(const std::string& text, double* out) {
    if (!out) return false;
    const std::string field = trim(text);
    if (field.empty()) return false;
    char* end_ptr = 0;
    const double value = std::strtod(field.c_str(), &end_ptr);
    if (!end_ptr || end_ptr == field.c_str()) {
        return false;
    }
    while (*end_ptr) {
        if (!std::isspace(static_cast<unsigned char>(*end_ptr))) {
            return false;
        }
        ++end_ptr;
    }
    *out = value;
    return std::isfinite(value);
}

bool parse_swiss_output(const std::string& output, ApparentPosition* out) {
    if (!out) return false;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string> fields = split_csv(line);
        double lon = 0.0;
        double lat = 0.0;
        double dist = 0.0;
        if (fields.size() >= 3
            && parse_double_field(fields[0], &lon)
            && parse_double_field(fields[1], &lat)
            && parse_double_field(fields[2], &dist)) {
            out->lon_deg = lon;
            out->lat_deg = lat;
            out->distance_au = dist;
            return true;
        }
        if (fields.size() >= 4
            && parse_double_field(fields[1], &lon)
            && parse_double_field(fields[2], &lat)
            && parse_double_field(fields[3], &dist)) {
            out->lon_deg = lon;
            out->lat_deg = lat;
            out->distance_au = dist;
            return true;
        }
    }
    return false;
}

bool query_swiss(
    const std::string& python_path,
    const std::string& swiss_ephe_path,
    int swiss_id,
    double jd_tt,
    ApparentPosition* out
) {
    std::ostringstream script;
    script.precision(17);
    script << "import swisseph as swe\n"
        << "swe.set_ephe_path(" << shell_quote(swiss_ephe_path) << ")\n"
        << "flag=swe.FLG_SWIEPH\n"
        << "xx,ret=swe.calc(" << jd_tt << "," << swiss_id << ",flag)\n"
        << "print('%.17g,%.17g,%.17g,%d' % (xx[0],xx[1],xx[2],ret))\n";

    std::ostringstream command;
    command << shell_quote(python_path) << " -c " << shell_quote(script.str()) << " 2>/dev/null";
    const std::string output = read_command_stdout(command.str());
    return parse_swiss_output(output, out);
}

double signed_degree_delta(double lhs, double rhs) {
    double diff = lhs - rhs;
    while (diff > 180.0) diff -= 360.0;
    while (diff < -180.0) diff += 360.0;
    return diff;
}

std::vector<BodySpec> make_bodies() {
    std::vector<BodySpec> bodies;
    bodies.push_back(BodySpec{ "mercury", "mer", 1, 10, 2 });
    bodies.push_back(BodySpec{ "venus", "ven", 2, 10, 3 });
    bodies.push_back(BodySpec{ "mars", "mar", 4, 10, 4 });
    bodies.push_back(BodySpec{ "jupiter", "jup", 5, 10, 5 });
    bodies.push_back(BodySpec{ "saturn", "sat", 6, 10, 6 });
    bodies.push_back(BodySpec{ "uranus", "ura", 7, 10, 7 });
    bodies.push_back(BodySpec{ "neptune", "nep", 8, 10, 8 });
    bodies.push_back(BodySpec{ "pluto", "plu", 9, 10, 9 });
    bodies.push_back(BodySpec{ "moon", "moon", 301, 399, 1 });
    return bodies;
}

bool compute_chart_apparent_step(PipelineFrame* frame, void* step_data) {
    BareChartScratch* scratch = static_cast<BareChartScratch*>(frame ? frame->scratch : 0);
    const ComputeApparentStepData* data = static_cast<const ComputeApparentStepData*>(step_data);
    if (!scratch || !data || !data->bodies || !data->opm4_root) {
        return false;
    }

    scratch->bodies.clear();
    scratch->bodies.reserve(data->bodies->size());
    for (size_t i = 0; i < data->bodies->size(); ++i) {
        const BodySpec& spec = (*data->bodies)[i];
        ApparentPosition apparent;
        std::string status;
        std::string path;
        ChartBody body;
        body.label = spec.label;
        body.ok = compute_opm4_apparent(spec, *data->opm4_root, data->jd_tdb, &apparent, &status, &path);
        body.lon_deg = apparent.lon_deg;
        body.lat_deg = apparent.lat_deg;
        body.distance_au = apparent.distance_au;
        body.opm_file = path;
        if (!body.ok) {
            std::cerr << "OPM4 apparent pipeline failed for " << spec.label << ": " << status << "\n";
            return false;
        }
        scratch->bodies.push_back(body);
    }
    return true;
}

bool write_chart_step(PipelineFrame* frame, void*) {
    BareChart* chart = static_cast<BareChart*>(frame ? frame->chart : 0);
    BareChartScratch* scratch = static_cast<BareChartScratch*>(frame ? frame->scratch : 0);
    if (!chart || !scratch) {
        return false;
    }
    chart->bodies = scratch->bodies;
    return true;
}

void run_epoch(
    const std::vector<BodySpec>& bodies,
    const std::string& opm4_root,
    const std::string& swiss_python,
    const std::string& swiss_ephe_path,
    double jd_tdb,
    int* failures
) {
    const double jd_tt = tdb_to_tt_jd(jd_tdb, TdbModel::FastPeriodic);
    ComputeApparentStepData compute_data;
    compute_data.bodies = &bodies;
    compute_data.opm4_root = &opm4_root;
    compute_data.jd_tdb = jd_tdb;

    BareChart chart;
    BareChartScratch scratch;
    PipelineFrame frame;
    frame.chart = &chart;
    frame.scratch = &scratch;

    Pipeline pipeline;
    expect_true(pipeline.add_step(PipelineStep("compute_apparent_chart", compute_chart_apparent_step, &compute_data)),
                "add compute apparent chart step", failures);
    expect_true(pipeline.add_step(PipelineStep("write_chart", write_chart_step, 0)),
                "add write chart step", failures);

    PipelineRunResult run_result;
    expect_true(pipeline.run(&frame, &run_result), "pipeline apparent chart run", failures);
    expect_size(chart.bodies.size(), bodies.size(), "pipeline apparent chart body count", failures);
    if (chart.bodies.size() != bodies.size()) {
        return;
    }

    std::printf("\njd_tdb=%.12f jd_tt=%.12f\n", jd_tdb, jd_tt);
    std::printf("body,dlon_arcsec,dlat_arcsec,pipeline_lon_deg,swiss_lon_deg,opm_file,status\n");
    for (size_t i = 0; i < bodies.size(); ++i) {
        ApparentPosition swiss;
        const bool swiss_ok = query_swiss(swiss_python, swiss_ephe_path, bodies[i].swiss_id, jd_tt, &swiss);
        expect_true(swiss_ok, "Swiss apparent query", failures);
        if (!swiss_ok) {
            std::printf("%s,,,,,,swiss_failed\n", bodies[i].label);
            continue;
        }

        const double dlon = signed_degree_delta(chart.bodies[i].lon_deg, swiss.lon_deg) * 3600.0;
        const double dlat = (chart.bodies[i].lat_deg - swiss.lat_deg) * 3600.0;
        std::printf(
            "%s,%.9f,%.9f,%.12f,%.12f,%s,ok\n",
            bodies[i].label,
            dlon,
            dlat,
            chart.bodies[i].lon_deg,
            swiss.lon_deg,
            chart.bodies[i].opm_file.c_str());

        expect_near(dlon, 0.0, 0.002, "pipeline apparent longitude vs Swiss", failures);
        expect_near(dlat, 0.0, 0.002, "pipeline apparent latitude vs Swiss", failures);
    }
}

}  // namespace

int main() {
    int failures = 0;
    const std::string opm4_root = getenv_or_default("TAIYIN_OPM4_ROOT", kDefaultOpm4Root);
    const std::string swiss_python = getenv_or_default("TAIYIN_SWISS_PYTHON", kDefaultSwissPython);
    const std::string swiss_ephe_path = getenv_or_default("TAIYIN_SWISS_EPHE", kDefaultSwissEphePath);

    if (!directory_exists(opm4_root)) {
        std::printf("skip test: OPM4 root not available: %s\n", opm4_root.c_str());
        return 0;
    }
    if (!file_exists(swiss_python)) {
        std::printf("skip test: Python Swiss Ephemeris not available: %s\n", swiss_python.c_str());
        return 0;
    }
    if (!directory_exists(swiss_ephe_path)) {
        std::printf("skip test: Swiss ephemeris path not available: %s\n", swiss_ephe_path.c_str());
        return 0;
    }

    const std::vector<BodySpec> bodies = make_bodies();
    const double epochs[] = {
        2451550.0,
        2460310.50080074091,
    };
    for (size_t i = 0; i < sizeof(epochs) / sizeof(epochs[0]); ++i) {
        run_epoch(bodies, opm4_root, swiss_python, swiss_ephe_path, epochs[i], &failures);
    }

    if (failures == 0) {
        std::cout << "test_pipeline_opm4_vs_swiss: ALL TESTS PASSED\n";
        return 0;
    }
    std::cerr << failures << " pipeline OPM4 Swiss oracle test failure(s)\n";
    return 1;
}
