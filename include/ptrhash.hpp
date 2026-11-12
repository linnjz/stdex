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

struct ptr_hash_options
{
  ptr_hash_scheme hash_scheme { ptr_hash_scheme::perfect };
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

  /// Parameters for the default fast variant (perfect hash fn, linear, average_bucket_size = 3.0).
  [[nodiscard]] static constexpr ptr_hash_options profile_fast() noexcept { return {}; }

  /// Parameters for the balanced variant (minimal perfect hash fn, cubic_eps, average_bucket_size = 3.5).
  [[nodiscard]] static constexpr ptr_hash_options profile_balanced() noexcept
  {
    return {
      .hash_scheme         = ptr_hash_scheme::minimal_perfect,
      .bucket_distribution = ptr_hash_bucket_distribution::cubic_eps,
      .average_bucket_size = 3.5
    };
  }

  /// Parameters for the compact variant (minimal perfect hash fn, cubic_eps, average_bucket_size = 3.9).
  [[nodiscard]] static constexpr ptr_hash_options profile_compact() noexcept
  {
    return {
      .hash_scheme         = ptr_hash_scheme::minimal_perfect,
      .bucket_distribution = ptr_hash_bucket_distribution::cubic_eps,
      .average_bucket_size = 3.9
    };
  }
};
struct ptr_hash_fast     { static constexpr ptr_hash_options value = ptr_hash_options::profile_fast();     };
struct ptr_hash_balanced { static constexpr ptr_hash_options value = ptr_hash_options::profile_balanced(); };
struct ptr_hash_compact  { static constexpr ptr_hash_options value = ptr_hash_options::profile_compact();  };

namespace ptr_hash_detail
{
template <typename Hasher, typename Key>
concept key_hasher = std::regular_invocable<Hasher const &, Key const &> &&
                     std::convertible_to<std::invoke_result_t<Hasher const &, Key const &>, std::uint64_t>;

template <typename Alloc>
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
} // namespace ptr_hash_detail

template <class Key, class Hasher = hash<Key>, class Options = ptr_hash_fast, class Allocator = std::allocator<Key>>
requires ptr_hash_detail::key_hasher<Hasher, Key> && ptr_hash_detail::standard_allocator<Allocator> && std::same_as<std::remove_cv_t<decltype(Options::value)>, ptr_hash_options>
class ptr_hash
{
public:
  using key_type        = Key;
  using hasher          = Hasher;
  using allocator_type  = Allocator;
  using value_type      = Key;
  using size_type       = std::size_t;
  using difference_type = std::ptrdiff_t;

private:
  template <class T> using rebind_alloc_t = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
  template <class T> using rebind_vector_t = std::vector<T, rebind_alloc_t<T>>;
  template <class T> using rebind_uninitialized_buffer_t = ::stdex::uninitialized_buffer<T, rebind_alloc_t<T>>;
  using rebind_inlined_bitset_t = inlined_bitset<std::uint64_t, 0, rebind_alloc_t<std::uint64_t>>;

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
    rebind_vector_t<std::uint32_t> const &bucket_offsets;
    rebind_uninitialized_buffer_t<std::uint64_t> const &hashes;
    rebind_uninitialized_buffer_t<std::uint32_t> &slot_owners;
    rebind_inlined_bitset_t &occupied_slots;
    rebind_vector_t<std::pair<std::uint32_t, std::uint32_t>> &eviction_heap;
    std::array<std::uint32_t, recent_bucket_capacity> &recent_buckets;
    splitmix64 &random_engine;
    size_type &eviction_count;
    rebind_uninitialized_buffer_t<size_type> &slot_scratch;
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

public:
  static constexpr size_type npos { static_cast<size_type>(-1) };
  static constexpr ptr_hash_options opts { Options::value };
  static_assert(
      opts.max_build_attempts > std::uint16_t { 0 } &&
      (opts.slot_load_factor > 0.0 && opts.slot_load_factor < 1.0) &&
      opts.average_bucket_size >= 1.0);

  static_assert(std::has_single_bit(opts.pilot_search_limit), "pilot_search_limit must be a power of two");
  static_assert(opts.pilot_search_limit <= 256, "pilot_search_limit must not exceed 256: pilots_ stores std::uint8_t");

private:
  struct build_tag {};

  constexpr ptr_hash(build_tag, Hasher hasher, Allocator allocator)
      : hasher_ { std::move(hasher) }
      , allocator_ { std::move(allocator) }
      , pilots_ { rebind_alloc_t<std::uint8_t> { allocator_ } }
      , remap_ { rebind_alloc_t<std::uint32_t> { allocator_ } }
  {
  }

public:
  constexpr ptr_hash() noexcept  = default;
  constexpr ~ptr_hash() noexcept = default;

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires std::same_as<std::remove_cvref_t<std::iter_value_t<Iterator>>, Key>
  constexpr ptr_hash(Iterator first, Sentinel last, Hasher hasher = {}, Allocator allocator = {})
      : ptr_hash { build_tag {}, std::move(hasher), std::move(allocator) }
  {
    if (!try_build_impl_(first, last)) throw_runtime_error_("construction failure");
  }

  constexpr ptr_hash(std::initializer_list<Key> keys, Hasher hasher = {}, Allocator allocator = {})
      : ptr_hash { keys.begin(), keys.end(), std::move(hasher), std::move(allocator) }
  {
  }

  template <std::ranges::forward_range Range>
  requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, Key>
  constexpr explicit ptr_hash(Range &&keys, Hasher hasher = {}, Allocator allocator = {})
      : ptr_hash { std::ranges::begin(keys), std::ranges::end(keys), std::move(hasher), std::move(allocator) }
  {
  }

private:
  [[nodiscard]] static constexpr bool has_duplicate_value_(rebind_uninitialized_buffer_t<std::uint64_t> const &hashes) noexcept
  {
    for (size_type i { 1 }; i < hashes.size(); ++i) {
      if (hashes[i - 1] == hashes[i]) return true;
    }
    return false;
  }

  constexpr void build_remap_(rebind_inlined_bitset_t const &occupied_slots)
  {
    if constexpr (opts.hash_scheme == ptr_hash_scheme::perfect) {
      remap_.clear();
      remap_.shrink_to_fit();
    } else {
      size_type const remapped_slot_count = slot_count_ - key_count_;
      if (remapped_slot_count == 0) {
        remap_.clear();
        remap_.shrink_to_fit();
        return;
      }

      // Entries for free high slots are never observed by a query, but keeping
      // them in range preserves the `index() < size()` guarantee for every input.
      slot_owner_type const fallback = key_count_ == 0 ? 0 : static_cast<slot_owner_type>(key_count_ - 1);
      remap_.assign(remapped_slot_count, fallback);

      // Free low slots and occupied high slots are equinumerous, so the two
      // cursors meet exactly once per occupied high slot without extra storage.
      size_type next_high_slot = key_count_;
      for (size_type low_slot = 0; low_slot < key_count_; ++low_slot) {
        if (occupied_slots[low_slot]) continue;
        assert(next_high_slot < slot_count_ && "free/occupied slot counts diverged");
        while (next_high_slot < slot_count_ && !occupied_slots[next_high_slot]) {
          ++next_high_slot;
        }
        assert(next_high_slot < slot_count_ && "free/occupied slot counts diverged");
        if (next_high_slot >= slot_count_) break;
        remap_[next_high_slot - key_count_] = static_cast<slot_owner_type>(low_slot);
        ++next_high_slot;
      }
      remap_.shrink_to_fit();
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
      if (!fits) continue;

      if constexpr (BucketSize > 1) {
        bool distinct = true;
        for (size_type i = 1; i < BucketSize; ++i) {
          for (size_type j = 0; j < i; ++j) {
            distinct = distinct && slots[i] != slots[j];
          }
        }
        if (!distinct) continue;
      }

      pilots_[bucket] = static_cast<std::uint8_t>(pilot);
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
        if (context.occupied_slots[context.slot_scratch[i]]) {
          fits = false;
          break;
        }
      }
      if (!fits) continue;

      // Marking doubles as the duplicate check for the rare large bucket.
      size_type marked = 0;
      bool distinct    = true;
      for (; marked < bucket_size; ++marked) {
        size_type const slot = context.slot_scratch[marked];
        if (context.occupied_slots[slot]) {
          distinct = false;
          break;
        }
        context.occupied_slots[slot] = true;
      }
      if (distinct) {
        pilots_[bucket] = static_cast<std::uint8_t>(pilot);
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

        if (!context.occupied_slots[slot]) continue;
        slot_owner_type const owner = context.slot_owners[slot];
        if (std::find(context.recent_buckets.begin(), context.recent_buckets.end(), owner) != context.recent_buckets.end()) {
          rejected = true;
          break;
        }
        size_type const owner_length = context.bucket_length(owner);
        std::uint64_t const owner_squared = static_cast<std::uint64_t>(owner_length) * owner_length;
        if (collision_score > (std::numeric_limits<std::uint64_t>::max)() - owner_squared) {
          collision_score = (std::numeric_limits<std::uint64_t>::max)();
          rejected        = true;
          break;
        }
        collision_score += owner_squared;
        if (collision_score >= best_collision_score) {
          rejected = true;
          break;
        }
      }
      if (rejected) continue;

      std::sort(context.slot_scratch.begin(), context.slot_scratch.begin() + static_cast<difference_type>(bucket_size));
      bool distinct = true;
      for (size_type i = 1; i < bucket_size; ++i) {
        distinct = distinct && context.slot_scratch[i - 1] != context.slot_scratch[i];
      }
      if (!distinct) continue;

      best_collision_score = collision_score;
      best_pilot           = pilot;
      if (collision_score == static_cast<std::uint64_t>(bucket_size) * bucket_size) break;
    }

    if (best_collision_score == (std::numeric_limits<std::uint64_t>::max)()) return false;

    pilots_[bucket]                = static_cast<std::uint8_t>(best_pilot);
    std::uint64_t const pilot_hash = pilot_hash_value_(best_pilot);

    for (std::uint64_t const hash : bucket_hashes) {
      size_type const slot = slot_index_(hash, pilot_hash);
      if (context.occupied_slots[slot]) {
        slot_owner_type const owner = context.slot_owners[slot];
        ++context.eviction_count;
        context.eviction_heap.emplace_back(static_cast<std::uint32_t>(context.bucket_length(owner)), owner);
        std::push_heap(context.eviction_heap.begin(), context.eviction_heap.end());

        std::span<std::uint64_t const> const owner_hashes = context.bucket_hashes(owner);
        std::uint64_t const owner_pilot_hash              = pilot_hash_value_(pilots_[owner]);
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

  [[nodiscard]] constexpr bool try_build_from_sorted_hashes_(rebind_uninitialized_buffer_t<std::uint64_t> const &hashes)
  {
    pilots_.assign(bucket_count_, std::uint8_t { 0 });

    // Build the histogram in one linear scan, then turn counts into the
    // starting offset of every bucket with one prefix-sum pass.
    rebind_vector_t<std::uint32_t> bucket_offsets(bucket_count_ + 1U, std::uint32_t { 0 }, rebind_alloc_t<std::uint32_t> { allocator_ });
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
      if (length > max_bucket_length) max_bucket_length = length;
    }

    rebind_vector_t<std::uint32_t> bucket_length_prefix(max_bucket_length + 1U, std::uint32_t { 0 }, rebind_alloc_t<std::uint32_t> { allocator_ });
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

    rebind_uninitialized_buffer_t<std::uint32_t> bucket_order(non_empty_bucket_count, rebind_alloc_t<std::uint32_t> { allocator_ });
    for (size_type bucket = 0; bucket < bucket_count_; ++bucket) {
      size_type const length = bucket_length_(bucket_offsets, bucket);
      if (length != 0) bucket_order[bucket_length_prefix[length]++] = static_cast<std::uint32_t>(bucket);
    }

    rebind_inlined_bitset_t occupied_slots(slot_count_, rebind_alloc_t<std::uint64_t> { allocator_ });

    {
      // The owner table is only read for occupied slots; leaving free entries
      // uninitialized removes a full-table memset from every build attempt.
      rebind_uninitialized_buffer_t<std::uint32_t> slot_owners(slot_count_, rebind_alloc_t<std::uint32_t> { allocator_ });
      rebind_vector_t<std::pair<std::uint32_t, std::uint32_t>> eviction_heap(rebind_alloc_t<std::pair<std::uint32_t, std::uint32_t>> { allocator_ });
      std::array<std::uint32_t, recent_bucket_capacity> recent_buckets;
      rebind_uninitialized_buffer_t<size_type> slot_scratch(max_bucket_length, rebind_alloc_t<size_type> { allocator_ });
      splitmix64 random_engine { hash_seed_ ^ 0x9E'37'79'B9'7F'4A'7C'15ULL };
      size_type eviction_count = 0;

      bucket_placement_context context {
        bucket_offsets, hashes, slot_owners, occupied_slots, eviction_heap, recent_buckets, random_engine, eviction_count, slot_scratch
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
              eviction_count >= static_cast<size_type>(opts.max_evictions_per_slot) * slot_count_) {
            return false;
          }
          if (try_place_bucket_(current_bucket, context)) continue;
          if (opts.max_evictions_per_slot == 0) return false;
          if (!try_place_bucket_with_evict_(current_bucket, context)) return false;
        }
      }
    }

    build_remap_(occupied_slots);
    return true;
  }

  constexpr void reset_build_state_() noexcept
  {
    pilots_.clear();
    remap_.clear();
    key_count_       = 0;
    slot_count_      = 0;
    bucket_count_    = 0;
    slot_reciprocal_ = 0;
    hash_seed_       = 0;
  }

  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  [[nodiscard]] constexpr bool try_build_impl_(Iterator first, Sentinel last)
  {
    size_type const key_count = static_cast<size_type>(std::ranges::distance(first, last));
    if (key_count > (std::numeric_limits<std::uint32_t>::max)()) {
      throw_length_error_();
    }
    if (key_count == 0) {
      reset_build_state_();
      return true;
    }

    double const slot_count_estimate = static_cast<double>(key_count) / opts.slot_load_factor;
    if (slot_count_estimate >= static_cast<double>((std::numeric_limits<size_type>::max)())) {
      throw_length_error_();
    }

    double const quotient { static_cast<double>(key_count) / opts.average_bucket_size };
    size_type const truncated { static_cast<size_type>(quotient) };
    size_type const bucket_count { (static_cast<double>(truncated) < quotient ? truncated + 1U : truncated) + 3U };
    if (bucket_count > (std::numeric_limits<std::uint32_t>::max)()) {
      throw_length_error_();
    }

    key_count_  = key_count;
    slot_count_ = static_cast<size_type>(slot_count_estimate);
    // a power-of-two slot count would make the low hash bits dominate the
    // multiply-range reduction; the extra slot keeps all bits in play.
    if (std::has_single_bit(slot_count_)) ++slot_count_;
    slot_reciprocal_ = slot_count_ == 0 ? 0 : (std::numeric_limits<std::uint64_t>::max)() / slot_count_ + 1U;
    bucket_count_    = bucket_count;

    rebind_uninitialized_buffer_t<std::uint64_t> hashes(key_count_, rebind_alloc_t<std::uint64_t> { allocator_ });
    bool const uses_external_radix_buffer = key_count_ > stable_radix_sort_detail::kRadixSortThreshold;
    rebind_uninitialized_buffer_t<std::uint64_t> radix_sort_buffer(
        uses_external_radix_buffer ? key_count_ : size_type { 0 }, rebind_alloc_t<std::uint64_t> { allocator_ });

    splitmix64 seed_eng(161803); // PI: 314159 / PHI: 161803
    for (size_type attempt { 0 }; attempt < opts.max_build_attempts; ++attempt) {
      hash_seed_ = seed_eng();

      auto current = first;
      for (size_type i { 0 }; i < key_count_; ++i, ++current) {
        hashes[i] = hash_key_(*current);
      }
      if (uses_external_radix_buffer) {
        stable_radix_sort<radix_sort_options {}>(
            hashes.data(),
            hashes.data() + key_count_,
            std::span<std::uint64_t> { radix_sort_buffer.data(), radix_sort_buffer.size() });
      } else {
        stable_radix_sort<radix_sort_options {}>(hashes.data(), hashes.data() + key_count_, rebind_alloc_t<std::uint64_t> { allocator_ });
      }

      if (has_duplicate_value_(hashes)) continue;
      if (try_build_from_sorted_hashes_(hashes)) return true;
    }
    reset_build_state_();
    return false;
  }

public:
  template <std::forward_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
  requires std::same_as<std::remove_cvref_t<std::iter_value_t<Iterator>>, Key>
  [[nodiscard]] constexpr bool try_build(Iterator first, Sentinel last)
  {
    return try_build_impl_(first, last);
  }

  [[nodiscard]] constexpr bool try_build(std::initializer_list<Key> keys)
  {
    return try_build(keys.begin(), keys.end());
  }

  template <std::ranges::forward_range Range>
  requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<Range>>, Key>
  [[nodiscard]] constexpr bool try_build(Range &&keys)
  {
    return try_build(std::ranges::begin(keys), std::ranges::end(keys));
  }

public:
  [[nodiscard]] constexpr bool empty() const noexcept { return key_count_ == 0; }
  [[nodiscard]] constexpr size_type size() const noexcept { return key_count_; }
  [[nodiscard]] constexpr size_type capacity() const noexcept { return slot_count_; }
  [[nodiscard]] constexpr size_type bucket_count() const noexcept { return bucket_count_; }
  [[nodiscard]] constexpr std::uint64_t hash_seed() const noexcept { return hash_seed_; }
  [[nodiscard]] constexpr hasher hash_function() const noexcept { return hasher_; }
  [[nodiscard]] constexpr allocator_type get_allocator() const noexcept { return allocator_; }

  [[nodiscard]] constexpr size_type index_unchecked(Key const &key) const
  {
    assert(!empty() && "stdex::ptr_hash::index_unchecked called on an empty table");
    std::uint64_t const hash   = hash_key_(key);
    std::uint8_t const pilot   = pilots_[bucket_index_(hash)];
    size_type const slot_index = slot_index_(hash, pilot_hash_value_(pilot));
    if constexpr (opts.hash_scheme == ptr_hash_scheme::minimal_perfect) {
      return slot_index < key_count_ ? slot_index : static_cast<size_type>(remap_[slot_index - key_count_]);
    } else {
      return slot_index;
    }
  }

  [[nodiscard]] constexpr size_type index(Key const &key) const
  {
    if (empty()) return npos;
    return index_unchecked(key);
  }

private:
  [[nodiscard]] constexpr std::uint64_t hash_key_(Key const &key) const noexcept(std::is_nothrow_invocable_v<Hasher const &, Key const &>)
  {
    // hash<T> intentionally omits its final avalanche multiply; the default
    // PtrHash path restores it here. Custom hashers keep their own semantics.
    if constexpr (std::same_as<std::remove_cv_t<Hasher>, hash<Key>>) {
      return static_cast<std::uint64_t>(hasher_(key, hash_seed_)) * kPhiMixMultConstant;
    } else {
      return static_cast<std::uint64_t>(hasher_(key)) ^ hash_seed_;
    }
  }

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

  [[nodiscard]] constexpr size_type bucket_length_(rebind_vector_t<std::uint32_t> const &bucket_offsets, std::size_t const bucket) const noexcept
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
  [[no_unique_address]] Hasher hasher_ {};
  [[no_unique_address]] Allocator allocator_ {};
  rebind_vector_t<std::uint8_t> pilots_ { rebind_alloc_t<std::uint8_t> { allocator_ } };
  rebind_vector_t<std::uint32_t> remap_ { rebind_alloc_t<std::uint32_t> { allocator_ } };
  size_type key_count_ {};
  size_type slot_count_ {};
  size_type bucket_count_ {};
  std::uint64_t slot_reciprocal_ {};
  std::uint64_t hash_seed_ {};
};
} // namespace stdex
