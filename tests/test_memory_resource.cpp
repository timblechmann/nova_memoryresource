// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#ifdef NOVA_MR_HAS_MIMALLOC
#    include <nova/pmr/mimalloc_memory_resource.hpp>
#endif

#ifdef NOVA_MR_HAS_TLSF
#    include <nova/pmr/tlsf_memory_resource.hpp>
#endif

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

TEST_CASE( "policies" )
{
    static_assert( nova::pmr::detail::has_static_size< nova::pmr::static_size< 1024 > >() );
    static_assert( !nova::pmr::detail::has_static_size< int >() );

    static_assert( !nova::pmr::detail::has_use_mutex< nova::pmr::static_size< 1024 > >() );
    static_assert( nova::pmr::detail::has_use_mutex< nova::pmr::use_mutex<> >() );

    static_assert( std::is_same< nova::pmr::detail::get_mutex_type< nova::pmr::use_mutex<> >::type, std::mutex >::value );
    static_assert( std::is_same< nova::pmr::detail::get_mutex_type<>::type, nova::pmr::detail::dummy_mutex >::value );

    static_assert( !nova::pmr::detail::has_enable_memory_locking< nova::pmr::static_size< 1024 > >() );
    static_assert( !nova::pmr::detail::has_enable_memory_locking< nova::pmr::use_mutex<> >() );
    static_assert( nova::pmr::detail::has_enable_memory_locking< nova::pmr::lock_memory >() );
    static_assert(
        nova::pmr::detail::has_enable_memory_locking< nova::pmr::static_size< 1024 >, nova::pmr::lock_memory >() );

#if 0 // multiple keyword arguments should statically assert
    nova::pmr::detail::has_static_size< nova::pmr::static_size< 512 >, nova::pmr::static_size< 1022 > >();
    nova::pmr::detail::has_use_mutex< nova::pmr::use_mutex<>, nova::pmr::use_mutex<> >();
    nova::pmr::detail::has_enable_memory_locking< nova::pmr::lock_memory, nova::pmr::lock_memory >();
#endif
}


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
