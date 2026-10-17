#pragma once

#include "citysim/core/job_system.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace citysim::core {

// One output buffer per chunk, so parallel chunks never share a write target, and a merge that always
// walks the buffers in chunk order (architecture §7.2). Typical use:
//
//     ChunkedOutput<Move> out(count, chunk_size);
//     jobs.parallel_for(count, chunk_size, [&](size_t b, size_t e, size_t chunk) {
//         for (size_t i = b; i < e; ++i) out.chunk(chunk).push_back(compute(i));
//     });
//     out.merge_into(all_moves); // identical for any worker count
template <class T> class ChunkedOutput {
  public:
    ChunkedOutput(std::size_t count, std::size_t chunk_size)
        : buffers_(JobSystem::chunk_count(count, chunk_size)) {}

    std::size_t chunk_count() const { return buffers_.size(); }

    // Only the thread running that chunk may touch its buffer while the parallel_for is in progress.
    std::vector<T>& chunk(std::size_t index) { return buffers_[index]; }

    // Appends every buffer to `out` in chunk order, then empties the buffers but keeps their capacity so
    // the object can be reused for the next call.
    void merge_into(std::vector<T>& out) {
        std::size_t total = 0;
        for (const auto& buffer : buffers_) {
            total += buffer.size();
        }
        out.reserve(out.size() + total);
        for (auto& buffer : buffers_) {
            out.insert(out.end(), std::make_move_iterator(buffer.begin()),
                       std::make_move_iterator(buffer.end()));
            buffer.clear();
        }
    }

  private:
    std::vector<std::vector<T>> buffers_;
};

} // namespace citysim::core
