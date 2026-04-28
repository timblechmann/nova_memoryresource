// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <memory>
#include <memory_resource>
#include <mutex>
#include <span>
#include <tlsf.h>

#include <nova/parameter/parameter.hpp>
#include <nova/pmr/detail/memlock.hpp>

#ifdef NOVA_MR_HAS_TLSF

namespace nova::pmr {

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// policies

namespace detail {
struct static_size_tag
{};
struct lock_memory_tag
{};
struct use_mutex_tag
{};

using tlsf_allowed_tags = std::tuple< static_size_tag, lock_memory_tag, use_mutex_tag >;
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
///
/// Example:
/// ```cpp
/// nova::pmr::tlsf_memory_resource< nova::pmr::static_size< 65536 >,
///                                  nova::pmr::lock_memory > mr;
/// assert( mr.is_memory_locked() ); // Usually true (unless OS refused lock)
/// ```
using lock_memory = parameter::flag_param< struct detail::lock_memory_tag >;

/// @brief Tag type for runtime memory locking via constructor argument.
///
/// Use an instance of this type as a constructor argument to opt-in to memory locking
/// for a specific memory resource instance (when `lock_memory` policy is not used).
///
/// Example:
/// ```cpp
/// nova::pmr::tlsf_memory_resource<> mr( 65536, nova::pmr::enable_memory_locking );
/// if ( mr.is_memory_locked() ) { /* physical RAM locked */ }
/// ```
struct enable_memory_locking_t
{};

/// @brief Convenience constant for runtime tag-dispatch constructor calls.
inline constexpr enable_memory_locking_t enable_memory_locking {};

namespace detail {


struct tlsf_heap_storage
{
    std::unique_ptr< std::byte[] > buffer;
    std::size_t                    size_;
    bool                           memory_locked {};

    explicit tlsf_heap_storage( std::size_t size, bool lock_memory = false ) :
        buffer( new std::byte[ size ] ),
        size_( size )
    {
        memory_locked = lock_memory ? try_lock_memory( bytes() ) : false;
        if ( memory_locked )
            std::ranges::fill( bytes(), std::byte {} );
    }

    ~tlsf_heap_storage()
    {
        if ( memory_locked )
            unlock_memory( bytes() );
    }

    void* data()
    {
        return buffer.get();
    }
    std::size_t size() const
    {
        return size_;
    }
    std::span< std::byte > bytes() noexcept
    {
        return std::span {
            buffer.get(),
            size(),
        };
    }
};

template < std::size_t Size >
struct tlsf_sized_storage
{
    static_assert( Size > 1024,
                   "TLSF pool size must be large enough to hold internal structures; 1 KB is a reasonable lower "
                   "bound." );

    std::array< std::byte, Size > buffer;
    bool                          memory_locked = false;

    explicit tlsf_sized_storage( bool lock_memory = false )
    {
        memory_locked = lock_memory ? try_lock_memory( bytes() ) : false;
        if ( memory_locked )
            std::ranges::fill( bytes(), std::byte {} );
    }

    ~tlsf_sized_storage()
    {
        if ( memory_locked )
            unlock_memory( bytes() );
    }

    void* data()
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
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// locking

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

} // namespace detail

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/// @brief A memory resource powered by the Two-Level Segregated Fit (TLSF) memory allocator.
///
/// Behaviour is controlled by zero or more policy tags:
///
/// | Policy | Effect |
/// |--------|--------|
/// | `static_size<N>` | Embeds an `N`-byte pool directly in the object (no heap allocation). |
/// | `use_mutex<M>` | Protects every allocation/deallocation with mutex `M` (default: `std::mutex`). |
/// | `lock_memory` | Locks the pool into physical RAM via `mlock` / `VirtualLock`, preventing pagefaults. |
///
/// When neither policy is supplied the resource allocates its pool on the heap at construction
/// time (size passed as constructor argument) and uses a no-op mutex.
///
/// ### Examples
///
/// ```cpp
/// // 64 KB static pool, single-threaded
/// nova::pmr::tlsf_memory_resource< nova::pmr::static_size< 65536 > > mr;
///
/// // 64 KB static pool, thread-safe
/// nova::pmr::tlsf_memory_resource< nova::pmr::static_size< 65536 >,
///                                  nova::pmr::use_mutex<> > mr;
///
/// // Dynamic pool of 64 KB, thread-safe
/// nova::pmr::tlsf_memory_resource< nova::pmr::use_mutex<> > mr( 65536 );
///
/// // Dynamic pool of 64 KB, single-threaded
/// nova::pmr::tlsf_memory_resource<> mr( 65536 );
///
/// // Static pool with compile-time memory locking (always locked)
/// nova::pmr::tlsf_memory_resource< nova::pmr::static_size< 65536 >,
///                                  nova::pmr::lock_memory > mr;
///
/// // Dynamic pool with runtime memory locking (opt-in via constructor tag)
/// nova::pmr::tlsf_memory_resource<> mr( 65536, nova::pmr::enable_memory_locking );
///
/// // Static pool with runtime memory locking (opt-in via constructor tag)
/// nova::pmr::tlsf_memory_resource< nova::pmr::static_size< 65536 > > mr( nova::pmr::enable_memory_locking );
/// ```
template < typename... Policies >
    requires( parameter::valid_parameters< detail::tlsf_allowed_tags, Policies... > )
class tlsf_memory_resource final : public std::pmr::memory_resource
{
    static constexpr bool static_sized         = parameter::has_parameter_v< detail::static_size_tag, Policies... >;
    static constexpr bool compile_time_locking = parameter::has_parameter_v< detail::lock_memory_tag, Policies... >;

    using storage_type = std::conditional_t<
        static_sized,
        detail::tlsf_sized_storage< parameter::extract_integral_v< detail::static_size_tag, std::size_t, 0, Policies... > >,
        detail::tlsf_heap_storage >;

public:
    using mutex_type = parameter::extract_t< detail::use_mutex_tag, detail::dummy_mutex, Policies... >;

private:
    alignas( alignof( std::max_align_t ) ) storage_type storage_;
    [[no_unique_address]] mutex_type mtx_;
    tlsf_t                           tlsf_ {
        tlsf_create_with_pool( storage_.data(), storage_.size() ),
    };

public:
    /// @brief Construct a dynamically-sized pool of \p size bytes.
    ///        Only available when no `static_size` policy is present.
    explicit tlsf_memory_resource( std::size_t size )
        requires( !static_sized )
        :
        storage_( size, compile_time_locking )
    {}

    /// @brief Construct a dynamically-sized pool of \p size bytes and lock it into physical memory.
    ///        Only available when no `static_size` policy and no compile-time
    ///        `lock_memory` policy are present — use one or the other, not both.
    tlsf_memory_resource( std::size_t size, enable_memory_locking_t )
        requires( !static_sized && !compile_time_locking )
        :
        storage_( size, true )
    {}

    /// @brief Default-construct using the embedded static pool.
    ///        Only available when a `static_size` policy is present.
    tlsf_memory_resource()
        requires( static_sized )
        :
        storage_( false )
    {}

    /// @brief Construct using the embedded static pool and lock it into physical memory.
    ///        Only available when a `static_size` policy is present and no compile-time
    ///        `lock_memory` policy is set — use one or the other, not both.
    template < typename T = std::bool_constant< !static_sized || compile_time_locking > >
        requires std::same_as< T, std::false_type >
    explicit tlsf_memory_resource( enable_memory_locking_t ) :
        storage_( true )
    {}

    ~tlsf_memory_resource() override
    {
        tlsf_destroy( tlsf_ );
    }

    tlsf_memory_resource( const tlsf_memory_resource& )            = delete;
    tlsf_memory_resource& operator=( const tlsf_memory_resource& ) = delete;

    bool is_memory_locked() const noexcept
    {
        return storage_.memory_locked;
    }

protected:
    void* do_allocate( std::size_t bytes, std::size_t alignment ) override
    {
        std::lock_guard lock( mtx_ );
        void*           p = tlsf_memalign( tlsf_, alignment, bytes );
        if ( !p )
            throw std::bad_alloc();

        return p;
    }

    void do_deallocate( void* p, std::size_t bytes, std::size_t alignment ) override
    {
        std::lock_guard lock( mtx_ );
        tlsf_free( tlsf_, p );
    }

    bool do_is_equal( const std::pmr::memory_resource& other ) const noexcept override
    {
        return this == &other;
    }
};

} // namespace nova::pmr

#endif // NOVA_MR_HAS_TLSF
