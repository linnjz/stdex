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

template <class Allocator>
struct ptr_hash_heap_storage_policy
{
  static constexpr bool is_fixed_capacity = false;
  using type = Allocator;
};

template <std::size_t N>
struct ptr_hash_stack_storage_policy
{
  static constexpr bool is_fixed_capacity = true;
  static constexpr std::size_t value      = N;
};

template <class Policy>
struct ptr_hash_options
{
  using storage_policy_type = Policy;

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
};
/// Ready-made option sets. The heap variants select the allocator backing the
/// dynamically sized tables; the stack variants embed a fixed key capacity @c N
/// so the whole table, including its build scratch, can live inline.
template <class Allocator>
struct ptr_hash_heap_option_fast
{
  static constexpr auto value = ptr_hash_options<ptr_hash_heap_storage_policy<Allocator>>::profile_fast();
};

template <class Allocator>
struct ptr_hash_heap_option_default
{
  static constexpr auto value = ptr_hash_options<ptr_hash_heap_storage_policy<Allocator>>::profile_default();
};

template <class Allocator>
struct ptr_hash_heap_option_balanced
{
  static constexpr auto value = ptr_hash_options<ptr_hash_heap_storage_policy<Allocator>>::profile_balanced();
};

/// The downside of compact is that building it is twice as slow as balanced, and it's more likely to fail.
template <class Allocator>
struct ptr_hash_heap_option_compact
{
  static constexpr auto value = ptr_hash_options<ptr_hash_heap_storage_policy<Allocator>>::profile_compact();
};

template <std::size_t N>
struct ptr_hash_stack_option_fast
{
  static constexpr auto value = ptr_hash_options<ptr_hash_stack_storage_policy<N>>::profile_fast();
};

template <std::size_t N>
struct ptr_hash_stack_option_default
{
  static constexpr auto value = ptr_hash_options<ptr_hash_stack_storage_policy<N>>::profile_default();
};

template <std::size_t N>
struct ptr_hash_stack_option_balanced
{
  static constexpr auto value = ptr_hash_options<ptr_hash_stack_storage_policy<N>>::profile_balanced();
};

template <std::size_t N>
struct ptr_hash_stack_option_compact
{
  static constexpr auto value = ptr_hash_options<ptr_hash_stack_storage_policy<N>>::profile_compact();
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

template <class Storage>
concept ptr_hash_storage = requires {
  typename Storage::allocator_type;
  typename Storage::size_type;
  typename Storage::pilot_type;
  typename Storage::pilot_table_type;
  typename Storage::remap_table_type;
  typename Storage::bucket_offsets_type;
  typename Storage::bucket_length_prefix_type;
  typename Storage::bucket_order_type;
  typename Storage::slot_owner_buffer_type;
  typename Storage::hash_buffer_type;
  typename Storage::slot_scratch_type;
  typename Storage::occupied_slots_type;
  typename Storage::eviction_heap_type;
  { Storage::max_key_count } -> std::convertible_to<std::size_t>;
  { Storage::max_slot_count } -> std::convertible_to<std::size_t>;
};

/// Heap-backed storage policy for `ptr_hash`.
///
/// Every container used by the build is obtained through this policy. A
/// fixed-capacity policy only has to provide the same aliases and factory
/// functions; the build algorithm itself never names `std::vector` or
/// `uninitialized_buffer`. Factory results must report their active element
/// count through `size()`; spare inline capacity is not part of that count.
template <class Options>
class heap_storage
{
public:
  using size_type      = std::size_t;
  using pilot_type     = std::uint8_t;
  using remap_type     = std::uint32_t;

  static constexpr auto opts { Options::value };
  static_assert(
      opts.max_build_attempts > std::uint16_t { 0 } &&
      (opts.slot_load_factor > 0.0 && opts.slot_load_factor < 1.0) &&
      opts.average_bucket_size >= 1.0);

  static_assert(std::has_single_bit(opts.pilot_search_limit), "pilot_search_limit must be a power of two");
  static_assert(opts.pilot_search_limit <= 256, "pilot_search_limit must not exceed 256: pilots_ stores std::uint8_t");

  using policy_type    = typename std::remove_cv_t<decltype(opts)>::storage_policy_type;
  using allocator_type = typename policy_type::type;
  static_assert(standard_allocator<allocator_type>, "heap storage requires a standard allocator");

  static constexpr size_type max_key_count = (std::numeric_limits<std::uint32_t>::max)();
  static constexpr size_type max_slot_count = (std::numeric_limits<size_type>::max)();

  template <typename T>
  using rebind_alloc_t = typename std::allocator_traits<allocator_type>::template rebind_alloc<T>;
  template <typename T>
  using vector_type = std::vector<T, rebind_alloc_t<T>>;
  template <typename T>
  using buffer_type = ::stdex::uninitialized_buffer<T, rebind_alloc_t<T>>;

  using pilot_table_type          = vector_type<pilot_type>;
  using remap_table_type          = vector_type<remap_type>;
  using bucket_offsets_type       = vector_type<std::uint32_t>;
  using bucket_length_prefix_type = vector_type<std::uint32_t>;
  using bucket_order_type         = buffer_type<std::uint32_t>;
  using slot_owner_buffer_type    = buffer_type<std::uint32_t>;
  using hash_buffer_type          = buffer_type<std::uint64_t>;
  using slot_scratch_type         = buffer_type<size_type>;
  using occupied_slots_type       = ::stdex::inlined_bitset<std::uint64_t, 0, rebind_alloc_t<std::uint64_t>>;
  using eviction_heap_type        = vector_type<std::pair<std::uint32_t, std::uint32_t>>;

  constexpr heap_storage() = default;

  constexpr explicit heap_storage(allocator_type const &allocator)
      : allocator_ { allocator }
      , pilots_ { rebind_alloc_t<pilot_type> { allocator } }
      , remap_ { rebind_alloc_t<remap_type> { allocator } }
  {
  }

  constexpr allocator_type const &get_allocator() const noexcept { return allocator_; }
  constexpr void reset() noexcept
  {
    pilots_.clear();
    remap_.clear();
  }

  constexpr void prepare_pilots(size_type const bucket_count) { pilots_.assign(bucket_count, pilot_type { 0 }); }
  [[nodiscard]] constexpr pilot_type pilot(size_type const bucket) const noexcept { return pilots_[bucket]; }
  constexpr void set_pilot(size_type const bucket, pilot_type const pilot) noexcept { pilots_[bucket] = pilot; }

  constexpr void clear_remap()
  {
    remap_.clear();
    remap_.shrink_to_fit();
  }
  constexpr void prepare_remap(size_type const count, remap_type const fallback) { remap_.assign(count, fallback); }
  [[nodiscard]] constexpr remap_type remap(size_type const index) const noexcept { return remap_[index]; }
  constexpr void set_remap(size_type const index, remap_type const value) noexcept { remap_[index] = value; }
  constexpr void shrink_remap() { remap_.shrink_to_fit(); }

  [[nodiscard]] constexpr bucket_offsets_type make_bucket_offsets(size_type const count) const
  {
    return bucket_offsets_type(count, std::uint32_t { 0 }, rebind_alloc_t<std::uint32_t> { allocator_ });
  }

  [[nodiscard]] constexpr bucket_length_prefix_type make_bucket_length_prefix(size_type const count) const
  {
    return bucket_length_prefix_type(count, std::uint32_t { 0 }, rebind_alloc_t<std::uint32_t> { allocator_ });
  }

  [[nodiscard]] constexpr bucket_order_type make_bucket_order(size_type const count) const
  {
    return bucket_order_type(count, rebind_alloc_t<std::uint32_t> { allocator_ });
  }

  [[nodiscard]] constexpr slot_owner_buffer_type make_slot_owners(size_type const count) const
  {
    return slot_owner_buffer_type(count, rebind_alloc_t<std::uint32_t> { allocator_ });
  }

  [[nodiscard]] constexpr hash_buffer_type make_hash_buffer(size_type const count) const
  {
    return hash_buffer_type(count, rebind_alloc_t<std::uint64_t> { allocator_ });
  }

  [[nodiscard]] constexpr slot_scratch_type make_slot_scratch(size_type const count) const
  {
    return slot_scratch_type(count, rebind_alloc_t<size_type> { allocator_ });
  }

  [[nodiscard]] constexpr occupied_slots_type make_occupied_slots(size_type const bit_count) const
  {
    return occupied_slots_type(bit_count, rebind_alloc_t<std::uint64_t> { allocator_ });
  }

  [[nodiscard]] constexpr eviction_heap_type make_eviction_heap() const
  {
    return eviction_heap_type(rebind_alloc_t<std::pair<std::uint32_t, std::uint32_t>> { allocator_ });
  }

private:
  [[no_unique_address]] allocator_type allocator_ {};
  pilot_table_type pilots_;
  remap_table_type remap_;
};

template <class T, std::size_t Capacity>
class fixed_vector
{
  static_assert(std::is_trivially_destructible_v<T>, "fixed_vector requires trivially destructible values");
  static_assert(Capacity > 0, "cannot emplace into zero-capacity fixed_vector");

public:
  using value_type      = T;
  using size_type       = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference       = T &;
  using const_reference = T const &;
  using iterator        = T *;
  using const_iterator  = T const *;

  static constexpr size_type capacity = Capacity;

  constexpr fixed_vector() = default;

  constexpr explicit fixed_vector(size_type const count)
      : size_ { count }
  {
    assert(count <= Capacity);
  }

  constexpr fixed_vector(size_type const count, T const &value)
      : size_ { count }
  {
    assert(count <= Capacity);
    std::fill_n(data_.data(), count, value);
  }

  [[nodiscard]] constexpr size_type size() const noexcept { return size_; }
  [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
  [[nodiscard]] constexpr T *data() noexcept { return data_.data(); }
  [[nodiscard]] constexpr T const *data() const noexcept { return data_.data(); }

  [[nodiscard]] constexpr T &operator[] (size_type const index) noexcept
  {
    assert(index < size_);
    return data_[index];
  }

  [[nodiscard]] constexpr T const &operator[] (size_type const index) const noexcept
  {
    assert(index < size_);
    return data_[index];
  }

  [[nodiscard]] constexpr iterator begin() noexcept { return data(); }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return data(); }
  [[nodiscard]] constexpr iterator end() noexcept { return empty() ? begin() : begin() + size_; }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return empty() ? begin() : begin() + size_; }

  constexpr void clear() noexcept { size_ = 0; }

  template <class... Args>
  constexpr T &emplace_back(Args &&...args)
  {
    T &slot = data_[size_];
    slot    = T(std::forward<Args>(args)...);
    ++size_;
    return slot;
  }

  constexpr void push_back(T const &value) { emplace_back(value); }
  constexpr void pop_back() noexcept
  {
    assert(size_ != 0);
    --size_;
  }

  [[nodiscard]] constexpr T &back() noexcept
  {
    assert(size_ != 0);
    return data_[size_ - 1U];
  }

  [[nodiscard]] constexpr T const &back() const noexcept
  {
    assert(size_ != 0);
    return data_[size_ - 1U];
  }

private:
  std::array<T, capacity> data_ {};
  size_type size_ {};
};

/// Minimal fixed-capacity bit set used by `stack_storage`.
///
/// Only the const/non-const bit access needed by the bucket-placement code is
/// provided. A plain block array keeps the type constexpr-friendly without
/// bringing in `inline_storage`'s union or heap fallback.
template <std::size_t BitCount>
class fixed_bitset
{
  static_assert(BitCount > 0, "fixed_bitset requires a non-zero bit count");

public:
  using block_type = std::uint64_t;
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

    constexpr reference &operator=(reference const &other) noexcept
    {
      return *this = static_cast<bool>(other);
    }

    constexpr reference &operator=(bool const value) noexcept
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
  static constexpr size_type bits_per_block = 64;
  static constexpr size_type block_count    = (BitCount + bits_per_block - 1U) / bits_per_block;

  [[nodiscard]] constexpr bool test_(size_type const bit) const noexcept
  {
    assert(bit < BitCount);
    return (blocks_[bit / bits_per_block] >> (bit % bits_per_block)) & block_type { 1 };
  }

  constexpr void set_(size_type const bit, bool const value) noexcept
  {
    assert(bit < BitCount);
    block_type &block     = blocks_[bit / bits_per_block];
    block_type const mask = block_type { 1 } << (bit % bits_per_block);
    if (value) {
      block |= mask;
    } else {
      block &= static_cast<block_type>(~mask);
    }
  }

  std::array<block_type, block_count> blocks_ {};
};

/// Stack-backed storage policy for `ptr_hash`.
///
/// Capacity and every table extent derive from the option's storage policy, so
/// the whole table and its build scratch live inline and the type is usable
/// during constant evaluation. There is no allocator: `allocator_type` is
/// `void`, which disables the allocator-taking `ptr_hash` constructors.
template <class Options>
class stack_storage
{
public:
  using allocator_type = void;
  using size_type      = std::size_t;
  using pilot_type     = std::uint8_t;
  using remap_type     = std::uint32_t;

  static constexpr auto opts { Options::value };
  static_assert(
      opts.max_build_attempts > std::uint16_t { 0 } &&
      (opts.slot_load_factor > 0.0 && opts.slot_load_factor < 1.0) &&
      opts.average_bucket_size >= 1.0);

  static_assert(std::has_single_bit(opts.pilot_search_limit), "pilot_search_limit must be a power of two");
  static_assert(opts.pilot_search_limit <= 256, "pilot_search_limit must not exceed 256: pilots_ stores std::uint8_t");

  static constexpr size_type max_key_count = std::remove_cv_t<decltype(opts)>::storage_policy_type::value;
  static_assert(max_key_count > 0, "stack_storage requires a non-zero key capacity");

private:
  [[nodiscard]] static constexpr size_type calculate_max_slot_count_() noexcept
  {
    double const estimate = static_cast<double>(max_key_count) / opts.slot_load_factor;
    // Very small load factors would require an impossible compile-time array;
    // clamp the layout and let the runtime slot check reject the build.
    if (!(estimate <= static_cast<double>(max_key_count) * 4.0)) return max_key_count * 4U + 1U;
    return static_cast<size_type>(estimate) + 1U;
  }

  [[nodiscard]] static constexpr size_type calculate_max_bucket_count_() noexcept
  {
    double const quotient     = static_cast<double>(max_key_count) / opts.average_bucket_size;
    size_type const truncated = static_cast<size_type>(quotient);
    return (static_cast<double>(truncated) < quotient ? truncated + 1U : truncated) + 3U;
  }

public:
  static constexpr size_type max_slot_count    = calculate_max_slot_count_();
  static constexpr size_type max_bucket_count  = calculate_max_bucket_count_();
  /// The eviction guard is sampled at powers of two, and one placement can
  /// enqueue a whole bucket before the next sample, hence the extra length.
  static constexpr size_type max_eviction_entries =
      opts.max_evictions_per_slot == 0
          ? 1U
          : static_cast<size_type>(opts.max_evictions_per_slot) * max_slot_count * 2U + max_key_count + 1U;

  using pilot_table_type          = std::array<pilot_type, max_bucket_count>;
  using remap_table_type          = std::array<remap_type, max_slot_count>;
  using bucket_offsets_type       = fixed_vector<std::uint32_t, max_bucket_count + 1U>;
  using bucket_length_prefix_type = fixed_vector<std::uint32_t, max_key_count + 1U>;
  using bucket_order_type         = fixed_vector<std::uint32_t, max_bucket_count>;
  using slot_owner_buffer_type    = fixed_vector<std::uint32_t, max_slot_count>;
  using hash_buffer_type          = fixed_vector<std::uint64_t, max_key_count>;
  using slot_scratch_type         = fixed_vector<size_type, max_key_count>;
  using occupied_slots_type       = fixed_bitset<max_slot_count>;
  using eviction_heap_type        = fixed_vector<std::pair<std::uint32_t, std::uint32_t>, max_eviction_entries>;

  constexpr stack_storage() = default;

  constexpr void reset() noexcept {}

  constexpr void prepare_pilots(size_type const bucket_count)
  {
    assert(bucket_count <= max_bucket_count);
    std::fill_n(pilots_.data(), bucket_count, pilot_type { 0 });
  }

  [[nodiscard]] constexpr pilot_type pilot(size_type const bucket) const noexcept
  {
    assert(bucket < max_bucket_count);
    return pilots_[bucket];
  }

  constexpr void set_pilot(size_type const bucket, pilot_type const pilot) noexcept
  {
    assert(bucket < max_bucket_count);
    pilots_[bucket] = pilot;
  }

  constexpr void clear_remap() noexcept {}

  constexpr void prepare_remap(size_type const count, remap_type const fallback)
  {
    assert(count <= max_slot_count);
    std::fill_n(remap_.data(), count, fallback);
  }

  [[nodiscard]] constexpr remap_type remap(size_type const index) const noexcept
  {
    assert(index < max_slot_count);
    return remap_[index];
  }

  constexpr void set_remap(size_type const index, remap_type const value) noexcept
  {
    assert(index < max_slot_count);
    remap_[index] = value;
  }

  constexpr void shrink_remap() noexcept {}

  [[nodiscard]] constexpr bucket_offsets_type make_bucket_offsets(size_type const count) const
  {
    return bucket_offsets_type(count, std::uint32_t { 0 });
  }

  [[nodiscard]] constexpr bucket_length_prefix_type make_bucket_length_prefix(size_type const count) const
  {
    return bucket_length_prefix_type(count, std::uint32_t { 0 });
  }

  [[nodiscard]] constexpr bucket_order_type make_bucket_order(size_type const count) const
  {
    return bucket_order_type(count);
  }

  [[nodiscard]] constexpr slot_owner_buffer_type make_slot_owners(size_type const count) const
  {
    return slot_owner_buffer_type(count);
  }

  [[nodiscard]] constexpr hash_buffer_type make_hash_buffer(size_type const count) const
  {
    return hash_buffer_type(count);
  }

  [[nodiscard]] constexpr slot_scratch_type make_slot_scratch(size_type const count) const
  {
    return slot_scratch_type(count);
  }

  [[nodiscard]] constexpr occupied_slots_type make_occupied_slots(size_type const bit_count) const
  {
    (void) bit_count;
    assert(bit_count <= max_slot_count);
    return occupied_slots_type {};
  }

  [[nodiscard]] constexpr eviction_heap_type make_eviction_heap() const { return {}; }

private:
  pilot_table_type pilots_ {};
  remap_table_type remap_ {};
};

template <class T>
inline constexpr bool is_ptr_hash_options_v = false;

template <class Policy>
inline constexpr bool is_ptr_hash_options_v<ptr_hash_options<Policy>> = true;

template <class Options>
using storage_policy_for_t = typename std::remove_cv_t<decltype(Options::value)>::storage_policy_type;

/// Selects the concrete storage implementation from the option's policy tag.
/// A heap option carries its allocator, a stack option its compile-time capacity.
template <class Options>
using storage_for_t = std::conditional_t<storage_policy_for_t<Options>::is_fixed_capacity,
                                         stack_storage<Options>,
                                         heap_storage<Options>>;
} // namespace ptr_hash_detail

/**
 *@brief Perfect / minimal-perfect hash table defined by a key, hasher, and option set.
 *
 * @details The third template parameter selects both the tuning profile and the
 * storage mode:
 * - @c ptr_hash_heap_option_*<Allocator> builds a dynamically sized table on the
 *   heap.
 * - @c ptr_hash_stack_option_*<N> stores everything inline in an allocation-free
 *   table of capacity @c N.
 *
 * The placement algorithm is shared, so only the storage policy differs between
 * the two modes.
 *
 * @tparam Key     Key type stored in the table.
 * @tparam Hasher  Hash function object used to hash keys.
 * @tparam Options Option set selecting the tuning profile and storage mode
 *                 (see @c ptr_hash_heap_option_* and @c ptr_hash_stack_option_*).
 */
template <class Key, class Hasher = hash<Key>, class Options = ptr_hash_heap_option_default<std::allocator<Key>>>
requires ptr_hash_detail::is_ptr_hash_options_v<std::remove_cv_t<decltype(Options::value)>> &&
         ptr_hash_detail::key_hasher<Hasher, Key> &&
         ptr_hash_detail::ptr_hash_storage<ptr_hash_detail::storage_for_t<Options>>
class ptr_hash
{
public:
  using key_type            = Key;
  using hasher              = Hasher;
  using storage_type        = ptr_hash_detail::storage_for_t<Options>;
  using storage_policy_type = ptr_hash_detail::storage_policy_for_t<Options>;
  using value_type          = key_type;
  using size_type           = std::size_t;
  using difference_type     = std::ptrdiff_t;
  using allocator_type      = typename storage_type::allocator_type;

  static constexpr auto opts { Options::value };
  static constexpr bool is_fixed_capacity = storage_policy_type::is_fixed_capacity;
  static constexpr size_type npos { static_cast<size_type>(-1) };

private:
  using pilot_type                = typename storage_type::pilot_type;
  using remap_type                = typename storage_type::remap_type;
  using bucket_offsets_type       = typename storage_type::bucket_offsets_type;
  using bucket_length_prefix_type = typename storage_type::bucket_length_prefix_type;
  using bucket_order_type         = typename storage_type::bucket_order_type;
  using slot_owner_buffer_type    = typename storage_type::slot_owner_buffer_type;
  using hash_buffer_type          = typename storage_type::hash_buffer_type;
  using slot_scratch_type         = typename storage_type::slot_scratch_type;
  using occupied_slots_type       = typename storage_type::occupied_slots_type;
  using eviction_heap_type        = typename storage_type::eviction_heap_type;

  using slot_owner_type     = std::uint32_t;
  static constexpr slot_owner_type vacant_slot      = (std::numeric_limits<slot_owner_type>::max)();
  static constexpr slot_owner_type no_bucket        = vacant_slot;
  static constexpr size_type recent_bucket_capacity = 16;

  /// Transient state shared by the bucket-placement routines below.
  ///
  /// `slot_owners` is only meaningful for occupied slots; free slots are left
  /// untouched so the table does not need a full-table initialization.
  struct bucket_placement_context
  {
    std::span<std::uint32_t const> bucket_offsets;
    std::span<std::uint64_t const> hashes;
    std::span<std::uint32_t> slot_owners;
    occupied_slots_type &occupied_slots;
    eviction_heap_type &eviction_heap;
    std::array<std::uint32_t, recent_bucket_capacity> &recent_buckets;
    splitmix64 &random_engine;
    size_type &eviction_count;
    std::span<size_type> slot_scratch;
    size_type recent_index { 0 };

    [[nodiscard]] constexpr std::span<std::uint64_t const> bucket_hashes(std::uint32_t const bucket) const noexcept
    {
      size_type const begin = bucket_offsets[bucket];
      size_type const end   = bucket_offsets[bucket + 1U];
      return { hashes.data() + begin, end - begin };
    }

    [[nodiscard]] constexpr size_type bucket_length(std::uint32_t const bucket) const noexcept
    {
      return bucket_offsets[bucket + 1U] - bucket_offsets[bucket];
    }
  };

private:
  struct build_tag {};

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  constexpr ptr_hash(build_tag, Iterator first, Sentinel last, hasher hasher_arg)
      : hasher_ { std::move(hasher_arg) }
  {
    if (!try_build_impl_(first, last)) [[unlikely]] {
      throw_runtime_error_("construction failure");
    }
  }

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel, class Alloc>
  requires (!std::is_void_v<allocator_type>) && std::same_as<Alloc, allocator_type>
  constexpr ptr_hash(build_tag, Iterator first, Sentinel last, hasher hasher_arg, Alloc const &allocator)
      : hasher_ { std::move(hasher_arg) }
      , storage_ { allocator }
  {
    if (!try_build_impl_(first, last)) [[unlikely]] {
      throw_runtime_error_("construction failure");
    }
  }

public:
  constexpr ptr_hash() noexcept  = default;
  constexpr ~ptr_hash() noexcept = default;

  // ---------- iterator-range constructors ----------
  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires std::same_as<std::remove_cvref_t<std::iter_value_t<Iterator>>, key_type>
  constexpr ptr_hash(Iterator first, Sentinel last)
      : ptr_hash { build_tag {}, std::move(first), std::move(last), hasher {} }
  {
  }

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires std::same_as<std::remove_cvref_t<std::iter_value_t<Iterator>>, key_type>
  constexpr ptr_hash(Iterator first, Sentinel last, hasher hasher_arg)
      : ptr_hash { build_tag {}, std::move(first), std::move(last), std::move(hasher_arg) }
  {
  }

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel, class Alloc = allocator_type>
  requires (!std::is_void_v<allocator_type>) &&
           std::same_as<std::remove_cvref_t<std::iter_value_t<Iterator>>, key_type> &&
           std::same_as<Alloc, allocator_type>
  constexpr ptr_hash(Iterator first, Sentinel last, hasher hasher_arg, Alloc const &allocator)
      : ptr_hash { build_tag {}, std::move(first), std::move(last), std::move(hasher_arg), allocator }
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
  requires (!std::is_void_v<allocator_type>) && std::same_as<Alloc, allocator_type>
  constexpr ptr_hash(std::initializer_list<key_type> keys, hasher hasher_arg, Alloc const &allocator)
      : ptr_hash { keys.begin(), keys.end(), std::move(hasher_arg), allocator }
  {
  }

#ifdef __cpp_lib_containers_ranges
  // ---------- from_range constructors ----------
  template <std::ranges::forward_range Range>
  requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, key_type>
  constexpr explicit ptr_hash(std::from_range_t, Range &&keys)
      : ptr_hash { std::ranges::begin(keys), std::ranges::end(keys) }
  {
  }

  template <std::ranges::forward_range Range>
  requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, key_type>
  constexpr explicit ptr_hash(std::from_range_t, Range &&keys, hasher hasher_arg)
      : ptr_hash { std::ranges::begin(keys), std::ranges::end(keys), std::move(hasher_arg) }
  {
  }

  template <std::ranges::forward_range Range, class Alloc = allocator_type>
  requires (!std::is_void_v<allocator_type>) &&
           std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, key_type> &&
           std::same_as<Alloc, allocator_type>
  constexpr explicit ptr_hash(std::from_range_t, Range &&keys, hasher hasher_arg, Alloc const &allocator)
      : ptr_hash { std::ranges::begin(keys), std::ranges::end(keys), std::move(hasher_arg), allocator }
  {
  }
#endif

private:
  [[nodiscard]] static constexpr bool has_duplicate_value_(std::span<std::uint64_t const> const hashes) noexcept
  {
    for (size_type i { 1 }; i < hashes.size(); ++i) {
      if (hashes[i - 1] == hashes[i]) [[unlikely]] return true;
    }
    return false;
  }

  constexpr void build_remap_(occupied_slots_type const &occupied_slots)
  {
    if constexpr (opts.hash_scheme == ptr_hash_scheme::perfect) {
      storage_.clear_remap();
    } else {
      size_type const remapped_slot_count = slot_count_ - key_count_;
      if (remapped_slot_count == 0) [[unlikely]] {
        storage_.clear_remap();
        return;
      }

      // Entries for free high slots are never observed by a query, but keeping
      // them in range preserves the `index() < size()` guarantee for every input.
      slot_owner_type const fallback = key_count_ == 0 ? 0 : static_cast<slot_owner_type>(key_count_ - 1);
      storage_.prepare_remap(remapped_slot_count, fallback);

      // Free low slots and occupied high slots are equinumerous, so the two
      // cursors meet exactly once per occupied high slot without extra storage.
      size_type next_high_slot = key_count_;
      for (size_type low_slot = 0; low_slot < key_count_; ++low_slot) {
        if (occupied_slots[low_slot]) [[likely]] continue;
        assert(next_high_slot < slot_count_ && "free/occupied slot counts diverged");
        while (next_high_slot < slot_count_ && !occupied_slots[next_high_slot]) {
          ++next_high_slot;
        }
        assert(next_high_slot < slot_count_ && "free/occupied slot counts diverged");
        if (next_high_slot >= slot_count_) [[unlikely]] break;
        storage_.set_remap(next_high_slot - key_count_, static_cast<remap_type>(low_slot));
        ++next_high_slot;
      }
      storage_.shrink_remap();
    }
  }

  /// Places a bucket at the first pilot whose slots are all free and distinct.
  template <size_type BucketSize>
  [[nodiscard]] constexpr bool try_place_fixed_size_bucket_(
    std::uint32_t const bucket, std::uint64_t const *const bucket_hashes, bucket_placement_context &context)
  {
    std::array<size_type, BucketSize> slots;
    for (size_type pilot = 0; pilot < opts.pilot_search_limit; ++pilot) {
      std::uint64_t const pilot_hash = pilot_hash_value_(pilot);

      bool fits = true;
      for (size_type i = 0; i < BucketSize; ++i) {
        slots[i] = slot_index_(bucket_hashes[i], pilot_hash);
        fits     = fits && !context.occupied_slots[slots[i]];
      }
      if (!fits) [[likely]] continue;

      if constexpr (BucketSize > 1) {
        bool distinct = true;
        for (size_type i = 1; i < BucketSize; ++i) {
          for (size_type j = 0; j < i; ++j) {
            distinct = distinct && slots[i] != slots[j];
          }
        }
        if (!distinct) [[unlikely]] continue;
      }

      storage_.set_pilot(bucket, static_cast<pilot_type>(pilot));
      for (size_type i = 0; i < BucketSize; ++i) {
        context.occupied_slots[slots[i]] = true;
        context.slot_owners[slots[i]]    = bucket;
      }
      return true;
    }
    return false;
  }

  [[nodiscard]] constexpr bool try_place_bucket_(std::uint32_t const bucket, bucket_placement_context &context)
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

    size_type const bucket_size = bucket_hashes.size();
    assert(context.slot_scratch.size() >= bucket_size);
    for (size_type pilot = 0; pilot < opts.pilot_search_limit; ++pilot) {
      std::uint64_t const pilot_hash = pilot_hash_value_(pilot);

      bool fits = true;
      for (size_type i = 0; i < bucket_size; ++i) {
        context.slot_scratch[i] = slot_index_(bucket_hashes[i], pilot_hash);
        if (context.occupied_slots[context.slot_scratch[i]]) [[unlikely]] {
          fits = false;
          break;
        }
      }
      if (!fits) [[likely]] continue;

      // Marking doubles as the duplicate check for the rare large bucket.
      size_type marked = 0;
      bool distinct    = true;
      for (; marked < bucket_size; ++marked) {
        size_type const slot = context.slot_scratch[marked];
        if (context.occupied_slots[slot]) [[unlikely]] {
          distinct = false;
          break;
        }
        context.occupied_slots[slot] = true;
      }
      if (distinct) [[likely]] {
        storage_.set_pilot(bucket, static_cast<pilot_type>(pilot));
        for (size_type i = 0; i < bucket_size; ++i) {
          context.slot_owners[context.slot_scratch[i]] = bucket;
        }
        return true;
      }
      for (size_type i = 0; i < marked; ++i) {
        context.occupied_slots[context.slot_scratch[i]] = false;
      }
    }
    return false;
  }

  /// Places a bucket by evicting already placed buckets with the smallest
  /// squared collision score. Evicted buckets are retried through the heap.
  [[nodiscard]] constexpr bool try_place_bucket_with_evict_(std::uint32_t const bucket, bucket_placement_context &context)
  {
    std::span<std::uint64_t const> const bucket_hashes { context.bucket_hashes(bucket) };
    size_type const bucket_size { bucket_hashes.size() };
    assert(context.slot_scratch.size() >= bucket_size);

    // A random start breaks the cycle that two buckets repeatedly evicting
    // each other would otherwise create.
    size_type const first_pilot { static_cast<size_type>(context.random_engine() & (opts.pilot_search_limit - 1u)) };
    std::uint64_t best_collision_score { (std::numeric_limits<std::uint64_t>::max)() };
    size_type best_pilot { 0 };

    for (size_type delta = 0; delta < opts.pilot_search_limit; ++delta) {
      size_type const pilot          = (first_pilot + delta) & (opts.pilot_search_limit - 1u);
      std::uint64_t const pilot_hash = pilot_hash_value_(pilot);

      std::uint64_t collision_score { 0 };
      bool rejected { false };
      for (size_type i = 0; i < bucket_size; ++i) {
        size_type const slot    = slot_index_(bucket_hashes[i], pilot_hash);
        context.slot_scratch[i] = slot;

        if (!context.occupied_slots[slot]) [[likely]] continue;
        slot_owner_type const owner = context.slot_owners[slot];
        if (std::find(context.recent_buckets.begin(), context.recent_buckets.end(), owner) != context.recent_buckets.end()) [[unlikely]] {
          rejected = true;
          break;
        }
        size_type const owner_length = context.bucket_length(owner);
        std::uint64_t const owner_squared = static_cast<std::uint64_t>(owner_length) * owner_length;
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

      std::sort(context.slot_scratch.begin(), context.slot_scratch.begin() + static_cast<difference_type>(bucket_size));
      bool distinct = true;
      for (size_type i = 1; i < bucket_size; ++i) {
        distinct = distinct && context.slot_scratch[i - 1] != context.slot_scratch[i];
      }
      if (!distinct) [[unlikely]] continue;

      best_collision_score = collision_score;
      best_pilot           = pilot;
      if (collision_score == static_cast<std::uint64_t>(bucket_size) * bucket_size) [[unlikely]] break;
    }

    if (best_collision_score == (std::numeric_limits<std::uint64_t>::max)()) [[unlikely]] return false;

    storage_.set_pilot(bucket, static_cast<pilot_type>(best_pilot));
    std::uint64_t const pilot_hash = pilot_hash_value_(best_pilot);

    for (std::uint64_t const hash : bucket_hashes) {
      size_type const slot = slot_index_(hash, pilot_hash);
      if (context.occupied_slots[slot]) [[likely]] {
        slot_owner_type const owner = context.slot_owners[slot];
        ++context.eviction_count;
        context.eviction_heap.emplace_back(static_cast<std::uint32_t>(context.bucket_length(owner)), owner);
        std::push_heap(context.eviction_heap.begin(), context.eviction_heap.end());

        std::span<std::uint64_t const> const owner_hashes = context.bucket_hashes(owner);
        std::uint64_t const owner_pilot_hash              = pilot_hash_value_(storage_.pilot(owner));
        for (std::uint64_t const owner_hash : owner_hashes) {
          size_type const owner_slot         = slot_index_(owner_hash, owner_pilot_hash);
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
    storage_.prepare_pilots(bucket_count_);

    // Build the histogram in one linear scan, then turn counts into the
    // starting offset of every bucket with one prefix-sum pass.
    bucket_offsets_type bucket_offsets = storage_.make_bucket_offsets(bucket_count_ + 1U);
    for (size_type i = 0; i < hashes.size(); ++i) {
      ++bucket_offsets[bucket_index_(hashes[i]) + 1U];
    }
    for (size_type bucket = 1; bucket <= bucket_count_; ++bucket) {
      bucket_offsets[bucket] += bucket_offsets[bucket - 1U];
    }
    assert(bucket_offsets[bucket_count_] == hashes.size());

    // Counting sort of buckets by descending length, ties by ascending index.
    size_type max_bucket_length = 0;
    for (size_type bucket = 0; bucket < bucket_count_; ++bucket) {
      size_type const length = bucket_length_(bucket_offsets, bucket);
      if (length > max_bucket_length) [[unlikely]] max_bucket_length = length;
    }

    bucket_length_prefix_type bucket_length_prefix = storage_.make_bucket_length_prefix(max_bucket_length + 1U);
    for (size_type bucket = 0; bucket < bucket_count_; ++bucket) {
      ++bucket_length_prefix[bucket_length_(bucket_offsets, bucket)];
    }
    std::uint32_t order_offset = 0;
    for (size_type length = max_bucket_length + 1U; length-- > 0;) {
      std::uint32_t const count    = bucket_length_prefix[length];
      bucket_length_prefix[length] = order_offset;
      order_offset += count;
    }
    size_type const non_empty_bucket_count = bucket_length_prefix[0];

    bucket_order_type bucket_order = storage_.make_bucket_order(non_empty_bucket_count);
    for (size_type bucket = 0; bucket < bucket_count_; ++bucket) {
      size_type const length = bucket_length_(bucket_offsets, bucket);
      if (length != 0) [[likely]] bucket_order[bucket_length_prefix[length]++] = static_cast<std::uint32_t>(bucket);
    }

    occupied_slots_type occupied_slots = storage_.make_occupied_slots(slot_count_);

    {
      // The owner table is only read for occupied slots; leaving free entries
      // uninitialized removes a full-table memset from every build attempt.
      slot_owner_buffer_type slot_owners = storage_.make_slot_owners(slot_count_);
      eviction_heap_type eviction_heap   = storage_.make_eviction_heap();
      std::array<std::uint32_t, recent_bucket_capacity> recent_buckets;
      slot_scratch_type slot_scratch = storage_.make_slot_scratch(max_bucket_length);
      splitmix64 random_engine { hash_seed_ ^ 0x9E'37'79'B9'7F'4A'7C'15ULL };
      size_type eviction_count = 0;

      bucket_placement_context context {
        std::span<std::uint32_t const> { bucket_offsets.data(), bucket_offsets.size() },
        std::span<std::uint64_t const> { hashes.data(), hashes.size() },
        std::span<std::uint32_t> { slot_owners.data(), slot_owners.size() },
        occupied_slots,
        eviction_heap,
        recent_buckets,
        random_engine,
        eviction_count,
        std::span<size_type> { slot_scratch.data(), slot_scratch.size() }
      };

      // Largest buckets first: they are the hardest to place and the cheapest
      // to evict while most of the table is still empty.
      for (size_type order_index = 0; order_index < non_empty_bucket_count; ++order_index) {
        std::uint32_t const bucket  = bucket_order[order_index];
        size_type const bucket_size = context.bucket_hashes(bucket).size();

        eviction_heap.clear();
        eviction_count = 0;
        recent_buckets.fill(no_bucket);
        recent_buckets[0]    = bucket;
        context.recent_index = 0;

        eviction_heap.emplace_back(static_cast<std::uint32_t>(bucket_size), bucket);
        std::push_heap(eviction_heap.begin(), eviction_heap.end());

        while (!eviction_heap.empty()) {
          std::pop_heap(eviction_heap.begin(), eviction_heap.end());
          std::uint32_t const current_bucket = eviction_heap.back().second;
          eviction_heap.pop_back();

          // Only sample the limit at powers of two above the slot count,
          // which avoids the cost of a check on every heap pop while still
          // bounding the total eviction chain.
          if (eviction_count > slot_count_ && std::has_single_bit(eviction_count) &&
              eviction_count >= static_cast<size_type>(opts.max_evictions_per_slot) * slot_count_) [[unlikely]] {
            return false;
          }
          if (try_place_bucket_(current_bucket, context)) [[likely]] continue;
          if (opts.max_evictions_per_slot == 0) [[unlikely]] return false;
          if (!try_place_bucket_with_evict_(current_bucket, context)) [[unlikely]] return false;
        }
      }
    }

    build_remap_(occupied_slots);
    return true;
  }

  constexpr void reset_build_state_() noexcept
  {
    storage_.reset();
    key_count_       = 0;
    slot_count_      = 0;
    bucket_count_    = 0;
    slot_reciprocal_ = 0;
    hash_seed_       = 0;
  }

  template <class K>
  static constexpr bool hash_value_uses_seeded_call_v =
      std::same_as<std::remove_cv_t<hasher>, hash<key_type>> &&
      std::invocable<hasher const &, K const &, std::uint64_t>;

  template <class K>
  [[nodiscard]] constexpr std::uint64_t hash_value_impl_(K const &key) const
      noexcept(hash_value_uses_seeded_call_v<K>
                   ? std::is_nothrow_invocable_v<hasher const &, K const &, std::uint64_t>
                   : std::is_nothrow_invocable_v<hasher const &, K const &>)
  {
    // hash<T> intentionally omits its final avalanche multiply; the default
    // PtrHash path restores it here. Custom hashers keep their own semantics.
    if constexpr (hash_value_uses_seeded_call_v<K>) {
      return static_cast<std::uint64_t>(hasher_(static_cast<key_type>(key),
                                                static_cast<std::uint64_t>(hash_seed_))) * kPhiMixMultConstant;
    } else {
      return static_cast<std::uint64_t>(hasher_(static_cast<key_type>(key))) ^ hash_seed_;
    }
  }

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  [[nodiscard]] constexpr bool try_build_impl_(Iterator first, Sentinel last)
  {
    size_type const key_count = static_cast<size_type>(std::ranges::distance(first, last));
    if (key_count == 0) [[unlikely]] {
      reset_build_state_();
      return true;
    }

    if (key_count > storage_type::max_key_count) [[unlikely]] {
      throw_length_error_();
    }

    double const slot_count_estimate = static_cast<double>(key_count) / opts.slot_load_factor;
    double const quotient { static_cast<double>(key_count) / opts.average_bucket_size };
    size_type const truncated { static_cast<size_type>(quotient) };
    size_type const bucket_count { (static_cast<double>(truncated) < quotient ? truncated + 1U : truncated) + 3U };

    if (slot_count_estimate >= static_cast<double>((std::numeric_limits<size_type>::max)()) ||
        static_cast<size_type>(slot_count_estimate) > storage_type::max_slot_count ||
        bucket_count > (std::numeric_limits<std::uint32_t>::max)()) [[unlikely]] {
      throw_length_error_();
    }

    key_count_  = key_count;
    slot_count_ = static_cast<size_type>(slot_count_estimate);
    // a power-of-two slot count would make the low hash bits dominate the
    // multiply-range reduction; the extra slot keeps all bits in play.
    if (std::has_single_bit(slot_count_)) [[unlikely]] ++slot_count_;
    slot_reciprocal_ = slot_count_ == 0 ? 0 : (std::numeric_limits<std::uint64_t>::max)() / slot_count_ + 1U;
    bucket_count_    = bucket_count;

    hash_buffer_type hashes = storage_.make_hash_buffer(key_count_);
    // Fixed-capacity storage has no allocator-backed fallback, so it always
    // provides the explicit radix buffer instead of letting the sorter allocate.
    bool const uses_external_radix_buffer = is_fixed_capacity || key_count_ > stable_radix_sort_detail::kRadixSortThreshold;
    hash_buffer_type radix_sort_buffer = storage_.make_hash_buffer(uses_external_radix_buffer ? key_count_ : size_type { 0 });

    splitmix64 seed_eng(161803); // PI: 314159 / PHI: 161803
    for (size_type attempt { 0 }; attempt < opts.max_build_attempts; ++attempt) {
      hash_seed_ = seed_eng();

      auto current = first;
      for (size_type i { 0 }; i < key_count_; ++i, ++current) {
        hashes[i] = hash_value(*current);
      }
      if (uses_external_radix_buffer) [[likely]] {
        stable_radix_sort<radix_sort_options {}>(
            hashes.data(),
            hashes.data() + key_count_,
            std::span<std::uint64_t> { radix_sort_buffer.data(), radix_sort_buffer.size() });
      } else {
        if constexpr (!std::is_same_v<allocator_type, void>) {
          stable_radix_sort<radix_sort_options {}>(
              hashes.data(),
              hashes.data() + key_count_,
              typename std::allocator_traits<allocator_type>::template rebind_alloc<std::uint64_t> { storage_.get_allocator() });
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

public:
  [[nodiscard]] constexpr bool empty() const noexcept { return key_count_ == 0; }
  [[nodiscard]] constexpr size_type size() const noexcept { return key_count_; }
  [[nodiscard]] constexpr size_type capacity() const noexcept { return slot_count_; }
  [[nodiscard]] constexpr size_type bucket_count() const noexcept { return bucket_count_; }
  [[nodiscard]] constexpr std::uint64_t hash_seed() const noexcept { return hash_seed_; }
  [[nodiscard]] constexpr hasher hash_function() const noexcept { return hasher_; }

  [[nodiscard]] constexpr allocator_type get_allocator() const noexcept
  requires (!std::is_same_v<allocator_type, void>)
  {
    if constexpr (!std::is_same_v<allocator_type, void>) {
      return storage_.get_allocator();
    }
  }

  [[nodiscard]] constexpr size_type index_unchecked(key_type const &key) const
  {
    assert(!empty() && "stdex::ptr_hash::index_unchecked called on an empty table");
    return index_from_hash(hash_value(key));
  }

  template <class K>
  requires (!std::same_as<std::remove_cvref_t<K>, key_type>) && ptr_hash_detail::key_hasher<hasher, K>
  [[nodiscard]] constexpr size_type index_unchecked(K const &key) const
  {
    assert(!empty() && "stdex::ptr_hash::index_unchecked called on an empty table");
    return index_from_hash(hash_value(key));
  }

  [[nodiscard]] constexpr std::uint64_t hash_value(key_type const &key) const noexcept(std::is_nothrow_invocable_v<hasher const &, key_type const &>)
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
    pilot_type const pilot     = storage_.pilot(bucket_index_(hash));
    size_type const slot_index = slot_index_(hash, pilot_hash_value_(pilot));
    if constexpr (opts.hash_scheme == ptr_hash_scheme::minimal_perfect) {
      return slot_index < key_count_ ? slot_index : static_cast<size_type>(storage_.remap(slot_index - key_count_));
    } else {
      return slot_index;
    }
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
  [[nodiscard]] constexpr size_type slot_index_(std::uint64_t const hash, std::uint64_t const pilot_hash) const noexcept
  {
    assert(slot_count_ != 0);
    std::uint64_t const low_bits = slot_reciprocal_ * (hash ^ pilot_hash);
    return static_cast<size_type>(umul128(low_bits, slot_count_).hi());
  }

  [[nodiscard]] constexpr size_type bucket_index_(std::uint64_t const hash) const noexcept
  {
    return static_cast<size_type>(umul128(bucket_count_, transform_bucket_hash_(hash)).hi());
  }

  template <class Offsets>
  [[nodiscard]] static constexpr size_type bucket_length_(Offsets const &bucket_offsets, std::size_t const bucket) noexcept
  {
    return bucket_offsets[bucket + 1] - bucket_offsets[bucket];
  }

  [[nodiscard]] constexpr std::uint64_t pilot_hash_value_(std::uint64_t const pilot) const noexcept
  {
    return kPiMixMultConstant * (pilot ^ hash_seed_);
  }

  [[nodiscard]] constexpr std::uint64_t transform_bucket_hash_(std::uint64_t const hash) const noexcept
  {
    static_assert(opts.bucket_distribution == ptr_hash_bucket_distribution::linear ||
                  opts.bucket_distribution == ptr_hash_bucket_distribution::cubic_eps);

    if constexpr (opts.bucket_distribution == ptr_hash_bucket_distribution::linear) {
      return hash;
    } else {
      std::uint64_t const squared = umul128(hash, hash).hi();
      std::uint64_t const cubic   = umul128(squared, (hash >> 1U) | (std::uint64_t { 1 } << 63U)).hi();
      return (cubic / 256U) * 255U + hash / 256U;
    }
  }

  [[noreturn]] void throw_length_error_()
  {
#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
    throw std::length_error("length exceeds 2^32 - 1");
#else
    std::abort();
#endif
  }

  [[noreturn]] void throw_runtime_error_(char const *description)
  {
#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
    throw std::runtime_error(description);
#else
    std::abort();
#endif
  }

private:
  [[no_unique_address]] hasher hasher_ {};
  storage_type storage_ {};
  size_type key_count_ {};
  size_type slot_count_ {};
  size_type bucket_count_ {};
  std::uint64_t slot_reciprocal_ {};
  std::uint64_t hash_seed_ {};
};
} // namespace stdex
