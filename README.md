# nova::memoryresource

[![CI](https://github.com/timblechmann/nova_memoryresource/actions/workflows/ci.yml/badge.svg)](https://github.com/timblechmann/nova_memoryresource/actions/workflows/ci.yml)

C++ `std::pmr::memory_resource` implementations for various allocators.

## Highlight: TLSF Memory Resource

The `tlsf_memory_resource` is a **real-time safe** memory allocator powered by the excelent
[Two-Level Segregated Fit (TLSF)](https://github.com/mattconte/tlsf) algorithm. It provides $O(1)$ constant time
allocation and deallocation guarantees and is great for e.g. real-time audio threads.

## Components

| Type | Backend | Notes |
|------|---------|-------|
| `tlsf_memory_resource` | [tlsf](https://github.com/mattconte/tlsf) | Real-time safe, $O(1)$ allocations. |
| `mimalloc_memory_resource` | [mimalloc](https://github.com/microsoft/mimalloc) | Backed by a purely-local `mi_heap_t`. Requires mimalloc v3. |

## Usage

`tlsf_memory_resource` is configured through policy tags:

| Policy | Effect |
|--------|--------|
| `static_size<N>` | Embeds an `N`-byte pool directly in the object (no heap allocation). |
| `use_mutex<M>` | Protects allocations with mutex `M` (default: `std::mutex`). |
| `lock_memory` | Locks the pool into physical memory via `mlock` (POSIX) / `VirtualLock` (Windows), preventing pagefaults. |

### Basic Examples

```cpp
#include <nova/pmr/tlsf_memory_resource.hpp>

// 64 KB static pool, single-threaded (real-time safe — no heap allocation)
nova::pmr::tlsf_memory_resource< nova::pmr::static_size< 65536 > > mr;
std::pmr::vector<int> v({1, 2, 3}, &mr);

// 64 KB static pool, thread-safe
nova::pmr::tlsf_memory_resource< nova::pmr::static_size< 65536 >,
                                 nova::pmr::use_mutex< std::mutex > > mr;

// Dynamic pool of 64 KB, single-threaded
nova::pmr::tlsf_memory_resource<> mr( 65536 );

// Dynamic pool of 64 KB, thread-safe
nova::pmr::tlsf_memory_resource< nova::pmr::use_mutex< std::mutex > > mr( 65536 );
```

### Memory Locking (Real-Time Safety)

Lock the memory pool into physical RAM to prevent OS page-outs during audio processing or other real-time tasks.
Locking is **best-effort**: if the OS call fails (e.g., insufficient privileges), the pool remains functional but unlocked.

```cpp
// Compile-time memory locking (always locked)
nova::pmr::tlsf_memory_resource< nova::pmr::static_size< 65536 >,
                                 nova::pmr::lock_memory > mr;

// Runtime opt-in via constructor tag (only that instance is locked)
nova::pmr::tlsf_memory_resource<> mr( 65536, nova::pmr::enable_memory_locking );
nova::pmr::tlsf_memory_resource< nova::pmr::static_size< 65536 > > mr( nova::pmr::enable_memory_locking );
```

## Requirements

- C++20 (GCC 12+, Clang 20+, MSVC 2022+)

## Build & test

```sh
cmake -B build
cmake --build build
ctest --test-dir build
```

## License

MIT — see [LICENSE](LICENSE)
