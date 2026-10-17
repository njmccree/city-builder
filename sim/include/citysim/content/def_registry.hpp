#pragma once

#include "citysim/content/content_id.hpp"
#include "citysim/content/def_handle.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace citysim::content {

// All definitions of one type (architecture §8). Filled with add() while content loads, then finalize()d:
// from then on the definitions sit in a vector sorted by id, so handles and iteration order never depend on
// the order files were read. Not tied to a clock; read-only after finalize(), so it is safe to share.
template <class T> class DefRegistry {
  public:
    // Returns false (and stores nothing) if the id is already taken. Throws std::logic_error after
    // finalize().
    bool add(ContentId id, T def) {
        if (finalized_) {
            throw std::logic_error("DefRegistry::add after finalize()");
        }
        if (!seen_.insert(id.str()).second) {
            return false;
        }
        ids_.push_back(std::move(id));
        defs_.push_back(std::move(def));
        return true;
    }

    // Sorts by id. After this the registry is read-only.
    void finalize() {
        if (finalized_) {
            return;
        }
        std::vector<std::size_t> order(defs_.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::sort(order.begin(), order.end(),
                  [this](std::size_t a, std::size_t b) { return ids_[a] < ids_[b]; });

        std::vector<ContentId> sorted_ids;
        std::vector<T> sorted_defs;
        sorted_ids.reserve(order.size());
        sorted_defs.reserve(order.size());
        for (const std::size_t index : order) {
            sorted_ids.push_back(std::move(ids_[index]));
            sorted_defs.push_back(std::move(defs_[index]));
        }
        ids_ = std::move(sorted_ids);
        defs_ = std::move(sorted_defs);
        seen_.clear();
        finalized_ = true;
    }

    bool finalized() const { return finalized_; }
    std::size_t size() const { return defs_.size(); }

    // Throws std::out_of_range for an invalid or out-of-range handle, std::logic_error before finalize().
    const T& get(DefHandle<T> handle) const {
        require_finalized();
        if (handle.index >= defs_.size()) {
            throw std::out_of_range("DefRegistry: bad handle");
        }
        return defs_[handle.index];
    }

    const ContentId& id(DefHandle<T> handle) const {
        require_finalized();
        if (handle.index >= ids_.size()) {
            throw std::out_of_range("DefRegistry: bad handle");
        }
        return ids_[handle.index];
    }

    std::optional<DefHandle<T>> find(const ContentId& id) const {
        require_finalized();
        const auto it = std::lower_bound(ids_.begin(), ids_.end(), id);
        if (it == ids_.end() || *it != id) {
            return std::nullopt;
        }
        return DefHandle<T>{static_cast<std::uint32_t>(it - ids_.begin())};
    }

    // Convenience for text ids; nullopt if `id` is not even a valid id.
    std::optional<DefHandle<T>> find(std::string_view id) const {
        const auto parsed = ContentId::parse(id);
        return parsed ? find(*parsed) : std::nullopt;
    }

    // Every definition, sorted by id; the index of an element is its handle.
    std::span<const T> all() const {
        require_finalized();
        return defs_;
    }
    std::span<const ContentId> ids() const {
        require_finalized();
        return ids_;
    }

  private:
    void require_finalized() const {
        if (!finalized_) {
            throw std::logic_error("DefRegistry used before finalize()");
        }
    }

    std::vector<ContentId> ids_;
    std::vector<T> defs_;
    std::set<std::string> seen_; // duplicate detection while loading; never iterated for results
    bool finalized_ = false;
};

} // namespace citysim::content
