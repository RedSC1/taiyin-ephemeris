#include "taiyin/body_id.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/orbital_events.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"

#include "test_env.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

using namespace taiyin;
using namespace taiyin::runtime;

const double kStartJdUt = 2460409.0;
const double kApsisToleranceDays = 1.0e-4;
// OPM2 and Swiss se1 agree closely on the Venus node, but their independently
// modeled heliocentric radial curves shift this 2024 perihelion by about 81 s.
const double kPlanetApsisToleranceDays = 1.5e-3;
const double kNodeToleranceDays = 1.0e-4;
const int kSwissMoon = 1;
const int kSwissVenus = 3;

#ifndef TAIYIN_TEST_PYTHON_EXECUTABLE
#define TAIYIN_TEST_PYTHON_EXECUTABLE "python3"
#endif

struct SwissOrbitalEvents {
    double pericenter_jd_ut;
    double apocenter_jd_ut;
    double ascending_node_jd_ut;
};

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_status(Status actual, Status expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << "\n";
        ++(*failures);
    }
}

std::string command_quote(const char* value) {
#ifdef _WIN32
    std::string out("\"");
#else
    std::string out("'");
#endif
    if (value) {
        for (const char* p = value; *p; ++p) {
#ifdef _WIN32
            if (*p == '\"') {
                out += "\\\"";
            } else {
                out += *p;
            }
#else
            if (*p == '\'') {
                out += "'\\''";
            } else {
                out += *p;
            }
#endif
        }
    }
#ifdef _WIN32
    out += "\"";
#else
    out += "'";
#endif
    return out;
}

long process_id() noexcept {
#ifdef _WIN32
    return static_cast<long>(_getpid());
#else
    return static_cast<long>(getpid());
#endif
}

bool read_command_output(const std::string& command, std::string* out) {
    if (!out) return false;
    out->clear();
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    if (!pipe) return false;
    char buffer[512] = {};
    while (std::fgets(buffer, sizeof(buffer), pipe)) {
        *out += buffer;
    }
#ifdef _WIN32
    return _pclose(pipe) == 0;
#else
    return pclose(pipe) == 0;
#endif
}

bool write_python_script(
    int swiss_body_id,
    const std::string& source,
    std::string* out_path
) {
    if (!out_path) return false;
    std::ostringstream path;
    path << "taiyin_orbital_events_swiss_" << process_id()
         << "_" << swiss_body_id << ".py";
    std::ofstream file(path.str().c_str(), std::ios::out | std::ios::trunc);
    if (!file) return false;
    file << source;
    file.close();
    if (!file) return false;
    *out_path = path.str();
    return true;
}

bool swiss_orbital_events_from_se1(
    const char* ephe_path,
    int swiss_body_id,
    bool heliocentric,
    double start_jd_ut,
    double apsis_window_days,
    double node_window_days,
    SwissOrbitalEvents* out
) {
    if (!ephe_path || !out) return false;
    *out = SwissOrbitalEvents();

    std::ostringstream python;
    python << std::setprecision(17)
           << "import math, os, swisseph as swe\n"
           << "import sys\n"
           << "swe.set_ephe_path(sys.argv[1])\n"
           << "body=" << swiss_body_id << "\n"
           << "flags=(swe.FLG_SWIEPH | swe.FLG_SPEED | swe.FLG_XYZ | swe.FLG_J2000 | swe.FLG_TRUEPOS"
           << (heliocentric ? " | swe.FLG_HELCTR" : "") << ")\n"
           << "def state(jd):\n"
           << "    values, returned = swe.calc_ut(jd, body, flags)\n"
           << "    if not (returned & swe.FLG_SWIEPH):\n"
           << "        raise RuntimeError('Swiss calc fell back from SWIEPH: %r' % returned)\n"
           << "    return values\n"
           << "def distance(jd):\n"
           << "    x = state(jd)\n"
           << "    return math.sqrt(x[0]*x[0] + x[1]*x[1] + x[2]*x[2])\n"
           << "def radial(jd):\n"
           << "    x = state(jd)\n"
           << "    return x[0]*x[3] + x[1]*x[4] + x[2]*x[5]\n"
           << "def bisect(func, lo, hi):\n"
           << "    flo = func(lo)\n"
           << "    for _ in range(96):\n"
           << "        mid = 0.5*(lo + hi)\n"
           << "        fmid = func(mid)\n"
           << "        if abs(hi-lo) <= 1e-12:\n"
           << "            return mid\n"
           << "        if flo*fmid <= 0.0:\n"
           << "            hi = mid\n"
           << "        else:\n"
           << "            lo, flo = mid, fmid\n"
           << "    return 0.5*(lo + hi)\n"
           << "def roots(func, start, end, step):\n"
           << "    result=[]\n"
           << "    lo=start\n"
           << "    flo=func(lo)\n"
           << "    while lo < end:\n"
           << "        hi=min(lo+step, end)\n"
           << "        fhi=func(hi)\n"
           << "        if flo*fhi <= 0.0:\n"
           << "            root=bisect(func, lo, hi)\n"
           << "            if not result or abs(root-result[-1]) > 1e-7:\n"
           << "                result.append(root)\n"
           << "        lo, flo = hi, fhi\n"
           << "    return result\n"
           << "def select_apsis(candidates, want_perigee):\n"
           << "    for root in candidates:\n"
           << "        before, at, after = distance(root-0.05), distance(root), distance(root+0.05)\n"
           << "        if want_perigee and at < before and at < after:\n"
           << "            return root\n"
           << "        if not want_perigee and at > before and at > after:\n"
           << "            return root\n"
           << "    raise RuntimeError('requested apsis was not bracketed')\n"
           << "pericenter=select_apsis(roots(radial, " << start_jd_ut << ", "
           << start_jd_ut + apsis_window_days << ", 0.125), True)\n"
           << "apocenter=select_apsis(list(reversed(roots(radial, "
           << start_jd_ut - apsis_window_days << ", " << start_jd_ut << ", 0.125))), False)\n"
           << "ascending=None\n"
           << "for root in roots(lambda jd: state(jd)[2], " << start_jd_ut << ", "
           << start_jd_ut + node_window_days << ", 0.1):\n"
           << "    if state(root)[5] > 0.0:\n"
           << "        ascending=root\n"
           << "        break\n"
           << "if ascending is None:\n"
           << "    raise RuntimeError('ascending node was not bracketed')\n"
           << "print(format(pericenter, '.17g'), format(apocenter, '.17g'), format(ascending, '.17g'))\n";

    std::string script_path;
    if (!write_python_script(swiss_body_id, python.str(), &script_path)) return false;
    std::ostringstream command;
    command << command_quote(TAIYIN_TEST_PYTHON_EXECUTABLE)
            << " " << command_quote(script_path.c_str())
            << " " << command_quote(ephe_path);
    std::string output;
    const bool ran = read_command_output(command.str(), &output);
    std::remove(script_path.c_str());
    if (!ran) return false;
    std::istringstream input(output);
    return static_cast<bool>(input >> out->pericenter_jd_ut >> out->apocenter_jd_ut
                             >> out->ascending_node_jd_ut);
}

bool initialize_opm2_runtime(const char* opm2_dir, int* failures) {
    const char* source_paths[] = { opm2_dir };
    EphemerisRuntimeConfig config;
    config.source_paths = source_paths;
    config.source_path_count = 1;
    config.segment_cache_max_entries = 4096;
    config.load_packaged_data = false;
    const bool ok = initialize_global_ephemeris_runtime(config);
    expect_true(ok, "initialize OPM2 global runtime", failures);
    return ok;
}

NativeCalcContext make_geocentric_context() {
    NativeCalcContext context;
    native_context_set_geocentric_observer(&context, TAIYIN_BODY_EARTH, TAIYIN_BODY_EARTH);
    return context;
}

SplitJulianDate split_jd(double value) {
    SplitJulianDate result;
    split_julian_date_from_double(value, &result);
    return result;
}

void expect_time_close(
    double taiyin_jd,
    double swiss_jd,
    double tolerance_days,
    const char* label,
    int* failures
) {
    const double delta_seconds = std::fabs(taiyin_jd - swiss_jd) * 86400.0;
    std::cout << std::setprecision(15) << label
              << " taiyin=" << taiyin_jd
              << " swiss_se1=" << swiss_jd
              << " delta_seconds=" << delta_seconds << "\n";
    expect_true(std::fabs(taiyin_jd - swiss_jd) <= tolerance_days, label, failures);
}

void expect_time_close(
    const SplitJulianDate& taiyin_jd,
    double swiss_jd,
    double tolerance_days,
    const char* label,
    int* failures
) {
    expect_time_close(
        split_julian_date_to_double(taiyin_jd), swiss_jd, tolerance_days, label, failures);
}

}  // namespace

int main() {
    const char* opm2_dir = taiyin_test::getenv_path("TAIYIN_OPM2_DATA_DIR");
    if (!taiyin_test::require_env_path(opm2_dir, "TAIYIN_OPM2_DATA_DIR")) return 0;
    const char* swiss_ephe_path = taiyin_test::getenv_path("TAIYIN_SWISS_EPHE_PATH");
    if (!taiyin_test::require_env_path(swiss_ephe_path, "TAIYIN_SWISS_EPHE_PATH")) return 0;

    SwissOrbitalEvents moon_swiss;
    SwissOrbitalEvents venus_swiss;
    if (!swiss_orbital_events_from_se1(
            swiss_ephe_path, kSwissMoon, false, kStartJdUt, 60.0, 20.0, &moon_swiss)
        || !swiss_orbital_events_from_se1(
            swiss_ephe_path, kSwissVenus, true, kStartJdUt, 400.0, 400.0, &venus_swiss)) {
        std::cout << "SKIP: python swisseph is unavailable or did not use SWIEPH se1 files\n";
        return 0;
    }

    int failures = 0;
    if (!initialize_opm2_runtime(opm2_dir, &failures)) return 1;
    const NativeCalcContext context = make_geocentric_context();
    EphemerisEvalDiagnostic diagnostic;

    BodyApsisSearchResult perigee;
    expect_status(
        search_next_body_apsis_ut(
            &context, TAIYIN_BODY_MOON, TAIYIN_BODY_APSIS_PERICENTER,
            split_jd(kStartJdUt), 0, &perigee, &diagnostic),
        TAIYIN_STATUS_OK, "search lunar perigee", &failures);
    expect_time_close(perigee.jd, moon_swiss.pericenter_jd_ut, kApsisToleranceDays,
                      "lunar perigee vs Swiss SWIEPH se1", &failures);

    BodyApsisSearchResult apogee;
    expect_status(
        search_next_body_apsis_ut(
            &context, TAIYIN_BODY_MOON, TAIYIN_BODY_APSIS_APOCENTER,
            split_jd(kStartJdUt), TAIYIN_ORBITAL_EVENT_REVERSE, &apogee, &diagnostic),
        TAIYIN_STATUS_OK, "search lunar apogee", &failures);
    expect_time_close(apogee.jd, moon_swiss.apocenter_jd_ut, kApsisToleranceDays,
                      "lunar apogee vs Swiss SWIEPH se1", &failures);

    BodyNodeSearchResult ascending_node;
    expect_status(
        search_next_body_plane_node_ut(
            &context, TAIYIN_BODY_MOON, TAIYIN_BODY_NODE_ASCENDING,
            split_jd(kStartJdUt), TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC,
            0, &ascending_node, &diagnostic),
        TAIYIN_STATUS_OK, "search lunar J2000 ascending node", &failures);
    expect_time_close(ascending_node.jd, moon_swiss.ascending_node_jd_ut, kNodeToleranceDays,
                      "lunar ascending node vs Swiss SWIEPH se1", &failures);

    BodyApsisSearchResult venus_perihelion;
    expect_status(
        search_next_body_apsis_ut(
            &context, TAIYIN_BODY_VENUS_BARYCENTER, TAIYIN_BODY_APSIS_PERICENTER,
            split_jd(kStartJdUt), 0, &venus_perihelion, &diagnostic),
        TAIYIN_STATUS_OK, "search Venus barycenter perihelion", &failures);
    expect_time_close(venus_perihelion.jd, venus_swiss.pericenter_jd_ut, kPlanetApsisToleranceDays,
                      "Venus perihelion vs Swiss SWIEPH se1", &failures);

    BodyNodeSearchResult venus_ascending_node;
    expect_status(
        search_next_body_plane_node_ut(
            &context, TAIYIN_BODY_VENUS_BARYCENTER, TAIYIN_BODY_NODE_ASCENDING,
            split_jd(kStartJdUt), TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC,
            0, &venus_ascending_node, &diagnostic),
        TAIYIN_STATUS_OK, "search Venus barycenter J2000 ascending node", &failures);
    expect_time_close(venus_ascending_node.jd, venus_swiss.ascending_node_jd_ut,
                      kNodeToleranceDays, "Venus ascending node vs Swiss SWIEPH se1", &failures);

    if (failures != 0) {
        std::cerr << failures << " orbital-events Swiss oracle test(s) failed\n";
        return 1;
    }
    std::cout << "orbital-events Swiss se1 oracle tests passed\n";
    return 0;
}
