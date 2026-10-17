#include "citysim/core/job_system.hpp"

namespace citysim::core {

namespace {
// True while this thread is inside a chunk function; a nested parallel_for then runs serially.
thread_local bool t_inside_chunk = false;

// Restores the previous value so a nested serial run does not clear the outer chunk's flag.
struct InsideChunkGuard {
    InsideChunkGuard() : previous_(t_inside_chunk) { t_inside_chunk = true; }
    ~InsideChunkGuard() { t_inside_chunk = previous_; }
    InsideChunkGuard(const InsideChunkGuard&) = delete;
    InsideChunkGuard& operator=(const InsideChunkGuard&) = delete;

  private:
    bool previous_;
};
} // namespace

JobSystem::JobSystem(std::size_t worker_count) {
    if (worker_count == 0) {
        const std::size_t hardware = std::thread::hardware_concurrency();
        worker_count = hardware > 2 ? hardware - 2 : 1;
    }
    threads_.reserve(worker_count - 1);
    for (std::size_t i = 1; i < worker_count; ++i) {
        threads_.emplace_back([this] { worker_main(); });
    }
}

JobSystem::~JobSystem() {
    {
        const std::lock_guard lock(mutex_);
        stop_ = true;
    }
    wake_cv_.notify_all();
    for (std::thread& thread : threads_) {
        thread.join();
    }
}

void JobSystem::record_error(std::size_t chunk, std::exception_ptr error) {
    const std::lock_guard lock(error_mutex_);
    if (!error_ || chunk < error_chunk_) {
        error_ = std::move(error);
        error_chunk_ = chunk;
    }
}

void JobSystem::work_on_chunks() {
    const InsideChunkGuard guard;
    for (;;) {
        const std::size_t chunk = next_chunk_.fetch_add(1, std::memory_order_relaxed);
        if (chunk >= chunk_total_) {
            return;
        }
        const std::size_t begin = chunk * chunk_size_;
        const std::size_t end = std::min(count_, begin + chunk_size_);
        try {
            trampoline_(target_, begin, end, chunk);
        } catch (...) {
            record_error(chunk, std::current_exception());
        }
    }
}

void JobSystem::worker_main() {
    std::uint64_t seen_epoch = 0;
    std::unique_lock lock(mutex_);
    for (;;) {
        wake_cv_.wait(lock, [&] { return stop_ || epoch_ != seen_epoch; });
        if (stop_) {
            return;
        }
        seen_epoch = epoch_;
        if (!job_active_) {
            continue; // the job finished before this worker woke up
        }
        ++workers_inside_;
        lock.unlock();
        work_on_chunks();
        lock.lock();
        if (--workers_inside_ == 0) {
            done_cv_.notify_all();
        }
    }
}

void JobSystem::run_serial(std::size_t count, std::size_t chunk_size, void* target, Trampoline trampoline) {
    // Same semantics as the parallel path: every chunk runs, and the lowest chunk's exception wins.
    const std::size_t size = std::max<std::size_t>(chunk_size, 1);
    const std::size_t chunks = chunk_count(count, size);
    std::exception_ptr first_error;
    const InsideChunkGuard guard;
    for (std::size_t chunk = 0; chunk < chunks; ++chunk) {
        const std::size_t begin = chunk * size;
        try {
            trampoline(target, begin, std::min(count, begin + size), chunk);
        } catch (...) {
            if (!first_error) {
                first_error = std::current_exception();
            }
        }
    }
    if (first_error) {
        std::rethrow_exception(first_error);
    }
}

void JobSystem::run(std::size_t count, std::size_t chunk_size, void* target, Trampoline trampoline) {
    if (count == 0) {
        return;
    }
    const std::size_t size = std::max<std::size_t>(chunk_size, 1);
    const std::size_t chunks = chunk_count(count, size);

    // No helpers, nothing to share, or we are already inside a chunk (nesting): run it right here.
    if (threads_.empty() || chunks == 1 || t_inside_chunk) {
        run_serial(count, size, target, trampoline);
        return;
    }

    const std::lock_guard submit_lock(submit_mutex_);
    {
        const std::lock_guard lock(mutex_);
        target_ = target;
        trampoline_ = trampoline;
        count_ = count;
        chunk_size_ = size;
        chunk_total_ = chunks;
        next_chunk_.store(0, std::memory_order_relaxed);
        error_ = nullptr;
        error_chunk_ = 0;
        job_active_ = true;
        ++epoch_;
    }
    wake_cv_.notify_all();

    work_on_chunks(); // the calling thread helps

    {
        std::unique_lock lock(mutex_);
        // Every chunk is claimed once the caller's loop ends; wait for helpers still finishing theirs.
        done_cv_.wait(lock, [&] { return workers_inside_ == 0; });
        job_active_ = false;
    }

    if (error_) {
        std::exception_ptr error = std::move(error_);
        error_ = nullptr;
        std::rethrow_exception(error);
    }
}

} // namespace citysim::core
