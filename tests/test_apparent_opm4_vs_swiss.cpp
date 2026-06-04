#include "taiyin/angle.h"
#include "taiyin/apparent_position.h"
#include "taiyin/corrections.h"
#include "taiyin/dispatch.h"
#include "taiyin/geometry.h"
#include "taiyin/internal/ephemeris_block.h"
#include "taiyin/internal/ephemeris_file_loader.h"
#include "taiyin/internal/opm4.h"
#include "taiyin/time.h"
#include "taiyin/vector3.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace taiyin;
using namespace taiyin::internal;

namespace {

const char* kDefaultOpm4Root = "";
const char* kDefaultSwissPython = "";
const char* kDefaultSwissEphePath = "";

const double kSampleHalfWindowDays = 0.25;
const double kSecondsPerDay = taiyin::SECONDS_PER_DAY;
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

struct ApparentPosition {
    double lon_deg;
    double lat_deg;
    double distance_au;

    ApparentPosition()
        : lon_deg(0.0), lat_deg(0.0), distance_au(0.0) {}
};

struct EquatorialPosition {
    double ra_deg;
    double dec_deg;
    double distance_au;

    EquatorialPosition()
        : ra_deg(0.0), dec_deg(0.0), distance_au(0.0) {}
};

struct CompareStats {
    double max_abs_dlon_se1_arcsec;
    double max_abs_dlat_se1_arcsec;
    double max_abs_ddist_se1_au;
    double max_abs_dlon_moshier_arcsec;
    double max_abs_dlat_moshier_arcsec;
    double max_abs_ddist_moshier_au;
    int samples;

    CompareStats()
        : max_abs_dlon_se1_arcsec(0.0),
          max_abs_dlat_se1_arcsec(0.0),
          max_abs_ddist_se1_au(0.0),
          max_abs_dlon_moshier_arcsec(0.0),
          max_abs_dlat_moshier_arcsec(0.0),
          max_abs_ddist_moshier_au(0.0),
          samples(0) {}
};

struct WeightedBlockData {
    const CompiledEphemerisBlock* first;
    const CompiledEphemerisBlock* second;
    double first_weight;
    double second_weight;
};

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

double tt_to_tdb_jd_fast(double jd_tt) {
    const double t = (jd_tt - taiyin::JD_J2000) / taiyin::DAYS_PER_JULIAN_CENTURY;
    const double tdb_minus_tt_seconds =
        0.001657 * std::sin(628.3076 * t + 6.2401)
        + 0.000022 * std::sin(575.3385 * t + 4.2970)
        + 0.000014 * std::sin(1256.6152 * t + 6.1969)
        + 0.000005 * std::sin(606.9777 * t + 4.0212)
        + 0.000005 * std::sin(52.9691 * t + 0.4444)
        + 0.000002 * std::sin(21.3299 * t + 5.5431)
        + 0.000010 * t * std::sin(628.3076 * t + 4.2490);
    return jd_tt + tdb_minus_tt_seconds / kSecondsPerDay;
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

bool equatorial_ra_dec_degrees(const Vector3& position, EquatorialPosition* out) {
    if (!out) {
        return false;
    }
    EquatorialPositionVelocityAcceleration equatorial;
    const Vector3 zero = local_zero_vector();
    if (!cartesian_position_velocity_acceleration_to_equatorial(position, zero, zero, &equatorial)) {
        return false;
    }
    out->ra_deg = taiyin::normalize_degrees(equatorial.right_ascension_rad * kRadToDeg);
    out->dec_deg = equatorial.declination_rad * kRadToDeg;
    out->distance_au = equatorial.distance_au;
    return true;
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
    bool moshier,
    bool no_deflection,
    ApparentPosition* out
) {
    std::ostringstream script;
    script.precision(17);
    script << "import swisseph as swe\n"
        << "swe.set_ephe_path(" << shell_quote(swiss_ephe_path) << ")\n"
        << "flag=" << (moshier ? "swe.FLG_MOSEPH" : "swe.FLG_SWIEPH") << "\n"
        << (no_deflection ? "flag |= swe.FLG_NOGDEFL\n" : "")
        << "xx,ret=swe.calc(" << jd_tt << "," << swiss_id << ",flag)\n"
        << "print('%.17g,%.17g,%.17g,%d' % (xx[0],xx[1],xx[2],ret))\n";

    std::ostringstream command;
    command << shell_quote(python_path) << " -c " << shell_quote(script.str()) << " 2>/dev/null";

    const std::string output = read_command_stdout(command.str());
    return parse_swiss_output(output, out);
}

bool query_swiss_equatorial(
    const std::string& python_path,
    const std::string& swiss_ephe_path,
    int swiss_id,
    double jd_tt,
    bool moshier,
    EquatorialPosition* out
) {
    ApparentPosition parsed;
    std::ostringstream script;
    script.precision(17);
    script << "import swisseph as swe\n"
        << "swe.set_ephe_path(" << shell_quote(swiss_ephe_path) << ")\n"
        << "flag=" << (moshier ? "swe.FLG_MOSEPH" : "swe.FLG_SWIEPH") << " | swe.FLG_EQUATORIAL\n"
        << "xx,ret=swe.calc(" << jd_tt << "," << swiss_id << ",flag)\n"
        << "print('%.17g,%.17g,%.17g,%d' % (xx[0],xx[1],xx[2],ret))\n";

    std::ostringstream command;
    command << shell_quote(python_path) << " -c " << shell_quote(script.str()) << " 2>/dev/null";

    const std::string output = read_command_stdout(command.str());
    if (!parse_swiss_output(output, &parsed) || !out) {
        return false;
    }
    out->ra_deg = parsed.lon_deg;
    out->dec_deg = parsed.lat_deg;
    out->distance_au = parsed.distance_au;
    return true;
}

double signed_degree_delta(double lhs, double rhs) {
    double diff = lhs - rhs;
    while (diff > 180.0) diff -= 360.0;
    while (diff < -180.0) diff += 360.0;
    return diff;
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
    bool use_deflection,
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

    if (target_header.target_id != body.target_id || target_header.center_id != body.center_id) {
        destroy_storage_ephemeris_block(&moon_storage);
        destroy_storage_ephemeris_block(&emb_storage);
        destroy_storage_ephemeris_block(&target_storage);
        if (out_status) *out_status = "target_header_mismatch";
        return false;
    }
    if (moon_header.target_id != 301 || moon_header.center_id != 399) {
        destroy_storage_ephemeris_block(&moon_storage);
        destroy_storage_ephemeris_block(&emb_storage);
        destroy_storage_ephemeris_block(&target_storage);
        if (out_status) *out_status = "moon_header_mismatch";
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
    uint32_t flags = TAIYIN_APPARENT_LIGHT_TIME
        | TAIYIN_APPARENT_ABERRATION
        | TAIYIN_APPARENT_SPHERICAL;
    if (use_deflection) {
        flags |= TAIYIN_APPARENT_DEFLECTION;
    }

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

bool compute_opm4_mars_horizons_oracle_equatorial(
    const std::string& opm4_root,
    double jd_tt,
    EquatorialPosition* out,
    std::string* out_status,
    std::string* out_target_file
) {
    if (out_status) *out_status = "ok";
    if (out_target_file) out_target_file->clear();

    const double jd_tdb = tt_to_tdb_jd_fast(jd_tt);
    std::vector<Opm4FileEntry> mars_files;
    std::vector<Opm4FileEntry> emb_files;
    std::vector<Opm4FileEntry> moon_files;
    if (!build_opm4_index(join_path(opm4_root, "mar"), &mars_files)) {
        if (out_status) *out_status = "missing_mars_index";
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

    StorageEphemerisBlock mars_storage;
    StorageEphemerisBlock emb_storage;
    StorageEphemerisBlock moon_storage;
    CompiledEphemerisBlock mars_opm;
    CompiledEphemerisBlock emb_opm;
    CompiledEphemerisBlock moon_geo_opm;
    OPM4Header mars_header;
    OPM4Header emb_header;
    OPM4Header moon_header;
    std::string target_path;

    bool ok = compile_opm4_block_for_sample(mars_files, jd_tdb, &mars_storage, &mars_opm, &mars_header, &target_path)
        && compile_opm4_block_for_sample(emb_files, jd_tdb, &emb_storage, &emb_opm, &emb_header, 0)
        && compile_opm4_block_for_sample(moon_files, jd_tdb, &moon_storage, &moon_geo_opm, &moon_header, 0);
    if (!ok) {
        destroy_storage_ephemeris_block(&moon_storage);
        destroy_storage_ephemeris_block(&emb_storage);
        destroy_storage_ephemeris_block(&mars_storage);
        if (out_status) *out_status = "compile_failed";
        return false;
    }
    if (out_target_file) {
        *out_target_file = target_path;
    }

    if (mars_header.target_id != 4 || mars_header.center_id != 10) {
        destroy_storage_ephemeris_block(&moon_storage);
        destroy_storage_ephemeris_block(&emb_storage);
        destroy_storage_ephemeris_block(&mars_storage);
        if (out_status) *out_status = "mars_header_mismatch";
        return false;
    }
    if (moon_header.target_id != 301 || moon_header.center_id != 399) {
        destroy_storage_ephemeris_block(&moon_storage);
        destroy_storage_ephemeris_block(&emb_storage);
        destroy_storage_ephemeris_block(&mars_storage);
        if (out_status) *out_status = "moon_header_mismatch";
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
    } else if (emb_header.target_id != 399 || emb_header.center_id != 10) {
        destroy_storage_ephemeris_block(&moon_storage);
        destroy_storage_ephemeris_block(&emb_storage);
        destroy_storage_ephemeris_block(&mars_storage);
        if (out_status) *out_status = "earth_header_mismatch";
        return false;
    }

    const CompiledEphemerisBlock earth_block = observer_block ? *observer_block : make_weighted_block(&earth_data);
    const CompiledEphemerisBlock zero_sun_block = make_zero_block();
    const int deflector_ids[] = { 10 };
    const CompiledEphemerisBlock* deflectors[] = { &zero_sun_block };
    const double schwarzschild[] = { TAIYIN_SOLAR_SCHWARZSCHILD_RADIUS_AU };
    const double deflection_limit[] = { TAIYIN_SOLAR_DEFLECTION_LIMIT };
    double apparent_pos[3];
    ok = taiyin_calc_apparent_flat(
        jd_tdb,
        jd_tt,
        4,
        &mars_opm,
        399,
        &earth_block,
        0, 0, 0,
        1,
        0,
        deflector_ids,
        deflectors,
        schwarzschild,
        deflection_limit,
        TAIYIN_APPARENT_LIGHT_TIME
            | TAIYIN_APPARENT_SHAPIRO_DELAY
            | TAIYIN_APPARENT_DEFLECTION
            | TAIYIN_APPARENT_ABERRATION,
        TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE,
        0, 0, 0, 0,
        dispatch::PRECESSION_VONDRAK2011,
        dispatch::NUTATION_IAU2000B,
        0,
        8,
        1.0e-14,
        1.0e-3,
        0, 0, 0,
        0, 0, 0,
        0, 0, 0,
        0, 0, 0,
        apparent_pos,
        0, 0,
        0, 0, 0,
        0, 0, 0,
        0, 0, 0,
        0, 0, 0, 0);

    if (ok && out) {
        const Vector3 apparent_vector = { apparent_pos[0], apparent_pos[1], apparent_pos[2] };
        ok = equatorial_ra_dec_degrees(apparent_vector, out);
    }
    if (!ok && out_status) {
        *out_status = "apparent_failed";
    }

    destroy_storage_ephemeris_block(&moon_storage);
    destroy_storage_ephemeris_block(&emb_storage);
    destroy_storage_ephemeris_block(&mars_storage);
    return ok;
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

void update_stats(
    CompareStats* stats,
    double dlon_se1,
    double dlat_se1,
    double ddist_se1,
    double dlon_moshier,
    double dlat_moshier,
    double ddist_moshier
) {
    if (!stats) return;
    stats->max_abs_dlon_se1_arcsec = std::max(stats->max_abs_dlon_se1_arcsec, std::abs(dlon_se1));
    stats->max_abs_dlat_se1_arcsec = std::max(stats->max_abs_dlat_se1_arcsec, std::abs(dlat_se1));
    stats->max_abs_ddist_se1_au = std::max(stats->max_abs_ddist_se1_au, std::abs(ddist_se1));
    stats->max_abs_dlon_moshier_arcsec = std::max(stats->max_abs_dlon_moshier_arcsec, std::abs(dlon_moshier));
    stats->max_abs_dlat_moshier_arcsec = std::max(stats->max_abs_dlat_moshier_arcsec, std::abs(dlat_moshier));
    stats->max_abs_ddist_moshier_au = std::max(stats->max_abs_ddist_moshier_au, std::abs(ddist_moshier));
    ++stats->samples;
}

void print_mars_horizons_oracle_comparison(
    const std::string& opm4_root,
    const std::string& swiss_python,
    const std::string& swiss_ephe_path
) {
    const double jd_tt = 2460310.50080074091;
    const double nasa_ra_deg = 267.054418704;
    const double nasa_dec_deg = -23.961388660;

    EquatorialPosition swiss_se1;
    EquatorialPosition swiss_moshier;
    EquatorialPosition opm4_mars;
    std::string opm4_status;
    std::string opm4_file;
    const bool swiss_se1_ok = query_swiss_equatorial(swiss_python, swiss_ephe_path, 4, jd_tt, false, &swiss_se1);
    const bool swiss_moshier_ok = query_swiss_equatorial(swiss_python, swiss_ephe_path, 4, jd_tt, true, &swiss_moshier);
    const bool opm4_ok = compute_opm4_mars_horizons_oracle_equatorial(opm4_root, jd_tt, &opm4_mars, &opm4_status, &opm4_file);

    std::printf("\nMars apparent true-equator RA/Dec vs NASA Horizons oracle\n");
    std::printf("jd_tt=%.12f nasa_ra=%.12f nasa_dec=%.12f\n", jd_tt, nasa_ra_deg, nasa_dec_deg);
    std::printf("source,dRA_arcsec,dDec_arcsec,ra_deg,dec_deg,distance_au,status\n");
    if (swiss_se1_ok) {
        std::printf(
            "swiss_se1,%.9f,%.9f,%.12f,%.12f,%.12f,ok\n",
            signed_degree_delta(swiss_se1.ra_deg, nasa_ra_deg) * 3600.0,
            (swiss_se1.dec_deg - nasa_dec_deg) * 3600.0,
            swiss_se1.ra_deg,
            swiss_se1.dec_deg,
            swiss_se1.distance_au);
    } else {
        std::printf("swiss_se1,,,,,,swiss_failed\n");
    }
    if (swiss_moshier_ok) {
        std::printf(
            "swiss_moshier,%.9f,%.9f,%.12f,%.12f,%.12f,ok\n",
            signed_degree_delta(swiss_moshier.ra_deg, nasa_ra_deg) * 3600.0,
            (swiss_moshier.dec_deg - nasa_dec_deg) * 3600.0,
            swiss_moshier.ra_deg,
            swiss_moshier.dec_deg,
            swiss_moshier.distance_au);
    } else {
        std::printf("swiss_moshier,,,,,,swiss_failed\n");
    }
    if (opm4_ok) {
        std::printf(
            "taiyin_opm4,%.9f,%.9f,%.12f,%.12f,%.12f,ok,%s\n",
            signed_degree_delta(opm4_mars.ra_deg, nasa_ra_deg) * 3600.0,
            (opm4_mars.dec_deg - nasa_dec_deg) * 3600.0,
            opm4_mars.ra_deg,
            opm4_mars.dec_deg,
            opm4_mars.distance_au,
            opm4_file.c_str());
    } else {
        std::printf("taiyin_opm4,,,,,,%s,%s\n", opm4_status.c_str(), opm4_file.c_str());
    }
}

}  // namespace

int main() {
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

    const double epochs[] = {
        2451550.0,
        2460310.50080074091,
    };
    std::vector<BodySpec> bodies = make_bodies();
    std::vector<CompareStats> stats(bodies.size());

    std::printf("OPM4 apparent true-ecliptic geocentric vs Swiss SE1/Moshier\n");
    std::printf("opm4_root=%s\n", opm4_root.c_str());
    std::printf("swiss_python=%s\n", swiss_python.c_str());

    print_mars_horizons_oracle_comparison(opm4_root, swiss_python, swiss_ephe_path);

    for (size_t epoch_index = 0; epoch_index < sizeof(epochs) / sizeof(epochs[0]); ++epoch_index) {
        const double jd_tdb = epochs[epoch_index];
        const double jd_tt = tdb_to_tt_jd(jd_tdb, TdbModel::FastPeriodic);
        std::printf("\njd_tdb=%.12f jd_tt=%.12f\n", jd_tdb, jd_tt);
        std::printf("body,full_minus_swiss_dlon_as,full_minus_swiss_dlat_as,nodefl_minus_swiss_nodefl_dlon_as,nodefl_minus_swiss_nodefl_dlat_as,taiyin_defl_dlon_as,swiss_defl_dlon_as,dlon_moshier_arcsec,dlat_moshier_arcsec,opm_file,status\n");

        for (size_t i = 0; i < bodies.size(); ++i) {
            ApparentPosition opm4_full;
            ApparentPosition opm4_no_deflection;
            ApparentPosition swiss_se1;
            ApparentPosition swiss_se1_no_deflection;
            ApparentPosition swiss_moshier;
            std::string status;
            std::string target_file;
            const bool opm_full_ok = compute_opm4_apparent(bodies[i], opm4_root, jd_tdb, true, &opm4_full, &status, &target_file);
            const bool opm_no_deflection_ok = compute_opm4_apparent(bodies[i], opm4_root, jd_tdb, false, &opm4_no_deflection, &status, 0);
            const bool se1_ok = query_swiss(swiss_python, swiss_ephe_path, bodies[i].swiss_id, jd_tt, false, false, &swiss_se1);
            const bool se1_no_deflection_ok = query_swiss(swiss_python, swiss_ephe_path, bodies[i].swiss_id, jd_tt, false, true, &swiss_se1_no_deflection);
            const bool moshier_ok = query_swiss(swiss_python, swiss_ephe_path, bodies[i].swiss_id, jd_tt, true, false, &swiss_moshier);
            if (!opm_full_ok || !opm_no_deflection_ok || !se1_ok || !se1_no_deflection_ok || !moshier_ok) {
                std::printf(
                    "%s,,,,,,,,,%s,%s%s%s%s%s\n",
                    bodies[i].label,
                    target_file.c_str(),
                    status.c_str(),
                    opm_no_deflection_ok ? "" : ";opm_nodefl_failed",
                    se1_ok ? "" : ";swiss_se1_failed",
                    se1_no_deflection_ok ? "" : ";swiss_se1_nodefl_failed",
                    moshier_ok ? "" : ";swiss_moshier_failed");
                continue;
            }

            const double dlon_se1 = signed_degree_delta(opm4_full.lon_deg, swiss_se1.lon_deg) * 3600.0;
            const double dlat_se1 = (opm4_full.lat_deg - swiss_se1.lat_deg) * 3600.0;
            const double ddist_se1 = opm4_full.distance_au - swiss_se1.distance_au;
            const double dlon_nodefl = signed_degree_delta(opm4_no_deflection.lon_deg, swiss_se1_no_deflection.lon_deg) * 3600.0;
            const double dlat_nodefl = (opm4_no_deflection.lat_deg - swiss_se1_no_deflection.lat_deg) * 3600.0;
            const double taiyin_defl_dlon = signed_degree_delta(opm4_full.lon_deg, opm4_no_deflection.lon_deg) * 3600.0;
            const double swiss_defl_dlon = signed_degree_delta(swiss_se1.lon_deg, swiss_se1_no_deflection.lon_deg) * 3600.0;
            const double dlon_moshier = signed_degree_delta(opm4_full.lon_deg, swiss_moshier.lon_deg) * 3600.0;
            const double dlat_moshier = (opm4_full.lat_deg - swiss_moshier.lat_deg) * 3600.0;
            const double ddist_moshier = opm4_full.distance_au - swiss_moshier.distance_au;
            update_stats(&stats[i], dlon_se1, dlat_se1, ddist_se1, dlon_moshier, dlat_moshier, ddist_moshier);

            std::printf(
                "%s,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%.9f,%s,ok\n",
                bodies[i].label,
                dlon_se1,
                dlat_se1,
                dlon_nodefl,
                dlat_nodefl,
                taiyin_defl_dlon,
                swiss_defl_dlon,
                dlon_moshier,
                dlat_moshier,
                target_file.c_str());
        }
    }

    std::printf("\nsummary,body,max_abs_dlon_se1_arcsec,max_abs_dlat_se1_arcsec,max_abs_ddist_se1_au,max_abs_dlon_moshier_arcsec,max_abs_dlat_moshier_arcsec,max_abs_ddist_moshier_au,samples\n");
    for (size_t i = 0; i < bodies.size(); ++i) {
        std::printf(
            "summary,%s,%.9f,%.9f,%.17g,%.9f,%.9f,%.17g,%d\n",
            bodies[i].label,
            stats[i].max_abs_dlon_se1_arcsec,
            stats[i].max_abs_dlat_se1_arcsec,
            stats[i].max_abs_ddist_se1_au,
            stats[i].max_abs_dlon_moshier_arcsec,
            stats[i].max_abs_dlat_moshier_arcsec,
            stats[i].max_abs_ddist_moshier_au,
            stats[i].samples);
    }

    return 0;
}
