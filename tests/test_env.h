#ifndef TAIYIN_TEST_ENV_H
#define TAIYIN_TEST_ENV_H

#include <cstdlib>
#include <iostream>

namespace taiyin_test {

inline const char* getenv_path(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] != '\0' ? value : 0;
}

inline bool require_env_path(const char* value, const char* name) {
    if (!value) {
        std::cout << "SKIP: set " << name << " to run this external-data test\n";
        return false;
    }
    return true;
}

}  // namespace taiyin_test

#endif  // TAIYIN_TEST_ENV_H
