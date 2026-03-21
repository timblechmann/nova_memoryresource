// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Tim Blechmann

#pragma once

#include <cstddef>
#include <span>

#if defined( _WIN32 )
extern "C" {
int __stdcall VirtualLock( void* lpAddress, unsigned long dwSize );
int __stdcall VirtualUnlock( void* lpAddress, unsigned long dwSize );
}
#elif defined( __unix__ ) || defined( __APPLE__ )
#    include <sys/mman.h>
#endif

namespace nova::pmr::detail {

bool try_lock_memory( std::span< std::byte > buffer ) noexcept
{
#if defined( _WIN32 )
    return ::VirtualLock( buffer.data(), buffer.size() ) != 0;
#elif defined( __unix__ ) || defined( __APPLE__ )
    return ::mlock( buffer.data(), buffer.size() ) == 0;
#else
    return false;
#endif
}

void unlock_memory( std::span< std::byte > buffer ) noexcept
{
#if defined( _WIN32 )
    ::VirtualUnlock( buffer.data(), buffer.size() );
#elif defined( __unix__ ) || defined( __APPLE__ )
    ::munlock( buffer.data(), buffer.size() );
#endif
}

} // namespace nova::pmr::detail
