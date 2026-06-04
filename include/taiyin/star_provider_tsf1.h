#ifndef TAIYIN_STAR_PROVIDER_TSF1_H
#define TAIYIN_STAR_PROVIDER_TSF1_H

#include "star_catalog_tsc1.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace taiyin {

class Tsf1StarProvider {
public:
    Tsf1StarProvider(size_t cache_max_bytes = 1024 * 1024, int catalog_id = 2) noexcept;
    ~Tsf1StarProvider() noexcept;

    Tsf1StarProvider(const Tsf1StarProvider&) = delete;
    Tsf1StarProvider& operator=(const Tsf1StarProvider&) = delete;

    bool load_from_file(const std::string& path) noexcept;
    bool load_from_memory(const uint8_t* data, size_t size) noexcept;
    void close() noexcept;

    bool resolve(const std::string& id_or_alias, Tsc1ResolvedStar* out) noexcept;
    bool eval_position(const std::string& id_or_alias, double jd_tdb, Vector3* out) noexcept;
    bool eval_velocity(const std::string& id_or_alias, double jd_tdb, Vector3* out) noexcept;
    bool eval_acceleration(const std::string& id_or_alias, double jd_tdb, Vector3* out) noexcept;
    bool eval_state(const std::string& id_or_alias, double jd_tdb, CartesianState* out) noexcept;

    bool contains_cached_star(const std::string& id_or_alias) noexcept;
    size_t cache_entry_count() const noexcept;
    size_t cache_total_bytes() const noexcept;

    const Tsc1Catalog* catalog() const noexcept;
    int catalog_id() const noexcept;

private:
    std::vector<uint8_t> owned_tsc1_bytes_;
    Tsc1StarProvider delegate_;
};

}  // namespace taiyin

#endif  // TAIYIN_STAR_PROVIDER_TSF1_H
