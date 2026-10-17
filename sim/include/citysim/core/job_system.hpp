#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace citysim::core {

// A small fixed pool for data-parallel work inside a tick (architecture §7.2). Runs on the tick clock:
// every call blocks until all of its work is finished, so no work outlives the system that started it.
//
// DETERMINISM CONTRACT
//   * The chunking depends only on (count, chunk_size), never on how many workers exist:
//     chunk i is [i * chunk_size, min(count, (i + 1) * chunk_size)).
//   * Which thread runs which chunk, and in what order, is unspecified. A chunk function must therefore
//     write only to output that belongs to its own chunk (see ChunkedOutput) and must not depend on
//     what other chunks do.
//   * Results are combined afterwards, serially, in chunk order, on the calling thread
//     (parallel_map_reduce and ChunkedOutput::merge_into do this). With that pattern the result is
//     bit-identical for 1, 2 or N workers, floating point included.
//   * If chunks throw, every chunk still runs, and the exception of the lowest chunk index is rethrown
//     on the calling thread, so which error you see does not depend on timing.
//
// A call made from inside a chunk function (nesting) runs serially on that thread instead of
// deadlocking. Calls from different threads are serialized.
class JobSystem {
  public:
    // `worker_count` is the number of threads that do work, counting the calling thread. 0 means
    // max(1, hardware_concurrency - 2). 1 means no extra threads at all: everything runs on the caller.
    explicit JobSystem(std::size_t worker_count = 0);
    ~JobSystem();
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    // The resolved number of working threads (at least 1), including the calling thread.
    std::size_t worker_count() const { return threads_.size() + 1; }

    static std::size_t chunk_count(std::size_t count, std::size_t chunk_size) {
        const std::size_t size = std::max<std::size_t>(chunk_size, 1);
        return count / size + (count % size != 0 ? 1 : 0);
    }

    // Calls fn(begin, end, chunk_index) for every chunk of [0, count). A chunk_size of 0 is treated as 1.
    template <class Fn> void parallel_for(std::size_t count, std::size_t chunk_size, Fn&& fn) {
        using Callable = std::remove_reference_t<Fn>;
        run(count, chunk_size, const_cast<void*>(static_cast<const void*>(&fn)),
            [](void* target, std::size_t begin, std::size_t end, std::size_t chunk) {
                (*static_cast<Callable*>(target))(begin, end, chunk);
            });
    }

    // map_fn(begin, end, chunk_index) -> R runs per chunk (R must be default constructible). The results
    // are then folded on the calling thread, in chunk order: acc = reduce_fn(std::move(acc), chunk_result).
    template <class T, class MapFn, class ReduceFn>
    T parallel_map_reduce(std::size_t count, std::size_t chunk_size, MapFn&& map_fn, T init,
                          ReduceFn&& reduce_fn) {
        using R = std::decay_t<decltype(map_fn(std::size_t{}, std::size_t{}, std::size_t{}))>;
        const std::size_t chunks = chunk_count(count, chunk_size);
        // A plain array, not vector<R>: vector<bool> would make neighboring chunks share a byte.
        const std::unique_ptr<R[]> results(new R[chunks]());
        parallel_for(count, chunk_size, [&](std::size_t begin, std::size_t end, std::size_t chunk) {
            results[chunk] = map_fn(begin, end, chunk);
        });
        T acc = std::move(init);
        for (std::size_t i = 0; i < chunks; ++i) {
            acc = reduce_fn(std::move(acc), std::move(results[i]));
        }
        return acc;
    }

  private:
    using Trampoline = void (*)(void*, std::size_t, std::size_t, std::size_t);

    void run(std::size_t count, std::size_t chunk_size, void* target, Trampoline trampoline);
    void run_serial(std::size_t count, std::size_t chunk_size, void* target, Trampoline trampoline);
    void work_on_chunks();
    void record_error(std::size_t chunk, std::exception_ptr error);
    void worker_main();

    std::vector<std::thread> threads_;

    std::mutex submit_mutex_; // one parallel_for at a time

    // Job description. Written by the submitting thread under mutex_ before epoch_ is bumped, and read
    // by workers after they observe the new epoch under mutex_, so no further synchronization is needed.
    void* target_ = nullptr;
    Trampoline trampoline_ = nullptr;
    std::size_t count_ = 0;
    std::size_t chunk_size_ = 1;
    std::size_t chunk_total_ = 0;

    std::mutex mutex_; // guards the fields below and pairs with the condition variables
    std::condition_variable wake_cv_;
    std::condition_variable done_cv_;
    std::uint64_t epoch_ = 0;
    bool job_active_ = false;
    std::size_t workers_inside_ = 0;
    bool stop_ = false;

    std::atomic<std::size_t> next_chunk_{0};

    std::mutex error_mutex_;
    std::exception_ptr error_;
    std::size_t error_chunk_ = 0;
};

} // namespace citysim::core
