# stdex

A header-only C++ extension library: portable 128-bit integers, hashing, fast modular arithmetic, flat hash tables, inlined bitsets, stable radix sort, minimal perfect hashing, 64-bit pseudorandom number generators, and small utilities.

All public library headers live under `include/` in namespace `stdex` (hash-table internals in `stdex::unordered_flat_detail` and `stdex::minimal_perfect_hash_detail`); the standalone macro headers live under `marco/` (see [Macro headers](#macro-headers-marco)). The library has no runtime dependencies; tests use CTest and benchmarks use Google Benchmark plus Boost / Abseil / frozen for comparison.

## Requirements

- C++ standard: the project builds with **C++23** (`CMAKE_CXX_STANDARD 23`, `tests` / `benchmarks` targets request `cxx_std_23`). Individual headers declare a lower minimum — **C++20** for most (see the header tables below) — and some wrap optional C++23 features (e.g. `std::from_range` constructors gated by `__cpp_lib_containers_ranges`). C++23 builds exercise the full surface; **C++20** builds also work and pass the suite, but the C++23-only paths are compiled out there and thus go untested. `field_variant.hpp` requires **C++26** static reflection (P2996 `<meta>`) and is currently parked until toolchains provide it.
- Compilers tested: MSVC 19.51 (VS 2026, `cl` / `clang-cl`), Clang 22, GCC 16. SIMD paths use SSE2 on x86-64 and NEON on ARM (little-endian); scalar fallbacks are provided.
- Build: CMake >= 3.30, Ninja recommended. `cmake-scaffold` is vendored as a git submodule and supplies the shared CMake modules and tool configuration; initialize it before configuring. Optional vcpkg packages for benchmarks: `boost-unordered`, `absl`, `benchmark`, `frozen`.

## Layout

```text
include/                        public headers (add to include path)
  unordered_flat_detail/        hash-table implementation details (see table)
  minimal_perfect_hash_detail/  minimal-perfect hash-table implementation details (see table)
marco/                          standalone macro headers (add to include path)
tests/                          one executable per test TU, registered with CTest
benchmarks/                     Google Benchmark binaries + Boost/Abseil/frozen comparisons
third_party/cmake-scaffold/     shared CMake modules and tool configuration (submodule)
CMakeLists.txt                  root project (tests + benchmarks)
CMakePresets.json               base/toolchain presets (MSVC, clang-cl, clang, gcc)
```

## Headers

| File                                   | Min. C++ | Purpose                                                      |
| -------------------------------------- | -------- | ------------------------------------------------------------ |
| `include/hybrid_alloc.hpp`             | C++17    | Stack-or-heap hybrid scratch allocation (`HYBRID_ALLOC` / `HYBRID_ALLOC_THRESHOLD`, `HYBRID_ALIGNED_ALLOC` / `HYBRID_ALIGNED_ALLOC_THRESHOLD`, `hybrid_free`, `hybrid_get_total_size`, `hybrid_mark_persistent`). Small buffers use `alloca`-style stack storage, large ones fall back to the heap; a header marker tracks origin and detects double-free. Used by `stable_radix_sort.hpp` for histogram scratch; its element temp buffer uses the caller-provided allocator. |
| `include/bit.hpp`                      | C++20    | Bit-level traits and bit-manipulation utilities shared by hash, sort, and the 128-bit wrappers: `unsigned_of_bits` / `signed_of_bits`, `unsigned_equivalent` (including bool-backed enums), `to_unsigned`, `countl_zero` / `countr_zero` / `countl_one` / `countr_one` / `popcount` / `bit_width` / `bit_floor` / `bit_ceil` / `has_single_bit`, `shl` / `shr` / `rotl` / `rotr`, `bit_repeat` / `bit_compress` / `bit_expand`, `highest_one_bit_index` / `lowest_one_bit_index`, `is_all_bits_zero`, `byteswap` / `bit_reverse`, and little-endian reads (`read64_le_unchecked` / `read32_le_unchecked`, `little_endian_array`); 128-bit overloads require `int128.hpp`. |
| `include/fancy_pointer_iterator.hpp`    | C++20    | Fancy-pointer-aware iterator (`stdex::fancy_pointer_iterator<ValueType, Pointer, Container>`) with the full comparison and arithmetic operator set, a `std::pointer_traits` specialization, and conditional `noexcept`. Layered concepts (`pointer_input` / `pointer_forward` / `pointer_bidirectional` / `pointer_random_access` / `pointer_contiguous`) select `iterator_concept`; a random-access pointer with linear addressing and a valid `std::to_address` yields `contiguous_iterator_tag` (with `iterator_category` kept at random access for legacy algorithms). Standalone utility for containers that cannot expose raw-pointer iterators. |
| `include/uninitialized_algorithms.hpp` | C++20    | Allocator-aware `uninitialized_*` range algorithms with sentinel and explicit-`Allocator` overloads: `uninitialized_copy` / `_n`, `uninitialized_move` / `_n`, `uninitialized_fill` / `_n`, `uninitialized_default_construct` / `_n`, `uninitialized_value_construct` / `_n`. Adds `default_construct_in_place`, the `has_custom_construct` / `has_custom_destroy` concepts, and `uninitialized_backout` rollback helpers. Used by `inline_storage.hpp` and `uninitialized_buffer.hpp`. |
| `include/uninitialized_buffer.hpp`     | C++20    | Fixed-size allocator-backed raw storage (`stdex::uninitialized_buffer<T, Allocator>`) for trivially default-constructible types, with rebound allocator support, fancy-pointer storage, accessors, size queries, and move semantics. Copy construction is enabled only when `T` is trivially copyable and both `T` and the allocator are copy-constructible; copy assignment additionally requires both to be copy-assignable. |
| `include/inline_storage.hpp`           | C++20    | Generic `stdex::inline_storage<T, N, Allocator>` base: inline (SBO) storage for `N` elements with heap fallback, plus the construct / destroy / relocate helpers and RAII guards (`allocation_guard`, `reallocation_guard`, `destruction_guard`). `swap` and `shrink_to_fit` keep heap/inline ownership and size state consistent when element transfers or allocation throws. Iterators expose raw pointers even when the allocator uses fancy pointers. Distinct from `unordered_flat_detail/inline_storage.hpp`; this one owns plain `T` storage and is reused by `inlined_bitset.hpp` and `inlined_vector.hpp`; it also hosts the shared synth-three-way comparator (`inline_storage_detail::synth_three_way_fn`, reachable as `base::synth_three_way`) used for `std::vector`-style ordering. |
| `include/inplace_storage.hpp`          | C++20    | Generic fixed-capacity element storage (`stdex::inplace_storage<T, N, SizePolicy>`) shared by `inplace_vector` and `inplace_array`, in the spirit of `inline_storage`. Selects `array_storage` (trivially-copyable values before C++26, usable in constant expressions), `trivial_union_storage` (trivial values from C++26, `std::start_lifetime` based), or `nontrivial_storage` (in-place construction, no allocation). The injected `SizePolicy` is `inplace_storage_detail::storage_size_base` (tracks a logical size, for `inplace_vector`) or `inplace_storage_detail::no_size_base` (fixed count `N`, for `inplace_array`), so the same code serves both a resizable and a fixed-size consumer. Union buffers are sized exactly `N` (`N == 0` reserves nothing via a partial specialization). Also hosts the shared `synth-three-way` comparator and `container_compatible_range` concept, so `inplace_vector` and `inplace_array` need no per-container detail namespace. |
| `include/int128.hpp`                   | C++20    | Full-width `stdex::uint128_t` / `stdex::int128_t` (16 bytes, 16-byte alignment, trivially copyable, standard layout): arithmetic, comparisons, shifts, bitwise operators, checked/wrapping/saturating arithmetic, integral conversions, little/big-endian byte serialization, `std::numeric_limits`, `std::formatter` (b/B/c/d/o/x/X, fill/alignment/sign/width/#/0, locale grouping, wide characters), and `stdex::hash` (via `hash.hpp`). `stdex::abs` / `gcd` / `lcm` / `midpoint` / `pow` accept the 128-bit types (`abs` returns the unsigned magnitude, including for signed minima), and `bit.hpp` adds the `<bit>`-style helpers. Uses native `__int128` where available; MSVC uses `_umul128` / `_addcarry_u64` / `_subborrow_u64` / `_udiv128` on x64 and `__umulh` / NEON shifts on ARM64, with portable Knuth division. |
| `include/fastmod.hpp`                  | C++20    | Lemire-style fast division/modulo built on `int128.hpp`: `computeM_u32` / `fastmod_u32` / `fastdiv_u32` / `is_divisible`, signed 32-bit `computeM_s32` / `fastmod_s32` / `fastdiv_s32`, plus 64-bit `computeM_u64` / `fastmod_u64` / `fastdiv_u64` / `is_divisible_u64`, and compile-time-divisor `fastmod<d>` / `fastdiv<d>`. |
| `include/hash.hpp`                     | C++20    | `stdex::hash<T>` and RapidHash-derived byte hashing (`hash_bytes` / `hash_range` / `hash_value`) with `standard` / `micro` / `nano` modes, generic contiguous-range support, and overloads for strings, smart pointers, and trivially-copyable types. `hash<std::basic_string>` / `hash<std::basic_string_view>` are transparent and accept string, string_view, and C-string forms with identical bytes. Default hasher for the unordered flat tables and the ptrhash/minimal-perfect hash containers. |
| `include/random.hpp`                   | C++20    | Header-only 64-bit pseudorandom number generators implementing the `UniformRandomBitGenerator` interface: `stdex::splitmix64` (2^64 period, O(1) `discard`), `stdex::wyrand` (Weyl state plus a 64x64->128 multiply-xorshift, 2^64 period), and `stdex::lehmer64` (128-bit multiplicative LCG, 2^126 period, O(log n) `discard`). All three support integral and SeedSequence seeding, comparison, and stream I/O; `lehmer64` also accepts a full 128-bit seed and is built on `int128.hpp`. |
| `include/fixed_string.hpp`             | C++20    | Compile-time fixed-capacity string (`basic_fixed_string`, `fixed_string`, `fixed_wstring`, ...) following P3094R5 with a trivial-type deviation so it stays usable in `constexpr` `inplace_vector`. Provides `std::formatter` / ostream support and user-defined literals. |
| `include/inplace_vector.hpp`           | C++20    | Fixed-capacity, dynamically-resizable contiguous container (`stdex::inplace_vector<T, N>`) following P0843R14: elements are embedded in the object, with the `vector`-like API (construction / `assign` / `append_range` / `insert` / `emplace` / `erase` / `resize` / `reserve` / `swap` / comparison), free `erase` / `erase_if`, and a zero-capacity specialization that is empty and trivially copyable for every `T`. `T` need not be default-constructible; exceeding `capacity()` throws `std::bad_alloc`. Storage is union-based: trivial values temporarily use a value-initialized `std::array` before C++26 so they remain usable in constant expressions (from C++26 the union plus `std::start_lifetime` covers all types), while non-trivial values are constructed in place with no allocation. |
| `include/inplace_array.hpp`            | C++20    | Fixed-size contiguous container (`stdex::inplace_array<T, N>`), the non-aggregate upgrade of `std::array`: exactly `N` elements are always alive (no partial construction, no shrinking), but every element's lifetime is started and ended by the container, so it works in constant expressions and with non-implicit-lifetime types. `std::array`-style interface (`[]` / `at` / `front` / `back` / `data` / iterators / `fill` / `swap` / comparison) plus `std::in_place` (fill `N` from args), `initializer_list` (exactly `N` values), `from_range` (exactly `N` elements) constructors, and an explicit `stdex::uninitialized` tag to skip value-initialization. Default construction value-initializes all `N` elements (`a` and `a{}` agree); storage is provided by `inplace_storage.hpp` with the fixed-size (`no_size_base`) policy. |
| `include/inlined_vector.hpp`           | C++20    | `std::vector`-like contiguous sequence (`stdex::inlined_vector<T, N, Allocator>`) with inline (`N > 0`) storage and heap fallback. Full `std::vector` API plus `inplace_vector` ergonomics (`push_back` returns a reference); `==` and synth-three-way `<=>` so element types only need `<` for ordering. Strong exception guarantee for `push_back`, `insert`, and growing `resize`; basic for `assign` and range inserts. Supports non-trivial and move-only elements; counted inserts / bulk fills use vectorizable-fill and bulk-relocation fast paths for trivial types. Built on `inline_storage.hpp`. |
| `include/inlined_bitset.hpp`           | C++20    | `std::bitset`-like bit container (`stdex::inlined_bitset<Block, N, Allocator>`) with optional inline (`N > 0`) block storage and heap fallback, dynamic growth (`push_back` / `insert` / `erase` / `resize` / `assign`), `from_blocks` / range constructors, bitwise operators, `find_first` / `find_next` / `find_last` and zero-variants, `to_string` / `to_ulong` / `to_ullong`, stream I/O, and SSE2 / AVX2 / NEON-accelerated count, scan, fill and pack paths. Built on `inline_storage.hpp`. |
| `include/stable_radix_sort.hpp`        | C++20    | Stable LSD/MSD radix sort for arithmetic types and projected key maps (including enums via an explicit projection): `stdex::stable_radix_sort(first, last, ...)`, `radix_sort_options` (strategy, execution policy, digit width, order, histogram storage, NaN handling), early exit when the input is already ordered by final radix key, contiguous / reverse-iterator / bool-proxy-range support, fancy-pointer allocator support, OpenMP, hybrid-alloc scratch storage. Includes 16-byte `stdex::uint128_t` / `stdex::int128_t` keys (forward-declared here, enabled by including `int128.hpp`). |
| `include/ptrhash.hpp`                  | C++20    | Perfect / minimal-perfect hash function `stdex::ptr_hash<Key, Hasher, Options>`, whose option set selects both the tuning profile and the storage mode: `ptr_hash_heap_option_*<Allocator>` builds a dynamically sized table on the heap, while `ptr_hash_stack_option_*<N>` stores everything inline in a fixed-capacity, allocation-free table that can be built during constant evaluation. The shared PtrHash-style pilot-and-eviction construction is configured by `ptr_hash_options<StoragePolicy>` (hash scheme, bucket distribution, build attempts, eviction and pilot-search limits, slot load factor, average bucket size) with ready-made `fast` / `default` / `balanced` / `compact` profiles; also provides range and initializer-list construction, `try_build` with failure-state reset, `index` / `index_unchecked` / `hash_value` / `index_from_hash` (including constrained heterogeneous key overloads), and bucket/hash-seed introspection. Builds on `hash.hpp`, `random.hpp`, `stable_radix_sort.hpp`, `inlined_bitset.hpp`, and `uninitialized_buffer.hpp`. |
| `include/minimal_perfect_hash_map.hpp` | C++20    | Immutable hash map (`stdex::minimal_perfect_hash_map<Key, T, Hasher, KeyEqual, Options>`) backed by a PtrHash minimal-perfect index. Like `ptr_hash`, the single `Options` value selects both the storage mode and the allocator: `ptr_hash_heap_options<Allocator>::profile_*()` stores the value vector on the heap, while `ptr_hash_stack_options<N>::profile_*()` embeds it in a fixed-capacity `stdex::inplace_vector`. The value vector is stored in index order and guarded by a parallel 8-bit fingerprint table, so `find` rejects most misses before touching the value vector and verifies hits with `key_equal`; provides `find` / `contains` / `count` / `equal_range` / `at` / `at_unchecked`, `max_size` / `bucket_count` / `max_bucket_count`, const iterators, CTAD, and no insertion, erase, or mutation operations. `lookup` is additionally available when `__cpp_lib_optional >= 202506L` (C++26 `std::optional<T&>`). |
| `include/minimal_perfect_hash_set.hpp` | C++20    | Immutable hash set (`stdex::minimal_perfect_hash_set<Key, Hasher, KeyEqual, Options>`) sharing the minimal-perfect table engine with the map; `Options` likewise carries the storage mode and allocator (`heap` vector or `stack` `inplace_vector`). Provides `find` / `contains` / `count` / `equal_range` / `at` / `at_unchecked` (including transparent heterogeneous overloads), `max_size` / `bucket_count` / `max_bucket_count`, CTAD, and const iterators over the index-ordered key vector, without insertion or erase. |
| `include/unordered_flat_map.hpp`       | C++20    | Open-addressing flat hash map (`stdex::unordered_flat_map<Key, T, N, Hasher, KeyEqual, Allocator>`) with optional inline (`N > 0`) stack storage, heterogeneous lookup, node/extraction APIs, and P3091 `lookup` when `__cpp_lib_optional >= 202506L` (C++26 `std::optional<T&>`). Thin facade over `unordered_flat_detail::hash_table`. |
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

### `include/minimal_perfect_hash_detail/` internals

| File | Purpose |
| ---- | ------- |
| `hash_table.hpp` | Immutable `hash_table<Traits, Hasher, KeyEqual, Allocator, Options>` engine: builds a `ptr_hash` index, permutes the value vector into minimal-perfect-index order (in-place cycle permutation where possible), maintains a parallel 8-bit fingerprint table, prefetches the candidate value line after a fingerprint hit, and implements equality-checked `find` / `contains` / `count` / `equal_range` / `at` / `at_unchecked` including transparent heterogeneous overloads, bucket/size observers, and const iteration. |
| `mphmap_traits.hpp` | Map traits: `value_type` is `std::pair<Key, Mapped>`, with `key_of` / `mapped_of` accessors. |
| `mphset_traits.hpp` | Set traits: key-is-value model with the same accessors. |

## Macro headers (`marco/`)

Standalone, header-only macro utilities. They are independent of the `stdex` namespace and the CMake targets; add `marco/` to the include path. `LinnSingleton.h` and `LinnProperty.h` include `LinnPimpl.h` / `LinnUtils.h` by quoted filename, so keep the four files together.

| File                    | Min. C++ | Purpose                                                                                                                              |
| ----------------------- | -------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| `marco/LinnUtils.h`     | C++11    | Preprocessor toolkit: compiler attributes, token pasting / stringification, boolean probes, tuple accessors, variadic arity counting. |
| `marco/LinnPimpl.h`     | C++17    | Qt-style d-pointer / q-pointer scaffolding; the d-pointer suppresses copying, and `LINN_DISABLE_COPY` / `LINN_DISABLE_MOVE` provide explicit copy/move control. |
| `marco/LinnSingleton.h` | C++17    | DLL-safe singleton factory with unique / shared / Meyers storage and optional one-way destruction.                                   |
| `marco/LinnProperty.h`  | C++17    | Property generator for inline, split (`_H` / `_CPP`), and Qt `Q_PROPERTY` members.                                                    |

### `LinnUtils.h` (C++11)

- Compiler helpers: `LINN_ALWAYS_INLINE`, `LINN_PRETTY_FUNCTION`, `LINN_RESTRICT`, `LINN_UNREACHABLE()`, `LINN_FLATTEN`, `LINN_LOOP_UNROLL(n)`.
- Token helpers: `LINN_EXPAND`, `LINN_STRINGIFY`, `LINN_CAT` through `LINN_CAT9`.
- Selection and probes: `LINN_IF`, `LINN_CHECK`, `LINN_PROBE`, `LINN_IS_PAREN`, `LINN_UNPAREN`, `LINN_OR`, `LINN_AND`.
- Tuple accessors: `LINN_PAIR`, `LINN_TRIPLE`, `LINN_QUAD`, `LINN_QUINT`, `LINN_SEXT`, `LINN_SEPT`, `LINN_OCT`, `LINN_NON`, with positional `*_FIRST`, `*_SECOND`, … accessors up to each tuple's arity (`LINN_NON` covers nine elements).
- Arity: `LINN_NARGS` (0–256), `LINN_HAS_COMMA`, `LINN_IS_EMPTY`. C++20 uses the `__VA_OPT__` fast path; earlier standards use a probe-based fallback. MSVC requires the conforming preprocessor (`/Zc:preprocessor`).

```cpp
static_assert(LINN_NARGS(a, b, c) == 3);
static_assert(LINN_IS_PAREN((x)) == 1);
// LINN_CAT(prefix_, Name) -> prefix_Name
```

### `LinnPimpl.h` (C++17)

- `LINN_DISABLE_COPY` / `LINN_DISABLE_MOVE` / `LINN_DISABLE_COPY_MOVE`.
- `LINN_Q_CREATE(CLASS)` adds `std::unique_ptr<CLASS##Private> d_ptr`, declares `explicit CLASS(CLASS##Private &)`, disables copying, and generates `d_func()`. `LINN_D_CREATE(CLASS)` adds the `CLASS *q_ptr` back-pointer and generates `q_func()`.
- `LINN_D(CLASS)` / `LINN_Q(CLASS)` bind local `CLASS##Private *const` pointers; `LINN_CONST_D` / `LINN_CONST_Q` bind `CLASS##Private const *const` pointers.
- Qt aliases `Q_Q_CREATE`, `Q_D_CREATE`, `Q_CONST_D`, `Q_CONST_Q` are available when Qt is detected.

```cpp
class WidgetPrivate;

class Widget {
  LINN_Q_CREATE(Widget)
public:
  explicit Widget();
};

class WidgetPrivate {
  LINN_D_CREATE(Widget)
public:
  int count = 0;
};

Widget::Widget() : d_ptr(std::make_unique<WidgetPrivate>())
{
  LINN_D(Widget);
  d->q_ptr = this;
}
```

### `LinnSingleton.h` (C++17)

`LINN_SINGLETON_CREATE(MODE_PAIR [, DESTROY_MODE])` injects the singleton API into a class body.

- Storage modes: `LSS_UNIQUE(CLASS)` (`std::unique_ptr`, `getInstance()` returns `CLASS *`), `LSS_SHARED(CLASS)` (`std::shared_ptr`, returns `std::shared_ptr<CLASS>`), `LSS_MEYERS(CLASS)` (function-local static, returns `CLASS &`; only with `LSS_NO_DESTROY`).
- The instance pointer and initialization flag are static members of the exported singleton class, so a DLL and its host EXE share one instance; a template-level static would be duplicated per module.
- Destroy modes: `LSS_NO_DESTROY` (default; no destroy API), `LSS_DESTROY_NOLOCK` (adds `destroyInstance()` / `isInstanceDestroyed()`; single-thread only), `LSS_DESTROY_LOCKED` (serialized get/destroy).
- One-time creation is thread-safe except in `LSS_DESTROY_NOLOCK`, which expects all calls on one thread. Destruction is one-way, and `getInstance()` returns null after `destroyInstance()`. `LSS_MEYERS` with a destroy mode degrades to `LSS_UNIQUE`.
- Define `LINN_SINGLETON_ENABLE_SHORT_MACROS` for `SINGLETON_CREATE`, `UNIQUE`, `SHARED`, `MEYERS`, `NO_DESTROY`, `DESTROY_NOLOCK`, `DESTROY_LOCKED`. Qt mode adds `Q_SINGLETON_CREATE` and `QSS_*`.

```cpp
class Foo {
  LINN_SINGLETON_CREATE(LSS_UNIQUE(Foo))
private:
  Foo() = default;
};

Foo *foo = Foo::getInstance();

class Bar {
  LINN_SINGLETON_CREATE(LSS_SHARED(Bar), LSS_DESTROY_LOCKED)
private:
  Bar() = default;
};

std::shared_ptr<Bar> bar = Bar::getInstance();
Bar::destroyInstance();
```

### `LinnProperty.h` (C++17)

Generates a private member plus setter/getter from strategy descriptors. Entry points:

| Macro | Use |
| --- | --- |
| `LINN_PROPERTY_CREATE(...)` | Inline member, setter, and getter. |
| `LINN_PROPERTY_CREATE_H(...)` | Setter/getter declarations in a header; the member comes from `_D`. |
| `LINN_PROPERTY_CREATE_CPP(CLASS, ...)` | Out-of-line setter/getter definitions; accesses the member through `d_func()`, so split mode requires PIMPL. |
| `LINN_PROPERTY_CREATE_D(TYPE, NAME[, DEFAULT])` | Private member definition for split mode, normally inside `CLASS##Private`; the only split form that accepts a default. |

Pre/post hooks belong in the `_CPP` descriptor; `_H` rejects them with a `static_assert`. A two-argument shortcut expands to the three-descriptor form (with a leading `CLASS` argument for `_CPP`):

```cpp
LINN_PROPERTY_CREATE_H(int, Count)
// equivalent to
LINN_PROPERTY_CREATE_H(LPS_MEMBER(int, Count), LPS_SETTER(int), LPS_GETTER(int))

LINN_PROPERTY_CREATE_H(LPS_SCREF(std::string), Name)
// equivalent to
LINN_PROPERTY_CREATE_H(LPS_MEMBER(std::string, Name),
                       LPS_SETTER(std::string const &),
                       LPS_GETTER(std::string))
```

`LPS_SCREF(TYPE)` expands to the pair `(TYPE, TYPE const &)`: member type and setter parameter type.

Strategies:

- `LPS_MEMBER(TYPE, NAME[, DEFAULT])`: member descriptor. `DEFAULT` is accepted by the inline forms (`LINN_PROPERTY_CREATE` / `Q_PROPERTY_CREATE`) only; `_H` / `_CPP` reject it with a `static_assert` and point to `_D`.
- `LPS_SETTER(PARAM_TYPE[, HOOK][, POST])`: 1 argument is the setter type; 2 arguments are the type plus a pre-contract or post action; 3 arguments are the type, pre-contract, and post action. A single unparenthesized post-action expression is wrapped automatically; use `LPS_POST_ACTION(...)` for multi-statement actions.
- `LPS_GETTER(TYPE)`: value getter; the wrapper is mandatory, a bare type is rejected.
- `LPS_GETTER_REF_QUALIFIED_OVERLOAD`: `TYPE const &` on lvalues and `TYPE` on rvalues.
- `LPS_PRE_CONTRACT(EXPR[, HANDLER])`: always-checked precondition; the default handler calls `linn_property_detail::contract_violation`.
- `LPS_PRE_CONTRACTD(EXPR[, HANDLER])`: checked only when `DEBUG` or `_DEBUG` is defined.
- `LPS_POST_ACTION(...)`: expression executed after assignment; comma-separated statements are allowed.

Accessor names use exactly one of `LINN_PROPERTY_ACCESSOR_PREFIX_CAMEL` (default), `_PASCAL`, or `_SNAKE`. Invalid configurations emit only a `static_assert`; no accessor or `_Pragma` expansion is produced.

Define `LINN_PROPERTY_ENABLE_SHORT_MACROS` for `MEMBER`, `SETTER`, `GETTER`, `GETTER_REF_QUALIFIED_OVERLOAD`, `PRE_CONTRACT`, `PRE_CONTRACTD`, `POST_ACTION`, `PROPERTY_CREATE`, `PROPERTY_CREATE_H`, `PROPERTY_CREATE_CPP`, and `PROPERTY_CREATE_D`.

Qt mode is enabled automatically when Qt is detected. It adds `Q_PROPERTY_CREATE`, `Q_PROPERTY_CREATE_H`, `Q_PROPERTY_CREATE_CPP`, and `Q_PROPERTY_CREATE_D`, which emit `Q_PROPERTY(... NOTIFY signalNameChanged)` plus the `signalNameChanged` signal declaration. The signal name follows the accessor prefix: `SignalNameChanged` with `LINN_PROPERTY_ACCESSOR_PREFIX_PASCAL`, or `signal_name_changed` with `_SNAKE`. `QPS_*` mirrors `LPS_*`, with `QPS_POST_ACTION_EMIT`, `QPS_POST_ACTION_UPDATE`, and `QPS_POST_ACTION_UPDATEMIT` as Qt-specific post actions. `QPS_SCREF(TYPE)` declares the NOTIFY signal without emitting it; `QPS_PAEMIT(TYPE)` is the by-value emitting counterpart. `QPS_SCREF_PAEMIT(TYPE)`, `QPS_SCREF_PAUPEMIT(TYPE)`, `QPS_SCREF_GOVLD_PAEMIT(TYPE)`, and `QPS_SCREF_GOVLD_PAUPEMIT(TYPE)` add emit or update+emit post actions; the `GOVLD` variants use a ref-qualified overload getter. In split mode pass the same shortcut to `_H` and `_CPP`: `_H` declares the accessor shape, while `_CPP` applies the post action.

Inline example:

```cpp
class Counter {
  LINN_PROPERTY_CREATE(LPS_MEMBER(int, Count, 0),
                       LPS_SETTER(int, LPS_PRE_CONTRACT(Count >= 0), LPS_POST_ACTION(this->notify())),
                       LPS_GETTER(int))
};
```

Split mode requires the owning class to be PIMPL-backed (`LINN_Q_CREATE` / `LINN_D_CREATE`): `_H` declares the accessors in the public class, `_D` defines the member in `CLASS##Private`, and `_CPP` reads and writes it through `d_func()`.

Split example:

```cpp
// header
class SamplePrivate;

class Sample {
  LINN_Q_CREATE(Sample)
public:
  Sample();
  ~Sample();

  LINN_PROPERTY_CREATE_H(LPS_MEMBER(int, Count), LPS_SETTER(int), LPS_GETTER(int))
};

class SamplePrivate {
  LINN_D_CREATE(Sample)
public:
  LINN_PROPERTY_CREATE_D(int, Count, 0)
};

// source
Sample::Sample() : d_ptr(std::make_unique<SamplePrivate>())
{
  LINN_D(Sample);
  d->q_ptr = this;
}

Sample::~Sample() = default;

LINN_PROPERTY_CREATE_CPP(Sample, LPS_MEMBER(int, Count),
                         LPS_SETTER(int, LPS_PRE_CONTRACT(Count >= 0)),
                         LPS_GETTER(int))
```

Qt example:

```cpp
class Counter : public QObject {
  Q_OBJECT

public:
  void update() noexcept {}

  Q_PROPERTY_CREATE(QPS_PAEMIT(QString), Title)
  Q_PROPERTY_CREATE(QPS_SCREF_PAEMIT(QString), Name)
  Q_PROPERTY_CREATE(QPS_SCREF_GOVLD_PAUPEMIT(QString), Label)
};
```

## Build, test, benchmark

```powershell
git submodule update --init --recursive
cmake --preset msvc_x64_debug  # see CMakeUserPresets.json / CMakePresets.json
cmake --build out/build/msvc_x64_debug
ctest --test-dir out/build/msvc_x64_debug --output-on-failure
```

Replace `msvc_x64_debug` with another configured preset as needed.

### vcpkg

Benchmarks need vcpkg packages for `benchmark`, `boost-unordered`, `absl`, and `frozen`. Point CMake at the vcpkg toolchain in `CMakePresets.json` or `CMakeUserPresets.json` (recommended):

```json
"cacheVariables": {
  "CMAKE_TOOLCHAIN_FILE": {
    "type": "FILEPATH",
    "value": "E:/Development/vcpkg/scripts/buildsystems/vcpkg.cmake"
  }
}
```

`vcpkg.cmake` derives `VCPKG_ROOT` from this path and forces the cache value; a separate `VCPKG_ROOT` setting is not needed. The equivalent alternative is to set only `"VCPKG_ROOT": { "type": "PATH", "value": "E:/Development/vcpkg" }`. Do not set `VCPKG_TARGET_TRIPLET` manually: the project module selects `x64-windows-static-md` from `MSVC_RUNTIME_MODE=MD` (and follows other runtime/architecture settings). If `VCPKG_ROOT` is defined as an environment variable, use `$penv{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake` as the toolchain value.

Install the benchmark dependencies:

```powershell
E:\Development\vcpkg\vcpkg.exe install boost-unordered absl benchmark frozen --triplet x64-windows-static-md
```

After configuring, verify that CMake reports the root and both triplet paths:

```text
-- Vcpkg root: E:/Development/vcpkg
-- Vcpkg static path: E:/Development/vcpkg/installed/x64-windows-static-md
-- Vcpkg dynamic path: E:/Development/vcpkg/installed/x64-windows
```

Benchmarks (`benchmarks/`): `bench_stable_radix_sort`, `bench_inlined_bitset`, `bench_int128`, `bench_hash`, `bench_minimal_perfect_hash`, and `bench_hash_table` (the latter links `Boost::unordered`, `absl::flat_hash_map`, `benchmark::benchmark` via vcpkg; `bench_minimal_perfect_hash` links the header-only `frozen` target; `bench_int128` also compares against a local MSVC-STL-style two-word baseline and adds native `__int128` / `absl::uint128` when available; `bench_hash` compares `stdex::hash` with `std::hash`). Benchmarks are skipped with a warning when vcpkg or Google Benchmark is unavailable; `bench_hash_table` alone is skipped when Boost/Abseil is missing, and `bench_minimal_perfect_hash` alone is skipped when `frozen` is missing.

## Design references

- `int128.hpp`: algorithm and platform-dispatch structure follow MSVC STL `__msvc_int128.hpp` (word order, `_umul128` / `_addcarry_u64` / `_udiv128`, normalized Knuth 4.3.1D division).
- `hybrid_alloc.hpp`: threshold, marker-ahead-of-pointer dispatch, and size computation mirror Windows Kits 10 UCRT `ucrt/malloc.h` `_malloca` / `_freea` (`_ALLOCA_S_THRESHOLD`, `_MarkAllocaS`, `_MallocaComputeSize`).
- `hash.hpp`: algorithm and stripe/lane structure derived from Nicolas De Carli's RapidHash — https://github.com/Nicoshev/rapidhash/blob/master/rapidhash.h
- `random.hpp`: `splitmix64` follows Sebastiano Vigna's reference implementation — https://prng.di.unimi.it/splitmix64.c; `wyrand` follows Wang Yi's wyhash — https://github.com/wangyi-fudan/wyhash; `lehmer64` follows Daniel Lemire's testingRNG reference — https://github.com/lemire/testingRNG/blob/master/source/lehmer64.h
- `ptrhash.hpp`: pilot/eviction construction and query layout adapted from Ragnar Groot Koerkamp's PtrHash — https://github.com/RagnarGrootKoerkamp/ptrhash
- `fastmod.hpp`: API and multiplier method ported from Daniel Lemire's fastmod — https://github.com/lemire/fastmod/blob/master/include/fastmod.h — on top of the local `int128.hpp` primitives.
- `unordered_flat_map.hpp` / `unordered_flat_set.hpp` (+ `unordered_flat_detail/`): table layout, group metadata, and growth policy adapted from Boost.Unordered FoA (`boost/unordered/detail/foa/core.hpp`, `table.hpp`) — https://github.com/boostorg/unordered/tree/develop/include/boost/unordered — with key-extraction semantics following libc++ `__utility/try_key_extraction.h` — https://github.com/llvm/llvm-project/blob/main/libcxx/include/__utility/try_key_extraction.h
- `unordered_flat_map.hpp` / `minimal_perfect_hash_map.hpp`: `lookup` follows P3091R6 and is enabled only when `__cpp_lib_optional >= 202506L` (P2988 `std::optional<T&>`).
- `fixed_string.hpp`: interface follows P3094R5 (`basic_fixed_string`); deviates by writing the null terminator explicitly instead of `charT data_[N+1] = {}` so the type stays trivial for `constexpr inplace_vector` use.
- `inlined_bitset.hpp`: public interface follows pinam45/dynamic_bitset (an implementation of the proposed `std::dynamic_bitset`) — https://github.com/pinam45/dynamic_bitset — only the API declarations were referenced; no implementation code was copied.
- `inlined_vector.hpp`: API surface follows `std::vector` / `std::inplace_vector` (P0843R14) conventions; it deviates by returning a reference from `push_back` (the inplace_vector form instead of `std::vector`'s `void`), exposing raw-pointer iterators even with fancy allocators, and using synth-three-way comparison so element types only need `<` for ordering.
