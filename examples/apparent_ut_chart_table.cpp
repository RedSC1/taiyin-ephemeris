#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/dispatch.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/time.h"

#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

using namespace taiyin;
using namespace taiyin::runtime;

struct BodySpec {
    const char* name;
    int body_id;
};

struct ZodiacPosition {
    int sign_index;
    int degree;
    int minute;
    double second;
};

const char* get_data_root(int argc, char** argv) {
    if (argc > 1 && argv[1] && argv[1][0] != '\0') {
        return argv[1];
    }
    const char* env = std::getenv("TAIYIN_DATA_ROOT");
    if (env && env[0] != '\0') {
        return env;
    }
    return "data";
}

SplitJulianDate split_jd(double value) {
    SplitJulianDate result;
    split_julian_date_from_double(value, &result);
    return result;
}

const char* body_name(int body_id) {
    switch (body_id) {
    case TAIYIN_BODY_SSB: return "SSB";
    case TAIYIN_BODY_MERCURY_BARYCENTER: return "Mercury barycenter";
    case TAIYIN_BODY_VENUS_BARYCENTER: return "Venus barycenter";
    case TAIYIN_BODY_EMB: return "EMB";
    case TAIYIN_BODY_MARS_BARYCENTER: return "Mars barycenter";
    case TAIYIN_BODY_JUPITER_BARYCENTER: return "Jupiter barycenter";
    case TAIYIN_BODY_SATURN_BARYCENTER: return "Saturn barycenter";
    case TAIYIN_BODY_URANUS_BARYCENTER: return "Uranus barycenter";
    case TAIYIN_BODY_NEPTUNE_BARYCENTER: return "Neptune barycenter";
    case TAIYIN_BODY_PLUTO_BARYCENTER: return "Pluto barycenter";
    case TAIYIN_BODY_SUN: return "Sun";
    case TAIYIN_BODY_MOON: return "Moon";
    case TAIYIN_BODY_EARTH: return "Earth";
    default: return "body";
    }
}

const char* output_frame_name(int frame_id) {
    switch (frame_id) {
    case TAIYIN_APPARENT_FRAME_ICRF: return "ICRF";
    case TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE: return "TRUE_EQUATOR_OF_DATE";
    case TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE: return "TRUE_ECLIPTIC_OF_DATE";
    case TAIYIN_APPARENT_FRAME_J2000_MEAN_EQUATOR: return "J2000_MEAN_EQUATOR";
    case TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC: return "J2000_ECLIPTIC";
    case TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE: return "MEAN_EQUATOR_OF_DATE";
    case TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE: return "MEAN_ECLIPTIC_OF_DATE";
    case TAIYIN_APPARENT_FRAME_CIRS: return "CIRS";
    default: return "UNKNOWN";
    }
}

const char* delta_t_model_name(int model_id) {
    switch (model_id) {
    case dispatch::DELTA_T_ESTIMATED_DEFAULT: return "ESTIMATED_DEFAULT";
    default: return "CUSTOM/UNKNOWN";
    }
}

const char* tdb_model_name(int model_id) {
    switch (model_id) {
    case dispatch::TDB_FAST_PERIODIC: return "FAST_PERIODIC";
    case dispatch::TDB_SOFA_FULL: return "SOFA_FULL";
    default: return "CUSTOM/UNKNOWN";
    }
}

const char* precession_model_name(int model_id) {
    switch (model_id) {
    case dispatch::MODEL_SELECTION_DEFAULT: return "DEFAULT";
    case dispatch::PRECESSION_VONDRAK2011: return "VONDRAK2011(frame_bias)";
    case dispatch::PRECESSION_IAU2006: return "IAU2006";
    default: return "CUSTOM/UNKNOWN";
    }
}

const char* nutation_model_name(int model_id) {
    switch (model_id) {
    case dispatch::MODEL_SELECTION_DEFAULT: return "DEFAULT";
    case dispatch::NUTATION_IAU2000B: return "IAU2000B";
    case dispatch::NUTATION_IAU2000A: return "IAU2000A";
    default: return "CUSTOM/UNKNOWN";
    }
}

const char* frame_route_name(int route_id) {
    switch (route_id) {
    case dispatch::FRAME_ROUTE_EQUINOX: return "EQUINOX";
    case dispatch::FRAME_ROUTE_CIRS: return "CIRS";
    default: return "CUSTOM/UNKNOWN";
    }
}

const char* aberration_model_name(int model_id) {
    switch (model_id) {
    case dispatch::ABERRATION_ANNUAL_RELATIVISTIC: return "ANNUAL_RELATIVISTIC";
    default: return "CUSTOM/UNKNOWN";
    }
}

const char* on_off(bool value) {
    return value ? "on" : "off";
}

void append_flag(std::ostream& out, bool* wrote_any, const char* name) {
    if (*wrote_any) {
        out << "|";
    }
    out << name;
    *wrote_any = true;
}

void print_native_flags(std::ostream& out, uint32_t flags) {
    bool wrote_any = false;
    if ((flags & TAIYIN_NATIVE_POSITION_SPEED) != 0u) append_flag(out, &wrote_any, "SPEED");
    if ((flags & TAIYIN_NATIVE_POSITION_XYZ) != 0u) append_flag(out, &wrote_any, "XYZ");
    if ((flags & TAIYIN_NATIVE_POSITION_EQUATORIAL) != 0u) append_flag(out, &wrote_any, "EQUATORIAL");
    if ((flags & TAIYIN_NATIVE_POSITION_RADIANS) != 0u) append_flag(out, &wrote_any, "RADIANS");
    if ((flags & TAIYIN_NATIVE_POSITION_TRUEPOS) != 0u) append_flag(out, &wrote_any, "TRUEPOS");
    if ((flags & TAIYIN_NATIVE_POSITION_NO_ABERR) != 0u) append_flag(out, &wrote_any, "NO_ABERR");
    if ((flags & TAIYIN_NATIVE_POSITION_NO_GDEFL) != 0u) append_flag(out, &wrote_any, "NO_GDEFL");
    if ((flags & TAIYIN_NATIVE_POSITION_ASTROMETRIC) != 0u) append_flag(out, &wrote_any, "ASTROMETRIC");
    if ((flags & TAIYIN_NATIVE_POSITION_NONUT) != 0u) append_flag(out, &wrote_any, "NONUT");
    if ((flags & TAIYIN_NATIVE_POSITION_TOPOCENTRIC) != 0u) append_flag(out, &wrote_any, "TOPOCENTRIC");
    if (!wrote_any) {
        out << "none";
    }
}

void print_apparent_flags(std::ostream& out, uint32_t flags) {
    bool wrote_any = false;
    if ((flags & TAIYIN_APPARENT_LIGHT_TIME) != 0u) append_flag(out, &wrote_any, "LIGHT_TIME");
    if ((flags & TAIYIN_APPARENT_USE_MATRIX) != 0u) append_flag(out, &wrote_any, "USE_MATRIX");
    if ((flags & TAIYIN_APPARENT_SPHERICAL) != 0u) append_flag(out, &wrote_any, "SPHERICAL");
    if ((flags & TAIYIN_APPARENT_ABERRATION) != 0u) append_flag(out, &wrote_any, "ABERRATION");
    if ((flags & TAIYIN_APPARENT_DEFLECTION) != 0u) append_flag(out, &wrote_any, "DEFLECTION");
    if ((flags & TAIYIN_APPARENT_VELOCITY) != 0u) append_flag(out, &wrote_any, "VELOCITY");
    if ((flags & TAIYIN_APPARENT_ACCELERATION) != 0u) append_flag(out, &wrote_any, "ACCELERATION");
    if ((flags & TAIYIN_APPARENT_SHAPIRO_DELAY) != 0u) append_flag(out, &wrote_any, "SHAPIRO_DELAY");
    if ((flags & TAIYIN_APPARENT_TOPOCENTRIC) != 0u) append_flag(out, &wrote_any, "TOPOCENTRIC");
    if (!wrote_any) {
        out << "none";
    }
}

void print_context_fields(std::ostream& out, const NativeCalcContext& context) {
    bool wrote_any = false;
    append_flag(out, &wrote_any, "global_leap_seconds");
    if (global_earth_orientation_table()) append_flag(out, &wrote_any, "global_eop");
    if (context.fields.has(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION)) append_flag(out, &wrote_any, "observer_location");
    if (context.fields.has(TAIYIN_NATIVE_FIELD_TOPOCENTRIC_OFFSET)) append_flag(out, &wrote_any, "topocentric_offset");
    if (context.fields.has(TAIYIN_NATIVE_FIELD_ATMOSPHERE)) append_flag(out, &wrote_any, "atmosphere");
    if (context.fields.has(TAIYIN_NATIVE_FIELD_DEFLECTORS)) append_flag(out, &wrote_any, "deflectors");
    if (context.fields.has(TAIYIN_NATIVE_FIELD_CELESTIAL_POLE_OFFSET)) append_flag(out, &wrote_any, "cpo");
    if (!wrote_any) {
        out << "none";
    }
}

const char* calculation_mode(const NativeCalcContext& context, uint32_t flags) {
    const bool topocentric_requested = (flags & TAIYIN_NATIVE_POSITION_TOPOCENTRIC) != 0u
        || (context.apparent_options.flags & TAIYIN_APPARENT_TOPOCENTRIC) != 0u;
    if (!topocentric_requested) {
        return "geocentric";
    }
    if (context.fields.has(TAIYIN_NATIVE_FIELD_OBSERVER_LOCATION)) {
        return "topocentric(observer_location)";
    }
    if (context.fields.has(TAIYIN_NATIVE_FIELD_TOPOCENTRIC_OFFSET)) {
        return "topocentric(offset)";
    }
    return "topocentric(missing_observer)";
}

void print_usage(const char* program) {
    std::cout << "usage: " << program << " [data_root]\n"
              << "\n"
              << "If data_root is omitted, the example uses TAIYIN_DATA_ROOT or ./data.\n"
              << "It prints a simple geocentric apparent ecliptic bare chart table.\n";
}

ZodiacPosition zodiac_position(double longitude_deg) {
    const double normalized = normalize_degrees(longitude_deg);
    int sign_index = static_cast<int>(normalized / 30.0);
    if (sign_index < 0) {
        sign_index = 0;
    } else if (sign_index > 11) {
        sign_index = 11;
    }

    double sign_degrees = normalized - static_cast<double>(sign_index) * 30.0;
    int degree = static_cast<int>(std::floor(sign_degrees));
    double minutes_full = (sign_degrees - static_cast<double>(degree)) * 60.0;
    int minute = static_cast<int>(std::floor(minutes_full));
    double second = (minutes_full - static_cast<double>(minute)) * 60.0;

    if (second >= 59.995) {
        second = 0.0;
        ++minute;
    }
    if (minute >= 60) {
        minute = 0;
        ++degree;
    }
    if (degree >= 30) {
        degree = 0;
        sign_index = (sign_index + 1) % 12;
    }
    return { sign_index, degree, minute, second };
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && argv[1] && std::string(argv[1]) == "--help") {
        print_usage(argv[0]);
        return 0;
    }

    const char* data_root = get_data_root(argc, argv);

    EphemerisRuntimeConfig config;
    config.segment_cache_max_entries = 4096;
    config.data_root = data_root;
    config.load_packaged_data = true;
    if (!initialize_global_ephemeris_runtime(config)) {
        std::cerr << "failed to initialize runtime from data root: " << data_root << "\n";
        return 1;
    }

    const CalendarDateTime datetime_ut = { 2024, 1, 1, 12, 0, 0.0 };
    const double jd_ut = julian_day(datetime_ut);

    NativeCalcContext context;
    context.apparent_options.flags |= TAIYIN_APPARENT_ABERRATION | TAIYIN_APPARENT_DEFLECTION;
    context.apparent_options.aberration_model_id = dispatch::ABERRATION_ANNUAL_RELATIVISTIC;
    context.model_context.precession_model_id = dispatch::PRECESSION_VONDRAK2011;
    context.model_context.nutation_model_id = dispatch::NUTATION_IAU2000B;
    context.model_context.frame_route_id = dispatch::FRAME_ROUTE_EQUINOX;
    if (native_context_use_solar_deflector(&context) != TAIYIN_STATUS_OK) {
        std::cerr << "failed to configure solar deflector\n";
        return 1;
    }
    const double delta_t_seconds = dispatch::eval_delta_t_with_ephemeris_correction(
        context.delta_t_model_id,
        context.ephemeris_family_id,
        split_jd(jd_ut),
        0,
        0);
    const double jd_tt = ut1_to_tt_jd(jd_ut, delta_t_seconds);
    const double tdb_minus_tt_seconds = dispatch::eval_tdb(
        context.model_context.tdb_model_id, split_jd(jd_tt), 0);
    const double jd_tdb = add_seconds_to_jd(jd_tt, tdb_minus_tt_seconds);

    const BodySpec bodies[] = {
        { "Sun", TAIYIN_BODY_SUN },
        { "Moon", TAIYIN_BODY_MOON },
        { "Mercury", TAIYIN_BODY_MERCURY_BARYCENTER },
        { "Venus", TAIYIN_BODY_VENUS_BARYCENTER },
        { "Mars", TAIYIN_BODY_MARS_BARYCENTER },
        { "Jupiter", TAIYIN_BODY_JUPITER_BARYCENTER },
        { "Saturn", TAIYIN_BODY_SATURN_BARYCENTER },
        { "Uranus", TAIYIN_BODY_URANUS_BARYCENTER },
        { "Neptune", TAIYIN_BODY_NEPTUNE_BARYCENTER },
        { "Pluto", TAIYIN_BODY_PLUTO_BARYCENTER },
    };
    const size_t body_count = sizeof(bodies) / sizeof(bodies[0]);
    int body_ids[sizeof(bodies) / sizeof(bodies[0])];
    for (size_t i = 0; i < body_count; ++i) {
        body_ids[i] = bodies[i].body_id;
    }

    double positions[sizeof(bodies) / sizeof(bodies[0])][6];
    EphemerisEvalDiagnostic diagnostics[sizeof(bodies) / sizeof(bodies[0])];
    const uint32_t flags = TAIYIN_NATIVE_POSITION_RADIANS;
    const Status status = calc_positions_ut(
        &context,
        body_ids,
        body_count,
        split_jd(jd_ut),
        flags,
        &positions[0][0],
        diagnostics);
    if (status != TAIYIN_STATUS_OK) {
        std::cerr << "calc_positions_ut failed: " << status_name(status) << "\n";
        for (size_t i = 0; i < body_count; ++i) {
            if (diagnostics[i].status != TAIYIN_STATUS_OK) {
                std::cerr << "  " << bodies[i].name
                          << ": " << status_name(diagnostics[i].status)
                          << " target=" << diagnostics[i].target_id
                          << " center=" << diagnostics[i].center_id << "\n";
            }
        }
        return 1;
    }

    const char* signs[12] = {
        "Aries",
        "Taurus",
        "Gemini",
        "Cancer",
        "Leo",
        "Virgo",
        "Libra",
        "Scorpio",
        "Sagittarius",
        "Capricorn",
        "Aquarius",
        "Pisces",
    };

    std::cout << "Taiyin apparent bare chart\n";
    std::cout << "Date: 2024-01-01 12:00 UT (simplified UT/UT1 path)\n";
    std::cout << "Data: " << data_root << "\n\n";

    std::cout << "Observer and frame\n";
    std::cout << "  Mode: " << calculation_mode(context, flags) << "\n";
    std::cout << "  Observer: " << body_name(context.observer_id) << " (" << context.observer_id << ")\n";
    std::cout << "  Center: " << body_name(context.center_id) << " (" << context.center_id << ")\n";
    std::cout << "  Output frame: apparent true ecliptic of date\n";
    std::cout << "  Internal frame id: " << output_frame_name(context.apparent_options.output_frame_id) << "\n\n";

    std::cout << "Apparent corrections\n";
    std::cout << "  Light-time: " << on_off((context.apparent_options.flags & TAIYIN_APPARENT_LIGHT_TIME) != 0u) << "\n";
    std::cout << "  Annual aberration: "
              << on_off((context.apparent_options.flags & TAIYIN_APPARENT_ABERRATION) != 0u)
              << " (" << aberration_model_name(context.apparent_options.aberration_model_id) << ")\n";
    std::cout << "  Gravitational deflection: "
              << on_off((context.apparent_options.flags & TAIYIN_APPARENT_DEFLECTION) != 0u)
              << " (Sun only, deflectors=" << context.apparent_options.deflector_count << ")\n\n";

    std::cout << "Time scale\n";
    std::cout << "  Delta-T: " << std::fixed << std::setprecision(9) << delta_t_seconds
              << " s (" << delta_t_model_name(context.delta_t_model_id) << ")\n";
    std::cout << "  TT JD: " << std::setprecision(12) << jd_tt << "\n";
    std::cout << "  TDB JD: " << std::setprecision(12) << jd_tdb
              << " (" << tdb_model_name(context.model_context.tdb_model_id) << ")\n\n";

    std::cout << "Orientation models\n";
    std::cout << "  Precession: " << precession_model_name(context.model_context.precession_model_id) << "\n";
    std::cout << "  Nutation: " << nutation_model_name(context.model_context.nutation_model_id) << "\n";
    std::cout << "  Route: " << frame_route_name(context.model_context.frame_route_id) << "\n\n";

    std::cout << "Raw API summary\n";
    std::cout << "  Native flags: ";
    print_native_flags(std::cout, flags);
    std::cout << "\n";
    std::cout << "  Apparent flags: ";
    print_apparent_flags(std::cout, context.apparent_options.flags);
    std::cout << "\n";
    std::cout << "  Context fields: ";
    print_context_fields(std::cout, context);
    std::cout << "\n\n";
    std::cout << std::left
              << std::setw(10) << "Body"
              << std::right
              << std::setw(13) << "Lon deg"
              << "  "
              << std::left
              << std::setw(28) << "Zodiac position"
              << std::right
              << std::setw(12) << "Lat deg"
              << std::setw(15) << "Distance AU"
              << "\n";
    std::cout << std::string(80, '-') << "\n";

    std::cout << std::fixed;
    for (size_t i = 0; i < body_count; ++i) {
        const double longitude_deg = normalize_degrees(rad_to_deg(positions[i][0]));
        const double latitude_deg = rad_to_deg(positions[i][1]);
        const double distance_au = positions[i][2];
        const ZodiacPosition zodiac = zodiac_position(longitude_deg);

        std::cout << std::left << std::setw(10) << bodies[i].name
                  << std::right << std::setw(12) << std::setprecision(6) << longitude_deg << " deg  "
                  << std::left
                  << std::setw(12) << signs[zodiac.sign_index]
                  << std::right
                  << std::setw(2) << zodiac.degree << " deg "
                  << std::setw(2) << zodiac.minute << "' "
                  << std::setw(6) << std::setprecision(2) << zodiac.second << "\""
                  << std::setw(12) << std::setprecision(6) << latitude_deg
                  << std::setw(15) << std::setprecision(9) << distance_au
                  << "\n";
    }
    return 0;
}
