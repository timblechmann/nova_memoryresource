// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <memory_resource>
#include <mimalloc.h>

#ifdef NOVA_MR_HAS_MIMALLOC

#    if MI_MALLOC_VERSION < 3000
#        error "nova::pmr::mimalloc_memory_resource requires mimalloc v3"
#    endif

namespace nova::pmr {

/// @brief A memory resource powered by the mimalloc allocator.
/// Manages a local `mi_heap_t` instance.
class mimalloc_memory_resource final : public std::pmr::memory_resource
{
    mi_heap_t* heap_;

public:
    mimalloc_memory_resource()
    {
        heap_ = mi_heap_new();
    }

    ~mimalloc_memory_resource() override
    {
        mi_heap_delete( heap_ );
    }

    mimalloc_memory_resource( const mimalloc_memory_resource& )            = delete;
    mimalloc_memory_resource& operator=( const mimalloc_memory_resource& ) = delete;

protected:
    void* do_allocate( std::size_t bytes, std::size_t alignment ) override
    {
        void* p = mi_heap_malloc_aligned( heap_, bytes, alignment );
        if ( !p )
            throw std::bad_alloc();
        return p;
    }

    void do_deallocate( void* p, std::size_t bytes, std::size_t alignment ) override
    {
        mi_free_size_aligned( p, bytes, alignment );
    }

    bool do_is_equal( const std::pmr::memory_resource& other ) const noexcept override
    {
        return this == &other;
    }
};

} // namespace nova::pmr

#endif // NOVA_MR_HAS_MIMALLOC
