#ifndef TAIYIN_INTERNAL_WIN32_DIRENT_H
#define TAIYIN_INTERNAL_WIN32_DIRENT_H

#if !defined(_WIN32)
#error "win32_dirent.h is only for Windows builds"
#endif

#if !defined(NOMINMAX)
#define NOMINMAX 1
#endif
#include <windows.h>

#include <cstring>
#include <new>
#include <string>

// Small compatibility layer for the two recursive catalog scanners. It keeps
// their POSIX-style traversal code portable while Taiyin remains C++11.
struct dirent {
    char d_name[MAX_PATH];
};

struct DIR {
    HANDLE handle;
    WIN32_FIND_DATAA data;
    dirent entry;
    bool first;
};

inline DIR* opendir(const char* path) {
    if (!path || path[0] == '\0') {
        return nullptr;
    }
    std::string pattern(path);
    const char last = pattern[pattern.size() - 1];
    if (last != '/' && last != '\\') {
        pattern += '\\';
    }
    pattern += '*';

    DIR* directory = new (std::nothrow) DIR();
    if (!directory) {
        return nullptr;
    }
    directory->handle = FindFirstFileA(pattern.c_str(), &directory->data);
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
    if (directory->first) {
        directory->first = false;
    } else if (!FindNextFileA(directory->handle, &directory->data)) {
        return nullptr;
    }
    std::strncpy(directory->entry.d_name, directory->data.cFileName,
                 sizeof(directory->entry.d_name) - 1);
    directory->entry.d_name[sizeof(directory->entry.d_name) - 1] = '\0';
    return &directory->entry;
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
