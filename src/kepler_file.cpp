#include "taiyin/internal/kepler_file.h"

#include "taiyin/internal/ephemeris_file_loader.h"
#include "taiyin/internal/path_utils.h"

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <unordered_map>

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

namespace taiyin {
namespace internal {
namespace {

bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && is_space(value[start])) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && is_space(value[end - 1])) {
        --end;
    }
    return value.substr(start, end - start);
}

bool parse_double_value(const std::string& value, double* out) noexcept {
    if (!out || value.empty()) {
        return false;
    }
    char* end = 0;
    const double parsed = std::strtod(value.c_str(), &end);
    if (!end || *end != '\0') {
        return false;
    }
    *out = parsed;
    return true;
}

bool parse_int_value(const std::string& value, int* out) noexcept {
    if (!out || value.empty()) {
        return false;
    }
    char* end = 0;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (!end || *end != '\0'
        || parsed < static_cast<long>(std::numeric_limits<int>::min())
        || parsed > static_cast<long>(std::numeric_limits<int>::max())) {
        return false;
    }
    *out = static_cast<int>(parsed);
    return true;
}

bool parse_size_value(const std::string& value, size_t* out) noexcept {
    if (!out || value.empty()) {
        return false;
    }
    char* end = 0;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    if (!end || *end != '\0' || parsed == 0) {
        return false;
    }
    *out = static_cast<size_t>(parsed);
    return true;
}

bool get_required_string(
    const std::unordered_map<std::string, std::string>& values,
    const std::string& key,
    std::string* out
) noexcept {
    if (!out) {
        return false;
    }
    std::unordered_map<std::string, std::string>::const_iterator it = values.find(key);
    if (it == values.end() || it->second.empty()) {
        return false;
    }
    *out = it->second;
    return true;
}

bool get_required_double(
    const std::unordered_map<std::string, std::string>& values,
    const std::string& key,
    double* out
) noexcept {
    std::string value;
    return get_required_string(values, key, &value) && parse_double_value(value, out);
}

bool get_required_int(
    const std::unordered_map<std::string, std::string>& values,
    const std::string& key,
    int* out
) noexcept {
    std::string value;
    return get_required_string(values, key, &value) && parse_int_value(value, out);
}

EphemerisFrame frame_from_id(int frame_id) noexcept {
    return frame_id == static_cast<int>(EphemerisFrame::IcrfJ2000Equatorial)
        ? EphemerisFrame::IcrfJ2000Equatorial
        : EphemerisFrame::FrameUnknown;
}

bool parse_kepler_file_values(
    const std::string& path,
    std::unordered_map<std::string, std::string>* values
) noexcept {
    if (!values || path.empty()) {
        return false;
    }

    std::vector<uint8_t> bytes;
    if (!read_file_bytes(path, &bytes) || bytes.empty()) {
        return false;
    }

    try {
        values->clear();
        const std::string text(reinterpret_cast<const char*>(&bytes[0]), bytes.size());
        std::istringstream stream(text);
        std::string line;
        bool saw_magic = false;
        while (std::getline(stream, line)) {
            const std::string stripped = trim(line);
            if (stripped.empty() || stripped[0] == '#') {
                continue;
            }
            if (!saw_magic) {
                if (stripped != "TKE1") {
                    return false;
                }
                saw_magic = true;
                continue;
            }
            const size_t equals = stripped.find('=');
            if (equals == std::string::npos || equals == 0) {
                return false;
            }
            const std::string key = trim(stripped.substr(0, equals));
            const std::string value = trim(stripped.substr(equals + 1));
            if (key.empty() || value.empty()) {
                return false;
            }
            (*values)[key] = value;
        }
        return saw_magic;
    } catch (...) {
        values->clear();
        return false;
    }
}

bool validate_kepler_elements_for_range(
    const KeplerElements* elements,
    size_t element_count,
    double jd_tdb_start,
    double jd_tdb_end
) noexcept {
    KeplerEphemerisData* data = 0;
    if (!compile_kepler_ephemeris_data(elements, element_count, jd_tdb_start, jd_tdb_end, &data)) {
        return false;
    }
    kepler_ephemeris_data_destroy(data);
    return true;
}

std::string slugify_label(const std::string& label) {
    std::string slug;
    bool last_was_separator = false;
    for (size_t i = 0; i < label.size(); ++i) {
        const unsigned char raw = static_cast<unsigned char>(label[i]);
        if (std::isalnum(raw)) {
            slug.push_back(static_cast<char>(std::tolower(raw)));
            last_was_separator = false;
        } else if (raw == '_' || raw == '-' || std::isspace(raw)) {
            if (!slug.empty() && !last_was_separator) {
                slug.push_back('_');
                last_was_separator = true;
            }
        }
    }
    while (!slug.empty() && slug[slug.size() - 1] == '_') {
        slug.erase(slug.size() - 1);
    }
    return slug.empty() ? std::string("custom_orbit") : slug;
}

bool make_one_directory(const std::string& path) noexcept {
    if (path.empty() || directory_exists(path)) {
        return !path.empty();
    }
#ifdef _WIN32
    if (_mkdir(path.c_str()) == 0) {
        return true;
    }
#else
    if (mkdir(path.c_str(), 0755) == 0) {
        return true;
    }
#endif
    return errno == EEXIST && directory_exists(path);
}

bool make_directories(const std::string& path) noexcept {
    if (path.empty()) {
        return false;
    }
    try {
        std::string current;
        size_t index = 0;
        if (is_path_separator(path[0])) {
            current = path.substr(0, 1);
            index = 1;
        }
        while (index < path.size()) {
            while (index < path.size() && is_path_separator(path[index])) {
                ++index;
            }
            const size_t start = index;
            while (index < path.size() && !is_path_separator(path[index])) {
                ++index;
            }
            if (start == index) {
                continue;
            }
            current = join_path(current, path.substr(start, index - start));
            if (!make_one_directory(current)) {
                return false;
            }
        }
        return directory_exists(path);
    } catch (...) {
        return false;
    }
}

bool make_descriptor(
    const std::string& path,
    uint64_t block_id,
    int target_id,
    int center_id,
    int method_id,
    EphemerisFrame frame,
    double jd_tdb_start,
    double jd_tdb_end,
    EphemerisBlockDescriptor* out
) noexcept {
    if (!out || path.empty()
        || block_id > static_cast<uint64_t>(std::numeric_limits<int>::max())
        || target_id == center_id
        || method_id == 0
        || frame == EphemerisFrame::FrameUnknown
        || jd_tdb_end <= jd_tdb_start) {
        return false;
    }

    EphemerisBlockDescriptor descriptor;
    descriptor.route_key = EphemerisRouteKey(target_id, center_id, method_id, static_cast<int>(block_id));
    descriptor.source_key = EphemerisBlockKey(
        TAIYIN_KEPLER_FILE_SOURCE_ID,
        block_id,
        TAIYIN_KEPLER_FILE_GENERATION,
        TAIYIN_KEPLER_FILE_PURPOSE);
    descriptor.target_id = target_id;
    descriptor.center_id = center_id;
    descriptor.method_id = method_id;
    descriptor.frame = frame;
    descriptor.format = EphemerisBlockFormat::Kepler;
    descriptor.jd_tdb_start = jd_tdb_start;
    descriptor.jd_tdb_end = jd_tdb_end;
    descriptor.path = path;
    *out = descriptor;
    return true;
}

}  // namespace

std::string default_user_kepler_directory() noexcept {
    return make_user_kepler_directory(TAIYIN_DEFAULT_DATA_DIRECTORY);
}

std::string make_user_kepler_directory(
    const std::string& data_root
) noexcept {
    try {
        const std::string root = trim_trailing_separators(
            data_root.empty() ? std::string(TAIYIN_DEFAULT_DATA_DIRECTORY) : data_root);
        return join_path(root, TAIYIN_USER_KEPLER_RELATIVE_DIRECTORY);
    } catch (...) {
        return std::string();
    }
}

std::string make_user_kepler_file_path(
    const std::string& data_root,
    int target_id,
    const std::string& label
) noexcept {
    try {
        std::ostringstream name;
        name << target_id << "_" << slugify_label(label) << ".tke1";
        return join_path(make_user_kepler_directory(data_root), name.str());
    } catch (...) {
        return std::string();
    }
}

bool save_user_kepler_file(
    const std::string& data_root,
    const std::string& label,
    const KeplerElements* elements,
    size_t element_count,
    EphemerisFrame frame,
    double jd_tdb_start,
    double jd_tdb_end,
    std::string* out_path
) noexcept {
    if (out_path) {
        out_path->clear();
    }
    if (!elements || element_count == 0) {
        return false;
    }
    const std::string directory = make_user_kepler_directory(data_root);
    const std::string path = make_user_kepler_file_path(data_root, elements[0].target_id, label);
    if (directory.empty() || path.empty() || !make_directories(directory)) {
        return false;
    }
    if (!save_kepler_file(
            path,
            elements,
            element_count,
            TAIYIN_KEPLER_FILE_METHOD_ID,
            frame,
            jd_tdb_start,
            jd_tdb_end)) {
        return false;
    }
    if (out_path) {
        *out_path = path;
    }
    return true;
}

bool save_kepler_file(
    const std::string& path,
    const KeplerElements* elements,
    size_t element_count,
    int method_id,
    EphemerisFrame frame,
    double jd_tdb_start,
    double jd_tdb_end
) noexcept {
    if (path.empty() || !elements || element_count == 0 || method_id == 0
        || frame == EphemerisFrame::FrameUnknown
        || !validate_kepler_elements_for_range(elements, element_count, jd_tdb_start, jd_tdb_end)) {
        return false;
    }

    const int target_id = elements[0].target_id;
    const int center_id = elements[0].center_id;
    for (size_t i = 0; i < element_count; ++i) {
        if (elements[i].target_id != target_id || elements[i].center_id != center_id) {
            return false;
        }
    }

    try {
        std::ofstream file(path.c_str(), std::ios::out | std::ios::trunc);
        if (!file) {
            return false;
        }
        file << std::setprecision(17);
        file << "TKE1\n";
        file << "version=1\n";
        file << "target_id=" << target_id << "\n";
        file << "center_id=" << center_id << "\n";
        file << "method_id=" << method_id << "\n";
        file << "frame_id=" << static_cast<int>(frame) << "\n";
        file << "jd_tdb_start=" << jd_tdb_start << "\n";
        file << "jd_tdb_end=" << jd_tdb_end << "\n";
        file << "element_count=" << element_count << "\n\n";
        for (size_t i = 0; i < element_count; ++i) {
            const KeplerElements& element = elements[i];
            file << "element." << i << ".jd_tdb_start=" << element.jd_tdb_start << "\n";
            file << "element." << i << ".jd_tdb_end=" << element.jd_tdb_end << "\n";
            file << "element." << i << ".epoch_jd_tdb=" << element.epoch_jd_tdb << "\n";
            file << "element." << i << ".mu_au3_day2=" << element.mu_au3_day2 << "\n";
            file << "element." << i << ".semi_major_axis_au=" << element.semi_major_axis_au << "\n";
            file << "element." << i << ".eccentricity=" << element.eccentricity << "\n";
            file << "element." << i << ".inclination_rad=" << element.inclination_rad << "\n";
            file << "element." << i << ".longitude_ascending_node_rad=" << element.longitude_ascending_node_rad << "\n";
            file << "element." << i << ".argument_periapsis_rad=" << element.argument_periapsis_rad << "\n";
            file << "element." << i << ".mean_anomaly_at_epoch_rad=" << element.mean_anomaly_at_epoch_rad << "\n";
            if (i + 1 < element_count) {
                file << "\n";
            }
        }
        return static_cast<bool>(file);
    } catch (...) {
        return false;
    }
}

bool load_kepler_file(
    const std::string& path,
    std::vector<KeplerElements>* out_elements,
    EphemerisBlockDescriptor* out_descriptor
) noexcept {
    return load_kepler_file_with_block_id(path, 1, out_elements, out_descriptor);
}

bool load_kepler_file_with_block_id(
    const std::string& path,
    uint64_t block_id,
    std::vector<KeplerElements>* out_elements,
    EphemerisBlockDescriptor* out_descriptor
) noexcept {
    if (!out_elements || !out_descriptor) {
        return false;
    }

    try {
        out_elements->clear();
        *out_descriptor = EphemerisBlockDescriptor();

        std::unordered_map<std::string, std::string> values;
        if (!parse_kepler_file_values(path, &values)) {
            return false;
        }

        int version = 0;
        int target_id = 0;
        int center_id = 0;
        int method_id = 0;
        int frame_id = 0;
        double jd_tdb_start = 0.0;
        double jd_tdb_end = 0.0;
        size_t element_count = 0;
        std::string element_count_value;
        if (!get_required_int(values, "version", &version)
            || version != 1
            || !get_required_int(values, "target_id", &target_id)
            || !get_required_int(values, "center_id", &center_id)
            || !get_required_int(values, "method_id", &method_id)
            || !get_required_int(values, "frame_id", &frame_id)
            || !get_required_double(values, "jd_tdb_start", &jd_tdb_start)
            || !get_required_double(values, "jd_tdb_end", &jd_tdb_end)
            || !get_required_string(values, "element_count", &element_count_value)
            || !parse_size_value(element_count_value, &element_count)) {
            return false;
        }

        const EphemerisFrame frame = frame_from_id(frame_id);
        if (frame == EphemerisFrame::FrameUnknown) {
            return false;
        }

        std::vector<KeplerElements> elements;
        elements.reserve(element_count);
        for (size_t i = 0; i < element_count; ++i) {
            std::ostringstream prefix;
            prefix << "element." << i << ".";
            double element_start = 0.0;
            double element_end = 0.0;
            double epoch = 0.0;
            double mu = 0.0;
            double a = 0.0;
            double e = 0.0;
            double inc = 0.0;
            double node = 0.0;
            double arg = 0.0;
            double mean = 0.0;
            if (!get_required_double(values, prefix.str() + "jd_tdb_start", &element_start)
                || !get_required_double(values, prefix.str() + "jd_tdb_end", &element_end)
                || !get_required_double(values, prefix.str() + "epoch_jd_tdb", &epoch)
                || !get_required_double(values, prefix.str() + "mu_au3_day2", &mu)
                || !get_required_double(values, prefix.str() + "semi_major_axis_au", &a)
                || !get_required_double(values, prefix.str() + "eccentricity", &e)
                || !get_required_double(values, prefix.str() + "inclination_rad", &inc)
                || !get_required_double(values, prefix.str() + "longitude_ascending_node_rad", &node)
                || !get_required_double(values, prefix.str() + "argument_periapsis_rad", &arg)
                || !get_required_double(values, prefix.str() + "mean_anomaly_at_epoch_rad", &mean)) {
                return false;
            }

            KeplerElements element;
            if (!make_elliptic_kepler_elements(
                    target_id,
                    center_id,
                    element_start,
                    element_end,
                    epoch,
                    mu,
                    a,
                    e,
                    inc,
                    node,
                    arg,
                    mean,
                    &element)) {
                return false;
            }
            elements.push_back(element);
        }

        if (!validate_kepler_elements_for_range(&elements[0], elements.size(), jd_tdb_start, jd_tdb_end)) {
            return false;
        }

        EphemerisBlockDescriptor descriptor;
        if (!make_descriptor(
                path,
                block_id,
                target_id,
                center_id,
                method_id,
                frame,
                jd_tdb_start,
                jd_tdb_end,
                &descriptor)) {
            return false;
        }

        *out_elements = elements;
        *out_descriptor = descriptor;
        return true;
    } catch (...) {
        out_elements->clear();
        *out_descriptor = EphemerisBlockDescriptor();
        return false;
    }
}

bool compile_kepler_file(
    const std::string& path,
    double jd_tdb_start,
    double jd_tdb_end,
    StorageEphemerisBlock* out
) noexcept {
    if (!out) {
        return false;
    }
    std::vector<KeplerElements> elements;
    EphemerisBlockDescriptor descriptor;
    if (!load_kepler_file(path, &elements, &descriptor)
        || jd_tdb_start < descriptor.jd_tdb_start
        || jd_tdb_end > descriptor.jd_tdb_end) {
        return false;
    }
    return compile_kepler_ephemeris_block(&elements[0], elements.size(), jd_tdb_start, jd_tdb_end, out);
}

}  // namespace internal
}  // namespace taiyin
