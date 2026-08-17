#ifndef TAIYIN_INTERNAL_WIN32_PATH_H
#define TAIYIN_INTERNAL_WIN32_PATH_H

#if !defined(_WIN32)
#error "win32_path.h is only for Windows builds"
#endif

#if !defined(NOMINMAX)
#define NOMINMAX 1
#endif
#include <windows.h>

#include <limits>
#include <string>

namespace taiyin {
namespace internal {

// Public path strings are UTF-8.  The Windows file APIs must receive UTF-16;
// using the A variants would make catalogue discovery depend on the active
// system code page and reject ordinary paths under non-ASCII user names.
inline bool win32_utf8_to_wide(
    const std::string& value,
    std::wstring* out
) noexcept {
    if (!out || value.empty()
        || value.size() > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    out->clear();
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), 0, 0);
    if (required <= 0) {
        return false;
    }
    try {
        out->resize(static_cast<std::size_t>(required));
    } catch (...) {
        out->clear();
        return false;
    }
    return MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), &(*out)[0], required) == required;
}

inline bool win32_wide_to_utf8(
    const wchar_t* value,
    std::string* out
) noexcept {
    if (!value || !out) {
        return false;
    }
    out->clear();
    const int required = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, 0, 0, 0, 0);
    if (required <= 1) {
        return required == 1;
    }
    try {
        out->resize(static_cast<std::size_t>(required));
    } catch (...) {
        out->clear();
        return false;
    }
    if (WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, &(*out)[0], required,
        0, 0) != required) {
        out->clear();
        return false;
    }
    out->resize(static_cast<std::size_t>(required - 1));
    return true;
}

}  // namespace internal
}  // namespace taiyin

#endif  // TAIYIN_INTERNAL_WIN32_PATH_H
