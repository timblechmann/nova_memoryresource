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
#include <tlsf.h>

#include <nova/parameter/parameter.hpp>
#include <nova/pmr/detail/memlock.hpp>
#include <nova/pmr/policies.hpp>

#ifdef NOVA_MR_HAS_TLSF

namespace nova::pmr {

namespace detail {


using tlsf_allowed_tags = std::tuple< static_size_tag, lock_memory_tag, use_mutex_tag >;


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

    using storage_type = std::conditional_t< static_sized,
                                             detail::tlsf_sized_storage< detail::extract_static_size_v< Policies... > >,
                                             detail::tlsf_heap_storage >;

public:
    using mutex_type = detail::extract_mutex_t< Policies... >;

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
    explicit tlsf_memory_resource( enable_memory_locking_t )
        requires( static_sized && !compile_time_locking )
        :
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
