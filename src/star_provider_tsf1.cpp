#include "taiyin/star_provider_tsf1.h"

#include "taiyin/internal/star_file.h"

namespace taiyin {

Tsf1StarProvider::Tsf1StarProvider(size_t cache_max_bytes, int catalog_id) noexcept
    : owned_tsc1_bytes_(),
      delegate_(cache_max_bytes, catalog_id) {}

Tsf1StarProvider::~Tsf1StarProvider() noexcept {
    close();
}

bool Tsf1StarProvider::load_from_file(const std::string& path) noexcept {
    close();
    std::vector<uint8_t> bytes;
    if (!internal::build_tsc1_catalog_bytes_from_star_file(path, &bytes) || bytes.empty()) {
        return false;
    }
    owned_tsc1_bytes_.swap(bytes);
    if (!delegate_.load_from_memory(&owned_tsc1_bytes_[0], owned_tsc1_bytes_.size())) {
        close();
        return false;
    }
    return true;
}

bool Tsf1StarProvider::load_from_memory(const uint8_t* data, size_t size) noexcept {
    close();
    if (!data || size == 0) {
        return false;
    }
    try {
        owned_tsc1_bytes_.assign(data, data + size);
    } catch (...) {
        owned_tsc1_bytes_.clear();
        return false;
    }
    if (!delegate_.load_from_memory(&owned_tsc1_bytes_[0], owned_tsc1_bytes_.size())) {
        close();
        return false;
    }
    return true;
}

void Tsf1StarProvider::close() noexcept {
    delegate_.close();
    owned_tsc1_bytes_.clear();
}

bool Tsf1StarProvider::resolve(const std::string& id_or_alias, Tsc1ResolvedStar* out) noexcept {
    return delegate_.resolve(id_or_alias, out);
}

bool Tsf1StarProvider::eval_position(const std::string& id_or_alias, double jd_tdb, Vector3* out) noexcept {
    return delegate_.eval_position(id_or_alias, jd_tdb, out);
}

bool Tsf1StarProvider::eval_velocity(const std::string& id_or_alias, double jd_tdb, Vector3* out) noexcept {
    return delegate_.eval_velocity(id_or_alias, jd_tdb, out);
}

bool Tsf1StarProvider::eval_acceleration(const std::string& id_or_alias, double jd_tdb, Vector3* out) noexcept {
    return delegate_.eval_acceleration(id_or_alias, jd_tdb, out);
}

bool Tsf1StarProvider::eval_state(const std::string& id_or_alias, double jd_tdb, CartesianState* out) noexcept {
    return delegate_.eval_state(id_or_alias, jd_tdb, out);
}

bool Tsf1StarProvider::contains_cached_star(const std::string& id_or_alias) noexcept {
    return delegate_.contains_cached_star(id_or_alias);
}

size_t Tsf1StarProvider::cache_entry_count() const noexcept {
    return delegate_.cache_entry_count();
}

size_t Tsf1StarProvider::cache_total_bytes() const noexcept {
    return delegate_.cache_total_bytes();
}

const Tsc1Catalog* Tsf1StarProvider::catalog() const noexcept {
    return delegate_.catalog();
}

int Tsf1StarProvider::catalog_id() const noexcept {
    return delegate_.catalog_id();
}

}  // namespace taiyin
