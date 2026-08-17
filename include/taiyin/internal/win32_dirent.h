#ifndef TAIYIN_INTERNAL_WIN32_DIRENT_H
#define TAIYIN_INTERNAL_WIN32_DIRENT_H

#if !defined(_WIN32)
#error "win32_dirent.h is only for Windows builds"
#endif

#include "taiyin/internal/win32_path.h"

#include <cstring>
#include <new>
#include <string>

// Small compatibility layer for the two recursive catalog scanners. It keeps
// their POSIX-style traversal code portable while Taiyin remains C++11.
struct dirent {
    // A Windows directory entry has a 255 UTF-16-code-unit leaf name.  UTF-8
    // needs up to four bytes per code unit, so MAX_PATH is not sufficient.
    char d_name[MAX_PATH * 4];
};

struct DIR {
    HANDLE handle;
    WIN32_FIND_DATAW data;
    dirent entry;
    bool first;
};

inline DIR* opendir(const char* path) {
    if (!path || path[0] == '\0') {
        return nullptr;
    }
    std::wstring pattern;
    if (!taiyin::internal::win32_utf8_to_wide(path, &pattern)) {
        return nullptr;
    }
    const wchar_t last = pattern[pattern.size() - 1];
    if (last != L'/' && last != L'\\') {
        pattern += L'\\';
    }
    pattern += L'*';

    DIR* directory = new (std::nothrow) DIR();
    if (!directory) {
        return nullptr;
    }
    directory->handle = FindFirstFileW(pattern.c_str(), &directory->data);
    if (directory->handle == INVALID_HANDLE_VALUE) {
        delete directory;
        return nullptr;
    }
    directory->first = true;
    directory->entry.d_name[0] = '\0';
    return directory;
}

inline dirent* readdir(DIR* directory) {
    if (!directory) {
        return nullptr;
    }
    for (;;) {
        if (directory->first) {
            directory->first = false;
        } else if (!FindNextFileW(directory->handle, &directory->data)) {
            return nullptr;
        }
        std::string name;
        if (!taiyin::internal::win32_wide_to_utf8(
                directory->data.cFileName, &name)
            || name.size() >= sizeof(directory->entry.d_name)) {
            // A single malformed UTF-16 leaf name must not truncate the
            // whole catalog scan. It cannot be represented by this UTF-8
            // compatibility interface, so skip it and continue.
            continue;
        }
        std::memcpy(directory->entry.d_name, name.c_str(), name.size() + 1);
        return &directory->entry;
    }
}

inline int closedir(DIR* directory) {
    if (!directory) {
        return -1;
    }
    const BOOL closed = FindClose(directory->handle);
    delete directory;
    return closed ? 0 : -1;
}

#endif  // TAIYIN_INTERNAL_WIN32_DIRENT_H
