// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <span>

#include <nova/parameter/parameter.hpp>
#include <nova/pmr/detail/memlock.hpp>
#include <nova/pmr/policies.hpp>

namespace nova::pmr {

namespace detail {

using monotonic_allowed_tags  = std::tuple< static_size_tag, lock_memory_tag, use_mutex_tag >;
using monotonic_required_tags = std::tuple< static_size_tag >;

template < std::size_t Size >
struct monotonic_static_storage
{
    static_assert( Size > 0, "Monotonic buffer size must be > 0" );

    std::array< std::byte, Size > buffer;
    bool                          memory_locked = false;
    std::size_t                   allocated     = 0;

    explicit monotonic_static_storage( bool lock_memory = false )
    {
        memory_locked = lock_memory ? try_lock_memory( bytes() ) : false;
        if ( memory_locked )
            std::ranges::fill( bytes(), std::byte {} );
    }

    ~monotonic_static_storage()
    {
        if ( memory_locked )
            unlock_memory( bytes() );
    }

    std::byte* data()
    {
        return buffer.data();
    }

    std::size_t size() const
    {
        return Size;
    }

    std::span< std::byte > bytes() noexcept
    {
        return buffer;
    }

    std::span< std::byte > available() noexcept
    {
        return std::span< std::byte >( buffer.data() + allocated, Size - allocated );
    }
};

} // namespace detail

/// @brief A monotonic buffer memory resource with statically allocated memory.
///
/// Equivalent to `std::pmr::monotonic_buffer_resource` but with compile-time-fixed size
/// and optionally locked memory via `lock_memory` policy.
///
/// Behaviour is controlled by zero or more policy tags:
///
/// | Policy | Effect |
/// |--------|--------|
/// | `static_size<N>` | Embeds an `N`-byte buffer directly in the object. **Required.** |
/// | `use_mutex<M>` | Protects allocations with mutex `M` (default: `std::mutex`). |
/// | `lock_memory` | Locks buffer into physical RAM via `mlock`/`VirtualLock`. |
///
/// ### Examples
///
/// ```cpp
/// // 64 KB static buffer, single-threaded
/// nova::pmr::static_monotonic_buffer_resource< nova::pmr::static_size< 65536 > > mr;
///
/// // 64 KB static buffer, thread-safe
/// nova::pmr::static_monotonic_buffer_resource< nova::pmr::static_size< 65536 >,
///                                              nova::pmr::use_mutex<> > mr;
///
/// // Static buffer with memory locking
/// nova::pmr::static_monotonic_buffer_resource< nova::pmr::static_size< 65536 >,
///                                              nova::pmr::lock_memory > mr;
/// ```
template < typename... Policies >
    requires( parameter::valid_parameters< detail::monotonic_allowed_tags, Policies... >
              && parameter::required_parameters< detail::monotonic_required_tags, Policies... > )
class static_monotonic_buffer_resource final : public std::pmr::memory_resource
{
    static constexpr std::size_t buffer_size
        = parameter::extract_integral_v< detail::static_size_tag, std::size_t, Policies... >;
    static constexpr bool compile_time_locking = parameter::has_parameter_v< detail::lock_memory_tag, Policies... >;

    using storage_type = detail::monotonic_static_storage< buffer_size >;

public:
    using mutex_type = parameter::extract_t< detail::use_mutex_tag, detail::dummy_mutex, Policies... >;

private:
    alignas( alignof( std::max_align_t ) ) storage_type storage_;
    [[no_unique_address]] mutex_type mtx_;

public:
    /// @brief Default-construct using the embedded static buffer.
    static_monotonic_buffer_resource() :
        storage_( compile_time_locking )
    {}

    /// @brief Construct and lock buffer into physical memory.
    ///        Only available when no compile-time `lock_memory` policy is set.
    explicit static_monotonic_buffer_resource( enable_memory_locking_t )
        requires( !compile_time_locking )
        :
        storage_( true )
    {}

    ~static_monotonic_buffer_resource() override = default;

    static_monotonic_buffer_resource( const static_monotonic_buffer_resource& )            = delete;
    static_monotonic_buffer_resource& operator=( const static_monotonic_buffer_resource& ) = delete;

    bool is_memory_locked() const noexcept
    {
        return storage_.memory_locked;
    }

    std::size_t used() const noexcept
    {
        return storage_.allocated;
    }

    std::size_t available() const noexcept
    {
        return buffer_size - storage_.allocated;
    }

protected:
    void* do_allocate( std::size_t bytes, std::size_t alignment ) override
    {
        std::lock_guard lock( mtx_ );

        std::byte*  p     = storage_.data() + storage_.allocated;
        std::size_t space = storage_.available().size();

        if ( std::align( alignment, bytes, reinterpret_cast< void*& >( p ), space ) == nullptr )
            throw std::bad_alloc();

        storage_.allocated = static_cast< std::size_t >( p - storage_.data() ) + bytes;
        return p;
    }

    void do_deallocate( void*, std::size_t /*bytes*/, std::size_t /*alignment*/ ) override
    {}

    bool do_is_equal( const std::pmr::memory_resource& other ) const noexcept override
    {
        return this == &other;
    }
};

} // namespace nova::pmr
