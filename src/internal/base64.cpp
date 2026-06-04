#include "taiyin/internal/base64.h"

namespace taiyin {
namespace internal {
namespace {

int base64_value(char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

bool is_space(char ch) {
    return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t';
}

}  // namespace

bool base64_decode(const char* text, int size, std::vector<uint8_t>* out) {
    if (!text || size < 0 || !out) {
        return false;
    }

    std::vector<uint8_t> result;
    result.reserve(static_cast<size_t>(size) * 3 / 4);

    int values[4] = { 0, 0, 0, 0 };
    int count = 0;
    bool saw_padding = false;

    for (int i = 0; i < size; ++i) {
        const char ch = text[i];
        if (is_space(ch)) {
            continue;
        }

        if (ch == '=') {
            saw_padding = true;
            values[count++] = -2;
        } else {
            const int value = base64_value(ch);
            if (value < 0 || saw_padding) {
                return false;
            }
            values[count++] = value;
        }

        if (count == 4) {
            if (values[0] < 0 || values[1] < 0) {
                return false;
            }
            if (values[2] == -2 && values[3] != -2) {
                return false;
            }

            const uint32_t packed = (static_cast<uint32_t>(values[0]) << 18)
                | (static_cast<uint32_t>(values[1]) << 12)
                | (static_cast<uint32_t>(values[2] < 0 ? 0 : values[2]) << 6)
                | static_cast<uint32_t>(values[3] < 0 ? 0 : values[3]);

            result.push_back(static_cast<uint8_t>((packed >> 16) & 0xFF));
            if (values[2] != -2) {
                result.push_back(static_cast<uint8_t>((packed >> 8) & 0xFF));
            }
            if (values[3] != -2) {
                result.push_back(static_cast<uint8_t>(packed & 0xFF));
            }

            count = 0;
        }
    }

    if (count != 0) {
        return false;
    }

    *out = result;
    return true;
}

}  // namespace internal
}  // namespace taiyin
