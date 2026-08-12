#include "taiyin/internal/ephemeris_block.h"

#include "taiyin/body_id.h"
#include "taiyin/internal/opm2.h"

#include <cmath>
#include <cstring>
#include <new>

namespace taiyin {
namespace internal {

const int PRIVATE_CELESTIAL_BODY_ID_START = TAIYIN_PRIVATE_CELESTIAL_BODY_ID_START;

struct GlobalIdRegistry {
    std::unordered_map<std::string, int> name_to_id;
    std::unordered_map<int, std::string> id_to_name;
    // Built-in solar-system bodies use strict NASA/NAIF IDs. Dynamically
    // registered stars and custom objects live in a private non-NAIF range.
    int next_dynamic_id = PRIVATE_CELESTIAL_BODY_ID_START;
};

static void register_builtin_body(GlobalIdRegistry& reg, const char* name, int id) noexcept {
    reg.name_to_id[name] = id;
    reg.id_to_name[id] = name;
}

static void register_builtin_alias(GlobalIdRegistry& reg, const char* alias, int id) noexcept {
    reg.name_to_id[alias] = id;
}

static void init_builtin_body_aliases(GlobalIdRegistry& reg) noexcept {
    register_builtin_body(reg, "solar_system_barycenter", TAIYIN_BODY_SOLAR_SYSTEM_BARYCENTER);
    register_builtin_alias(reg, "ssb", TAIYIN_BODY_SSB);

    register_builtin_body(reg, "mercury_barycenter", TAIYIN_BODY_MERCURY_BARYCENTER);
    register_builtin_body(reg, "venus_barycenter", TAIYIN_BODY_VENUS_BARYCENTER);
    register_builtin_body(reg, "earth_moon_barycenter", TAIYIN_BODY_EARTH_MOON_BARYCENTER);
    register_builtin_alias(reg, "emb", TAIYIN_BODY_EMB);
    register_builtin_body(reg, "mars_barycenter", TAIYIN_BODY_MARS_BARYCENTER);
    register_builtin_body(reg, "jupiter_barycenter", TAIYIN_BODY_JUPITER_BARYCENTER);
    register_builtin_body(reg, "saturn_barycenter", TAIYIN_BODY_SATURN_BARYCENTER);
    register_builtin_body(reg, "uranus_barycenter", TAIYIN_BODY_URANUS_BARYCENTER);
    register_builtin_body(reg, "neptune_barycenter", TAIYIN_BODY_NEPTUNE_BARYCENTER);
    register_builtin_body(reg, "pluto_barycenter", TAIYIN_BODY_PLUTO_BARYCENTER);

    register_builtin_body(reg, "sun", TAIYIN_BODY_SUN);
    register_builtin_body(reg, "mercury", TAIYIN_BODY_MERCURY);
    register_builtin_body(reg, "venus", TAIYIN_BODY_VENUS);
    register_builtin_body(reg, "moon", TAIYIN_BODY_MOON);
    register_builtin_alias(reg, "luna", TAIYIN_BODY_MOON);
    register_builtin_body(reg, "earth", TAIYIN_BODY_EARTH);
    register_builtin_body(reg, "mars", TAIYIN_BODY_MARS);
    register_builtin_body(reg, "jupiter", TAIYIN_BODY_JUPITER);
    register_builtin_body(reg, "saturn", TAIYIN_BODY_SATURN);
    register_builtin_body(reg, "uranus", TAIYIN_BODY_URANUS);
    register_builtin_body(reg, "neptune", TAIYIN_BODY_NEPTUNE);
    register_builtin_body(reg, "pluto", TAIYIN_BODY_PLUTO);

    register_builtin_body(reg, "phobos", TAIYIN_BODY_PHOBOS);
    register_builtin_body(reg, "deimos", TAIYIN_BODY_DEIMOS);
    register_builtin_body(reg, "io", TAIYIN_BODY_IO);
    register_builtin_body(reg, "europa", TAIYIN_BODY_EUROPA);
    register_builtin_body(reg, "ganymede", TAIYIN_BODY_GANYMEDE);
    register_builtin_body(reg, "callisto", TAIYIN_BODY_CALLISTO);
    register_builtin_body(reg, "mimas", TAIYIN_BODY_MIMAS);
    register_builtin_body(reg, "enceladus", TAIYIN_BODY_ENCELADUS);
    register_builtin_body(reg, "tethys", TAIYIN_BODY_TETHYS);
    register_builtin_body(reg, "dione", TAIYIN_BODY_DIONE);
    register_builtin_body(reg, "rhea", TAIYIN_BODY_RHEA);
    register_builtin_body(reg, "titan", TAIYIN_BODY_TITAN);
    register_builtin_body(reg, "hyperion", TAIYIN_BODY_HYPERION);
    register_builtin_body(reg, "iapetus", TAIYIN_BODY_IAPETUS);
    register_builtin_body(reg, "miranda", TAIYIN_BODY_MIRANDA);
    register_builtin_body(reg, "ariel", TAIYIN_BODY_ARIEL);
    register_builtin_body(reg, "umbriel", TAIYIN_BODY_UMBRIEL);
    register_builtin_body(reg, "titania", TAIYIN_BODY_TITANIA);
    register_builtin_body(reg, "oberon", TAIYIN_BODY_OBERON);
    register_builtin_body(reg, "triton", TAIYIN_BODY_TRITON);
    register_builtin_body(reg, "charon", TAIYIN_BODY_CHARON);
    register_builtin_body(reg, "nix", TAIYIN_BODY_NIX);
    register_builtin_body(reg, "hydra", TAIYIN_BODY_HYDRA);
    register_builtin_body(reg, "kerberos", TAIYIN_BODY_KERBEROS);
    register_builtin_body(reg, "styx", TAIYIN_BODY_STYX);

    register_builtin_body(reg, "ceres", TAIYIN_BODY_CERES);
    register_builtin_body(reg, "pallas", TAIYIN_BODY_PALLAS);
    register_builtin_body(reg, "juno", TAIYIN_BODY_JUNO);
    register_builtin_body(reg, "vesta", TAIYIN_BODY_VESTA);
    register_builtin_body(reg, "eros", TAIYIN_BODY_EROS);
    register_builtin_body(reg, "chiron", TAIYIN_BODY_CHIRON);
    register_builtin_body(reg, "pholus", TAIYIN_BODY_PHOLUS);
    register_builtin_body(reg, "nessus", TAIYIN_BODY_NESSUS);
    register_builtin_body(reg, "lilith", TAIYIN_BODY_LILITH);
}

static GlobalIdRegistry& get_global_id_registry() noexcept {
    static GlobalIdRegistry registry;
    static bool initialized = (init_builtin_body_aliases(registry), true);
    (void)initialized;
    return registry;
}

int register_celestial_body(const std::string& name) noexcept {
    auto& reg = get_global_id_registry();
    auto it = reg.name_to_id.find(name);
    if (it != reg.name_to_id.end()) {
        return it->second;
    }
    int new_id = reg.next_dynamic_id++;
    reg.name_to_id[name] = new_id;
    reg.id_to_name[new_id] = name;
    return new_id;
}

void register_celestial_body_alias(const std::string& alias, int id) noexcept {
    auto& reg = get_global_id_registry();
    reg.name_to_id[alias] = id;
}

bool query_celestial_body_id(const std::string& name, int* out_id) noexcept {
    if (!out_id) {
        return false;
    }
    const auto& reg = get_global_id_registry();
    auto it = reg.name_to_id.find(name);
    if (it == reg.name_to_id.end()) {
        return false;
    }
    *out_id = it->second;
    return true;
}

std::string query_celestial_body_name(int id) noexcept {
    const auto& reg = get_global_id_registry();
    auto it = reg.id_to_name.find(id);
    if (it != reg.id_to_name.end()) {
        return it->second;
    }
    return "";
}

void destroy_storage_ephemeris_block(StorageEphemerisBlock* storage) noexcept {
    if (!storage) return;
    if (storage->destroy_element) {
        for (size_t i = 0; i < storage->data_vector.size(); ++i) {
            void* element = storage->data_vector[i];
            if (element) {
                storage->destroy_element(element);
            }
        }
    }
    storage->data_vector.clear();
    storage->id_to_index.clear();
    storage->source_owner.reset();
    storage->total_bytes = 0;
}

bool get_compiled_block_from_storage(const StorageEphemerisBlock* storage, int target_id, CompiledEphemerisBlock* out) noexcept {
    if (!storage || !out) return false;
    if (storage->data_vector.size() == 1) {
        *out = CompiledEphemerisBlock();
        out->data = storage->data_vector[0];
        out->bytes = storage->total_bytes;
        out->position = storage->position;
        out->velocity = storage->velocity;
        out->position_velocity = storage->position_velocity;
        out->acceleration = storage->acceleration;
        out->state = storage->state;
        out->format = storage->format;
        return true;
    }

    auto it = storage->id_to_index.find(target_id);
    if (it == storage->id_to_index.end()) {
        return false;
    }
    size_t idx = it->second;
    if (idx >= storage->data_vector.size()) {
        return false;
    }

    *out = CompiledEphemerisBlock();
    out->data = storage->data_vector[idx];
    out->bytes = storage->total_bytes / storage->data_vector.size();
    out->position = storage->position;
    out->velocity = storage->velocity;
    out->position_velocity = storage->position_velocity;
    out->acceleration = storage->acceleration;
    out->state = storage->state;
    out->format = storage->format;
    return true;
}

namespace {

const double FINITE_DIFFERENCE_STEP_DAYS = 1.0e-3;

bool has_magic(const void* bytes, size_t byte_count, const char expected[4]) noexcept {
    if (!bytes || byte_count < 4 || !expected) {
        return false;
    }
    return std::memcmp(bytes, expected, 4) == 0;
}

bool finite_difference_velocity(
    const SplitJulianDate& jd_tdb,
    const CompiledEphemerisBlock* block,
    Vector3* out
) noexcept {
    if (!block || !block->data || !block->position || !out) {
        return false;
    }

    SplitJulianDate jd_minus;
    SplitJulianDate jd_plus;
    if (!add_days_to_split_jd(jd_tdb, -FINITE_DIFFERENCE_STEP_DAYS, &jd_minus)
        || !add_days_to_split_jd(jd_tdb, FINITE_DIFFERENCE_STEP_DAYS, &jd_plus)) {
        return false;
    }
    Vector3 p_minus;
    Vector3 p_plus;
    if (!block->position(jd_minus, block->data, &p_minus)
        || !block->position(jd_plus, block->data, &p_plus)) {
        return false;
    }

    const double scale = 1.0 / (2.0 * FINITE_DIFFERENCE_STEP_DAYS);
    out->x = (p_plus.x - p_minus.x) * scale;
    out->y = (p_plus.y - p_minus.y) * scale;
    out->z = (p_plus.z - p_minus.z) * scale;
    return true;
}

bool finite_difference_acceleration(
    const SplitJulianDate& jd_tdb,
    const CompiledEphemerisBlock* block,
    Vector3* out
) noexcept {
    if (!block || !block->data || !block->position || !out) {
        return false;
    }

    SplitJulianDate jd_minus;
    SplitJulianDate jd_plus;
    if (!add_days_to_split_jd(jd_tdb, -FINITE_DIFFERENCE_STEP_DAYS, &jd_minus)
        || !add_days_to_split_jd(jd_tdb, FINITE_DIFFERENCE_STEP_DAYS, &jd_plus)) {
        return false;
    }
    Vector3 p0;
    Vector3 p_minus;
    Vector3 p_plus;
    if (!block->position(jd_tdb, block->data, &p0)
        || !block->position(jd_minus, block->data, &p_minus)
        || !block->position(jd_plus, block->data, &p_plus)) {
        return false;
    }

    const double scale = 1.0 / (FINITE_DIFFERENCE_STEP_DAYS * FINITE_DIFFERENCE_STEP_DAYS);
    out->x = (p_plus.x - 2.0 * p0.x + p_minus.x) * scale;
    out->y = (p_plus.y - 2.0 * p0.y + p_minus.y) * scale;
    out->z = (p_plus.z - 2.0 * p0.z + p_minus.z) * scale;
    return true;
}

}  // namespace

bool make_compiled_ephemeris_block(
    const void* data,
    size_t bytes,
    EphemerisPositionFn position,
    EphemerisVelocityFn velocity,
    EphemerisPositionVelocityFn position_velocity,
    EphemerisAccelerationFn acceleration,
    CompiledEphemerisBlock* out
) noexcept {
    if (!data || !position || !out) {
        return false;
    }

    *out = CompiledEphemerisBlock();
    out->data = data;
    out->bytes = bytes;
    out->position = position;
    out->velocity = velocity;
    out->position_velocity = position_velocity;
    out->acceleration = acceleration;
    out->format = EphemerisBlockFormat::FormatUnknown;
    return true;
}

bool compile_ephemeris_block(
    const void* bytes,
    size_t byte_count,
    const EphemerisBlockCompileOptions* options,
    StorageEphemerisBlock* out
) noexcept {
    if (!bytes || !out) {
        return false;
    }

    *out = StorageEphemerisBlock();

    if (has_magic(bytes, byte_count, "OPM2")) {
        Opm2EphemerisData* ephemeris = 0;
        if (options && options->has_required_jd_tdb_range) {
            if (!compile_opm2_ephemeris_data_for_range(
                    bytes,
                    byte_count,
                    options->required_jd_tdb_start,
                    options->required_jd_tdb_end,
                    &ephemeris)) {
                return false;
            }
        } else if (!compile_opm2_ephemeris_data(bytes, byte_count, &ephemeris)) {
            return false;
        }

        out->format = EphemerisBlockFormat::Opm2;
        out->position = calc_opm2_position_void;
        out->velocity = calc_opm2_velocity_void;
        out->position_velocity = calc_opm2_position_velocity_void;
        out->acceleration = calc_opm2_acceleration_void;
        out->state = calc_opm2_state_void;
        out->destroy_element = opm2_ephemeris_data_destroy_void;
        out->data_vector.push_back(ephemeris);
        out->total_bytes = ephemeris->bytes;
        return true;
    }

    return false;
}

bool eval_compiled_ephemeris_block(
    const SplitJulianDate& jd_tdb,
    const CompiledEphemerisBlock* block,
    CartesianState* out
) noexcept {
    return eval_compiled_ephemeris_block_components(
        jd_tdb,
        block,
        EPHEMERIS_BLOCK_COMPONENT_STATE,
        out);
}

bool eval_compiled_ephemeris_block_components(
    const SplitJulianDate& jd_tdb,
    const CompiledEphemerisBlock* block,
    uint32_t components,
    CartesianState* out
) noexcept {
    if (!block || !block->data || !block->position || !out) {
        return false;
    }

    components &= EPHEMERIS_BLOCK_COMPONENT_STATE;
    if (components == 0u) {
        components = EPHEMERIS_BLOCK_COMPONENT_POSITION;
    }

    *out = CartesianState();
    // A block that supplies the position/velocity pair can answer POSITION +
    // VELOCITY with one segment decode and one model-frame transform, without
    // evaluating the acceleration that a full-state callback would compute.
    if (block->position_velocity
        && (components & EPHEMERIS_BLOCK_COMPONENT_VELOCITY) != 0u
        && (components & EPHEMERIS_BLOCK_COMPONENT_ACCELERATION) == 0u) {
        Vector3 position;
        Vector3 velocity;
        if (!block->position_velocity(jd_tdb, block->data, &position, &velocity)) {
            return false;
        }
        if ((components & EPHEMERIS_BLOCK_COMPONENT_POSITION) != 0u) {
            out->position_au = position;
        }
        if ((components & EPHEMERIS_BLOCK_COMPONENT_VELOCITY) != 0u) {
            out->velocity_au_per_day = velocity;
        }
        return true;
    }
    if (block->state
        && (components & (EPHEMERIS_BLOCK_COMPONENT_VELOCITY | EPHEMERIS_BLOCK_COMPONENT_ACCELERATION)) != 0u) {
        CartesianState state;
        if (!block->state(jd_tdb, block->data, &state)) {
            return false;
        }
        if ((components & EPHEMERIS_BLOCK_COMPONENT_POSITION) != 0u) {
            out->position_au = state.position_au;
        }
        if ((components & EPHEMERIS_BLOCK_COMPONENT_VELOCITY) != 0u) {
            out->velocity_au_per_day = state.velocity_au_per_day;
        }
        if ((components & EPHEMERIS_BLOCK_COMPONENT_ACCELERATION) != 0u) {
            out->acceleration_au_per_day2 = state.acceleration_au_per_day2;
        }
        return true;
    }

    if ((components & EPHEMERIS_BLOCK_COMPONENT_POSITION) != 0u
        && !block->position(jd_tdb, block->data, &out->position_au)) {
        return false;
    }
    if ((components & EPHEMERIS_BLOCK_COMPONENT_VELOCITY) != 0u
        && !(block->velocity
            ? block->velocity(jd_tdb, block->data, &out->velocity_au_per_day)
            : finite_difference_velocity(jd_tdb, block, &out->velocity_au_per_day))) {
        return false;
    }
    if ((components & EPHEMERIS_BLOCK_COMPONENT_ACCELERATION) != 0u
        && !(block->acceleration
            ? block->acceleration(jd_tdb, block->data, &out->acceleration_au_per_day2)
            : finite_difference_acceleration(jd_tdb, block, &out->acceleration_au_per_day2))) {
        return false;
    }
    return true;
}

bool eval_compiled_ephemeris_block_position(
    const SplitJulianDate& jd_tdb,
    const CompiledEphemerisBlock* block,
    Vector3* out
) noexcept {
    if (!out || !block || !block->data || !block->position) {
        return false;
    }
    return block->position(jd_tdb, block->data, out);
}

bool eval_compiled_ephemeris_block_velocity(
    const SplitJulianDate& jd_tdb,
    const CompiledEphemerisBlock* block,
    Vector3* out
) noexcept {
    if (!out || !block || !block->data || !block->position) {
        return false;
    }
    if (block->velocity) {
        return block->velocity(jd_tdb, block->data, out);
    }
    return finite_difference_velocity(jd_tdb, block, out);
}

bool eval_compiled_ephemeris_block_acceleration(
    const SplitJulianDate& jd_tdb,
    const CompiledEphemerisBlock* block,
    Vector3* out
) noexcept {
    if (!out || !block || !block->data || !block->position) {
        return false;
    }
    if (block->acceleration) {
        return block->acceleration(jd_tdb, block->data, out);
    }
    return finite_difference_acceleration(jd_tdb, block, out);
}

}  // namespace internal
}  // namespace taiyin
