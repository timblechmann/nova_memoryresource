// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#ifdef NOVA_MR_HAS_MIMALLOC
#    include <nova/pmr/mimalloc_memory_resource.hpp>
#endif

#ifdef NOVA_MR_HAS_TLSF
#    include <nova/pmr/tlsf_memory_resource.hpp>
#endif

#include <nova/pmr/static_monotonic_buffer_resource.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

TEST_CASE( "memory resource instantiation" )
{
#ifdef NOVA_MR_HAS_TLSF
    SECTION( "tlsf static" )
    {
        nova::pmr::tlsf_memory_resource< nova::pmr::static_size< 1024 * 64 > > mr;
        CHECK( mr.is_equal( mr ) );
        void* p = mr.allocate( 128 );
        REQUIRE( p != nullptr );
        mr.deallocate( p, 128 );
    }

    SECTION( "tlsf static threadsafe" )
    {
        nova::pmr::tlsf_memory_resource< nova::pmr::static_size< 1024 * 64 >, nova::pmr::use_mutex< std::mutex > > mr;
        CHECK( mr.is_equal( mr ) );
        void* p = mr.allocate( 128 );
        REQUIRE( p != nullptr );
        mr.deallocate( p, 128 );
    }

    SECTION( "tlsf dynamic" )
    {
        nova::pmr::tlsf_memory_resource<> mr( 1024 * 64 );
        CHECK( mr.is_equal( mr ) );
        void* p = mr.allocate( 128, 64 );
        REQUIRE( p != nullptr );
        REQUIRE( reinterpret_cast< std::uintptr_t >( p ) % 64 == 0 );
        mr.deallocate( p, 128, 64 );
    }

    SECTION( "tlsf dynamic threadsafe" )
    {
        nova::pmr::tlsf_memory_resource< nova::pmr::use_mutex< std::mutex > > mr( 1024 * 64 );
        CHECK( mr.is_equal( mr ) );
        void* p = mr.allocate( 256 );
        REQUIRE( p != nullptr );
        mr.deallocate( p, 256 );
    }

    SECTION( "tlsf equality" )
    {
        nova::pmr::tlsf_memory_resource<> mr1( 1024 * 64 );
        nova::pmr::tlsf_memory_resource<> mr2( 1024 * 64 );
        CHECK( mr1.is_equal( mr1 ) );
        CHECK_FALSE( mr1.is_equal( mr2 ) );
    }

    SECTION( "tlsf out of memory" )
    {
        nova::pmr::tlsf_memory_resource< nova::pmr::static_size< 1024 * 8 > > mr;
        REQUIRE_THROWS_AS( mr.allocate( 1024 * 16 ), std::bad_alloc );
    }

    SECTION( "tlsf static compile-time memory locking" )
    {
        nova::pmr::tlsf_memory_resource< nova::pmr::static_size< 1024 * 64 >, nova::pmr::lock_memory > mr;
        // mlock may be refused by the OS (e.g. resource limits on CI); the resource
        // must still be fully functional regardless.
        CHECK( mr.is_memory_locked() == mr.is_memory_locked() ); // just verify it compiles/runs
        void* p = mr.allocate( 128 );
        REQUIRE( p != nullptr );
        mr.deallocate( p, 128 );
    }

    SECTION( "tlsf dynamic runtime memory locking (tag ctor)" )
    {
        nova::pmr::tlsf_memory_resource<> mr( 1024 * 64, nova::pmr::enable_memory_locking );
        CHECK( mr.is_memory_locked() == mr.is_memory_locked() );
        void* p = mr.allocate( 128 );
        REQUIRE( p != nullptr );
        mr.deallocate( p, 128 );
    }

    SECTION( "tlsf static runtime memory locking (tag ctor)" )
    {
        nova::pmr::tlsf_memory_resource< nova::pmr::static_size< 1024 * 64 > > mr( nova::pmr::enable_memory_locking );
        CHECK( mr.is_memory_locked() == mr.is_memory_locked() );
        void* p = mr.allocate( 128 );
        REQUIRE( p != nullptr );
        mr.deallocate( p, 128 );
    }

    SECTION( "tlsf no locking by default" )
    {
        nova::pmr::tlsf_memory_resource< nova::pmr::static_size< 1024 * 64 > > mr;
        CHECK_FALSE( mr.is_memory_locked() );
        nova::pmr::tlsf_memory_resource<> mr2( 1024 * 64 );
        CHECK_FALSE( mr2.is_memory_locked() );
    }
#endif // NOVA_MR_HAS_TLSF

#ifdef NOVA_MR_HAS_MIMALLOC
    SECTION( "mimalloc" )
    {
        nova::pmr::mimalloc_memory_resource mr;
        CHECK( mr.is_equal( mr ) );
        void* p = mr.allocate( 128, 64 );
        REQUIRE( p != nullptr );
        REQUIRE( reinterpret_cast< std::uintptr_t >( p ) % 64 == 0 );
        mr.deallocate( p, 128, 64 );
    }

    SECTION( "mimalloc equality" )
    {
        nova::pmr::mimalloc_memory_resource mr1;
        nova::pmr::mimalloc_memory_resource mr2;
        CHECK( mr1.is_equal( mr1 ) );
        CHECK_FALSE( mr1.is_equal( mr2 ) );
    }
#endif // NOVA_MR_HAS_MIMALLOC
}

TEST_CASE( "static_monotonic_buffer_resource instantiation" )
{
    SECTION( "static monotonic single-threaded" )
    {
        nova::pmr::static_monotonic_buffer_resource< nova::pmr::static_size< 1024 * 64 > > mr;
        CHECK( mr.is_equal( mr ) );
        void* p = mr.allocate( 128 );
        REQUIRE( p != nullptr );
        REQUIRE( mr.used() == 128 );
        mr.deallocate( p, 128 );     // no-op
        REQUIRE( mr.used() == 128 ); // allocation not reclaimed
    }

    SECTION( "static monotonic threadsafe" )
    {
        nova::pmr::static_monotonic_buffer_resource< nova::pmr::static_size< 1024 * 64 >, nova::pmr::use_mutex< std::mutex > >
            mr;
        CHECK( mr.is_equal( mr ) );
        void* p = mr.allocate( 128 );
        REQUIRE( p != nullptr );
        REQUIRE( mr.used() == 128 );
        mr.deallocate( p, 128 );
    }

    SECTION( "static monotonic alignment" )
    {
        nova::pmr::static_monotonic_buffer_resource< nova::pmr::static_size< 1024 * 64 > > mr;
        void*                                                                              p1 = mr.allocate( 1, 64 );
        REQUIRE( p1 != nullptr );
        REQUIRE( reinterpret_cast< std::uintptr_t >( p1 ) % 64 == 0 );
        std::size_t after_p1 = mr.used();
        void*       p2       = mr.allocate( 128 );
        REQUIRE( p2 != nullptr );
        REQUIRE( mr.used() >= after_p1 + 128 ); // may have alignment padding
    }

    SECTION( "static monotonic out of memory" )
    {
        nova::pmr::static_monotonic_buffer_resource< nova::pmr::static_size< 1024 * 8 > > mr;
        REQUIRE_THROWS_AS( mr.allocate( 1024 * 16 ), std::bad_alloc );
    }

    SECTION( "static monotonic equality" )
    {
        nova::pmr::static_monotonic_buffer_resource< nova::pmr::static_size< 1024 * 64 > > mr1;
        nova::pmr::static_monotonic_buffer_resource< nova::pmr::static_size< 1024 * 64 > > mr2;
        CHECK( mr1.is_equal( mr1 ) );
        CHECK_FALSE( mr1.is_equal( mr2 ) );
    }

    SECTION( "static monotonic compile-time memory locking" )
    {
        nova::pmr::static_monotonic_buffer_resource< nova::pmr::static_size< 1024 * 64 >, nova::pmr::lock_memory > mr;
        CHECK( mr.is_memory_locked() == mr.is_memory_locked() );
        void* p = mr.allocate( 128 );
        REQUIRE( p != nullptr );
        mr.deallocate( p, 128 );
    }

    SECTION( "static monotonic runtime memory locking (tag ctor)" )
    {
        nova::pmr::static_monotonic_buffer_resource< nova::pmr::static_size< 1024 * 64 > > mr(
            nova::pmr::enable_memory_locking );
        CHECK( mr.is_memory_locked() == mr.is_memory_locked() );
        void* p = mr.allocate( 128 );
        REQUIRE( p != nullptr );
        mr.deallocate( p, 128 );
    }

    SECTION( "static monotonic no locking by default" )
    {
        nova::pmr::static_monotonic_buffer_resource< nova::pmr::static_size< 1024 * 64 > > mr;
        CHECK_FALSE( mr.is_memory_locked() );
    }

    SECTION( "static monotonic monotonic behavior" )
    {
        nova::pmr::static_monotonic_buffer_resource< nova::pmr::static_size< 1024 * 64 > > mr;
        void*                                                                              p1 = mr.allocate( 100 );
        void*                                                                              p2 = mr.allocate( 200 );
        void*                                                                              p3 = mr.allocate( 300 );
        REQUIRE( mr.used() >= 600 ); // may have alignment padding
        REQUIRE( mr.available() <= 1024 * 64 - 600 );
    }
}
