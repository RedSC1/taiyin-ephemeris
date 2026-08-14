#include "taiyin/dispatch.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace taiyin {
namespace dispatch {

namespace wrappers {
void register_builtin_refraction_wrappers();
void register_builtin_precession_wrappers();
void register_builtin_nutation_wrappers();
void register_builtin_tdb_wrappers();
void register_builtin_frame_route_wrappers();
void register_builtin_delta_t_wrappers();
void register_builtin_aberration_wrappers();
void register_builtin_eclipse_shadow_wrappers();
void register_builtin_eclipse_moon_radius_wrappers();
void register_builtin_heliacal_visibility_wrappers();
}  // namespace wrappers

static std::atomic<uint64_t>& model_registry_generation_counter() noexcept {
    static std::atomic<uint64_t> generation(1u);
    return generation;
}

static void advance_model_registry_generation() noexcept {
    const uint64_t next = model_registry_generation_counter().fetch_add(
        1u, std::memory_order_release) + 1u;
    if (next == 0u) {
        model_registry_generation_counter().fetch_add(
            1u, std::memory_order_release);
    }
}

static bool& builtin_registration_in_progress() {
    static bool in_progress = false;
    return in_progress;
}

static void register_builtin_wrappers() {
    static bool registered = []() -> bool {
        builtin_registration_in_progress() = true;
        wrappers::register_builtin_refraction_wrappers();
        wrappers::register_builtin_precession_wrappers();
        wrappers::register_builtin_nutation_wrappers();
        wrappers::register_builtin_tdb_wrappers();
        wrappers::register_builtin_frame_route_wrappers();
        wrappers::register_builtin_delta_t_wrappers();
        wrappers::register_builtin_aberration_wrappers();
        wrappers::register_builtin_eclipse_shadow_wrappers();
        wrappers::register_builtin_eclipse_moon_radius_wrappers();
        wrappers::register_builtin_heliacal_visibility_wrappers();
        builtin_registration_in_progress() = false;
        return true;
    }();
    (void)registered;
}

uint64_t model_registry_generation() noexcept {
    register_builtin_wrappers();
    return model_registry_generation_counter().load(std::memory_order_acquire);
}

// --- Registry storage ---

static std::mutex& dispatch_mutex() {
    static std::mutex mutex;
    return mutex;
}

static std::unordered_map<int, RefractionFn>& refraction_models() {
    static std::unordered_map<int, RefractionFn> models;
    return models;
}

static std::unordered_map<int, HeliacalVisibilityModelEntry>& heliacal_visibility_models() {
    static std::unordered_map<int, HeliacalVisibilityModelEntry> models;
    return models;
}

static std::unordered_map<int, PrecessionModelEntry>& precession_models() {
    static std::unordered_map<int, PrecessionModelEntry> models;
    return models;
}

static std::unordered_map<int, NutationModelEntry>& nutation_models() {
    static std::unordered_map<int, NutationModelEntry> models;
    return models;
}

static std::vector<int>& precession_priority_order() {
    static std::vector<int> order;
    return order;
}

static std::vector<int>& nutation_priority_order() {
    static std::vector<int> order;
    return order;
}

static std::unordered_map<int, TdbFn>& tdb_models() {
    static std::unordered_map<int, TdbFn> models;
    return models;
}

static std::unordered_map<int, TdbInverseFn>& tdb_inverse_models() {
    static std::unordered_map<int, TdbInverseFn> models;
    return models;
}

static std::unordered_map<int, DeltaTModelEntry>& delta_t_models() {
    static std::unordered_map<int, DeltaTModelEntry> models;
    return models;
}

static std::unordered_map<int, AberrationModelEntry>& aberration_models() {
    static std::unordered_map<int, AberrationModelEntry> models;
    return models;
}

static std::vector<int>& aberration_priority_order() {
    static std::vector<int> order;
    return order;
}

static std::unordered_map<int, EclipseShadowModelEntry>& eclipse_shadow_models() {
    static std::unordered_map<int, EclipseShadowModelEntry> models;
    return models;
}

static std::vector<int>& eclipse_shadow_priority_order() {
    static std::vector<int> order;
    return order;
}

static std::unordered_map<int, EclipseMoonRadiusModelEntry>& eclipse_moon_radius_models() {
    static std::unordered_map<int, EclipseMoonRadiusModelEntry> models;
    return models;
}

static std::unordered_map<int, FrameRouteFn>& frame_routes() {
    static std::unordered_map<int, FrameRouteFn> routes;
    return routes;
}

struct DeltaTEphemerisCorrectionKeyHash {
    size_t operator()(const DeltaTEphemerisCorrectionKey& key) const noexcept {
        const size_t a = static_cast<size_t>(key.delta_t_model_id);
        const size_t b = static_cast<size_t>(key.ephemeris_family_id);
        return a ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2));
    }
};

static std::unordered_map<int, DeltaTEphemerisCorrectionEntry>& delta_t_ephemeris_corrections() {
    static std::unordered_map<int, DeltaTEphemerisCorrectionEntry> corrections;
    return corrections;
}

static std::unordered_map<
    DeltaTEphemerisCorrectionKey,
    int,
    DeltaTEphemerisCorrectionKeyHash
>& delta_t_ephemeris_correction_bindings() {
    static std::unordered_map<
        DeltaTEphemerisCorrectionKey,
        int,
        DeltaTEphemerisCorrectionKeyHash
    > bindings;
    return bindings;
}

template <typename Fn>
static void register_model(std::unordered_map<int, Fn>& models, int id, Fn fn) {
    if (!fn) return;
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    models[id] = fn;
    advance_model_registry_generation();
}

template <typename Fn>
static Fn lookup_model(const std::unordered_map<int, Fn>& models, int id) {
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    auto it = models.find(id);
    return it != models.end() ? it->second : nullptr;
}

static bool contains_id(const std::vector<int>& order, int id) {
    return std::find(order.begin(), order.end(), id) != order.end();
}

static bool set_priority_order_locked(
    std::vector<int>& order,
    const int* model_ids,
    size_t count
) {
    if (!model_ids && count > 0) {
        return false;
    }

    std::vector<int> replacement;
    try {
        for (size_t i = 0; i < count; ++i) {
            if (contains_id(replacement, model_ids[i])) {
                return false;
            }
            replacement.push_back(model_ids[i]);
        }
        order.swap(replacement);
    } catch (...) {
        return false;
    }
    return true;
}

static bool push_priority_model_locked(std::vector<int>& order, int id) {
    if (contains_id(order, id)) {
        return false;
    }
    try {
        order.push_back(id);
    } catch (...) {
        return false;
    }
    return true;
}

static bool insert_priority_model_locked(std::vector<int>& order, size_t index, int id) {
    if (index > order.size() || contains_id(order, id)) {
        return false;
    }
    try {
        order.insert(order.begin() + static_cast<std::vector<int>::difference_type>(index), id);
    } catch (...) {
        return false;
    }
    return true;
}

static bool remove_priority_model_locked(std::vector<int>& order, int id) {
    std::vector<int>::iterator it = std::find(order.begin(), order.end(), id);
    if (it == order.end()) {
        return false;
    }
    order.erase(it);
    return true;
}

// --- Refraction ---

void register_refraction_model(int id, RefractionFn fn) {
    register_model(refraction_models(), id, fn);
}

bool has_refraction_model(int id) noexcept {
    register_builtin_wrappers();
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    return refraction_models().find(id) != refraction_models().end();
}

double eval_refraction(int id, const void* data) {
    register_builtin_wrappers();
    RefractionFn fn = lookup_model(refraction_models(), id);
    return fn ? fn(data) : 0.0;
}

// --- Heliacal visibility ---

void register_heliacal_visibility_model(int id, HeliacalVisibilityFn fn) {
    if (!fn) return;
    register_heliacal_visibility_model(HeliacalVisibilityModelEntry(
        id, fn, 0, 0, 0, 0.25));
}

void register_heliacal_visibility_model(const HeliacalVisibilityModelEntry& entry) {
    if (!entry.eval || !(entry.default_extinction_mag_per_airmass > 0.0)) return;
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    heliacal_visibility_models()[entry.model_id] = entry;
}

bool add_heliacal_visibility_model(const HeliacalVisibilityModelEntry& entry) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    if (entry.model_id < HELIACAL_VISIBILITY_CUSTOM_START || !entry.eval
        || !(entry.default_extinction_mag_per_airmass > 0.0)) {
        return false;
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    std::unordered_map<int, HeliacalVisibilityModelEntry>& models = heliacal_visibility_models();
    if (models.find(entry.model_id) != models.end()) {
        return false;
    }
    try {
        models[entry.model_id] = entry;
    } catch (...) {
        return false;
    }
    return true;
}

bool find_heliacal_visibility_model(int id, HeliacalVisibilityModelEntry* out) noexcept {
    register_builtin_wrappers();
    if (out) {
        *out = HeliacalVisibilityModelEntry();
    }
    if (!out) {
        return false;
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    const std::unordered_map<int, HeliacalVisibilityModelEntry>::const_iterator it =
        heliacal_visibility_models().find(id);
    if (it == heliacal_visibility_models().end() || !it->second.eval) {
        return false;
    }
    *out = it->second;
    return true;
}

bool has_heliacal_visibility_model(int id) noexcept {
    HeliacalVisibilityModelEntry entry;
    return find_heliacal_visibility_model(id, &entry);
}

bool eval_heliacal_visibility(int id, const void* input, void* output) noexcept {
    HeliacalVisibilityModelEntry entry;
    if (!input || !output || !find_heliacal_visibility_model(id, &entry) || !entry.eval) {
        return false;
    }
    return entry.eval(input, output);
}

// --- Precession ---

void register_precession_model(int id, PrecessionFn fn) {
    if (!fn) return;
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    precession_models()[id] = PrecessionModelEntry(id, fn);
    advance_model_registry_generation();
}

bool add_precession_model(const PrecessionModelEntry& entry) noexcept {
    register_builtin_wrappers();
    if (entry.model_id < PRECESSION_CUSTOM_START || !entry.eval) {
        return false;
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    std::unordered_map<int, PrecessionModelEntry>& models = precession_models();
    if (models.find(entry.model_id) != models.end()) {
        return false;
    }
    try {
        models[entry.model_id] = entry;
    } catch (...) {
        return false;
    }
    advance_model_registry_generation();
    return true;
}

bool find_precession_model(int id, PrecessionModelEntry* out) noexcept {
    register_builtin_wrappers();
    if (out) {
        *out = PrecessionModelEntry();
    }
    if (!out) {
        return false;
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    std::unordered_map<int, PrecessionModelEntry>::const_iterator it = precession_models().find(id);
    if (it == precession_models().end()) {
        return false;
    }
    *out = it->second;
    return true;
}

bool set_precession_priority_order(const int* model_ids, size_t count) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    for (size_t i = 0; i < count; ++i) {
        if (precession_models().find(model_ids[i]) == precession_models().end()) {
            return false;
        }
    }
    const bool changed = set_priority_order_locked(
        precession_priority_order(), model_ids, count);
    if (changed) advance_model_registry_generation();
    return changed;
}

bool push_precession_priority_model(int id) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    if (precession_models().find(id) == precession_models().end()) {
        return false;
    }
    const bool changed = push_priority_model_locked(precession_priority_order(), id);
    if (changed) advance_model_registry_generation();
    return changed;
}

bool insert_precession_priority_model(size_t index, int id) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    if (precession_models().find(id) == precession_models().end()) {
        return false;
    }
    const bool changed = insert_priority_model_locked(
        precession_priority_order(), index, id);
    if (changed) advance_model_registry_generation();
    return changed;
}

bool remove_precession_priority_model(int id) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    const bool changed = remove_priority_model_locked(precession_priority_order(), id);
    if (changed) advance_model_registry_generation();
    return changed;
}

bool select_precession_model(int requested_id, PrecessionModelEntry* out) noexcept {
    register_builtin_wrappers();
    if (out) {
        *out = PrecessionModelEntry();
    }
    if (!out) {
        return false;
    }

    std::lock_guard<std::mutex> lock(dispatch_mutex());
    const std::unordered_map<int, PrecessionModelEntry>& models = precession_models();
    if (requested_id != MODEL_SELECTION_DEFAULT) {
        std::unordered_map<int, PrecessionModelEntry>::const_iterator it = models.find(requested_id);
        if (it == models.end()) {
            return false;
        }
        *out = it->second;
        return true;
    }

    const std::vector<int>& order = precession_priority_order();
    for (size_t i = 0; i < order.size(); ++i) {
        std::unordered_map<int, PrecessionModelEntry>::const_iterator it = models.find(order[i]);
        if (it != models.end() && it->second.eval) {
            *out = it->second;
            return true;
        }
    }
    return false;
}

bool eval_precession(int id, const SplitJulianDate& jd_tt, const void* data, Matrix3x3* out, double* out_mean_obliquity_rad) {
    PrecessionModelEntry model;
    if (!select_precession_model(id, &model) || !model.eval) {
        return false;
    }
    return model.eval(jd_tt, data, out, out_mean_obliquity_rad);
}

bool eval_selected_precession(int requested_id, const SplitJulianDate& jd_tt, const void* data, Matrix3x3* out, double* out_mean_obliquity_rad) {
    return eval_precession(requested_id, jd_tt, data, out, out_mean_obliquity_rad);
}

// --- Nutation ---

void register_nutation_model(int id, NutationFn fn) {
    if (!fn) return;
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    nutation_models()[id] = NutationModelEntry(id, fn);
    advance_model_registry_generation();
}

bool add_nutation_model(const NutationModelEntry& entry) noexcept {
    register_builtin_wrappers();
    if (entry.model_id < NUTATION_CUSTOM_START || !entry.eval) {
        return false;
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    std::unordered_map<int, NutationModelEntry>& models = nutation_models();
    if (models.find(entry.model_id) != models.end()) {
        return false;
    }
    try {
        models[entry.model_id] = entry;
    } catch (...) {
        return false;
    }
    advance_model_registry_generation();
    return true;
}

bool find_nutation_model(int id, NutationModelEntry* out) noexcept {
    register_builtin_wrappers();
    if (out) {
        *out = NutationModelEntry();
    }
    if (!out) {
        return false;
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    std::unordered_map<int, NutationModelEntry>::const_iterator it = nutation_models().find(id);
    if (it == nutation_models().end()) {
        return false;
    }
    *out = it->second;
    return true;
}

bool set_nutation_priority_order(const int* model_ids, size_t count) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    for (size_t i = 0; i < count; ++i) {
        if (nutation_models().find(model_ids[i]) == nutation_models().end()) {
            return false;
        }
    }
    const bool changed = set_priority_order_locked(
        nutation_priority_order(), model_ids, count);
    if (changed) advance_model_registry_generation();
    return changed;
}

bool push_nutation_priority_model(int id) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    if (nutation_models().find(id) == nutation_models().end()) {
        return false;
    }
    const bool changed = push_priority_model_locked(nutation_priority_order(), id);
    if (changed) advance_model_registry_generation();
    return changed;
}

bool insert_nutation_priority_model(size_t index, int id) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    if (nutation_models().find(id) == nutation_models().end()) {
        return false;
    }
    const bool changed = insert_priority_model_locked(
        nutation_priority_order(), index, id);
    if (changed) advance_model_registry_generation();
    return changed;
}

bool remove_nutation_priority_model(int id) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    const bool changed = remove_priority_model_locked(nutation_priority_order(), id);
    if (changed) advance_model_registry_generation();
    return changed;
}

bool select_nutation_model(int requested_id, NutationModelEntry* out) noexcept {
    register_builtin_wrappers();
    if (out) {
        *out = NutationModelEntry();
    }
    if (!out) {
        return false;
    }

    std::lock_guard<std::mutex> lock(dispatch_mutex());
    const std::unordered_map<int, NutationModelEntry>& models = nutation_models();
    if (requested_id != MODEL_SELECTION_DEFAULT) {
        std::unordered_map<int, NutationModelEntry>::const_iterator it = models.find(requested_id);
        if (it == models.end()) {
            return false;
        }
        *out = it->second;
        return true;
    }

    const std::vector<int>& order = nutation_priority_order();
    for (size_t i = 0; i < order.size(); ++i) {
        std::unordered_map<int, NutationModelEntry>::const_iterator it = models.find(order[i]);
        if (it != models.end() && it->second.eval) {
            *out = it->second;
            return true;
        }
    }
    return false;
}

bool eval_nutation(int id, const SplitJulianDate& jd_tt, const void* data, NutationAngles* out) {
    NutationModelEntry model;
    if (!select_nutation_model(id, &model) || !model.eval) {
        return false;
    }
    return model.eval(jd_tt, data, out);
}

bool eval_selected_nutation(int requested_id, const SplitJulianDate& jd_tt, const void* data, NutationAngles* out) {
    return eval_nutation(requested_id, jd_tt, data, out);
}

// --- TDB ---

void register_tdb_model(int id, TdbFn fn) {
    register_model(tdb_models(), id, fn);
}

double eval_tdb(int id, const SplitJulianDate& jd_tt, const void* data) {
    register_builtin_wrappers();
    TdbFn fn = lookup_model(tdb_models(), id);
    return fn ? fn(jd_tt, data) : 0.0;
}

void register_tdb_inverse_model(int id, TdbInverseFn fn) {
    register_model(tdb_inverse_models(), id, fn);
}

bool eval_tdb_inverse(
    int id,
    const SplitJulianDate& jd_tdb,
    const void* data,
    SplitJulianDate* out_jd_tt
) {
    register_builtin_wrappers();
    TdbInverseFn fn = lookup_model(tdb_inverse_models(), id);
    return fn && fn(jd_tdb, data, out_jd_tt);
}

// --- Delta-T ---

void register_delta_t_model(int id, DeltaTFn fn) {
    if (!fn) return;
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    delta_t_models()[id] = DeltaTModelEntry(id, fn);
    advance_model_registry_generation();
}

bool add_delta_t_model(const DeltaTModelEntry& entry) noexcept {
    register_builtin_wrappers();
    if (entry.model_id < DELTA_T_CUSTOM_START || !entry.eval) {
        return false;
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    std::unordered_map<int, DeltaTModelEntry>& models = delta_t_models();
    if (models.find(entry.model_id) != models.end()) {
        return false;
    }
    try {
        models[entry.model_id] = entry;
    } catch (...) {
        return false;
    }
    advance_model_registry_generation();
    return true;
}

bool find_delta_t_model(int id, DeltaTModelEntry* out) noexcept {
    register_builtin_wrappers();
    if (out) {
        *out = DeltaTModelEntry();
    }
    if (!out) {
        return false;
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    std::unordered_map<int, DeltaTModelEntry>::const_iterator it = delta_t_models().find(id);
    if (it == delta_t_models().end()) {
        return false;
    }
    *out = it->second;
    return true;
}

double eval_delta_t(int id, const SplitJulianDate& jd_ut, const void* data) {
    register_builtin_wrappers();
    DeltaTModelEntry entry;
    {
        std::lock_guard<std::mutex> lock(dispatch_mutex());
        std::unordered_map<int, DeltaTModelEntry>::const_iterator it = delta_t_models().find(id);
        if (it == delta_t_models().end() || !it->second.eval) {
            return 0.0;
        }
        entry = it->second;
    }
    return entry.eval(jd_ut, data);
}

double eval_delta_t_with_ephemeris_correction(
    int delta_t_model_id,
    int ephemeris_family_id,
    const SplitJulianDate& jd_ut,
    const void* delta_t_data,
    const void* correction_data
) {
    return eval_delta_t(delta_t_model_id, jd_ut, delta_t_data)
        + eval_delta_t_ephemeris_correction(delta_t_model_id, ephemeris_family_id, jd_ut, correction_data);
}

// --- Aberration ---

void register_aberration_model(int id, AberrationFn fn) {
    if (!fn) return;
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    aberration_models()[id] = AberrationModelEntry(id, fn);
}

bool add_aberration_model(const AberrationModelEntry& entry) noexcept {
    register_builtin_wrappers();
    if (entry.model_id < ABERRATION_CUSTOM_START || !entry.eval) {
        return false;
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    std::unordered_map<int, AberrationModelEntry>& models = aberration_models();
    if (models.find(entry.model_id) != models.end()) {
        return false;
    }
    try {
        models[entry.model_id] = entry;
    } catch (...) {
        return false;
    }
    return true;
}

bool find_aberration_model(int id, AberrationModelEntry* out) noexcept {
    register_builtin_wrappers();
    if (out) {
        *out = AberrationModelEntry();
    }
    if (!out) {
        return false;
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    std::unordered_map<int, AberrationModelEntry>::const_iterator it = aberration_models().find(id);
    if (it == aberration_models().end()) {
        return false;
    }
    *out = it->second;
    return true;
}

bool set_aberration_priority_order(const int* model_ids, size_t count) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    for (size_t i = 0; i < count; ++i) {
        if (aberration_models().find(model_ids[i]) == aberration_models().end()) {
            return false;
        }
    }
    return set_priority_order_locked(aberration_priority_order(), model_ids, count);
}

bool push_aberration_priority_model(int id) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    if (aberration_models().find(id) == aberration_models().end()) {
        return false;
    }
    return push_priority_model_locked(aberration_priority_order(), id);
}

bool insert_aberration_priority_model(size_t index, int id) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    if (aberration_models().find(id) == aberration_models().end()) {
        return false;
    }
    return insert_priority_model_locked(aberration_priority_order(), index, id);
}

bool remove_aberration_priority_model(int id) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    return remove_priority_model_locked(aberration_priority_order(), id);
}

bool select_aberration_model(int requested_id, AberrationModelEntry* out) noexcept {
    register_builtin_wrappers();
    if (out) {
        *out = AberrationModelEntry();
    }
    if (!out) {
        return false;
    }

    std::lock_guard<std::mutex> lock(dispatch_mutex());
    const std::unordered_map<int, AberrationModelEntry>& models = aberration_models();
    if (requested_id != MODEL_SELECTION_DEFAULT) {
        std::unordered_map<int, AberrationModelEntry>::const_iterator it = models.find(requested_id);
        if (it == models.end()) {
            return false;
        }
        *out = it->second;
        return true;
    }

    const std::vector<int>& order = aberration_priority_order();
    for (size_t i = 0; i < order.size(); ++i) {
        std::unordered_map<int, AberrationModelEntry>::const_iterator it = models.find(order[i]);
        if (it != models.end() && it->second.eval) {
            *out = it->second;
            return true;
        }
    }
    return false;
}

bool eval_aberration(int id, const AberrationDispatchData* data, Vector3* out_position_au, Vector3* out_velocity_au_per_day, Vector3* out_acceleration_au_per_day2) {
    AberrationModelEntry model;
    if (!select_aberration_model(id, &model) || !model.eval) {
        return false;
    }
    return model.eval(data, out_position_au, out_velocity_au_per_day, out_acceleration_au_per_day2);
}

bool eval_selected_aberration(int requested_id, const AberrationDispatchData* data, Vector3* out_position_au, Vector3* out_velocity_au_per_day, Vector3* out_acceleration_au_per_day2) {
    return eval_aberration(requested_id, data, out_position_au, out_velocity_au_per_day, out_acceleration_au_per_day2);
}

// --- Lunar Eclipse Shadow Model ---

bool add_eclipse_shadow_model(const EclipseShadowModelEntry& entry) noexcept {
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    eclipse_shadow_models()[entry.model_id] = entry;
    return true;
}

bool find_eclipse_shadow_model(int id, EclipseShadowModelEntry* out) noexcept {
    if (!out) return false;
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    std::unordered_map<int, EclipseShadowModelEntry>::const_iterator it =
        eclipse_shadow_models().find(id);
    if (it == eclipse_shadow_models().end()) return false;
    *out = it->second;
    return true;
}

bool set_eclipse_shadow_priority_order(const int* model_ids, size_t count) noexcept {
    if (!model_ids) return false;
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    eclipse_shadow_priority_order().assign(model_ids, model_ids + count);
    return true;
}

bool select_eclipse_shadow_model(int requested_id, EclipseShadowModelEntry* out) noexcept {
    if (!out) return false;
    register_builtin_wrappers();
    if (requested_id != MODEL_SELECTION_DEFAULT) {
        return find_eclipse_shadow_model(requested_id, out);
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    const std::unordered_map<int, EclipseShadowModelEntry>& models = eclipse_shadow_models();
    const std::vector<int>& order = eclipse_shadow_priority_order();
    for (size_t i = 0; i < order.size(); ++i) {
        std::unordered_map<int, EclipseShadowModelEntry>::const_iterator it = models.find(order[i]);
        if (it != models.end()) {
            *out = it->second;
            return true;
        }
    }
    return false;
}

// --- Lunar Eclipse Moon Radius Model ---

bool add_eclipse_moon_radius_model(const EclipseMoonRadiusModelEntry& entry) noexcept {
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    eclipse_moon_radius_models()[entry.model_id] = entry;
    return true;
}

bool find_eclipse_moon_radius_model(int id, EclipseMoonRadiusModelEntry* out) noexcept {
    if (!out) return false;
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    std::unordered_map<int, EclipseMoonRadiusModelEntry>::const_iterator it =
        eclipse_moon_radius_models().find(id);
    if (it == eclipse_moon_radius_models().end()) return false;
    *out = it->second;
    return true;
}

// --- Frame Route ---

void register_frame_route(int id, FrameRouteFn fn) {
    register_model(frame_routes(), id, fn);
}

bool eval_frame_route(int id, const SplitJulianDate& jd_ut1, const SplitJulianDate& jd_tt, const void* data, Matrix3x3* out) {
    register_builtin_wrappers();
    FrameRouteFn fn = lookup_model(frame_routes(), id);
    return fn ? fn(jd_ut1, jd_tt, data, out) : false;
}

// --- Delta-T / Ephemeris Compatibility ---

void register_delta_t_ephemeris_correction(int correction_id, DeltaTEphemerisCorrectionFn fn) {
    if (!fn) return;
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    delta_t_ephemeris_corrections()[correction_id] = DeltaTEphemerisCorrectionEntry(correction_id, fn);
    advance_model_registry_generation();
}

bool add_delta_t_ephemeris_correction(const DeltaTEphemerisCorrectionEntry& entry) noexcept {
    register_builtin_wrappers();
    if (entry.correction_id < DELTA_T_EPHEMERIS_CORRECTION_CUSTOM_START || !entry.eval) {
        return false;
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    std::unordered_map<int, DeltaTEphemerisCorrectionEntry>& corrections = delta_t_ephemeris_corrections();
    if (corrections.find(entry.correction_id) != corrections.end()) {
        return false;
    }
    try {
        corrections[entry.correction_id] = entry;
    } catch (...) {
        return false;
    }
    advance_model_registry_generation();
    return true;
}

bool find_delta_t_ephemeris_correction(int correction_id, DeltaTEphemerisCorrectionEntry* out) noexcept {
    register_builtin_wrappers();
    if (out) {
        *out = DeltaTEphemerisCorrectionEntry();
    }
    if (!out) {
        return false;
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    std::unordered_map<int, DeltaTEphemerisCorrectionEntry>::const_iterator it =
        delta_t_ephemeris_corrections().find(correction_id);
    if (it == delta_t_ephemeris_corrections().end()) {
        return false;
    }
    *out = it->second;
    return true;
}

bool bind_delta_t_ephemeris_correction(
    int delta_t_model_id,
    int ephemeris_family_id,
    int correction_id
) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    if (correction_id != DELTA_T_EPHEMERIS_CORRECTION_NONE
        && delta_t_ephemeris_corrections().find(correction_id) == delta_t_ephemeris_corrections().end()) {
        return false;
    }
    try {
        delta_t_ephemeris_correction_bindings()[
            DeltaTEphemerisCorrectionKey(delta_t_model_id, ephemeris_family_id)
        ] = correction_id;
    } catch (...) {
        return false;
    }
    advance_model_registry_generation();
    return true;
}

int find_delta_t_ephemeris_correction_binding(int delta_t_model_id, int ephemeris_family_id) noexcept {
    register_builtin_wrappers();
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    std::unordered_map<
        DeltaTEphemerisCorrectionKey,
        int,
        DeltaTEphemerisCorrectionKeyHash
    >::const_iterator it = delta_t_ephemeris_correction_bindings().find(
        DeltaTEphemerisCorrectionKey(delta_t_model_id, ephemeris_family_id)
    );
    return it != delta_t_ephemeris_correction_bindings().end()
        ? it->second
        : DELTA_T_EPHEMERIS_CORRECTION_NONE;
}

double eval_delta_t_ephemeris_correction(
    int delta_t_model_id,
    int ephemeris_family_id,
    const SplitJulianDate& jd_ut,
    const void* data
) noexcept {
    register_builtin_wrappers();
    DeltaTEphemerisCorrectionEntry entry;
    {
        std::lock_guard<std::mutex> lock(dispatch_mutex());
        std::unordered_map<
            DeltaTEphemerisCorrectionKey,
            int,
            DeltaTEphemerisCorrectionKeyHash
        >::const_iterator binding_it = delta_t_ephemeris_correction_bindings().find(
            DeltaTEphemerisCorrectionKey(delta_t_model_id, ephemeris_family_id)
        );
        if (binding_it == delta_t_ephemeris_correction_bindings().end()
            || binding_it->second == DELTA_T_EPHEMERIS_CORRECTION_NONE) {
            return 0.0;
        }

        std::unordered_map<int, DeltaTEphemerisCorrectionEntry>::const_iterator correction_it =
            delta_t_ephemeris_corrections().find(binding_it->second);
        if (correction_it == delta_t_ephemeris_corrections().end() || !correction_it->second.eval) {
            return 0.0;
        }
        entry = correction_it->second;
    }
    return entry.eval(jd_ut, delta_t_model_id, ephemeris_family_id, data);
}

}  // namespace dispatch
}  // namespace taiyin
