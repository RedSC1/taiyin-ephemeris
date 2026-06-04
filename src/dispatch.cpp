#include "taiyin/dispatch.h"

#include <algorithm>
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
}  // namespace wrappers

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
        builtin_registration_in_progress() = false;
        return true;
    }();
    (void)registered;
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

static std::unordered_map<int, FrameRouteFn>& frame_routes() {
    static std::unordered_map<int, FrameRouteFn> routes;
    return routes;
}

template <typename Fn>
static void register_model(std::unordered_map<int, Fn>& models, int id, Fn fn) {
    if (!fn) return;
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    models[id] = fn;
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

double eval_refraction(int id, const void* data) {
    register_builtin_wrappers();
    RefractionFn fn = lookup_model(refraction_models(), id);
    return fn ? fn(data) : 0.0;
}

// --- Precession ---

void register_precession_model(int id, PrecessionFn fn) {
    if (!fn) return;
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    precession_models()[id] = PrecessionModelEntry(id, fn);
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
    return set_priority_order_locked(precession_priority_order(), model_ids, count);
}

bool push_precession_priority_model(int id) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    if (precession_models().find(id) == precession_models().end()) {
        return false;
    }
    return push_priority_model_locked(precession_priority_order(), id);
}

bool insert_precession_priority_model(size_t index, int id) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    if (precession_models().find(id) == precession_models().end()) {
        return false;
    }
    return insert_priority_model_locked(precession_priority_order(), index, id);
}

bool remove_precession_priority_model(int id) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    return remove_priority_model_locked(precession_priority_order(), id);
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

bool eval_precession(int id, double jd_tt, const void* data, Matrix3x3* out, double* out_mean_obliquity_rad) {
    PrecessionModelEntry model;
    if (!select_precession_model(id, &model) || !model.eval) {
        return false;
    }
    return model.eval(jd_tt, data, out, out_mean_obliquity_rad);
}

bool eval_selected_precession(int requested_id, double jd_tt, const void* data, Matrix3x3* out, double* out_mean_obliquity_rad) {
    return eval_precession(requested_id, jd_tt, data, out, out_mean_obliquity_rad);
}

// --- Nutation ---

void register_nutation_model(int id, NutationFn fn) {
    if (!fn) return;
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    nutation_models()[id] = NutationModelEntry(id, fn);
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
    return set_priority_order_locked(nutation_priority_order(), model_ids, count);
}

bool push_nutation_priority_model(int id) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    if (nutation_models().find(id) == nutation_models().end()) {
        return false;
    }
    return push_priority_model_locked(nutation_priority_order(), id);
}

bool insert_nutation_priority_model(size_t index, int id) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    if (nutation_models().find(id) == nutation_models().end()) {
        return false;
    }
    return insert_priority_model_locked(nutation_priority_order(), index, id);
}

bool remove_nutation_priority_model(int id) noexcept {
    if (!builtin_registration_in_progress()) {
        register_builtin_wrappers();
    }
    std::lock_guard<std::mutex> lock(dispatch_mutex());
    return remove_priority_model_locked(nutation_priority_order(), id);
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

bool eval_nutation(int id, double jd_tt, const void* data, NutationAngles* out) {
    NutationModelEntry model;
    if (!select_nutation_model(id, &model) || !model.eval) {
        return false;
    }
    return model.eval(jd_tt, data, out);
}

bool eval_selected_nutation(int requested_id, double jd_tt, const void* data, NutationAngles* out) {
    return eval_nutation(requested_id, jd_tt, data, out);
}

// --- TDB ---

void register_tdb_model(int id, TdbFn fn) {
    register_model(tdb_models(), id, fn);
}

double eval_tdb(int id, double jd_tt, const void* data) {
    register_builtin_wrappers();
    TdbFn fn = lookup_model(tdb_models(), id);
    return fn ? fn(jd_tt, data) : 0.0;
}

void register_tdb_inverse_model(int id, TdbInverseFn fn) {
    register_model(tdb_inverse_models(), id, fn);
}

double eval_tdb_inverse(int id, double jd_tdb, const void* data) {
    register_builtin_wrappers();
    TdbInverseFn fn = lookup_model(tdb_inverse_models(), id);
    return fn ? fn(jd_tdb, data) : 0.0;
}

// --- Frame Route ---

void register_frame_route(int id, FrameRouteFn fn) {
    register_model(frame_routes(), id, fn);
}

bool eval_frame_route(int id, double jd_ut1, double jd_tt, const void* data, Matrix3x3* out) {
    register_builtin_wrappers();
    FrameRouteFn fn = lookup_model(frame_routes(), id);
    return fn ? fn(jd_ut1, jd_tt, data, out) : false;
}

}  // namespace dispatch
}  // namespace taiyin
