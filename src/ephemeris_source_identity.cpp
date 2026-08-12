#include "taiyin/internal/ephemeris_source_identity.h"

#include "taiyin/internal/path_utils.h"

namespace taiyin {
namespace internal {
namespace {

char ascii_lower(char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value - 'A' + 'a')
        : value;
}

std::string lowercase_basename(const std::string& path) {
    size_t begin = 0;
    for (size_t i = 0; i < path.size(); ++i) {
        if (is_path_separator(path[i])) {
            begin = i + 1;
        }
    }
    std::string result = path.substr(begin);
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] = ascii_lower(result[i]);
    }
    return result;
}

struct KnownSpkProduct {
    const char* stem;
    uint64_t source_id;
};

bool starts_with(const std::string& value, const char* prefix) noexcept {
    if (!prefix) return false;
    for (size_t i = 0; prefix[i] != '\0'; ++i) {
        if (i >= value.size() || value[i] != prefix[i]) {
            return false;
        }
    }
    return true;
}

uint64_t classify_jpl_spk_basename(const std::string& basename) noexcept {
    static const KnownSpkProduct products[] = {
        {"de442", SPK_SOURCE_JPL_DE442}, {"de441", SPK_SOURCE_JPL_DE441},
        {"de440", SPK_SOURCE_JPL_DE440}, {"de438", SPK_SOURCE_JPL_DE438},
        {"de435", SPK_SOURCE_JPL_DE435}, {"de432", SPK_SOURCE_JPL_DE432},
        {"de431", SPK_SOURCE_JPL_DE431}, {"de430", SPK_SOURCE_JPL_DE430},
        {"de423", SPK_SOURCE_JPL_DE423}, {"de421", SPK_SOURCE_JPL_DE421},
        {"de418", SPK_SOURCE_JPL_DE418}, {"de414", SPK_SOURCE_JPL_DE414},
        {"de413", SPK_SOURCE_JPL_DE413}, {"de410", SPK_SOURCE_JPL_DE410},
        {"de408", SPK_SOURCE_JPL_DE408}, {"de406", SPK_SOURCE_JPL_DE406},
        {"de405", SPK_SOURCE_JPL_DE405}, {"de403", SPK_SOURCE_JPL_DE403},
        {"de245", SPK_SOURCE_JPL_DE245}, {"de202", SPK_SOURCE_JPL_DE202},
        {"de200", SPK_SOURCE_JPL_DE200}, {"de130", SPK_SOURCE_JPL_DE130},
        {"de125", SPK_SOURCE_JPL_DE125}, {"de118", SPK_SOURCE_JPL_DE118},
        {"de102", SPK_SOURCE_JPL_DE102},
        {"mar099", SPK_SOURCE_JPL_MAR099},
        {"jup365", SPK_SOURCE_JPL_JUP365}, {"jup349", SPK_SOURCE_JPL_JUP349},
        {"jup348", SPK_SOURCE_JPL_JUP348}, {"jup347", SPK_SOURCE_JPL_JUP347},
        {"sat480", SPK_SOURCE_JPL_SAT480}, {"sat459", SPK_SOURCE_JPL_SAT459},
        {"sat458", SPK_SOURCE_JPL_SAT458}, {"sat457", SPK_SOURCE_JPL_SAT457},
        {"sat456", SPK_SOURCE_JPL_SAT456}, {"sat455", SPK_SOURCE_JPL_SAT455},
        {"sat441", SPK_SOURCE_JPL_SAT441}, {"sat415", SPK_SOURCE_JPL_SAT415},
        {"ura184", SPK_SOURCE_JPL_URA184}, {"ura182", SPK_SOURCE_JPL_URA182},
        {"ura117", SPK_SOURCE_JPL_URA117},
        {"nep105", SPK_SOURCE_JPL_NEP105}, {"nep104", SPK_SOURCE_JPL_NEP104},
        {"nep098", SPK_SOURCE_JPL_NEP098}, {"nep097", SPK_SOURCE_JPL_NEP097},
        {"plu060", SPK_SOURCE_JPL_PLU060},
    };
    for (size_t i = 0; i < sizeof(products) / sizeof(products[0]); ++i) {
        if (starts_with(basename, products[i].stem)) {
            return products[i].source_id;
        }
    }
    return SPK_SOURCE_EXTERNAL;
}

}  // namespace

uint64_t classify_spk_source_id_from_path(const std::string& path) noexcept {
    return classify_jpl_spk_basename(lowercase_basename(path));
}

bool is_jpl_de_spk_source_id(uint64_t source_id) noexcept {
    switch (source_id) {
    case SPK_SOURCE_JPL_DE442: case SPK_SOURCE_JPL_DE441:
    case SPK_SOURCE_JPL_DE440: case SPK_SOURCE_JPL_DE438:
    case SPK_SOURCE_JPL_DE435: case SPK_SOURCE_JPL_DE432:
    case SPK_SOURCE_JPL_DE431: case SPK_SOURCE_JPL_DE430:
    case SPK_SOURCE_JPL_DE423: case SPK_SOURCE_JPL_DE421:
    case SPK_SOURCE_JPL_DE418: case SPK_SOURCE_JPL_DE414:
    case SPK_SOURCE_JPL_DE413: case SPK_SOURCE_JPL_DE410:
    case SPK_SOURCE_JPL_DE408: case SPK_SOURCE_JPL_DE406:
    case SPK_SOURCE_JPL_DE405: case SPK_SOURCE_JPL_DE403:
    case SPK_SOURCE_JPL_DE245: case SPK_SOURCE_JPL_DE202:
    case SPK_SOURCE_JPL_DE200: case SPK_SOURCE_JPL_DE130:
    case SPK_SOURCE_JPL_DE125: case SPK_SOURCE_JPL_DE118:
    case SPK_SOURCE_JPL_DE102:
        return true;
    default:
        return false;
    }
}

int default_spk_source_priority(uint64_t source_id) noexcept {
    switch (source_id) {
    case SPK_SOURCE_JPL_DE442: return 1000;
    case SPK_SOURCE_JPL_DE441: return 990;
    case SPK_SOURCE_JPL_DE440: return 980;
    case SPK_SOURCE_JPL_DE438: return 970;
    case SPK_SOURCE_JPL_DE435: return 960;
    case SPK_SOURCE_JPL_DE432: return 950;
    case SPK_SOURCE_JPL_DE431: return 940;
    case SPK_SOURCE_JPL_DE430: return 930;
    case SPK_SOURCE_JPL_DE423: return 920;
    case SPK_SOURCE_JPL_DE421: return 910;
    case SPK_SOURCE_JPL_DE418: return 900;
    case SPK_SOURCE_JPL_DE414: return 890;
    case SPK_SOURCE_JPL_DE413: return 880;
    case SPK_SOURCE_JPL_DE410: return 870;
    case SPK_SOURCE_JPL_DE408: return 860;
    case SPK_SOURCE_JPL_DE406: return 850;
    case SPK_SOURCE_JPL_DE405: return 840;
    case SPK_SOURCE_JPL_DE403: return 830;
    case SPK_SOURCE_JPL_DE245: return 820;
    case SPK_SOURCE_JPL_DE202: return 810;
    case SPK_SOURCE_JPL_DE200: return 800;
    case SPK_SOURCE_JPL_DE130: return 790;
    case SPK_SOURCE_JPL_DE125: return 780;
    case SPK_SOURCE_JPL_DE118: return 770;
    case SPK_SOURCE_JPL_DE102: return 760;
    case SPK_SOURCE_JPL_MAR099: return 800;
    case SPK_SOURCE_JPL_JUP365: return 800;
    case SPK_SOURCE_JPL_SAT441: return 800;
    case SPK_SOURCE_JPL_URA182: return 800;
    case SPK_SOURCE_JPL_NEP098: return 800;
    case SPK_SOURCE_JPL_PLU060: return 800;
    case SPK_SOURCE_JPL_JUP349: return 700;
    case SPK_SOURCE_JPL_JUP348: return 690;
    case SPK_SOURCE_JPL_JUP347: return 680;
    case SPK_SOURCE_JPL_SAT459: return 700;
    case SPK_SOURCE_JPL_SAT458: return 690;
    case SPK_SOURCE_JPL_SAT457: return 680;
    case SPK_SOURCE_JPL_SAT456: return 670;
    case SPK_SOURCE_JPL_SAT455: return 660;
    case SPK_SOURCE_JPL_SAT480: return 650;
    case SPK_SOURCE_JPL_SAT415: return 640;
    case SPK_SOURCE_JPL_URA184: return 700;
    case SPK_SOURCE_JPL_URA117: return 690;
    case SPK_SOURCE_JPL_NEP105: return 700;
    case SPK_SOURCE_JPL_NEP104: return 690;
    case SPK_SOURCE_JPL_NEP097: return 680;
    default: return 0;
    }
}

uint64_t normalize_opm2_source_id(uint32_t header_source_id) noexcept {
    return header_source_id == OPM2_SOURCE_UNDEFINED
        ? static_cast<uint64_t>(OPM2_SOURCE_TAIYIN_PRERELEASE)
        : static_cast<uint64_t>(header_source_id);
}

}  // namespace internal
}  // namespace taiyin
