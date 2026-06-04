#include "taiyin/stars.h"
#include "taiyin/angle.h"
#include "taiyin/internal/ephemeris_block.h"

#include <cmath>
#include <iostream>
#include <fstream>
#include <cstdio>

namespace {

bool near(double actual, double expected, double tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}

void expect_near(double actual, double expected, double tolerance, const char* message, int* failures) {
    if (!near(actual, expected, tolerance)) {
        std::cerr.precision(15);
        std::cerr << "FAIL: " << message
                  << ": actual=" << actual
                  << " expected=" << expected
                  << " diff=" << std::fabs(actual - expected)
                  << " tolerance=" << tolerance << "\n";
        ++(*failures);
    }
}

bool star_catalog_load_from_file(taiyin::StarCatalog* catalog, const std::string& filepath) noexcept {
    if (!catalog) return false;
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    try {
        catalog->owned_buffer.resize(size);
    } catch (...) {
        return false;
    }

    if (!file.read(reinterpret_cast<char*>(catalog->owned_buffer.data()), size)) {
        catalog->owned_buffer.clear();
        return false;
    }

    file.close();

    if (!taiyin::star_catalog_load_from_memory(catalog, catalog->owned_buffer.data(), catalog->owned_buffer.size())) {
        catalog->owned_buffer.clear();
        catalog->records = nullptr;
        catalog->star_count = 0;
        catalog->alias_map.clear();
        return false;
    }

    return true;
}

void test_file_catalog(int* failures) {
    const char* filename = "test_catalog.tsc";
    
    // Create header
    taiyin::StarCatalogHeader header = {
        { 'T', 'S', 'C', 'A' },
        1,
        1,
        {0}
    };
    
    // Create one record
    taiyin::BinaryStarRecord record = {
        "test_star",
        "Test Star",
        "alias1,alias2",
        1.5,
        100.0,
        20.0,
        10.0,
        -5.0,
        100.0,
        15.0
    };
    
    // Write to file
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "FAIL: Cannot open temp file for writing\n";
        ++(*failures);
        return;
    }
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&record), sizeof(record));
    file.close();
    
    // Load from file
    taiyin::StarCatalog catalog;
    if (!star_catalog_load_from_file(&catalog, filename)) {
        std::cerr << "FAIL: star_catalog_load_from_file failed\n";
        ++(*failures);
        std::remove(filename);
        return;
    }
    
    expect_near(catalog.star_count, 1, 0, "file catalog star count", failures);
    
    taiyin::StarData data;
    if (!taiyin::star_catalog_find_star(&catalog, "alias2", &data)) {
        std::cerr << "FAIL: find_star by alias in file catalog failed\n";
        ++(*failures);
    } else {
        expect_near(data.ra_j2000_deg, 100.0, 1e-9, "file catalog star RA", failures);
        expect_near(data.dec_j2000_deg, 20.0, 1e-9, "file catalog star Dec", failures);
        expect_near(data.pm_ra_mas_yr, 10.0, 1e-9, "file catalog star PM RA", failures);
        expect_near(data.radial_velocity_km_s, 15.0, 1e-9, "file catalog star RV", failures);
    }
    
    // Clean up
    taiyin::star_catalog_destroy(&catalog);
    std::remove(filename);
}

}  // namespace

int main() {
    int failures = 0;

    // 1. Test Builtin Catalog Loading and size
    {
        const auto* catalog = taiyin::get_builtin_star_catalog();
        expect_near(catalog->star_count, 3, 0, "Builtin catalog count", &failures);

        // Find Spica by ID, Name, and Aliases
        taiyin::StarData spica_data;
        if (!taiyin::star_catalog_find_star(catalog, "spica", &spica_data)) {
            std::cerr << "FAIL: Could not find Spica by ID\n";
            ++failures;
        } else {
            expect_near(spica_data.ra_j2000_deg, 201.298247375, 1e-12, "Spica RA", &failures);
        }

        if (!taiyin::star_catalog_find_star(catalog, "Spica", &spica_data)) {
            std::cerr << "FAIL: Could not find Spica by Name\n";
            ++failures;
        }

        if (!taiyin::star_catalog_find_star(catalog, "alpha_vir", &spica_data)) {
            std::cerr << "FAIL: Could not find Spica by Alias\n";
            ++failures;
        }

        // Find GC
        taiyin::StarData gc_data;
        if (!taiyin::star_catalog_find_star(catalog, "gc", &gc_data)) {
            std::cerr << "FAIL: Could not find Galactic Center by alias 'gc'\n";
            ++failures;
        } else {
            expect_near(gc_data.ra_j2000_deg, 266.4168371, 1e-11, "GC RA", &failures);
        }
    }

    // 2. Test File Loading
    test_file_catalog(&failures);

    // 3. Test J2000 Spica coordinates propagation (dt = 0)
    {
        const auto* catalog = taiyin::get_builtin_star_catalog();
        int spica_id = taiyin::internal::register_celestial_body("spica");

        taiyin::internal::CompiledEphemerisBlock block;
        if (!taiyin::star_catalog_get_compiled_block(catalog, spica_id, &block)) {
            std::cerr << "FAIL: Could not get Spica compiled block\n";
            ++failures;
        } else {
            taiyin::Vector3 pos;
            if (!taiyin::internal::eval_compiled_ephemeris_block_position(2451545.0, &block, &pos)) {
                std::cerr << "FAIL: Could not evaluate Spica position\n";
                ++failures;
            } else {
                double ra_rad = 0.0, dec_rad = 0.0, radius = 0.0;
                taiyin::cartesian_to_spherical(pos, &ra_rad, &dec_rad, &radius);
                ra_rad = taiyin::normalize_radians(ra_rad);

                expect_near(ra_rad, 201.298247375 * taiyin::TAIYIN_DEG_TO_RAD, 1e-12, "Spica RA J2000", &failures);
                expect_near(dec_rad, -11.161319472 * taiyin::TAIYIN_DEG_TO_RAD, 1e-12, "Spica Dec J2000", &failures);
            }
        }
    }

    // 4. Test J2024 Spica coordinates propagation (dt = 24.0 years)
    // Julian Date: 2460310.5
    {
        const auto* catalog = taiyin::get_builtin_star_catalog();
        int spica_id = taiyin::internal::register_celestial_body("spica");

        taiyin::internal::CompiledEphemerisBlock block;
        if (!taiyin::star_catalog_get_compiled_block(catalog, spica_id, &block)) {
            std::cerr << "FAIL: Could not get Spica compiled block\n";
            ++failures;
        } else {
            taiyin::Vector3 pos;
            if (!taiyin::internal::eval_compiled_ephemeris_block_position(2460310.5, &block, &pos)) {
                std::cerr << "FAIL: Could not evaluate Spica position J2024\n";
                ++failures;
            } else {
                double ra_rad = 0.0, dec_rad = 0.0, radius = 0.0;
                taiyin::cartesian_to_spherical(pos, &ra_rad, &dec_rad, &radius);
                ra_rad = taiyin::normalize_radians(ra_rad);

                double expected_ra_deg = 201.297959614589;
                double expected_dec_deg = -11.161523927152492;

                expect_near(ra_rad * taiyin::TAIYIN_RAD_TO_DEG, expected_ra_deg, 1e-8, "Spica RA J2024", &failures);
                expect_near(dec_rad * taiyin::TAIYIN_RAD_TO_DEG, expected_dec_deg, 1e-8, "Spica Dec J2024", &failures);
            }
        }
    }

    // 5. Test ecliptic_position J2000 under identity precession and custom obliquity
    {
        const auto* catalog = taiyin::get_builtin_star_catalog();
        int spica_id = taiyin::internal::register_celestial_body("spica");

        taiyin::internal::CompiledEphemerisBlock block;
        if (!taiyin::star_catalog_get_compiled_block(catalog, spica_id, &block)) {
            std::cerr << "FAIL: Could not get Spica compiled block\n";
            ++failures;
        } else {
            taiyin::Vector3 pos;
            if (!taiyin::internal::eval_compiled_ephemeris_block_position(2451545.0, &block, &pos)) {
                std::cerr << "FAIL: Could not evaluate Spica position\n";
                ++failures;
            } else {
                taiyin::Matrix3x3 precession = {
                    { { 1.0, 0.0, 0.0 },
                      { 0.0, 1.0, 0.0 },
                      { 0.0, 0.0, 1.0 } }
                };
                double eps = 23.4392911 * taiyin::TAIYIN_DEG_TO_RAD;

                taiyin::Vector3 v_precessed = taiyin::matrix3x3_multiply_vector(precession, pos);
                taiyin::Vector3 v_ecliptic = taiyin::rotate_x(v_precessed, -eps);

                double lon_rad = 0.0, lat_rad = 0.0, radius = 0.0;
                taiyin::cartesian_to_spherical(v_ecliptic, &lon_rad, &lat_rad, &radius);
                double lon_deg = taiyin::normalize_degrees(lon_rad * taiyin::TAIYIN_RAD_TO_DEG);
                double lat_deg = lat_rad * taiyin::TAIYIN_RAD_TO_DEG;

                expect_near(lon_deg, 203.84135780790072, 1e-10, "Spica ecliptic longitude J2000", &failures);
                expect_near(lat_deg, -2.054488654753366, 1e-10, "Spica ecliptic latitude J2000", &failures);
            }
        }
    }

    if (failures == 0) {
        std::cout << "test_stars: ALL TESTS PASSED\n";
        return 0;
    } else {
        std::cerr << "test_stars: " << failures << " FAILURES\n";
        return 1;
    }
}
