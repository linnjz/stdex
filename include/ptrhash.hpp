/*
 * Copyright (c) 2026 LinnJz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <algorithm>        // std::find, std::sort, std::pop_heap, std::push_heap
#include <array>            // std::array
#include <bit>              // std::has_single_bit
#include <cassert>          // assert
#include <concepts>         // std::convertible_to, std::same_as, std::regular_invocable, std::copy_constructible
#include <cstddef>          // std::size_t, std::ptrdiff_t
#include <cstdint>          // std::uint8_t, std::uint32_t, std::uint64_t
#include <cstdlib>          // std::abort
#include <initializer_list> // std::initializer_list
#include <iterator>         // std::forward_iterator, std::sentinel_for, std::iter_value_t
#include <limits>           // std::numeric_limits
#include <memory>           // std::allocator, std::allocator_traits, std::uninitialized_default_construct_n
#include <ranges>           // std::ranges::forward_range, std::ranges::range_value_t,
                            // std::ranges::begin, std::ranges::end, std::ranges::distance
#include <span>             // std::span
#include <stdexcept>        // std::length_error, std::runtime_error
#include <type_traits>      // std::remove_cvref_t, std::remove_cv_t, std::invoke_result_t,
                            // std::is_convertible_v, std::is_same_v, std::enable_if_t,
                            // std::is_trivially_default_constructible_v,
                            // std::is_trivially_destructible_v, std::is_nothrow_invocable_v
#include <utility>          // std::move, std::pair, std::declval
#include <vector>           // std::vector
#include <version>          // __cpp_lib_containers_ranges

#include "hash.hpp"
#include "inlined_bitset.hpp"
#include "inplace_vector.hpp"
#include "int128.hpp"
#include "random.hpp"
#include "stable_radix_sort.hpp"
#include "uninitialized_buffer.hpp"

namespace stdex
{
enum class ptr_hash_bucket_distribution : std::uint8_t
{
  linear,
  cubic_eps
};

enum class ptr_hash_scheme : std::uint8_t
{
  perfect,
  minimal_perfect
};

enum class ptr_hash_storage_policy : std::uint8_t
{
  heap,
  stack
};

namespace ptr_hash_detail
{
template <class hasher, class Key>
concept key_hasher = std::regular_invocable<hasher const &, Key const &> &&
                     std::convertible_to<std::invoke_result_t<hasher const &, Key const &>, std::uint64_t>;

template <class Alloc>
concept standard_allocator =
    requires { typename Alloc::value_type; } &&
    requires (Alloc a, typename std::allocator_traits<Alloc>::size_type n, typename std::allocator_traits<Alloc>::pointer p) {
      requires std::copy_constructible<Alloc>;
      requires std::default_initializable<typename std::allocator_traits<Alloc>::pointer>;
      {
        p == typename std::allocator_traits<Alloc>::pointer {}
      } -> std::convertible_to<bool>;
      {
        std::to_address(p)
      } -> std::same_as<typename std::allocator_traits<Alloc>::value_type *>;
      {
        a.allocate(n)
      } -> std::same_as<typename std::allocator_traits<Alloc>::pointer>;
      {
        a.deallocate(std::declval<typename std::allocator_traits<Alloc>::pointer>(), n)
      } -> std::same_as<void>;
    };

template <std::size_t BitCount>
class fixed_bitset
{
  static_assert(BitCount > 0, "fixed_bitset requires a non-zero bit count");

public:
  using block_type = std::size_t;
  using size_type  = std::size_t;

  class reference
  {
  public:
    constexpr reference(fixed_bitset &owner, size_type const bit) noexcept
        : owner_ { owner }
        , bit_ { bit }
    {
    }

    constexpr reference(reference const &) noexcept = default;

    constexpr reference &operator= (reference const &other) noexcept
    {
      return *this = static_cast<bool>(other);
    }

    constexpr reference &operator= (bool const value) noexcept
    {
      owner_.set_(bit_, value);
      return *this;
    }

    [[nodiscard]] constexpr operator bool() const noexcept { return owner_.test_(bit_); }

  private:
    fixed_bitset &owner_;
    size_type bit_;
  };

  constexpr fixed_bitset() = default;

  [[nodiscard]] constexpr bool operator[] (size_type const bit) const noexcept { return test_(bit); }

  [[nodiscard]] constexpr reference operator[] (size_type const bit) noexcept
  {
    assert(bit < BitCount);
    return reference { *this, bit };
  }

private:
  static constexpr size_type bits_per_block = std::numeric_limits<block_type>::digits;
  static constexpr size_type block_count    = (BitCount + bits_per_block - 1U) / bits_per_block;

  [[nodiscard]] constexpr bool test_(size_type const pos) const noexcept
  {
    assert(pos < BitCount);
    size_type const block_index = pos / bits_per_block;
    size_type const bit_index   = pos % bits_per_block;
    return (blocks_[block_index] >> bit_index) & block_type { 1 };
  }

  constexpr void set_(size_type const pos, bool const value) noexcept
  {
    assert(pos < BitCount);
    block_type &block     = blocks_[pos / bits_per_block];
    block_type const mask = block_type { 1 } << (pos % bits_per_block);
    if (value) {
      block |= mask;
    } else {
      block &= static_cast<block_type>(~mask);
    }
  }

  std::array<block_type, block_count> blocks_ {};
};

template <standard_allocator Allocator>
struct heap_storage
{
  using allocator_type = Allocator;
  template <typename T>
  using rebind_alloc_t = typename std::allocator_traits<allocator_type>::template rebind_alloc<T>;

  using pilot_value_type   = std::uint8_t;
  using remap_value_type   = std::uint32_t;
  using slot_index_type    = std::uint32_t;
  using bucket_index_type  = std::uint32_t;
  using bucket_length_type = std::uint32_t; // A bucket may hold up to key_count keys, so it cannot be uint8_t.
  using offset_type        = std::uint32_t;

  using pilot_table_type          = std::vector<pilot_value_type, rebind_alloc_t<pilot_value_type>>;
  using remap_table_type          = std::vector<remap_value_type, rebind_alloc_t<remap_value_type>>;

  // eviction heap: (bucket_length, bucket_index_type)
  using eviction_heap_type        = std::vector<
      std::pair<bucket_length_type, bucket_index_type>,
      rebind_alloc_t<std::pair<bucket_length_type, bucket_index_type>>>;

  using bucket_length_prefix_type = std::vector<offset_type, rebind_alloc_t<offset_type>>;
  using bucket_offsets_type       = std::vector<offset_type, rebind_alloc_t<offset_type>>;
  using bucket_order_type         = uninitialized_buffer<bucket_index_type, rebind_alloc_t<bucket_index_type>>;
  using slot_owner_buffer_type    = uninitialized_buffer<bucket_index_type, rebind_alloc_t<bucket_index_type>>;
  using slot_scratch_type         = uninitialized_buffer<slot_index_type, rebind_alloc_t<slot_index_type>>;
  using hash_buffer_type          = uninitialized_buffer<std::uint64_t, rebind_alloc_t<std::uint64_t>>;
  using occupied_slots_type       = inlined_bitset<std::uint64_t, 0, rebind_alloc_t<std::uint64_t>>;
};

template <std::uint32_t max_slot_count, std::uint32_t max_bucket_count>
struct stack_storage
{
  using pilot_value_type   = std::uint8_t;
  using remap_value_type   = std::uint32_t;
  using slot_index_type    = std::uint32_t;
  using bucket_index_type  = std::uint32_t;
  using bucket_length_type = std::uint32_t;
  using offset_type        = std::uint32_t;

  using pilot_table_type          = std::array<pilot_value_type, max_bucket_count>;
  using remap_table_type          = std::array<remap_value_type, max_slot_count>;

  using eviction_heap_type        = std::vector<std::pair<bucket_length_type, bucket_index_type>>;
  using bucket_length_prefix_type = std::vector<offset_type>;
  using bucket_offsets_type       = inplace_vector<offset_type, max_bucket_count + 1U>;
  using bucket_order_type         = inplace_vector<bucket_index_type, max_bucket_count>;
  using slot_owner_buffer_type    = uninitialized_buffer<bucket_index_type>;
  using slot_scratch_type         = uninitialized_buffer<slot_index_type>;
  using hash_buffer_type          = uninitialized_buffer<std::uint64_t>;
  using occupied_slots_type       = fixed_bitset<max_slot_count>;
};

template <ptr_hash_storage_policy Policy, class Allocator, std::uint32_t MaxSlotCount, std::uint32_t MaxBucketCount>
struct ptr_hash_storage_selector
{
  using type = stack_storage<MaxSlotCount, MaxBucketCount>;
};

template <class Allocator, std::uint32_t MaxSlotCount, std::uint32_t MaxBucketCount>
struct ptr_hash_storage_selector<ptr_hash_storage_policy::heap, Allocator, MaxSlotCount, MaxBucketCount>
{
  using type = heap_storage<Allocator>;
};

template <ptr_hash_storage_policy Policy, class... Args>
struct ptr_hash_storage_traits;

template <class Allocator>
struct ptr_hash_storage_traits<ptr_hash_storage_policy::heap, Allocator>
{
  using allocator_type = Allocator;
  static constexpr auto storage_policy { ptr_hash_storage_policy::heap };
  static constexpr std::uint32_t max_key_count { (std::numeric_limits<std::uint32_t>::max)() };
};

template <std::uint32_t N>
struct ptr_hash_storage_traits<ptr_hash_storage_policy::stack, std::integral_constant<std::uint32_t, N>>
{
  using allocator_type = std::allocator<char>;
  static constexpr auto storage_policy { ptr_hash_storage_policy::stack };
  static constexpr std::uint32_t max_key_count { N };
};

} // namespace ptr_hash_detail

template <ptr_hash_storage_policy Policy, class ... Args>
struct ptr_hash_options
{
  ptr_hash_scheme hash_scheme { ptr_hash_scheme::minimal_perfect };
  ptr_hash_bucket_distribution bucket_distribution { ptr_hash_bucket_distribution::linear };

  /// Number of times the hash seed is regenerated before the build fails.
  std::uint16_t max_build_attempts { 10 };
  /// Abort a build attempt once a single bucket evicts this many slots.
  std::uint16_t max_evictions_per_slot { 10 };
  /// Number of pilots tried before a bucket falls back to eviction.
  std::uint16_t pilot_search_limit { 256 };

  /// Target ratio of keys to slots. Values close to 1.0 reduce memory but
  /// make collision-free placement harder; must be in (0, 1).
  double slot_load_factor { 0.99 };
  /// Target number of keys per bucket. Larger values reduce the pilot table
  /// size but increase the probability of placement failures per bucket.
  double average_bucket_size { 3.0 };

  /// Parameters for the fast variant (perfect hash fn, linear, average_bucket_size = 3.0).
  [[nodiscard]] static constexpr ptr_hash_options profile_fast() noexcept
  {
    return { .hash_scheme = ptr_hash_scheme::perfect, };
  }

  [[nodiscard]] static constexpr ptr_hash_options profile_default() noexcept { return {}; }

  /// Parameters for the balanced variant (minimal perfect hash fn, cubic_eps, average_bucket_size = 3.5).
  [[nodiscard]] static constexpr ptr_hash_options profile_balanced() noexcept
  {
    return {
      .bucket_distribution = ptr_hash_bucket_distribution::cubic_eps,
      .average_bucket_size = 3.5
    };
  }

  /// Parameters for the compact variant (minimal perfect hash fn, cubic_eps, average_bucket_size = 3.9).
  [[nodiscard]] static constexpr ptr_hash_options profile_compact() noexcept
  {
    return {
      .bucket_distribution = ptr_hash_bucket_distribution::cubic_eps,
      .average_bucket_size = 3.9
    };
  }

  using storage_traits = ptr_hash_detail::ptr_hash_storage_traits<Policy, Args...>;
};

template <ptr_hash_detail::standard_allocator Allocator>
using ptr_hash_heap_options = ptr_hash_options<ptr_hash_storage_policy::heap, Allocator>;

template <std::size_t N> requires (N > 0)
using ptr_hash_stack_options = ptr_hash_options<ptr_hash_storage_policy::stack, std::integral_constant<std::uint32_t, static_cast<std::uint32_t>(N)>>;

namespace ptr_hash_detail
{
template <class T>
inline constexpr bool is_ptr_hash_options_v = false;

template <class Allocator>
inline constexpr bool is_ptr_hash_options_v<ptr_hash_options<ptr_hash_storage_policy::heap, Allocator>> = true;

template <std::uint32_t N>
inline constexpr bool is_ptr_hash_options_v<ptr_hash_options<ptr_hash_storage_policy::stack, std::integral_constant<std::uint32_t, N>>> = true;
} // namespace ptr_hash_detail

template <class Key, class Hasher = hash<Key>, auto Options = ptr_hash_heap_options<std::allocator<Key>>::profile_default()>
requires ptr_hash_detail::key_hasher<Hasher, Key> &&
         ptr_hash_detail::is_ptr_hash_options_v<std::remove_cvref_t<decltype(Options)>>
class ptr_hash
{
  static_assert(
      Options.max_build_attempts > std::uint16_t { 0 } &&
      (Options.slot_load_factor > 0.0 && Options.slot_load_factor < 1.0) &&
      Options.average_bucket_size >= 1.0);

  static_assert(std::has_single_bit(Options.pilot_search_limit), "pilot_search_limit must be a power of two");
  static_assert(Options.pilot_search_limit <= 256, "pilot_search_limit must not exceed 256: pilots_ stores std::uint8_t");
  
public:
  using key_type        = Key;
  using hasher          = Hasher;
  using value_type      = key_type;
  using size_type       = std::size_t;
  using difference_type = std::ptrdiff_t;
  using options_type    = std::remove_cvref_t<decltype(Options)>;
  using allocator_type  = typename options_type::storage_traits::allocator_type;

  static constexpr options_type options_value { Options };
  static constexpr ptr_hash_storage_policy storage_policy { options_type::storage_traits::storage_policy };
  static constexpr std::uint32_t max_key_count { options_type::storage_traits::max_key_count };

private:
  [[nodiscard]] static consteval std::uint32_t calculate_max_slot_count_() noexcept
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::stack) {
      double const estimate = static_cast<double>(max_key_count) / options_value.slot_load_factor;
      // Very small load factors would require an impossible compile-time array;
      // clamp the layout and let the runtime slot check reject the build.
      if (!(estimate <= static_cast<double>(max_key_count) * 4.0)) return max_key_count * 4U + 1U;
      return static_cast<std::uint32_t>(estimate) + 1U;
    } else {
      return (std::numeric_limits<std::uint32_t>::max)();
    }
  }

  [[nodiscard]] static consteval std::uint32_t calculate_max_bucket_count_() noexcept
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::stack) {
      double const quotient       = static_cast<double>(max_key_count) / options_value.average_bucket_size;
      std::size_t const truncated = static_cast<std::size_t>(quotient);
      return static_cast<std::uint32_t>((static_cast<double>(truncated) < quotient ? truncated + 1U : truncated)) + 3U;
    } else {
      return (std::numeric_limits<std::uint32_t>::max)();
    }
  }

public:
  static constexpr std::uint32_t max_slot_count   = calculate_max_slot_count_();
  static constexpr std::uint32_t max_bucket_count = calculate_max_bucket_count_();

  using storage_type = typename ptr_hash_detail::ptr_hash_storage_selector<
      storage_policy,
      allocator_type,
      max_slot_count,
      max_bucket_count>::type;

  static constexpr size_type npos { static_cast<size_type>(-1) };

private:
  template <typename T>
  using rebind_alloc_t = typename std::allocator_traits<allocator_type>::template rebind_alloc<T>;

  using pilot_value_type   = typename storage_type::pilot_value_type;
  using remap_value_type   = typename storage_type::remap_value_type;
  using slot_index_type    = typename storage_type::slot_index_type;
  using bucket_index_type  = typename storage_type::bucket_index_type;
  using bucket_length_type = typename storage_type::bucket_length_type;
  using offset_type        = typename storage_type::offset_type;

  using pilot_table_type          = typename storage_type::pilot_table_type;
  using remap_table_type          = typename storage_type::remap_table_type;
  using eviction_heap_type        = typename storage_type::eviction_heap_type;
  using bucket_length_prefix_type = typename storage_type::bucket_length_prefix_type;
  using bucket_offsets_type       = typename storage_type::bucket_offsets_type;
  using bucket_order_type         = typename storage_type::bucket_order_type;
  using slot_owner_buffer_type    = typename storage_type::slot_owner_buffer_type;
  using slot_scratch_type         = typename storage_type::slot_scratch_type;
  using hash_buffer_type          = typename storage_type::hash_buffer_type;
  using occupied_slots_type       = typename storage_type::occupied_slots_type;

  static constexpr remap_value_type vacant_slot     = (std::numeric_limits<remap_value_type>::max)();
  static constexpr remap_value_type no_bucket       = vacant_slot;
  static constexpr size_type recent_bucket_capacity = 16;

  /// Transient state shared by the bucket-placement routines below.
  ///
  /// `slot_owners` is only meaningful for occupied slots; free slots are left
  /// untouched so the table does not need a full-table initialization.
  struct bucket_placement_context
  {
    std::span<offset_type const> bucket_offsets;
    std::span<std::uint64_t const> hashes;
    std::span<bucket_index_type> slot_owners;
    occupied_slots_type &occupied_slots;
    eviction_heap_type &eviction_heap;
    std::array<bucket_index_type, recent_bucket_capacity> &recent_buckets;
    splitmix64 &random_engine;
    std::uint32_t &eviction_count;
    std::span<slot_index_type> slot_scratch;
    std::uint32_t recent_index { 0 };

    [[nodiscard]] constexpr std::span<std::uint64_t const> bucket_hashes(bucket_index_type const bucket) const noexcept
    {
      offset_type const begin = bucket_offsets[bucket];
      offset_type const end   = bucket_offsets[bucket + 1U];
      return { hashes.data() + begin, static_cast<std::size_t>(end - begin) };
    }

    [[nodiscard]] constexpr bucket_length_type bucket_length(bucket_index_type const bucket) const noexcept
    {
      return static_cast<bucket_length_type>(bucket_offsets[bucket + 1U] - bucket_offsets[bucket]);
    }
  };

  struct build_tag {};

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  constexpr ptr_hash(build_tag, Iterator first, Sentinel last, hasher hasher_arg)
      : hasher_ { std::move(hasher_arg) }
  {
    if (!try_build_impl_(first, last)) [[unlikely]] {
      throw_runtime_error_("construction failure");
    }
  }

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires (storage_policy == ptr_hash_storage_policy::heap)
  constexpr ptr_hash(build_tag, Iterator first, Sentinel last, hasher hasher_arg, allocator_type const &allocator)
      : pilots_ { rebind_alloc_t<pilot_value_type> { allocator } }
      , remap_ { rebind_alloc_t<remap_value_type> { allocator } }
      , hasher_ { std::move(hasher_arg) }
      , allocator_ { allocator }
  {
    if (!try_build_impl_(first, last)) [[unlikely]] {
      throw_runtime_error_("construction failure");
    }
  }

public:  
  constexpr ptr_hash() noexcept                         = default;
  constexpr ~ptr_hash() noexcept                        = default;
  constexpr ptr_hash(ptr_hash const &other)             = default;
  constexpr ptr_hash(ptr_hash &&other)                  = default;
  constexpr ptr_hash &operator= (ptr_hash const &other) = default;
  constexpr ptr_hash &operator= (ptr_hash &&other)      = default;

  constexpr explicit ptr_hash(allocator_type const &allocator)
  requires (storage_policy == ptr_hash_storage_policy::heap)
      : pilots_ { rebind_alloc_t<pilot_value_type> { allocator } }
      , remap_ { rebind_alloc_t<remap_value_type> { allocator } }
      , allocator_ { allocator }
  {
  }

  constexpr ptr_hash(ptr_hash const &other, allocator_type const &allocator)
  requires (storage_policy == ptr_hash_storage_policy::heap)
      : pilots_ { other.pilots_, rebind_alloc_t<pilot_value_type> { allocator } }
      , remap_ { other.remap_, rebind_alloc_t<remap_value_type> { allocator } }
      , key_count_ { other.key_count_ }
      , slot_count_ { other.slot_count_ }
      , bucket_count_ { other.bucket_count_ }
      , slot_reciprocal_ { other.slot_reciprocal_ }
      , hash_seed_ { other.hash_seed_ }
      , hasher_ { other.hasher_ }
      , allocator_ { allocator }
  {
  }

  constexpr ptr_hash(ptr_hash &&other, allocator_type const &allocator)
  requires (storage_policy == ptr_hash_storage_policy::heap)
      : pilots_ { std::move(other.pilots_), rebind_alloc_t<pilot_value_type> { allocator } }
      , remap_ { std::move(other.remap_), rebind_alloc_t<remap_value_type> { allocator } }
      , key_count_ { std::exchange(other.key_count_, std::uint32_t { 0 }) }
      , slot_count_ { std::exchange(other.slot_count_, std::uint32_t { 0 }) }
      , bucket_count_ { std::exchange(other.bucket_count_, std::uint32_t { 0 }) }
      , slot_reciprocal_ { std::exchange(other.slot_reciprocal_, std::uint64_t { 0 }) }
      , hash_seed_ { std::exchange(other.hash_seed_, std::uint64_t { 0 }) }
      , hasher_ { std::move(other.hasher_) }
      , allocator_ { allocator }
  {
  }

  // ---------- iterator-range constructors ----------
  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  constexpr ptr_hash(Iterator first, Sentinel last)
      : ptr_hash { build_tag {}, std::move(first), std::move(last), hasher {} }
  {
  }

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  constexpr ptr_hash(Iterator first, Sentinel last, hasher hasher_arg)
      : ptr_hash { build_tag {}, std::move(first), std::move(last), std::move(hasher_arg) }
  {
  }

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel, class Alloc = allocator_type>
  requires (storage_policy == ptr_hash_storage_policy::heap)
  constexpr ptr_hash(Iterator first, Sentinel last, hasher hasher_arg, Alloc const &allocator)
      : ptr_hash { build_tag {}, std::move(first), std::move(last), std::move(hasher_arg), static_cast<allocator_type>(allocator) }
  {
  }

  // ---------- initializer_list constructors ----------
  constexpr ptr_hash(std::initializer_list<key_type> keys)
      : ptr_hash { keys.begin(), keys.end() }
  {
  }

  constexpr ptr_hash(std::initializer_list<key_type> keys, hasher hasher_arg)
      : ptr_hash { keys.begin(), keys.end(), std::move(hasher_arg) }
  {
  }

  template <class Alloc = allocator_type>
  requires (storage_policy == ptr_hash_storage_policy::heap)
  constexpr ptr_hash(std::initializer_list<key_type> keys, hasher hasher_arg, Alloc const &allocator)
      : ptr_hash { keys.begin(), keys.end(), std::move(hasher_arg), allocator }
  {
  }

#ifdef __cpp_lib_containers_ranges
  // ---------- from_range constructors ----------
  template <std::ranges::forward_range Range>
  constexpr explicit ptr_hash(std::from_range_t, Range &&keys)
      : ptr_hash { std::ranges::begin(keys), std::ranges::end(keys) }
  {
  }

  template <std::ranges::forward_range Range>
  constexpr explicit ptr_hash(std::from_range_t, Range &&keys, hasher hasher_arg)
      : ptr_hash { std::ranges::begin(keys), std::ranges::end(keys), std::move(hasher_arg) }
  {
  }

  template <std::ranges::forward_range Range, class Alloc = allocator_type>
  requires (storage_policy == ptr_hash_storage_policy::heap)
  constexpr explicit ptr_hash(std::from_range_t, Range &&keys, hasher hasher_arg, Alloc const &allocator)
      : ptr_hash { std::ranges::begin(keys), std::ranges::end(keys), std::move(hasher_arg), static_cast<allocator_type>(allocator) }
  {
  }
#endif

private:
  constexpr void reset_build_state_() noexcept
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      pilots_.clear();
      remap_.clear();
    }
    key_count_       = {};
    slot_count_      = {};
    bucket_count_    = {};
    slot_reciprocal_ = {};
    hash_seed_       = {};
  }

  constexpr void prepare_pilots_(std::uint32_t const bucket_count)
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      pilots_.assign(bucket_count, pilot_value_type { 0 });
    } else {
      std::fill_n(pilots_.data(), bucket_count, pilot_value_type { 0 });
    }
  }

  constexpr void clear_remap_()
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      remap_.clear();
      remap_.shrink_to_fit();
    }
  }

  constexpr void prepare_remap_(std::uint32_t const count, remap_value_type const fallback)
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      remap_.assign(count, fallback);
    } else {
      std::fill_n(remap_.data(), count, fallback);
    }
  }

  constexpr void shrink_remap_()
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) remap_.shrink_to_fit();
  }

  [[nodiscard]] constexpr bucket_offsets_type make_bucket_offsets_(std::uint32_t const count) const
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      return bucket_offsets_type(count, offset_type { 0 }, rebind_alloc_t<offset_type> { allocator_ });
    } else {
      return bucket_offsets_type(count, offset_type { 0 });
    }
  }

  [[nodiscard]] constexpr bucket_length_prefix_type make_bucket_length_prefix_(std::uint32_t const count) const
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      return bucket_length_prefix_type(count, offset_type { 0 }, rebind_alloc_t<offset_type> { allocator_ });
    } else {
      return bucket_length_prefix_type(count, offset_type { 0 });
    }
  }

  [[nodiscard]] constexpr bucket_order_type make_bucket_order_(std::uint32_t const count) const
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      return bucket_order_type(count, rebind_alloc_t<bucket_index_type> { allocator_ });
    } else {
      return bucket_order_type(count);
    }
  }

  [[nodiscard]] constexpr slot_owner_buffer_type make_slot_owners_(std::uint32_t const count) const
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      return slot_owner_buffer_type(count, rebind_alloc_t<bucket_index_type> { allocator_ });
    } else {
      return slot_owner_buffer_type(count);
    }
  }

  [[nodiscard]] constexpr hash_buffer_type make_hash_buffer_(std::uint32_t const count) const
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      return hash_buffer_type(count, rebind_alloc_t<std::uint64_t> { allocator_ });
    } else {
      return hash_buffer_type(count);
    }
  }

  [[nodiscard]] constexpr slot_scratch_type make_slot_scratch_(std::uint32_t const count) const
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      return slot_scratch_type(count, rebind_alloc_t<slot_index_type> { allocator_ });
    } else {
      return slot_scratch_type(count);
    }
  }

  [[nodiscard]] constexpr occupied_slots_type make_occupied_slots_(std::uint32_t const bit_count) const
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      return occupied_slots_type(bit_count, rebind_alloc_t<std::uint64_t> { allocator_ });
    } else {
      (void) bit_count;
      return occupied_slots_type {};
    }
  }

  [[nodiscard]] constexpr eviction_heap_type make_eviction_heap_() const
  {
    if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
      return eviction_heap_type(rebind_alloc_t<std::pair<bucket_length_type, bucket_index_type>> { allocator_ });
    } else {
      return eviction_heap_type {};
    }
  }

  [[nodiscard]] static constexpr bool has_duplicate_value_(std::span<std::uint64_t const> const hashes) noexcept
  {
    std::uint32_t const count = static_cast<std::uint32_t>(hashes.size());
    for (std::uint32_t i { 1 }; i < count; ++i) {
      if (hashes[i - 1] == hashes[i]) [[unlikely]]
        return true;
    }
    return false;
  }

  constexpr void build_remap_(occupied_slots_type const &occupied_slots)
  {
    if constexpr (options_value.hash_scheme == ptr_hash_scheme::perfect) {
      clear_remap_();
    } else {
      std::uint32_t const remapped_slot_count = slot_count_ - key_count_;
      if (remapped_slot_count == 0) [[unlikely]] {
        clear_remap_();
        return;
      }

      // Entries for free high slots are never observed by a query, but keeping
      // them in range preserves the `index() < size()` guarantee for every input.
      remap_value_type const fallback = key_count_ == 0 ? 0 : static_cast<remap_value_type>(key_count_ - 1);
      prepare_remap_(remapped_slot_count, fallback);

      // Free low slots and occupied high slots are equinumerous, so the two
      // cursors meet exactly once per occupied high slot without extra storage.
      slot_index_type next_high_slot = key_count_;
      for (slot_index_type low_slot = 0; low_slot < key_count_; ++low_slot) {
        if (occupied_slots[low_slot]) [[likely]] continue;
        assert(next_high_slot < slot_count_ && "free/occupied slot counts diverged");
        while (next_high_slot < slot_count_ && !occupied_slots[next_high_slot]) {
          ++next_high_slot;
        }
        assert(next_high_slot < slot_count_ && "free/occupied slot counts diverged");
        if (next_high_slot >= slot_count_) [[unlikely]] break;
        remap_[next_high_slot - key_count_] = static_cast<remap_value_type>(low_slot);
        ++next_high_slot;
      }
      shrink_remap_();
    }
  }

  /// Places a bucket at the first pilot whose slots are all free and distinct.
  template <std::uint32_t BucketSize>
  [[nodiscard]] constexpr bool try_place_fixed_size_bucket_(
      bucket_index_type const bucket, std::uint64_t const *const bucket_hashes, bucket_placement_context &context)
  {
    std::array<slot_index_type, BucketSize> slots;
    for (decltype(options_value.pilot_search_limit) pilot = 0; pilot < options_value.pilot_search_limit; ++pilot) {
      std::uint64_t const pilot_hash = pilot_hash_value_(static_cast<pilot_value_type>(pilot));

      bool is_fits = true;
      for (std::uint32_t i = 0; i < BucketSize; ++i) {
        slots[i] = slot_index_(bucket_hashes[i], pilot_hash);
        is_fits  = is_fits && !context.occupied_slots[slots[i]];
      }
      if (!is_fits) [[likely]] continue;

      if constexpr (BucketSize > 1) {
        bool distinct = true;
        for (std::uint32_t i = 1; i < BucketSize; ++i) {
          for (std::uint32_t j = 0; j < i; ++j) {
            distinct = distinct && slots[i] != slots[j];
          }
        }
        if (!distinct) [[unlikely]] continue;
      }

      pilots_[bucket] = static_cast<pilot_value_type>(pilot);
      for (std::uint32_t i = 0; i < BucketSize; ++i) {
        context.occupied_slots[slots[i]] = true;
        context.slot_owners[slots[i]]    = bucket;
      }
      return true;
    }
    return false;
  }

  [[nodiscard]] constexpr bool try_place_bucket_(bucket_index_type const bucket, bucket_placement_context &context)
  {
    std::span<std::uint64_t const> const bucket_hashes = context.bucket_hashes(bucket);
    switch (bucket_hashes.size()) {
    case 0  : return true;
    case 1  : return try_place_fixed_size_bucket_<1>(bucket, bucket_hashes.data(), context);
    case 2  : return try_place_fixed_size_bucket_<2>(bucket, bucket_hashes.data(), context);
    case 3  : return try_place_fixed_size_bucket_<3>(bucket, bucket_hashes.data(), context);
    case 4  : return try_place_fixed_size_bucket_<4>(bucket, bucket_hashes.data(), context);
    case 5  : return try_place_fixed_size_bucket_<5>(bucket, bucket_hashes.data(), context);
    case 6  : return try_place_fixed_size_bucket_<6>(bucket, bucket_hashes.data(), context);
    case 7  : return try_place_fixed_size_bucket_<7>(bucket, bucket_hashes.data(), context);
    case 8  : return try_place_fixed_size_bucket_<8>(bucket, bucket_hashes.data(), context);
    default : break;
    }

    std::uint32_t const bucket_size = static_cast<std::uint32_t>(bucket_hashes.size());
    assert(context.slot_scratch.size() >= bucket_size);
    for (decltype(options_value.pilot_search_limit) pilot = 0; pilot < options_value.pilot_search_limit; ++pilot) {
      std::uint64_t const pilot_hash = pilot_hash_value_(static_cast<pilot_value_type>(pilot));

      bool is_fits = true;
      for (std::uint32_t i = 0; i < bucket_size; ++i) {
        context.slot_scratch[i] = slot_index_(bucket_hashes[i], pilot_hash);
        if (context.occupied_slots[context.slot_scratch[i]]) [[unlikely]] {
          is_fits = false;
          break;
        }
      }
      if (!is_fits) [[likely]] continue;

      // Marking doubles as the duplicate check for the rare large bucket.
      std::uint32_t marked = 0;
      bool distinct        = true;
      for (; marked < bucket_size; ++marked) {
        slot_index_type const slot = context.slot_scratch[marked];
        if (context.occupied_slots[slot]) [[unlikely]] {
          distinct = false;
          break;
        }
        context.occupied_slots[slot] = true;
      }
      if (distinct) [[likely]] {
        pilots_[bucket] = static_cast<pilot_value_type>(pilot);
        for (std::uint32_t i = 0; i < bucket_size; ++i) {
          context.slot_owners[context.slot_scratch[i]] = bucket;
        }
        return true;
      }
      for (std::uint32_t i = 0; i < marked; ++i) {
        context.occupied_slots[context.slot_scratch[i]] = false;
      }
    }
    return false;
  }

  /// Places a bucket by evicting already placed buckets with the smallest
  /// squared collision score. Evicted buckets are retried through the heap.
  [[nodiscard]] constexpr bool try_place_bucket_with_evict_(bucket_index_type const bucket, bucket_placement_context &context)
  {
    std::span<std::uint64_t const> const bucket_hashes { context.bucket_hashes(bucket) };
    std::uint32_t const bucket_size { static_cast<std::uint32_t>(bucket_hashes.size()) };
    assert(context.slot_scratch.size() >= bucket_size);

    // A random start breaks the cycle that two buckets repeatedly evicting
    // each other would otherwise create.
    std::uint32_t const first_pilot { static_cast<std::uint32_t>(context.random_engine() & (options_value.pilot_search_limit - 1u)) };
    std::uint64_t best_collision_score { (std::numeric_limits<std::uint64_t>::max)() };
    std::uint32_t best_pilot { 0 };

    for (decltype(options_value.pilot_search_limit) delta = 0; delta < options_value.pilot_search_limit; ++delta) {
      std::uint32_t const pilot      = (first_pilot + delta) & (options_value.pilot_search_limit - 1u);
      std::uint64_t const pilot_hash = pilot_hash_value_(static_cast<pilot_value_type>(pilot));

      std::uint64_t collision_score { 0 };
      bool rejected { false };
      for (std::uint32_t i = 0; i < bucket_size; ++i) {
        slot_index_type const slot = slot_index_(bucket_hashes[i], pilot_hash);
        context.slot_scratch[i]    = slot;

        if (!context.occupied_slots[slot]) [[likely]] continue;
        bucket_index_type const owner = context.slot_owners[slot];
        if (std::find(context.recent_buckets.begin(), context.recent_buckets.end(), owner) != context.recent_buckets.end()) [[unlikely]] {
          rejected = true;
          break;
        }
        std::uint64_t const owner_length  = static_cast<std::uint64_t>(context.bucket_length(owner));
        std::uint64_t const owner_squared = owner_length * owner_length;
        if (collision_score > (std::numeric_limits<std::uint64_t>::max)() - owner_squared) [[unlikely]] {
          collision_score = (std::numeric_limits<std::uint64_t>::max)();
          rejected        = true;
          break;
        }
        collision_score += owner_squared;
        if (collision_score >= best_collision_score) [[likely]] {
          rejected = true;
          break;
        }
      }
      if (rejected) [[likely]] continue;

      std::sort(context.slot_scratch.begin(), context.slot_scratch.begin() + static_cast<std::ptrdiff_t>(bucket_size));
      bool distinct = true;
      for (std::uint32_t i = 1; i < bucket_size; ++i) {
        distinct = distinct && context.slot_scratch[i - 1] != context.slot_scratch[i];
      }
      if (!distinct) [[unlikely]] continue;

      best_collision_score = collision_score;
      best_pilot           = pilot;
      if (collision_score == static_cast<std::uint64_t>(bucket_size) * bucket_size) [[unlikely]] break;
    }

    if (best_collision_score == (std::numeric_limits<std::uint64_t>::max)()) [[unlikely]] return false;

    pilots_[bucket]                = static_cast<pilot_value_type>(best_pilot);
    std::uint64_t const pilot_hash = pilot_hash_value_(static_cast<pilot_value_type>(best_pilot));

    for (std::uint64_t const hash : bucket_hashes) {
      slot_index_type const slot = slot_index_(hash, pilot_hash);
      if (context.occupied_slots[slot]) [[likely]] {
        bucket_index_type const owner = context.slot_owners[slot];
        ++context.eviction_count;
        context.eviction_heap.emplace_back(static_cast<bucket_length_type>(context.bucket_length(owner)), owner);
        std::push_heap(context.eviction_heap.begin(), context.eviction_heap.end());

        std::span<std::uint64_t const> const owner_hashes = context.bucket_hashes(owner);
        std::uint64_t const owner_pilot_hash              = pilot_hash_value_(pilots_[owner]);
        for (std::uint64_t const owner_hash : owner_hashes) {
          slot_index_type const owner_slot   = slot_index_(owner_hash, owner_pilot_hash);
          context.occupied_slots[owner_slot] = false;
          context.slot_owners[owner_slot]    = vacant_slot;
        }
      }
      context.occupied_slots[slot] = true;
      context.slot_owners[slot]    = bucket;
    }

    context.recent_index                         = (context.recent_index + 1u) % recent_bucket_capacity;
    context.recent_buckets[context.recent_index] = bucket;
    return true;
  }

  [[nodiscard]] constexpr bool try_build_from_sorted_hashes_(std::span<std::uint64_t const> const hashes)
  {
    prepare_pilots_(bucket_count_);

    // Build the histogram in one linear scan, then turn counts into the
    // starting offset of every bucket with one prefix-sum pass.
    bucket_offsets_type bucket_offsets = make_bucket_offsets_(bucket_count_ + 1U);
    for (std::uint32_t i = 0; i < key_count_; ++i) {
      ++bucket_offsets[static_cast<offset_type>(bucket_index_(hashes[i]) + 1U)];
    }
    for (std::uint32_t bucket = 1; bucket <= bucket_count_; ++bucket) {
      bucket_offsets[bucket] = static_cast<offset_type>(bucket_offsets[bucket] + bucket_offsets[bucket - 1U]);
    }
    assert(bucket_offsets[bucket_count_] == hashes.size());

    // Counting sort of buckets by descending length, ties by ascending index.
    bucket_length_type max_bucket_length = 0;
    for (std::uint32_t bucket = 0; bucket < bucket_count_; ++bucket) {
      bucket_length_type const length = bucket_length_(bucket_offsets, bucket);
      if (length > max_bucket_length) [[unlikely]]
        max_bucket_length = length;
    }

    std::uint32_t const length_slot_count          = static_cast<std::uint32_t>(max_bucket_length) + 1U;
    bucket_length_prefix_type bucket_length_prefix = make_bucket_length_prefix_(length_slot_count);
    for (std::uint32_t bucket = 0; bucket < bucket_count_; ++bucket) {
      ++bucket_length_prefix[bucket_length_(bucket_offsets, bucket)];
    }
    offset_type order_offset = 0;
    for (std::uint32_t length = length_slot_count; length-- > 0;) {
      offset_type const count      = bucket_length_prefix[length];
      bucket_length_prefix[length] = order_offset;
      order_offset                 = static_cast<offset_type>(order_offset + count);
    }
    std::uint32_t const non_empty_bucket_count = static_cast<std::uint32_t>(bucket_length_prefix[0]);

    bucket_order_type bucket_order = make_bucket_order_(non_empty_bucket_count);
    for (std::uint32_t bucket = 0; bucket < bucket_count_; ++bucket) {
      bucket_length_type const length = bucket_length_(bucket_offsets, bucket);
      if (length != 0) [[likely]]
        bucket_order[bucket_length_prefix[length]++] = bucket;
    }

    occupied_slots_type occupied_slots = make_occupied_slots_(slot_count_);

    {
      // The owner table is only read for occupied slots; leaving free entries
      // uninitialized removes a full-table memset from every build attempt.
      slot_owner_buffer_type slot_owners = make_slot_owners_(slot_count_);
      eviction_heap_type eviction_heap   = make_eviction_heap_();
      std::array<bucket_index_type, recent_bucket_capacity> recent_buckets;
      slot_scratch_type slot_scratch = make_slot_scratch_(static_cast<std::uint32_t>(max_bucket_length));
      splitmix64 random_engine { hash_seed_ ^ 0x9E'37'79'B9'7F'4A'7C'15ULL };
      std::uint32_t eviction_count = 0;

      bucket_placement_context context {
        std::span<offset_type const> { bucket_offsets.data(), bucket_offsets.size() },
        std::span<std::uint64_t const> { hashes.data(),       hashes.size()         },
        std::span<bucket_index_type> { slot_owners.data(),    slot_owners.size()    },
        occupied_slots,
        eviction_heap,
        recent_buckets,
        random_engine,
        eviction_count,
        std::span<slot_index_type> { slot_scratch.data(),   slot_scratch.size()   }
      };

      // Largest buckets first: they are the hardest to place and the cheapest
      // to evict while most of the table is still empty.
      for (std::uint32_t order_index = 0; order_index < non_empty_bucket_count; ++order_index) {
        bucket_index_type const bucket  = bucket_order[order_index];
        std::uint32_t const bucket_size = static_cast<std::uint32_t>(context.bucket_hashes(bucket).size());

        eviction_heap.clear();
        eviction_count = 0;
        recent_buckets.fill(no_bucket);
        recent_buckets[0]    = bucket;
        context.recent_index = 0;

        eviction_heap.emplace_back(static_cast<bucket_length_type>(bucket_size), bucket);
        std::push_heap(eviction_heap.begin(), eviction_heap.end());

        while (!eviction_heap.empty()) {
          std::pop_heap(eviction_heap.begin(), eviction_heap.end());
          bucket_index_type const current_bucket = eviction_heap.back().second;
          eviction_heap.pop_back();

          // Only sample the limit at powers of two above the slot count,
          // which avoids the cost of a check on every heap pop while still
          // bounding the total eviction chain.
          if (eviction_count > slot_count_ && std::has_single_bit(eviction_count) &&
              static_cast<std::uint64_t>(eviction_count) >= static_cast<std::uint64_t>(options_value.max_evictions_per_slot) * slot_count_) [[unlikely]] {
            return false;
          }
          if (try_place_bucket_(current_bucket, context)) [[likely]] continue;
          if (options_value.max_evictions_per_slot == 0) [[unlikely]] return false;
          if (!try_place_bucket_with_evict_(current_bucket, context)) [[unlikely]] return false;
        }
      }
    }

    build_remap_(occupied_slots);
    return true;
  }

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  [[nodiscard]] constexpr bool try_build_impl_(Iterator first, Sentinel last)
  {
    auto const distance = std::ranges::distance(first, last);
    if (distance <= 0) [[unlikely]] {
      reset_build_state_();
      return true;
    }
    std::uint64_t const key_count64 = static_cast<std::uint64_t>(distance);
    if (key_count64 > max_key_count) [[unlikely]] {
      throw_length_error_();
    }
    std::uint32_t const key_count = static_cast<std::uint32_t>(key_count64);

    double const slot_count_estimate   = static_cast<double>(key_count) / options_value.slot_load_factor;
    double const bucket_quotient       = static_cast<double>(key_count) / options_value.average_bucket_size;
    std::uint64_t const bucket_floor   = static_cast<std::uint64_t>(bucket_quotient);
    std::uint64_t const bucket_count64 = (static_cast<double>(bucket_floor) < bucket_quotient ? bucket_floor + 1U : bucket_floor) + 3U;

    if (slot_count_estimate >= static_cast<double>((std::numeric_limits<std::uint32_t>::max)()) ||
        static_cast<std::uint64_t>(slot_count_estimate) > max_slot_count ||
        bucket_count64 > max_bucket_count) [[unlikely]] {
      throw_length_error_();
    }

    key_count_  = key_count;
    slot_count_ = static_cast<std::uint32_t>(slot_count_estimate);
    // a power-of-two slot count would make the low hash bits dominate the
    // multiply-range reduction; the extra slot keeps all bits in play.
    if (std::has_single_bit(slot_count_)) [[unlikely]] ++slot_count_;
    slot_reciprocal_ = slot_count_ == 0 ? 0 : static_cast<std::uint64_t>((std::numeric_limits<std::uint64_t>::max)() / slot_count_ + 1U);
    bucket_count_    = static_cast<std::uint32_t>(bucket_count64);

    hash_buffer_type hashes = make_hash_buffer_(key_count_);
    // Fixed-capacity storage has no allocator-backed fallback, so it always
    // provides the explicit radix buffer instead of letting the sorter allocate.
    bool const uses_external_radix_buffer = (storage_policy == ptr_hash_storage_policy::stack) || key_count_ > stable_radix_sort_detail::kRadixSortThreshold;
    hash_buffer_type radix_sort_buffer = make_hash_buffer_(uses_external_radix_buffer ? key_count_ : std::uint32_t { 0 });

    splitmix64 seed_eng(161803); // PI: 314159 / PHI: 161803
    for (decltype(options_value.max_build_attempts) attempt { 0 }; attempt < options_value.max_build_attempts; ++attempt) {
      hash_seed_ = seed_eng();

      auto current = first;
      for (std::uint32_t i { 0 }; i < key_count_; ++i, ++current) {
        hashes[i] = hash_value(*current);
      }

      if (uses_external_radix_buffer) {
        stable_radix_sort<radix_sort_options {}>(
            hashes.data(),
            hashes.data() + key_count_,
            std::span<std::uint64_t> { radix_sort_buffer.data(), radix_sort_buffer.size() });
      } else {
        if constexpr (storage_policy == ptr_hash_storage_policy::heap) {
          stable_radix_sort<radix_sort_options {}>(hashes.data(), hashes.data() + key_count_, rebind_alloc_t<std::uint64_t> { allocator_ });
        } else {
          stable_radix_sort<radix_sort_options {}>(hashes.data(), hashes.data() + key_count_);
        }
      }

      if (has_duplicate_value_(std::span<std::uint64_t const> { hashes.data(), hashes.size() })) [[unlikely]] continue;
      if (try_build_from_sorted_hashes_(std::span<std::uint64_t const> { hashes.data(), hashes.size() })) [[likely]] return true;
    }
    reset_build_state_();
    return false;
  }

public:
  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires std::same_as<std::remove_cvref_t<std::iter_value_t<Iterator>>, key_type>
  [[nodiscard]] constexpr bool try_build(Iterator first, Sentinel last)
  {
    return try_build_impl_(first, last);
  }

  [[nodiscard]] constexpr bool try_build(std::initializer_list<key_type> keys)
  {
    return try_build(keys.begin(), keys.end());
  }

  template <std::ranges::forward_range Range>
  requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, key_type>
  [[nodiscard]] constexpr bool try_build(Range &&keys)
  {
    return try_build(std::ranges::begin(std::forward<Range>(keys)), std::ranges::end(std::forward<Range>(keys)));
  }

  [[nodiscard]] constexpr bool empty() const noexcept { return key_count_ == 0; }
  [[nodiscard]] constexpr size_type size() const noexcept { return key_count_; }
  [[nodiscard]] constexpr size_type capacity() const noexcept { return slot_count_; }
  [[nodiscard]] constexpr size_type bucket_count() const noexcept { return bucket_count_; }
  [[nodiscard]] constexpr std::uint64_t hash_seed() const noexcept { return hash_seed_; }
  [[nodiscard]] constexpr hasher hash_function() const noexcept { return hasher_; }
  [[nodiscard]] constexpr allocator_type get_allocator() const noexcept(std::is_nothrow_copy_constructible_v<allocator_type>)
  requires (storage_policy == ptr_hash_storage_policy::heap)
  {
    return allocator_;
  }

private:
  template <class K>
  static constexpr bool hash_value_uses_seeded_call_v =
      std::same_as<std::remove_cv_t<hasher>, hash<key_type>> &&
      std::invocable<hasher const &, K const &, std::uint64_t>;

  template <class K>
  [[nodiscard]] constexpr std::uint64_t hash_value_impl_(K const &key) const
      noexcept(hash_value_uses_seeded_call_v<K> ? std::is_nothrow_invocable_v<hasher const &, K const &, std::uint64_t> : std::is_nothrow_invocable_v<hasher const &, K const &>)
  {
    // hash<T> intentionally omits its final avalanche multiply; the default
    // PtrHash path restores it here. Custom hashers keep their own semantics.
    if constexpr (hash_value_uses_seeded_call_v<K>) {
      return static_cast<std::uint64_t>(hasher_(static_cast<key_type>(key), static_cast<std::uint64_t>(hash_seed_))) * kPhiMixMultConstant;
    } else {
      return static_cast<std::uint64_t>(hasher_(static_cast<key_type>(key))) ^ hash_seed_;
    }
  }

public:
  [[nodiscard]] constexpr std::uint64_t hash_value(key_type const &key) const
      noexcept(std::is_nothrow_invocable_v<hasher const &, key_type const &>)
  {
    return hash_value_impl_(key);
  }

  template <class K>
  requires (!std::same_as<std::remove_cvref_t<K>, key_type>) && ptr_hash_detail::key_hasher<hasher, K>
  [[nodiscard]] constexpr std::uint64_t hash_value(K const &key) const
      noexcept(hash_value_uses_seeded_call_v<K>
                   ? std::is_nothrow_invocable_v<hasher const &, K const &, std::uint64_t>
                   : std::is_nothrow_invocable_v<hasher const &, K const &>)
  {
    return hash_value_impl_(key);
  }

  [[nodiscard]] constexpr size_type index_from_hash(std::uint64_t const hash) const noexcept
  {
    assert(!empty() && "stdex::ptr_hash::index_from_hash called on an empty table");
    pilot_value_type const pilot_value = pilots_[bucket_index_(hash)];
    slot_index_type const slot_index   = slot_index_(hash, pilot_hash_value_(pilot_value));
    if constexpr (options_value.hash_scheme == ptr_hash_scheme::minimal_perfect) {
      return static_cast<size_type>(slot_index < key_count_ ? slot_index : remap_[slot_index - key_count_]);
    } else {
      return static_cast<size_type>(slot_index);
    }
  }

  [[nodiscard]] constexpr size_type index_unchecked(key_type const &key) const
  {
    return index_from_hash(hash_value(key));
  }

  template <class K>
  requires (!std::same_as<std::remove_cvref_t<K>, key_type>) && ptr_hash_detail::key_hasher<hasher, K>
  [[nodiscard]] constexpr size_type index_unchecked(K const &key) const
  {
    return index_from_hash(hash_value(key));
  }

  [[nodiscard]] constexpr size_type index(key_type const &key) const
  {
    if (empty()) [[unlikely]] return npos;
    return index_unchecked(key);
  }

  template <class K>
  requires (!std::same_as<std::remove_cvref_t<K>, key_type>) && ptr_hash_detail::key_hasher<hasher, K>
  [[nodiscard]] constexpr size_type index(K const &key) const
  {
    if (empty()) [[unlikely]] return npos;
    return index_unchecked(key);
  }

private:
  [[nodiscard]] constexpr slot_index_type slot_index_(std::uint64_t const hash, std::uint64_t const pilot_hash) const noexcept
  {
    assert(slot_count_ != 0);
    std::uint64_t const low_bits = static_cast<std::uint64_t>(slot_reciprocal_) * (hash ^ pilot_hash);
    return static_cast<slot_index_type>(umul128(low_bits, slot_count_).hi());
  }

  [[nodiscard]] constexpr bucket_index_type bucket_index_(std::uint64_t const hash) const noexcept
  {
    return static_cast<bucket_index_type>(umul128(bucket_count_, transform_bucket_hash_(hash)).hi());
  }

  template <class Offsets>
  [[nodiscard]] static constexpr bucket_length_type bucket_length_(Offsets const &bucket_offsets, bucket_index_type const bucket) noexcept
  {
    return bucket_offsets[bucket + 1] - bucket_offsets[bucket];
  }

  [[nodiscard]] constexpr std::uint64_t pilot_hash_value_(pilot_value_type const pilot) const noexcept
  {
    return kPiMixMultConstant * (static_cast<std::uint64_t>(pilot) ^ hash_seed_);
  }

  [[nodiscard]] constexpr std::uint64_t transform_bucket_hash_(std::uint64_t const hash) const noexcept
  {
    static_assert(options_value.bucket_distribution == ptr_hash_bucket_distribution::linear ||
                  options_value.bucket_distribution == ptr_hash_bucket_distribution::cubic_eps);

    if constexpr (options_value.bucket_distribution == ptr_hash_bucket_distribution::linear) {
      return hash;
    } else {
      std::uint64_t const squared = umul128(hash, hash).hi();
      std::uint64_t const cubic   = umul128(squared, (hash >> 1U) | (std::uint64_t { 1 } << 63U)).hi();
      return (cubic / 256U) * 255U + hash / 256U;
    }
  }

private:
  [[noreturn]] static void throw_length_error_()
  {
#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
    throw std::length_error("length exceeds 2^32 - 1");
#else
    std::abort();
#endif
  }

  [[noreturn]] static void throw_runtime_error_(char const *description)
  {
#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
    throw std::runtime_error(description);
#else
    std::abort();
#endif
  }

private:
  pilot_table_type pilots_;
  remap_table_type remap_;
  std::uint32_t key_count_ {};
  std::uint32_t slot_count_ {};
  std::uint32_t bucket_count_ {};
  std::uint64_t slot_reciprocal_ {};
  std::uint64_t hash_seed_ {};
  [[no_unique_address]] hasher hasher_ {};
  [[no_unique_address]] allocator_type allocator_ {};
};
} // namespace stdex
