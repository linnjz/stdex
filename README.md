# stdex

A header-only C++ extension library: hashing, fast modular arithmetic, flat hash tables, inlined bitsets, stable radix sort, and small utilities.

All public headers live under `include/` in namespace `stdex` (hash-table internals in `stdex::unordered_flat_detail`). The library has no runtime dependencies; tests use CTest and benchmarks use Google Benchmark plus Boost / Abseil for comparison.

## Requirements

- C++ standard: the project builds with **C++23** (`CMAKE_CXX_STANDARD 23`, `tests` / `benchmarks` targets request `cxx_std_23`). Individual headers declare a lower minimum (see table below); `field_variant.hpp` requires **C++26** static reflection (P2996 `<meta>`) and is currently parked until toolchains provide it.
- Compilers tested: MSVC 19.51 (VS 2026, `cl` / `clang-cl`), Clang 22, GCC 16. SIMD paths use SSE2 on x86-64 and NEON on ARM (little-endian); scalar fallbacks are provided.
- Build: CMake >= 3.30, Ninja recommended. `cmake-scaffold` is vendored as a git submodule and supplies the shared CMake modules and tool configuration; initialize it before configuring. Optional vcpkg packages for benchmarks: `boost-unordered`, `absl`, `benchmark`.

## Layout

```text
include/                  public headers (add to include path)
  unordered_flat_detail/  hash-table implementation details (see table)
tests/                    one executable per test TU, registered with CTest
benchmarks/                Google Benchmark binaries + Boost/Abseil comparisons
third_party/cmake-scaffold/  shared CMake modules and tool configuration (submodule)
CMakeLists.txt            root project (tests + benchmarks)
CMakePresets.json         base/toolchain presets (MSVC, clang-cl, clang, gcc)
```

## Headers

| File                                   | Min. C++ | Purpose                                                      |
| -------------------------------------- | -------- | ------------------------------------------------------------ |
| `include/hybrid_alloc.hpp`             | C++17    | Stack-or-heap hybrid scratch allocation (`HYBRID_ALLOC` / `HYBRID_ALIGNED_ALLOC`, `hybrid_free`, `hybrid_realloc`). Small buffers use `alloca`-style stack storage, large ones fall back to the heap; a header marker tracks origin and detects double-free. Used by `stable_radix_sort.hpp` for histograms and temp buffers. |
| `include/bit_traits.hpp`               | C++20    | Bit-level type traits shared by hash and sort: `unsigned_of_bits` / `signed_of_bits`, `unsigned_equivalent`, `to_unsigned`, `highest_one_bit_index`, float-to-ordered-unsigned mapping. |
| `include/byte_utils.hpp`               | C++20    | Little-endian byte I/O over any `byte_like` type: `byteswap_compat`, `read64_le` / `read32_le`, `little_endian_array`. Endian-safe loading primitive for `hash.hpp`. |
| `include/pointer_iterator.hpp`         | C++20    | Fancy-pointer-aware random-access / contiguous iterator (`stdex::pointer_iterator<ValueType, Pointer, Container>`) with the full comparison and arithmetic operator set and a `std::pointer_traits` specialization. Lets a container whose `pointer` is not a raw `T*` still expose contiguous iterators; used by `inline_storage.hpp`. |
| `include/uninitialized_algorithms.hpp` | C++20    | Allocator-aware `uninitialized_*` range algorithms with sentinel and explicit-`Allocator` overloads: `uninitialized_copy` / `_n`, `uninitialized_move` / `_n`, `uninitialized_fill` / `_n`, `uninitialized_value_construct` / `_n`. Adds `default_construct_in_place`, the `has_custom_construct` / `has_custom_destroy` concepts, and `uninitialized_backout` rollback helpers. Used by `inline_storage.hpp`. |
| `include/inline_storage.hpp`           | C++20    | Generic `stdex::inline_storage<T, N, Allocator>` base: inline (SBO) storage for `N` elements with heap fallback, plus the construct / destroy / relocate helpers and RAII guards (`allocation_guard`, `reallocation_guard`, `destruction_guard`). Distinct from `unordered_flat_detail/inline_storage.hpp`; this one owns plain `T` storage and is reused by `inlined_bitset.hpp`. |
| `include/int128.hpp`                   | C++20    | Portable 128-bit integer helpers: `uint128_t`, `umul128` / `umulh`, `add128`, `mul128_u64_lo/hi`, `mix` / `mum`. Selects `_umul128` / `__umulh` / `__uint128_t` intrinsics per platform with a 32-bit-half-word portable fallback. |
| `include/fastmod.hpp`                  | C++20    | Lemire-style fast division/modulo (`computeM_u32`, `fastmod_u32`, `fastdiv_u32`, `is_divisible`, signed 32-bit variants) built on `int128.hpp`. Used to replace `%` / `/` by multiplication + high-word multiply on hash-table probe paths. |
| `include/hash.hpp`                     | C++20    | `stdex::hash<T>` and `rapidhash` / `wyhash`-family byte hashing with `standard` / `micro` / `nano` modes, generic contiguous-range support, and overloads for strings, smart pointers, and trivially-copyable types. Default hasher for the flat tables. |
| `include/fixed_string.hpp`             | C++20    | Compile-time fixed-capacity string (`basic_fixed_string`, `fixed_string`, `fixed_wstring`, ...) following P3094R5 with a trivial-type deviation so it stays usable in `constexpr` `inplace_vector`. Provides `std::formatter` / ostream support and user-defined literals. |
| `include/inlined_bitset.hpp`           | C++20    | `std::bitset`-like bit container (`stdex::inlined_bitset<Block, N, Allocator>`) with optional inline (`N > 0`) block storage and heap fallback, dynamic growth (`push_back` / `insert` / `erase` / `resize` / `assign`), `from_blocks` / range constructors, bitwise operators, `find_first` / `find_next` / `find_last` and zero-variants, `to_string` / `to_ulong` / `to_ullong`, stream I/O, and SSE2 / AVX2 / NEON-accelerated count, scan, fill and pack paths. Built on `inline_storage.hpp`. |
| `include/stable_radix_sort.hpp`        | C++20    | Stable LSD/MSD radix sort for arithmetic types, enums, and projected key maps: `stdex::stable_radix_sort(first, last, ...)`, `RadixOptions` (digit width, order, NaN handling), OpenMP support, hybrid-alloc scratch storage. |
| `include/unordered_flat_map.hpp`       | C++20    | Open-addressing flat hash map (`stdex::unordered_flat_map<Key, T, N, Hasher, KeyEqual, Allocator>`) with optional inline (`N > 0`) stack storage, heterogeneous lookup, and node/extraction APIs. Thin facade over `unordered_flat_detail::hash_table`. |
| `include/unordered_flat_set.hpp`       | C++20    | Open-addressing flat hash set (`stdex::unordered_flat_set<Key, N, ...>`). Same engine and API shape as the map, with set traits. |
| `include/field_variant.hpp`            | C++26    | Reflection-based named-field variant over a struct's non-static data members (P2996 `<meta>`): access by index, type, or name, `visit`, comparison, and hashing. Requires a P2996-capable compiler; its test target is currently disabled. |

### `include/unordered_flat_detail/` internals

Included indirectly by the map/set facades (upstream Boost.FoA layout: public headers include sibling detail headers by bare filename, so this directory must also be on the include path for `test_hash_table` / `bench_hash_table`).

| File                      | Purpose                                                      |
| ------------------------- | ------------------------------------------------------------ |
| `hash_table.hpp`          | Generic open-addressing engine (`hash_table<Traits, Hasher, KeyEqual, Allocator>`): probing, insert/emplace/erase/find, rehash/reserve, heterogeneous overloads, ranges support. |
| `inline_storage.hpp`      | Inline (SBO, `N > 0`) vs. heap storage policy for the flat tables, group array management, load-factor control. Distinct from the top-level `include/inline_storage.hpp`. |
| `group15.hpp`             | 15-slot fingerprint group with SSE2 / NEON / scalar paths (available / sentinel metadata, match/mismatch scans). |
| `hash_table_iterator.hpp` | Forward iterators (const/mutable + lazy variants) shared by map and set. |
| `ufmap_traits.hpp`        | Map traits: `pair<const Key, T>` value model, `key_fn`, reference proxies. |
| `ufset_traits.hpp`        | Set traits: key-is-value model.                              |
| `try_key_extraction.hpp`  | `try_key_extraction`: perfect-forwarding key hint extraction for `emplace` (avoids hashing before knowing whether construction is possible). |

## Build, test, benchmark

```powershell
git submodule update --init --recursive
cmake --preset msvc_x64_debug  # see CMakeUserPresets.json / CMakePresets.json
cmake --build out/build/msvc_x64_debug
ctest --test-dir out/build/msvc_x64_debug --output-on-failure
```

Replace `msvc_x64_debug` with another configured preset as needed.

Benchmarks (`benchmarks/`): `bench_stable_radix_sort` and `bench_inlined_bitset` (no third-party deps), plus `bench_hash_table` (links `Boost::unordered`, `absl::flat_hash_map`, `benchmark::benchmark` via vcpkg). Benchmarks are skipped with a warning when vcpkg or those packages are unavailable.

## Design references

- `hybrid_alloc.hpp`: threshold, marker-ahead-of-pointer dispatch, and size computation mirror Windows Kits 10 UCRT `ucrt/malloc.h` `_malloca` / `_freea` (`_ALLOCA_S_THRESHOLD`, `_MarkAllocaS`, `_MallocaComputeSize`).
- `hash.hpp`: algorithm and stripe/lane structure derived from Nicolas De Carli's RapidHash — https://github.com/Nicoshev/rapidhash/blob/master/rapidhash.h
- `fastmod.hpp`: API and multiplier method ported from Daniel Lemire's fastmod — https://github.com/lemire/fastmod/blob/master/include/fastmod.h — on top of the local `int128.hpp` primitives.
- `unordered_flat_map.hpp` / `unordered_flat_set.hpp` (+ `unordered_flat_detail/`): table layout, group metadata, and growth policy adapted from Boost.Unordered FoA (`boost/unordered/detail/foa/core.hpp`, `table.hpp`) — https://github.com/boostorg/unordered/tree/develop/include/boost/unordered — with key-extraction semantics following libc++ `__utility/try_key_extraction.h` — https://github.com/llvm/llvm-project/blob/main/libcxx/include/__utility/try_key_extraction.h
- `fixed_string.hpp`: interface follows P3094R5 (`basic_fixed_string`); deviates by writing the null terminator explicitly instead of `charT data_[N+1] = {}` so the type stays trivial for `constexpr inplace_vector` use.
- `inlined_bitset.hpp`: public interface follows pinam45/dynamic_bitset (an implementation of the proposed `std::dynamic_bitset`) — https://github.com/pinam45/dynamic_bitset — only the API declarations were referenced; no implementation code was copied.
