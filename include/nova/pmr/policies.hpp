// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <cstddef>
#include <mutex>
#include <nova/parameter/parameter.hpp>

namespace nova::pmr {

namespace detail {
struct static_size_tag
{};
struct lock_memory_tag
{};
struct use_mutex_tag
{};

} // namespace detail

/// @brief Policy tag: statically sized memory pool embedded in the object.
/// @tparam Size The size of the pool in bytes.
template < std::size_t Size >
using static_size = parameter::size_param< struct detail::static_size_tag, Size >;

/// @brief Policy tag: enable thread-safe locking with the given mutex type.
/// @tparam MutexType The mutex type to use; defaults to `std::mutex`.
template < typename MutexType = std::mutex >
using use_mutex = parameter::type_param< struct detail::use_mutex_tag, MutexType >;

/// @brief Policy tag: lock the pool buffer into physical memory (`mlock`/`VirtualLock`),
///        preventing it from being swapped out by the OS.
///
/// This policy causes memory locking to be applied at construction time (compile-time opt-in).
/// The buffer is zero-filled before locking to ensure physical pages are allocated.
using lock_memory = parameter::flag_param< struct detail::lock_memory_tag >;

/// @brief Tag type for runtime memory locking via constructor argument.
///
/// Use an instance of this type as a constructor argument to opt-in to memory locking
/// for a specific memory resource instance (when `lock_memory` policy is not used).
struct enable_memory_locking_t
{};

/// @brief Convenience constant for runtime tag-dispatch constructor calls.
inline constexpr enable_memory_locking_t enable_memory_locking {};

//----------------------------------------------------------------------------------------------------------------------

namespace detail {

struct dummy_mutex
{
    void lock()
    {}
    void unlock()
    {}
    bool try_lock()
    {
        return true;
    }
};

template < typename... Policies >
using extract_mutex_t = parameter::extract_t< use_mutex_tag, dummy_mutex, Policies... >;

template < typename... Policies >
constexpr size_t extract_static_size_v = parameter::extract_integral_v< static_size_tag, std::size_t, 0, Policies... >;

} // namespace detail

} // namespace nova::pmr
