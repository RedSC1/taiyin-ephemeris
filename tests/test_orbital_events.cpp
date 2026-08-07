#include "taiyin/angle.h"
#include "taiyin/body_id.h"
#include "taiyin/physical_constants.h"
#include "taiyin/runtime/native_context.h"
#include "taiyin/runtime/native_position.h"
#include "taiyin/runtime/orbital_events.h"
#include "taiyin/runtime/runtime.h"
#include "taiyin/status.h"
#include "taiyin/time.h"
#include "taiyin/vector3.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

const taiyin::SplitJulianDate kJdUt2024Apr(2460409, 0.0);
const int kDefaultOrbitalFrame = taiyin::TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC;

taiyin::SplitJulianDate split_jd(double jd) {
    taiyin::SplitJulianDate out;
    taiyin::split_julian_date_from_double(jd, &out);
    return out;
}

void expect_true(bool value, const char* label, int* failures) {
    if (!value) {
        std::cerr << "FAIL: expected true: " << label << "\n";
        ++(*failures);
    }
}

void expect_status(taiyin::Status actual, taiyin::Status expected, const char* label, int* failures) {
    if (actual != expected) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << "\n";
        ++(*failures);
    }
}

void expect_near(double actual, double expected, double tolerance, const char* label, int* failures) {
    if (!(std::fabs(actual - expected) <= tolerance)) {
        std::cerr << "FAIL: " << label << ": actual=" << actual
                  << " expected=" << expected << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

void expect_near(
    const taiyin::SplitJulianDate& actual,
    double expected,
    double tolerance,
    const char* label,
    int* failures
) {
    expect_near(
        taiyin::split_julian_date_to_double(actual), expected, tolerance, label, failures);
}

void expect_near(
    const taiyin::SplitJulianDate& actual,
    const taiyin::SplitJulianDate& expected,
    double tolerance,
    const char* label,
    int* failures
) {
    expect_near(actual - expected, 0.0, tolerance, label, failures);
}

std::string packaged_data_root() {
    const char* root = std::getenv("TAIYIN_REPO_ROOT");
    if (root && root[0] != '\0') {
        return std::string(root) + "/data/ephemerides/opm2/major-bodies/600y";
    }
    return "../data/ephemerides/opm2/major-bodies/600y";
}

bool initialize_runtime(int* failures) {
    taiyin::runtime::EphemerisRuntimeConfig config;
    const std::string path = packaged_data_root();
    const char* paths[] = { path.c_str() };
    config.source_paths = paths;
    config.source_path_count = 1;
    config.load_packaged_data = true;
    config.segment_cache_max_entries = 128;
    const bool ok = taiyin::runtime::initialize_global_ephemeris_runtime(config);
    expect_true(ok, "initialize packaged OPM2 runtime", failures);
    return ok;
}

taiyin::runtime::NativeCalcContext make_context() {
    taiyin::runtime::NativeCalcContext context;
    taiyin::runtime::native_context_set_geocentric_observer(
        &context, taiyin::TAIYIN_BODY_EARTH, taiyin::TAIYIN_BODY_EARTH);
    context.apparent_options.output_frame_id = taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE;
    return context;
}

double orbit_distance(
    const taiyin::runtime::NativeCalcContext& context,
    int body_id,
    double jd_ut,
    int* failures
) {
    taiyin::runtime::BodyOsculatingOrbit orbit;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    expect_status(
        taiyin::runtime::calc_body_osculating_orbit_ut(
            &context, body_id, split_jd(jd_ut), kDefaultOrbitalFrame, 0, &orbit, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "evaluate orbital distance",
        failures);
    return orbit.current_distance_au;
}

double orbit_distance(
    const taiyin::runtime::NativeCalcContext& context,
    int body_id,
    const taiyin::SplitJulianDate& jd_ut,
    int* failures
) {
    return orbit_distance(
        context, body_id, taiyin::split_julian_date_to_double(jd_ut), failures);
}

double reference_plane_z(
    const taiyin::runtime::NativeCalcContext& context,
    int body_id,
    int center_id,
    double jd_ut,
    int reference_frame_id,
    int* failures
) {
    taiyin::runtime::NativeCalcContext geometry = context;
    geometry.observer_id = center_id;
    geometry.apparent_options.output_frame_id = reference_frame_id;
    taiyin::CartesianState state;
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;
    expect_status(
        taiyin::runtime::calc_state_ut(
            &geometry,
            body_id,
            split_jd(jd_ut),
            taiyin::runtime::TAIYIN_NATIVE_POSITION_TRUEPOS,
            &state,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "evaluate geometric reference-plane state",
        failures);
    return state.position_au.z;
}

double reference_plane_z(
    const taiyin::runtime::NativeCalcContext& context,
    int body_id,
    int center_id,
    const taiyin::SplitJulianDate& jd_ut,
    int reference_frame_id,
    int* failures
) {
    return reference_plane_z(
        context,
        body_id,
        center_id,
        taiyin::split_julian_date_to_double(jd_ut),
        reference_frame_id,
        failures);
}

}  // namespace

int main() {
    int failures = 0;
    if (!initialize_runtime(&failures)) return 1;
    const taiyin::runtime::NativeCalcContext context = make_context();
    taiyin::runtime::EphemerisEvalDiagnostic diagnostic;

    taiyin::runtime::BodyOsculatingOrbit moon;
    expect_status(
        taiyin::runtime::calc_body_osculating_orbit_ut(
            &context, taiyin::TAIYIN_BODY_MOON, kJdUt2024Apr, kDefaultOrbitalFrame,
            0, &moon, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "compute Moon osculating orbit",
        &failures);
    expect_true(moon.center_id == taiyin::TAIYIN_BODY_EARTH, "Moon center is Earth", &failures);
    expect_true(moon.eccentricity > 0.01 && moon.eccentricity < 0.2, "Moon eccentricity is physical", &failures);
    expect_true(moon.osculating_period_days > 20.0 && moon.osculating_period_days < 35.0,
                "Moon osculating period is physical", &failures);
    expect_true(moon.periapsis_distance_au < moon.current_distance_au
                    && moon.current_distance_au < moon.apoapsis_distance_au,
                "Moon current distance lies within osculating apsides", &failures);

    taiyin::runtime::BodyOrbitReferencePoints moon_points;
    expect_status(
        taiyin::runtime::calc_body_orbit_reference_points_ut(
            &context, taiyin::TAIYIN_BODY_MOON, kJdUt2024Apr, kDefaultOrbitalFrame,
            0, &moon_points, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "compute Moon osculating reference points",
        &failures);
    expect_true(
        moon_points.model
            == taiyin::runtime::TAIYIN_BODY_ORBIT_REFERENCE_POINTS_OSCULATING,
        "reference-point result records osculating model",
        &failures);
    expect_true(
        moon_points.body_id == taiyin::TAIYIN_BODY_MOON
            && moon_points.center_id == taiyin::TAIYIN_BODY_EARTH,
        "reference-point result records body and physical center",
        &failures);
    expect_true(
        moon_points.ascending_node.position_au.z == 0.0
            && moon_points.descending_node.position_au.z == 0.0,
        "osculating nodes lie exactly on the selected reference plane",
        &failures);
    expect_near(
        moon_points.ascending_node.longitude_rad,
        moon.longitude_of_ascending_node_rad,
        1.0e-12,
        "ascending-node longitude agrees with osculating elements",
        &failures);
    expect_near(
        moon_points.periapsis.distance_au,
        moon.periapsis_distance_au,
        1.0e-15,
        "periapsis point distance agrees with osculating elements",
        &failures);
    expect_near(
        moon_points.apoapsis.distance_au,
        moon.apoapsis_distance_au,
        1.0e-15,
        "apoapsis point distance agrees with osculating elements",
        &failures);
    expect_true(
        taiyin::vector3_dot(
            moon_points.periapsis.position_au,
            moon_points.apoapsis.position_au) < 0.0,
        "osculating periapsis and apoapsis are antipodal",
        &failures);
    expect_near(
        moon_points.second_focus.distance_au,
        2.0 * moon.semi_major_axis_au * moon.eccentricity,
        1.0e-15,
        "second-focus distance agrees with two-focus geometry",
        &failures);

    const double start_delta_t_seconds = taiyin::estimated_delta_t_seconds_from_ut1_jd(
        taiyin::split_julian_date_to_double(kJdUt2024Apr));
    taiyin::SplitJulianDate kJdTt2024Apr;
    taiyin::ut1_to_tt_split_jd(kJdUt2024Apr, start_delta_t_seconds, &kJdTt2024Apr);
    taiyin::runtime::BodyOsculatingOrbit moon_tt;
    expect_status(
        taiyin::runtime::calc_body_osculating_orbit_tt(
            &context, taiyin::TAIYIN_BODY_MOON, kJdTt2024Apr, kDefaultOrbitalFrame,
            0, &moon_tt, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "compute Moon osculating orbit in TT",
        &failures);
    expect_near(moon_tt.current_distance_au, moon.current_distance_au, 1.0e-13,
                "UT and TT osculating orbit distance agree", &failures);
    expect_near(moon_tt.eccentricity, moon.eccentricity, 1.0e-13,
                "UT and TT osculating orbit eccentricity agree", &failures);
    taiyin::runtime::BodyOrbitReferencePoints moon_points_tt;
    expect_status(
        taiyin::runtime::calc_body_orbit_reference_points_tt(
            &context, taiyin::TAIYIN_BODY_MOON, kJdTt2024Apr, kDefaultOrbitalFrame,
            0, &moon_points_tt, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "compute Moon osculating reference points in TT",
        &failures);
    expect_near(
        moon_points_tt.periapsis.longitude_rad,
        moon_points.periapsis.longitude_rad,
        1.0e-12,
        "UT and TT reference-point geometry agrees",
        &failures);

    taiyin::runtime::NativeCalcContext topocentric_context = context;
    const taiyin::runtime::NativeObserverLocation location =
        taiyin::runtime::native_observer_location_degrees(116.4, 39.9, 50.0);
    expect_status(
        taiyin::runtime::native_context_set_simple_topocentric_observer(
            &topocentric_context, location, kJdUt2024Apr,
            kJdUt2024Apr + 70.0 / taiyin::SECONDS_PER_DAY),
        taiyin::TAIYIN_STATUS_OK,
        "install a topocentric context",
        &failures);
    taiyin::runtime::BodyOsculatingOrbit topocentric_moon;
    expect_status(
        taiyin::runtime::calc_body_osculating_orbit_ut(
            &topocentric_context, taiyin::TAIYIN_BODY_MOON, kJdUt2024Apr, kDefaultOrbitalFrame, 0,
            &topocentric_moon, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "compute Moon orbit from a topocentric context",
        &failures);
    expect_near(topocentric_moon.current_distance_au, moon.current_distance_au, 1.0e-14,
                "orbital distance ignores context topocentric offset", &failures);
    expect_near(topocentric_moon.semi_major_axis_au, moon.semi_major_axis_au, 1.0e-14,
                "orbital elements use the fixed physical center", &failures);

    taiyin::runtime::BodyOsculatingOrbit venus;
    expect_status(
        taiyin::runtime::calc_body_osculating_orbit_ut(
            &context, taiyin::TAIYIN_BODY_VENUS_BARYCENTER, kJdUt2024Apr, kDefaultOrbitalFrame,
            0, &venus, &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "compute Venus barycenter osculating orbit",
        &failures);
    expect_true(venus.center_id == taiyin::TAIYIN_BODY_SUN, "Venus barycenter center is Sun", &failures);
    expect_true(venus.semi_major_axis_au > 0.6 && venus.semi_major_axis_au < 0.85,
                "Venus semi-major axis is physical", &failures);
    expect_true(venus.eccentricity > 0.0 && venus.eccentricity < 0.05,
                "Venus eccentricity is physical", &failures);

    taiyin::runtime::BodyApsisSearchResult venus_perihelion;
    expect_status(
        taiyin::runtime::search_next_body_apsis_ut(
            &context,
            taiyin::TAIYIN_BODY_VENUS_BARYCENTER,
            taiyin::runtime::TAIYIN_BODY_APSIS_PERICENTER,
            kJdUt2024Apr,
            0,
            &venus_perihelion,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "search next Venus barycenter perihelion",
        &failures);
    expect_true(venus_perihelion.center_id == taiyin::TAIYIN_BODY_SUN,
                "Venus perihelion center is Sun", &failures);
    const double venus_perihelion_before = orbit_distance(
        context, taiyin::TAIYIN_BODY_VENUS_BARYCENTER, venus_perihelion.jd - 1.0, &failures);
    const double venus_perihelion_after = orbit_distance(
        context, taiyin::TAIYIN_BODY_VENUS_BARYCENTER, venus_perihelion.jd + 1.0, &failures);
    expect_true(venus_perihelion.distance_au < venus_perihelion_before
                    && venus_perihelion.distance_au < venus_perihelion_after,
                "Venus perihelion is a local distance minimum", &failures);

    taiyin::runtime::BodyApsisSearchResult perigee;
    expect_status(
        taiyin::runtime::search_next_body_apsis_ut(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            taiyin::runtime::TAIYIN_BODY_APSIS_PERICENTER,
            kJdUt2024Apr,
            0,
            &perigee,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "search next lunar perigee",
        &failures);
    expect_true(perigee.jd > kJdUt2024Apr, "lunar perigee is in the future", &failures);
    expect_true(std::fabs(perigee.radial_velocity_au_per_day) < 1.0e-8,
                "lunar perigee radial velocity is near zero", &failures);
    // Swiss Ephemeris 2.10.03, SWIEPH se1 files, true position: 2024-05-05
    // 22:04:17 UT. Retain a small cross-implementation tolerance rather than
    // claiming bitwise equivalence.
    expect_near(perigee.jd, 2460436.4196451753, 1.0e-4,
                "lunar perigee vs Swiss se1 oracle", &failures);
    const double perigee_before = orbit_distance(context, taiyin::TAIYIN_BODY_MOON, perigee.jd - 0.1, &failures);
    const double perigee_after = orbit_distance(context, taiyin::TAIYIN_BODY_MOON, perigee.jd + 0.1, &failures);
    expect_true(perigee.distance_au < perigee_before && perigee.distance_au < perigee_after,
                "lunar perigee is a local distance minimum", &failures);

    taiyin::runtime::BodyApsisSearchResult perigee_tt;
    expect_status(
        taiyin::runtime::search_next_body_apsis_tt(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            taiyin::runtime::TAIYIN_BODY_APSIS_PERICENTER,
            kJdTt2024Apr,
            0,
            &perigee_tt,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "search lunar perigee in TT",
        &failures);
    taiyin::SplitJulianDate expected_perigee_tt;
    taiyin::ut1_to_tt_split_jd(
        perigee.jd,
        taiyin::estimated_delta_t_seconds_from_ut1_jd(
            taiyin::split_julian_date_to_double(perigee.jd)),
        &expected_perigee_tt);
    expect_near(
        perigee_tt.jd,
        expected_perigee_tt,
        1.0e-10,
        "UT and TT perigee searches agree",
        &failures);

    taiyin::runtime::BodyApsisSearchResult following_perigee;
    expect_status(
        taiyin::runtime::search_next_body_apsis_ut(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            taiyin::runtime::TAIYIN_BODY_APSIS_PERICENTER,
            perigee.jd,
            0,
            &following_perigee,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "search perigee after an exact perigee start",
        &failures);
    expect_true(following_perigee.jd > perigee.jd + 20.0,
                "exact perigee start advances to the following perigee", &failures);

    taiyin::runtime::BodyApsisSearchResult previous_apogee;
    expect_status(
        taiyin::runtime::search_next_body_apsis_ut(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            taiyin::runtime::TAIYIN_BODY_APSIS_APOCENTER,
            kJdUt2024Apr,
            taiyin::runtime::TAIYIN_ORBITAL_EVENT_REVERSE,
            &previous_apogee,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "search previous lunar apogee",
        &failures);
    expect_true(previous_apogee.jd < kJdUt2024Apr, "lunar apogee is in the past", &failures);
    // Swiss Ephemeris 2.10.03, SWIEPH se1 files, true position: 2024-03-23
    // 15:44:59 UT.
    expect_near(previous_apogee.jd, 2460393.1562406393, 1.0e-4,
                "previous lunar apogee vs Swiss se1 oracle", &failures);

    taiyin::runtime::BodyNodeSearchResult ascending_node;
    expect_status(
        taiyin::runtime::search_next_body_plane_node_ut(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            taiyin::runtime::TAIYIN_BODY_NODE_ASCENDING,
            kJdUt2024Apr,
            kDefaultOrbitalFrame,
            0,
            &ascending_node,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "search next lunar ascending node",
        &failures);
    expect_true(ascending_node.jd > kJdUt2024Apr, "ascending node is in the future", &failures);
    expect_true(std::fabs(reference_plane_z(
                    context,
                    taiyin::TAIYIN_BODY_MOON,
                    taiyin::TAIYIN_BODY_EARTH,
                    ascending_node.jd,
                    kDefaultOrbitalFrame,
                    &failures)) < 1.0e-8,
                "ascending node lies on J2000 ecliptic", &failures);
    // Swiss Ephemeris 2.10.03, SWIEPH se1 files, J2000 true ecliptic latitude root.
    expect_near(ascending_node.jd, 2460409.0138973210, 1.0e-4,
                "lunar ascending node vs Swiss se1 oracle", &failures);

    taiyin::runtime::BodyNodeSearchResult ascending_node_tt;
    expect_status(
        taiyin::runtime::search_next_body_plane_node_tt(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            taiyin::runtime::TAIYIN_BODY_NODE_ASCENDING,
            kJdTt2024Apr,
            kDefaultOrbitalFrame,
            0,
            &ascending_node_tt,
            &diagnostic),
        taiyin::TAIYIN_STATUS_OK,
        "search lunar ascending node in TT",
        &failures);
    taiyin::SplitJulianDate expected_ascending_node_tt;
    taiyin::ut1_to_tt_split_jd(
        ascending_node.jd,
        taiyin::estimated_delta_t_seconds_from_ut1_jd(
            taiyin::split_julian_date_to_double(ascending_node.jd)),
        &expected_ascending_node_tt);
    expect_near(
        ascending_node_tt.jd,
        expected_ascending_node_tt,
        1.0e-10,
        "UT and TT ascending-node searches agree",
        &failures);

    const int reference_frames[] = {
        taiyin::TAIYIN_APPARENT_FRAME_ICRF,
        taiyin::TAIYIN_APPARENT_FRAME_J2000_MEAN_EQUATOR,
        taiyin::TAIYIN_APPARENT_FRAME_J2000_ECLIPTIC,
        taiyin::TAIYIN_APPARENT_FRAME_MEAN_EQUATOR_OF_DATE,
        taiyin::TAIYIN_APPARENT_FRAME_TRUE_EQUATOR_OF_DATE,
        taiyin::TAIYIN_APPARENT_FRAME_MEAN_ECLIPTIC_OF_DATE,
        taiyin::TAIYIN_APPARENT_FRAME_TRUE_ECLIPTIC_OF_DATE,
        taiyin::TAIYIN_APPARENT_FRAME_CIRS,
    };
    taiyin::runtime::BodyOsculatingOrbit frame_orbits[8];
    taiyin::runtime::BodyNodeSearchResult frame_nodes[8];
    for (size_t i = 0; i < 8; ++i) {
        const int frame_id = reference_frames[i];
        expect_status(
            taiyin::runtime::calc_body_osculating_orbit_ut(
                &context, taiyin::TAIYIN_BODY_MOON, kJdUt2024Apr, frame_id, 0,
                &frame_orbits[i], &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            "compute Moon orbit in each supported reference frame",
            &failures);
        expect_true(frame_orbits[i].reference_frame_id == frame_id,
                    "orbit result records its reference frame", &failures);
        expect_near(frame_orbits[i].semi_major_axis_au, moon.semi_major_axis_au, 1.0e-12,
                    "semi-major axis is reference-frame invariant", &failures);
        expect_near(frame_orbits[i].eccentricity, moon.eccentricity, 1.0e-12,
                    "eccentricity is reference-frame invariant", &failures);
        expect_status(
            taiyin::runtime::search_next_body_plane_node_ut(
                &context, taiyin::TAIYIN_BODY_MOON,
                taiyin::runtime::TAIYIN_BODY_NODE_ASCENDING,
                kJdUt2024Apr, frame_id, 0, &frame_nodes[i], &diagnostic),
            taiyin::TAIYIN_STATUS_OK,
            "search ascending node in each supported reference frame",
            &failures);
        expect_true(frame_nodes[i].reference_frame_id == frame_id,
                    "node result records its reference frame", &failures);
        const double node_z = reference_plane_z(
                        context, taiyin::TAIYIN_BODY_MOON, taiyin::TAIYIN_BODY_EARTH,
                        frame_nodes[i].jd, frame_id, &failures);
        expect_true(std::fabs(node_z) < 1.0e-8,
                    "node lies on its selected reference plane", &failures);
    }
    expect_true(std::fabs(frame_nodes[0].jd - frame_nodes[2].jd) > 1.0e-10,
                "ICRF-plane and J2000-ecliptic nodes are distinct", &failures);
    expect_true(std::fabs(
                    frame_orbits[5].longitude_of_ascending_node_rad
                    - frame_orbits[6].longitude_of_ascending_node_rad) > 1.0e-7,
                "mean and true-of-date frames retain distinct origins", &failures);
    expect_near(frame_nodes[5].jd, frame_nodes[6].jd, 1.0e-12,
                "mean and true-of-date frames share the node plane", &failures);

    taiyin::runtime::BodyOsculatingOrbit invalid;
    expect_status(
        taiyin::runtime::calc_body_osculating_orbit_ut(
            &context, taiyin::TAIYIN_BODY_SUN, kJdUt2024Apr, kDefaultOrbitalFrame,
            0, &invalid, &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject Sun orbit request",
        &failures);
    expect_status(
        taiyin::runtime::calc_body_osculating_orbit_ut(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            kJdUt2024Apr,
            kDefaultOrbitalFrame,
            taiyin::runtime::TAIYIN_NATIVE_POSITION_TOPOCENTRIC,
            &invalid,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject observer-dependent orbital flags",
        &failures);
    expect_status(
        taiyin::runtime::calc_body_orbit_reference_points_ut(
            &context, taiyin::TAIYIN_BODY_MOON, kJdUt2024Apr,
            kDefaultOrbitalFrame, 0, nullptr, &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject null orbit-reference-point output",
        &failures);
    expect_status(
        taiyin::runtime::search_next_body_plane_node_ut(
            &context,
            taiyin::TAIYIN_BODY_MOON,
            taiyin::runtime::TAIYIN_BODY_NODE_ASCENDING,
            kJdUt2024Apr,
            999999,
            0,
            &ascending_node,
            &diagnostic),
        taiyin::TAIYIN_ERROR_INVALID_ARGUMENT,
        "reject an unknown reference frame",
        &failures);

    if (failures != 0) {
        std::cerr << failures << " orbital-event test(s) failed\n";
        return 1;
    }
    std::cout << "orbital-event tests passed\n";
    return 0;
}
