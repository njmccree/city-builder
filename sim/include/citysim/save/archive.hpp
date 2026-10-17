#pragma once

// Serialization pattern (architecture §10). Read this once; every stateful type follows it.
//
// A type is saved and loaded by ONE function template, found by argument-dependent lookup, that works for
// both BinaryWriter and BinaryReader. When saving, `ar` writes the fields; when loading, `ar` reads into
// them. `Ar::is_loading` says which, for the rare field that needs different handling.
//
//     struct Farm {
//         std::uint32_t workers = 0;
//         std::string name;
//         std::vector<std::int32_t> yields;
//         std::optional<entt::entity> owner;  // a reference to another entity
//         std::uint8_t irrigation = 0;        // added in version 2
//     };
//
//     template <class Ar> void serialize(Ar& ar, Farm& farm, std::uint32_t version) {
//         io(ar, farm.workers);   // io() picks the right encoding for the type of the field
//         io(ar, farm.name);
//         io(ar, farm.yields);
//         io(ar, farm.owner);     // entities go through the archive's remap, never as raw ids
//         if (version >= 2) {
//             io(ar, farm.irrigation);
//         }                       // older saves: the field keeps its default
//     }
//
//     // At registration (see bind_serializer): the current version is what is written; `version` in
//     // serialize() is what a save being loaded was written with.
//     core::Hooks<Farm> hooks;
//     hooks.hash = &hash_farm;
//     save::bind_serializer(hooks, /*version=*/2);
//     registry.register_type<Farm>("farm.Farm", hooks);
//
// Rules:
//   * Write every field that is part of the hashed state, in a fixed order. Never reorder or reuse a field;
//     bump the version and branch on it. Old versions must keep loading (migrations: S0.5.1).
//   * Never write unordered containers by iteration; sort them first. Never write pointers, handles or raw
//     entity ids: use entities through io() (remapped) and content definitions as DefHandle (saved as ids).
//   * Collections are length-prefixed; the reader rejects a length larger than the bytes left.
//   * Fixed-width integers are little-endian; use ar.varint()/ar.svarint() explicitly where values are
//     usually small.

#include "citysim/save/binary_reader.hpp"
#include "citysim/save/binary_writer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <entt/entt.hpp>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace citysim::save {

namespace detail {

template <class T> struct IsVector : std::false_type {};
template <class T, class A> struct IsVector<std::vector<T, A>> : std::true_type {};
template <class T> struct IsStdArray : std::false_type {};
template <class T, std::size_t N> struct IsStdArray<std::array<T, N>> : std::true_type {};
template <class T> struct IsOptional : std::false_type {};
template <class T> struct IsOptional<std::optional<T>> : std::true_type {};
template <class T> struct IsVariant : std::false_type {};
template <class... Ts> struct IsVariant<std::variant<Ts...>> : std::true_type {};

template <class V, std::size_t I = 0> void emplace_alternative(V& variant, std::size_t index) {
    if constexpr (I < std::variant_size_v<V>) {
        if (index == I) {
            variant.template emplace<I>();
        } else {
            emplace_alternative<V, I + 1>(variant, index);
        }
    }
}

} // namespace detail

// Reads or writes `value`, choosing the encoding from its type:
//   bool; integers (fixed width, little-endian); enums (as their underlying integer, varint); float and
//   double (bit patterns); std::string; entt::entity (remapped); std::vector, std::array, std::optional and
//   std::variant of any of these (std::monostate is an alternative with no data); and any type with a
//   `serialize(Ar&, T&, uint32_t)` function.
// `version` is passed on to user types, so nested types see the version of the section they are in.
template <class Ar, class T> void io(Ar& ar, T& value, std::uint32_t version = 0) {
    if constexpr (std::is_same_v<T, bool>) {
        ar.flag(value);
    } else if constexpr (std::is_same_v<T, entt::entity>) {
        ar.entity(value);
    } else if constexpr (std::is_enum_v<T>) {
        using U = std::underlying_type_t<T>;
        if constexpr (Ar::is_loading) {
            if constexpr (std::is_signed_v<U>) {
                std::int64_t raw = 0;
                ar.svarint(raw);
                value = static_cast<T>(static_cast<U>(raw));
            } else {
                std::uint64_t raw = 0;
                ar.varint(raw);
                value = static_cast<T>(static_cast<U>(raw));
            }
        } else {
            if constexpr (std::is_signed_v<U>) {
                const auto raw = static_cast<std::int64_t>(static_cast<U>(value));
                ar.svarint(raw);
            } else {
                const auto raw = static_cast<std::uint64_t>(static_cast<U>(value));
                ar.varint(raw);
            }
        }
    } else if constexpr (std::is_integral_v<T>) {
        ar.fixed(value);
    } else if constexpr (std::is_same_v<T, float>) {
        ar.f32(value);
    } else if constexpr (std::is_same_v<T, double>) {
        ar.f64(value);
    } else if constexpr (std::is_same_v<T, std::string>) {
        ar.string(value);
    } else if constexpr (detail::IsVector<T>::value) {
        static_assert(!std::is_same_v<typename T::value_type, bool>,
                      "std::vector<bool> is not supported; use std::vector<std::uint8_t>");
        std::uint64_t count = value.size();
        ar.size(count);
        if constexpr (Ar::is_loading) {
            value.clear();
            value.resize(static_cast<std::size_t>(count));
        }
        for (auto& element : value) {
            io(ar, element, version);
        }
    } else if constexpr (detail::IsStdArray<T>::value) {
        for (auto& element : value) {
            io(ar, element, version);
        }
    } else if constexpr (detail::IsOptional<T>::value) {
        bool present = value.has_value();
        ar.flag(present);
        if constexpr (Ar::is_loading) {
            if (present) {
                value.emplace();
            } else {
                value.reset();
            }
        }
        if (present) {
            io(ar, *value, version);
        }
    } else if constexpr (detail::IsVariant<T>::value) {
        std::uint64_t index = value.index();
        ar.varint(index);
        if constexpr (Ar::is_loading) {
            if (index >= std::variant_size_v<T>) {
                ar.fail("variant alternative " + std::to_string(index) + " does not exist");
            }
            detail::emplace_alternative(value, static_cast<std::size_t>(index));
        }
        std::visit([&](auto& alternative) { io(ar, alternative, version); }, value);
    } else if constexpr (std::is_same_v<T, std::monostate>) {
        // carries no data; only its place among the alternatives is saved
    } else {
        serialize(ar, value, version); // found by argument-dependent lookup
    }
}

} // namespace citysim::save
