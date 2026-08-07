#ifndef TAIYIN_INTERNAL_PATH_UTILS_H
#define TAIYIN_INTERNAL_PATH_UTILS_H

#include <cstddef>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <sys/stat.h>
#else
#include <sys/stat.h>
#endif

namespace taiyin {
namespace internal {

inline bool is_path_separator(char ch) noexcept {
    return ch == '/' || ch == '\\';
}

inline bool has_suffix(const std::string& value, const char* suffix) noexcept {
    const size_t suffix_len = std::strlen(suffix);
    return value.size() >= suffix_len
        && value.compare(value.size() - suffix_len, suffix_len, suffix) == 0;
}

inline bool has_suffix_case_insensitive(const std::string& value, const char* suffix) noexcept {
    const size_t suffix_len = std::strlen(suffix);
    if (value.size() < suffix_len) {
        return false;
    }
    const size_t offset = value.size() - suffix_len;
    for (size_t i = 0; i < suffix_len; ++i) {
        char a = value[offset + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) {
            return false;
        }
    }
    return true;
}

inline std::string trim_trailing_separators(const std::string& path) {
    size_t end = path.size();
    while (end > 1 && is_path_separator(path[end - 1])) {
        --end;
    }
    return path.substr(0, end);
}

inline std::string join_path(const std::string& lhs, const std::string& rhs) {
    if (lhs.empty()) {
        return rhs;
    }
    if (rhs.empty()) {
        return lhs;
    }
    if (is_path_separator(lhs[lhs.size() - 1])) {
        return lhs + rhs;
    }
    return lhs + "/" + rhs;
}

inline bool directory_exists(const std::string& path) noexcept {
#ifdef _WIN32
    struct _stat st;
    return _stat(path.c_str(), &st) == 0 && (st.st_mode & _S_IFDIR) != 0;
#else
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_PATH_UTILS_H
