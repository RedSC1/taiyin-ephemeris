#include "taiyin/internal/ephemeris_block.h"

#include "taiyin/internal/opm4.h"

#include <cmath>
#include <cstring>
#include <new>

namespace taiyin {
namespace internal {

const int PRIVATE_CELESTIAL_BODY_ID_START = 1000000000;

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
    register_builtin_body(reg, "solar_system_barycenter", 0);
    register_builtin_alias(reg, "ssb", 0);

    register_builtin_body(reg, "mercury_barycenter", 1);
    register_builtin_body(reg, "venus_barycenter", 2);
    register_builtin_body(reg, "earth_moon_barycenter", 3);
    register_builtin_alias(reg, "emb", 3);
    register_builtin_body(reg, "mars_barycenter", 4);
    register_builtin_body(reg, "jupiter_barycenter", 5);
    register_builtin_body(reg, "saturn_barycenter", 6);
    register_builtin_body(reg, "uranus_barycenter", 7);
    register_builtin_body(reg, "neptune_barycenter", 8);
    register_builtin_body(reg, "pluto_barycenter", 9);

    register_builtin_body(reg, "sun", 10);
    register_builtin_body(reg, "mercury", 199);
    register_builtin_body(reg, "venus", 299);
    register_builtin_body(reg, "moon", 301);
    register_builtin_alias(reg, "luna", 301);
    register_builtin_body(reg, "earth", 399);
    register_builtin_body(reg, "mars", 499);
    register_builtin_body(reg, "jupiter", 599);
    register_builtin_body(reg, "saturn", 699);
    register_builtin_body(reg, "uranus", 799);
    register_builtin_body(reg, "neptune", 899);
    register_builtin_body(reg, "pluto", 999);

    register_builtin_body(reg, "phobos", 401);
    register_builtin_body(reg, "deimos", 402);
    register_builtin_body(reg, "io", 501);
    register_builtin_body(reg, "europa", 502);
    register_builtin_body(reg, "ganymede", 503);
    register_builtin_body(reg, "callisto", 504);
    register_builtin_body(reg, "titan", 606);
    register_builtin_body(reg, "triton", 801);
    register_builtin_body(reg, "charon", 901);

    register_builtin_body(reg, "ceres", 2000001);
    register_builtin_body(reg, "pallas", 2000002);
    register_builtin_body(reg, "juno", 2000003);
    register_builtin_body(reg, "vesta", 2000004);
    register_builtin_body(reg, "eros", 2000433);
    register_builtin_body(reg, "chiron", 20002060);
    register_builtin_body(reg, "pholus", 20005145);
    register_builtin_body(reg, "nessus", 20007066);
    register_builtin_body(reg, "lilith", 20001181);
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
        out->acceleration = storage->acceleration;
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
    out->acceleration = storage->acceleration;
    out->format = storage->format;
    return true;
}

namespace {

struct StateEvalAdapterData {
    void* data;
    CartesianStateEvalFn eval;
    EphemerisBlockDestroyFn destroy;
};

bool has_magic(const void* bytes, size_t byte_count, const char expected[4]) noexcept {
    if (!bytes || byte_count < 4 || !expected) {
        return false;
    }
    return std::memcmp(bytes, expected, 4) == 0;
}

bool eval_position_from_state(double jd_tdb, CartesianStateEvalFn eval, const void* data, Vector3* out) noexcept {
    if (!eval || !out) {
        return false;
    }
    CartesianState state;
    if (!eval(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.position_au;
    return true;
}

bool eval_velocity_from_state(double jd_tdb, CartesianStateEvalFn eval, const void* data, Vector3* out) noexcept {
    if (!eval || !out) {
        return false;
    }
    CartesianState state;
    if (!eval(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.velocity_au_per_day;
    return true;
}

bool eval_acceleration_from_state(double jd_tdb, CartesianStateEvalFn eval, const void* data, Vector3* out) noexcept {
    if (!eval || !out) {
        return false;
    }
    CartesianState state;
    if (!eval(jd_tdb, data, &state)) {
        return false;
    }
    *out = state.acceleration_au_per_day2;
    return true;
}

bool calc_opm_state_void_position(double jd_tdb, const void* data, Vector3* out) noexcept {
    return eval_position_from_state(jd_tdb, calc_opm_state_void, data, out);
}

bool calc_opm_state_void_velocity(double jd_tdb, const void* data, Vector3* out) noexcept {
    return eval_velocity_from_state(jd_tdb, calc_opm_state_void, data, out);
}

bool calc_opm_state_void_acceleration(double jd_tdb, const void* data, Vector3* out) noexcept {
    return eval_acceleration_from_state(jd_tdb, calc_opm_state_void, data, out);
}

bool state_eval_adapter_position(double jd_tdb, const void* data, Vector3* out) noexcept {
    const StateEvalAdapterData* adapter = static_cast<const StateEvalAdapterData*>(data);
    return adapter && eval_position_from_state(jd_tdb, adapter->eval, adapter->data, out);
}

bool state_eval_adapter_velocity(double jd_tdb, const void* data, Vector3* out) noexcept {
    const StateEvalAdapterData* adapter = static_cast<const StateEvalAdapterData*>(data);
    return adapter && eval_velocity_from_state(jd_tdb, adapter->eval, adapter->data, out);
}

bool state_eval_adapter_acceleration(double jd_tdb, const void* data, Vector3* out) noexcept {
    const StateEvalAdapterData* adapter = static_cast<const StateEvalAdapterData*>(data);
    return adapter && eval_acceleration_from_state(jd_tdb, adapter->eval, adapter->data, out);
}

void destroy_state_eval_adapter_void(void* data) noexcept {
    StateEvalAdapterData* adapter = static_cast<StateEvalAdapterData*>(data);
    if (!adapter) {
        return;
    }
    if (adapter->data && adapter->destroy) {
        adapter->destroy(adapter->data);
    }
    delete adapter;
}

bool compile_opm4_block(
    const void* bytes,
    size_t byte_count,
    const EphemerisBlockCompileOptions* options,
    StorageEphemerisBlock* out
) noexcept {
    OpmEphemerisData* ephemeris = 0;
    if (options && options->has_required_jd_tdb_range) {
        const OpmCompileRange range(options->required_jd_tdb_start, options->required_jd_tdb_end);
        if (!compile_opm4_ephemeris_data_for_range(bytes, byte_count, range, &ephemeris)) {
            return false;
        }
    } else {
        if (!compile_opm4_ephemeris_data(bytes, byte_count, &ephemeris)) {
            return false;
        }
    }

    *out = StorageEphemerisBlock();
    out->cache_id = 0;
    out->format = EphemerisBlockFormat::Opm4;
    out->position = calc_opm_state_void_position;
    out->velocity = calc_opm_state_void_velocity;
    out->acceleration = calc_opm_state_void_acceleration;
    out->destroy_element = opm_ephemeris_data_destroy_void;
    out->data_vector.push_back(ephemeris);
    out->total_bytes = ephemeris->get_total_allocated_bytes();
    return true;
}

}  // namespace

bool make_compiled_ephemeris_block(
    const void* data,
    size_t bytes,
    EphemerisPositionFn position,
    EphemerisVelocityFn velocity,
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
    out->acceleration = acceleration;
    out->format = EphemerisBlockFormat::FormatUnknown;
    return true;
}

bool make_compiled_ephemeris_block_from_state_eval(
    const void* data,
    size_t bytes,
    CartesianStateEvalFn eval,
    uint32_t available_components,
    CompiledEphemerisBlock* out
) noexcept {
    if (!data || !eval || !out
        || (available_components & EPHEMERIS_BLOCK_COMPONENT_POSITION) == 0u) {
        return false;
    }

    StateEvalAdapterData* adapter = new (std::nothrow) StateEvalAdapterData();
    if (!adapter) {
        return false;
    }
    adapter->data = const_cast<void*>(data);
    adapter->eval = eval;
    adapter->destroy = 0;

    *out = CompiledEphemerisBlock();
    out->data = adapter;
    out->bytes = bytes + sizeof(StateEvalAdapterData);
    out->position = state_eval_adapter_position;
    out->velocity = (available_components & EPHEMERIS_BLOCK_COMPONENT_VELOCITY) != 0u
        ? state_eval_adapter_velocity
        : 0;
    out->acceleration = (available_components & EPHEMERIS_BLOCK_COMPONENT_ACCELERATION) != 0u
        ? state_eval_adapter_acceleration
        : 0;
    out->format = EphemerisBlockFormat::FormatUnknown;
    return true;
}

bool make_ephemeris_block_cache_metadata(
    const EphemerisBlockCacheKey& key,
    double jd_tdb_start,
    double jd_tdb_end,
    int priority,
    const CompiledEphemerisBlock* block,
    EphemerisBlockCacheMetadata* out
) noexcept {
    if (!block || !block->data || !block->position || !out
        || !std::isfinite(jd_tdb_start)
        || !std::isfinite(jd_tdb_end)
        || jd_tdb_end < jd_tdb_start) {
        return false;
    }

    *out = EphemerisBlockCacheMetadata();
    out->key = key;
    out->jd_tdb_start = jd_tdb_start;
    out->jd_tdb_end = jd_tdb_end;
    out->priority = priority;
    out->bytes = block->bytes;
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

    if (has_magic(bytes, byte_count, "OPM4")) {
        return compile_opm4_block(bytes, byte_count, options, out);
    }

    return false;
}

bool eval_compiled_ephemeris_block(
    double jd_tdb,
    const CompiledEphemerisBlock* block,
    CartesianState* out
) noexcept {
    if (!block || !block->data || !block->position || !out) {
        return false;
    }

    *out = CartesianState();
    if (!block->position(jd_tdb, block->data, &out->position_au)) {
        return false;
    }
    if (block->velocity && !block->velocity(jd_tdb, block->data, &out->velocity_au_per_day)) {
        return false;
    }
    if (block->acceleration && !block->acceleration(jd_tdb, block->data, &out->acceleration_au_per_day2)) {
        return false;
    }
    return true;
}

bool eval_compiled_ephemeris_block_position(
    double jd_tdb,
    const CompiledEphemerisBlock* block,
    Vector3* out
) noexcept {
    if (!out || !block || !block->data || !block->position) {
        return false;
    }
    return block->position(jd_tdb, block->data, out);
}

bool eval_compiled_ephemeris_block_velocity(
    double jd_tdb,
    const CompiledEphemerisBlock* block,
    Vector3* out
) noexcept {
    if (!out || !block || !block->data || !block->velocity) {
        return false;
    }
    return block->velocity(jd_tdb, block->data, out);
}

bool eval_compiled_ephemeris_block_acceleration(
    double jd_tdb,
    const CompiledEphemerisBlock* block,
    Vector3* out
) noexcept {
    if (!out || !block || !block->data || !block->acceleration) {
        return false;
    }
    return block->acceleration(jd_tdb, block->data, out);
}

}  // namespace internal
}  // namespace taiyin
