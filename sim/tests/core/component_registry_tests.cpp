#include "citysim/core/component_registry.hpp"

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string>
#include <vector>

using citysim::core::ComponentRegistry;
using citysim::core::Hooks;
using citysim::core::StateHasher;

namespace {
struct A {};
struct B {};
struct C {};

int g_hash_calls = 0;
void hash_a(StateHasher&, const A&) {
    ++g_hash_calls;
}

std::vector<std::string> names(const ComponentRegistry& registry) {
    std::vector<std::string> out;
    for (const auto& info : registry.entries()) {
        out.push_back(info.name);
    }
    return out;
}
} // namespace

TEST_CASE("registry iterates by stable name regardless of registration order", "[core][registry]") {
    ComponentRegistry first;
    first.register_type<A>("core.Zeta", {});
    first.register_type<B>("core.Alpha", {});
    first.register_type<C>("core.Mid", {});

    ComponentRegistry second;
    second.register_type<C>("core.Mid", {});
    second.register_type<A>("core.Zeta", {});
    second.register_type<B>("core.Alpha", {});

    const std::vector<std::string> expected{"core.Alpha", "core.Mid", "core.Zeta"};
    REQUIRE(names(first) == expected);
    REQUIRE(names(second) == expected);
}

TEST_CASE("registry lookup by name and by type", "[core][registry]") {
    ComponentRegistry registry;
    registry.register_type<A>("core.A", {});
    registry.register_type<B>("core.B", {});

    REQUIRE(registry.size() == 2);
    REQUIRE(registry.find("core.B") != nullptr);
    REQUIRE(registry.find("core.B")->name == "core.B");
    REQUIRE(registry.find("core.Missing") == nullptr);
    REQUIRE(registry.find<A>() != nullptr);
    REQUIRE(registry.find<A>()->name == "core.A");
    REQUIRE(registry.find<C>() == nullptr);
}

TEST_CASE("registry rejects duplicate names and types", "[core][registry]") {
    ComponentRegistry registry;
    registry.register_type<A>("core.A", {});

    REQUIRE_THROWS_AS(registry.register_type<B>("core.A", {}), std::logic_error);
    REQUIRE_THROWS_AS(registry.register_type<A>("core.Other", {}), std::logic_error);
    REQUIRE(registry.size() == 1);
}

TEST_CASE("registry stores hooks type-erased", "[core][registry]") {
    ComponentRegistry registry;
    Hooks<A> hooks;
    hooks.hash = &hash_a;
    registry.register_type<A>("core.A", hooks);
    registry.register_type<B>("core.B", {});

    const auto* with_hook = registry.find<A>();
    REQUIRE(with_hook->hash);
    REQUIRE_FALSE(with_hook->serialize);
    REQUIRE_FALSE(registry.find<B>()->hash);
    // Invoking the hook needs a real StateHasher; the task that implements it adds that coverage.
}
