cmake_minimum_required(VERSION 3.25)

project(nova_memoryresource VERSION 0.1.0 LANGUAGES CXX C)

if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 20)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (PROJECT_IS_TOP_LEVEL AND NOT CMAKE_MSVC_DEBUG_INFORMATION_FORMAT)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "Embedded")
endif()

add_custom_target(nova_memoryresource_project_files SOURCES
    .clang-tidy
    .pre-commit-config.yaml
    _clang-format
    README.md
    LICENSE
    .github/workflows/ci.yml
)

function(nova_mr_install_pmr)
    if (NOT COMMAND CPMAddPackage)
        set(CPM_DOWNLOAD_VERSION 0.42.1)
        if(NOT EXISTS "${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
        message(STATUS "Downloading CPM.cmake ${CPM_DOWNLOAD_VERSION}...")
        file(DOWNLOAD
            "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
            "${CMAKE_BINARY_DIR}/cmake/CPM.cmake"
        )
        endif()
        include("${CMAKE_BINARY_DIR}/cmake/CPM.cmake")
    endif()
endfunction()

# Configuration options
option(NOVA_MR_TLSF "Enable TLSF memory resource" ON)
option(NOVA_MR_MIMALLOC "Enable mimalloc memory resource" ON)
set(NOVA_MR_MIMALLOC_TARGET "mimalloc-static" CACHE STRING "Target name for mimalloc to link against")

# Dependencies
if (NOVA_MR_MIMALLOC AND NOT TARGET ${NOVA_MR_MIMALLOC_TARGET})
    nova_mr_install_pmr()

    CPMAddPackage(
        NAME mimalloc
        GITHUB_REPOSITORY microsoft/mimalloc
        VERSION 3.2.8
        OPTIONS "MI_OVERRIDE OFF"
                "MI_BUILD_TESTS OFF"
                "MI_BUILD_SHARED OFF"
                "MI_BUILD_OBJECT OFF"
    )
endif()

if (NOVA_MR_TLSF AND NOT TARGET tlsf)
    nova_mr_install_pmr()

    CPMAddPackage(
        NAME tlsf
        GITHUB_REPOSITORY mattconte/tlsf
        GIT_TAG master
        DOWNLOAD_ONLY YES
    )
    if (tlsf_ADDED)
        add_library(tlsf STATIC "${tlsf_SOURCE_DIR}/tlsf.c" "${tlsf_SOURCE_DIR}/tlsf.h")
        target_include_directories(tlsf PUBLIC
            $<BUILD_INTERFACE:${tlsf_SOURCE_DIR}>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        )
        set_target_properties(tlsf PROPERTIES C_STANDARD 99)
        install(TARGETS tlsf EXPORT nova_memoryresourceTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
        install(FILES "${tlsf_SOURCE_DIR}/tlsf.h" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    endif()
endif()

# Project layout
add_library(nova_memoryresource INTERFACE)
add_library(nova::memoryresource ALIAS nova_memoryresource)

target_sources(nova_memoryresource PUBLIC FILE_SET HEADERS BASE_DIRS include)

if (NOVA_MR_MIMALLOC)
    target_sources(nova_memoryresource PUBLIC FILE_SET HEADERS FILES include/nova/pmr/mimalloc_memory_resource.hpp)
    target_link_libraries(nova_memoryresource INTERFACE ${NOVA_MR_MIMALLOC_TARGET})
    target_compile_definitions(nova_memoryresource INTERFACE NOVA_MR_HAS_MIMALLOC=1)
endif()

if (NOVA_MR_TLSF)
    target_sources(nova_memoryresource PUBLIC FILE_SET HEADERS FILES
        include/nova/pmr/tlsf_memory_resource.hpp
        include/nova/pmr/detail/memlock.hpp
    )
    target_link_libraries(nova_memoryresource INTERFACE tlsf)
    target_compile_definitions(nova_memoryresource INTERFACE NOVA_MR_HAS_TLSF=1)
endif()

target_include_directories(nova_memoryresource INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Testing
option(NOVA_BUILD_TESTS "Build unit tests" ON)
if(NOVA_BUILD_TESTS)
    if (NOT TARGET Catch2::Catch2WithMain)
        nova_mr_install_pmr()
        block()
            set(CMAKE_UNITY_BUILD ON)
            CPMAddPackage(
                NAME Catch2
                GITHUB_REPOSITORY catchorg/Catch2
                VERSION 3.13.0
            )
        endblock()
    endif()

    add_executable(nova_memoryresource_tests
        tests/test_memory_resource.cpp
    )
    target_link_libraries(nova_memoryresource_tests PRIVATE nova::memoryresource Catch2::Catch2WithMain)

    enable_testing()
    add_test(NAME nova_memoryresource_tests COMMAND nova_memoryresource_tests)
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS nova_memoryresource
    EXPORT nova_memoryresourceTargets
    FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT nova_memoryresourceTargets
    FILE nova_memoryresourceTargets.cmake
    NAMESPACE nova::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nova_memoryresource
)
