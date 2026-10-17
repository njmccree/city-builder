#include "citysim/core/command_queue.hpp"

#include <utility>

namespace citysim::core {

std::uint64_t CommandQueue::submit(Command cmd, std::uint64_t issue_tick) {
    // The sequence is assigned and the command stored under one lock, so the vector is always in
    // sequence order.
    const std::lock_guard lock(mutex_);
    const std::uint64_t sequence = next_sequence_++;
    pending_.push_back(SequencedCommand{sequence, issue_tick, std::move(cmd)});
    return sequence;
}

void CommandQueue::drain(std::vector<SequencedCommand>& out) {
    const std::lock_guard lock(mutex_);
    out.insert(out.end(), std::make_move_iterator(pending_.begin()), std::make_move_iterator(pending_.end()));
    pending_.clear();
}

std::size_t CommandQueue::size() const {
    const std::lock_guard lock(mutex_);
    return pending_.size();
}

} // namespace citysim::core
