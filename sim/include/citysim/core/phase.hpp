#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace citysim::core {

// The fixed order in which a tick runs (architecture §5.2). The order never changes at runtime.
enum class Phase : std::uint8_t {
    ApplyCommands,
    PreUpdate,
    Update,
    PostUpdate,
    Calendar,
    Publish,
};

inline constexpr std::size_t k_phase_count = 6;

inline constexpr std::array<Phase, k_phase_count> k_all_phases{
    Phase::ApplyCommands, Phase::PreUpdate, Phase::Update, Phase::PostUpdate, Phase::Calendar, Phase::Publish,
};

constexpr std::string_view phase_name(Phase phase) {
    switch (phase) {
    case Phase::ApplyCommands:
        return "ApplyCommands";
    case Phase::PreUpdate:
        return "PreUpdate";
    case Phase::Update:
        return "Update";
    case Phase::PostUpdate:
        return "PostUpdate";
    case Phase::Calendar:
        return "Calendar";
    case Phase::Publish:
        return "Publish";
    }
    return "?";
}

} // namespace citysim::core
