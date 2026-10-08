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

#include <algorithm>        // std::min
#include <bit>              // std::popcount
#include <concepts>         // std::convertible_to, std::unsigned_integral
#include <cstdlib>          // std::abort
#include <initializer_list> // std::initializer_list
#include <iosfwd>           // std::basic_istream, std::basic_ostream
#include <iterator>         // std::input_iterator, std::iter_reference_t, std::iter_value_t
#include <memory>           // std::allocator
#include <limits>           // std::numeric_limits
#include <ranges>           // std::from_range_t, std::ranges::input_range, std::ranges::range_reference_t, std::ranges::range_value_t
#include <stdexcept>        // std::overflow_error
#include <string>           // std::basic_string, std::char_traits
#include <string_view>      // std::basic_string_view
#include <type_traits>      // std::remove_cv_t, std::remove_cvref_t

#include "inline_storage.hpp"

namespace stdex
{
// ---------- block-construction tag ----------
// Untagged ctors are bit-wise by default; taking from_blocks opts into block-wise.
struct from_blocks_t
{
  explicit constexpr from_blocks_t() noexcept = default;
};

inline constexpr from_blocks_t from_blocks {};

template <std::unsigned_integral Block = uint64_t, size_t N = 0, class Allocator = std::allocator<Block>>
class inlined_bitset : private inline_storage<Block, N, Allocator>
{
  using base = inline_storage<Block, N, Allocator>;

public:
  using block_type = typename base::value_type;
  using value_type = bool;
  using typename base::allocator_traits;
  using typename base::allocator_type;

  using typename base::const_pointer;
  using typename base::difference_type;
  using typename base::pointer;
  using typename base::size_type;
  
  using const_reference = bool;

  class reference
  {
    friend inlined_bitset;

  public:
    constexpr reference(reference const &) noexcept = default;
    constexpr reference(reference &&) noexcept      = default;
    constexpr ~reference() noexcept                 = default;

    constexpr reference &operator= (reference const &other) noexcept
    {
      return *this = static_cast<bool>(other);
    }

    constexpr reference &operator= (reference &&other) noexcept
    {
      return *this = static_cast<bool>(other);
    }

    constexpr reference &operator= (bool const val) noexcept
    {
      if (val) {
        *block_ |= single_bit_mask_;
      } else {
        *block_ &= static_cast<block_type>(~single_bit_mask_);
      }
      return *this;
    }

    constexpr reference &operator&= (bool val) noexcept
    {
      if (!val) {
        *block_ &= static_cast<block_type>(~single_bit_mask_);
      }
      return *this;
    }

    constexpr reference &operator|= (bool val) noexcept
    {
      if (val) {
        *block_ |= single_bit_mask_;
      }
      return *this;
    }

    constexpr reference &operator^= (bool val) noexcept
    {
      if (val) {
        *block_ ^= single_bit_mask_;
      }
      return *this;
    }

    constexpr reference &operator-= (bool val) noexcept
    {
      return *this &= static_cast<bool>(!val);
    }

    constexpr void operator& () = delete;

    [[nodiscard]] constexpr bool operator~() const noexcept
    {
      return (*block_ & single_bit_mask_) == 0;
    }

    [[nodiscard]] constexpr operator bool() const noexcept
    {
      return (*block_ & single_bit_mask_) != 0;
    }

    constexpr reference &set() noexcept
    {
      *block_ |= single_bit_mask_;
      return *this;
    }

    constexpr reference &reset() noexcept
    {
      *block_ &= static_cast<block_type>(~single_bit_mask_);
      return *this;
    }

    constexpr reference &flip() noexcept
    {
      *block_ ^= single_bit_mask_;
      return *this;
    }

    constexpr reference &assign(bool val) noexcept
    {
      return *this = val;
    }

    friend constexpr void swap(reference lhs, reference rhs) noexcept
    {
      bool const tmp = static_cast<bool>(lhs);
      lhs            = static_cast<bool>(rhs);
      rhs            = tmp;
    }

    friend constexpr void swap(reference lhs, bool &rhs) noexcept
    {
      bool const tmp = static_cast<bool>(lhs);
      lhs            = rhs;
      rhs            = tmp;
    }

    friend constexpr void swap(bool &lhs, reference rhs) noexcept
    {
      bool const tmp = lhs;
      lhs            = static_cast<bool>(rhs);
      rhs            = tmp;
    }

  private:
    constexpr reference(block_type *block, size_type bit_pos) noexcept
        : block_ { block }
        , single_bit_mask_ { static_cast<block_type>(block_type { 1 } << (bit_pos % bits_per_block)) }
    {
    }

    block_type *block_;
    block_type single_bit_mask_;
  };

  class iterator
  {
  };

  class const_iterator
  {
  };
  using reverse_iterator       = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  using block_iterator               = typename base::iterator;
  using block_const_iterator         = typename base::const_iterator;
  using block_reverse_iterator       = typename base::reverse_iterator;
  using block_const_reverse_iterator = typename base::const_reverse_iterator;

  using base::inline_capacity;

  static constexpr size_type bits_per_block = std::numeric_limits<block_type>::digits;
  static_assert(std::has_single_bit(bits_per_block), "bits_per_block must be a power of two.");
  static constexpr size_type npos = static_cast<size_type>(-1);

private:
  using base::throw_length_error_;
  using base::max_size_;
  using base::get_flag_;
  using base::set_size_;
  using base::allocate_unchecked_;
  using base::construct_elements_unchecked_;
  using base::destroy_elements_unchecked_;
  using base::promote_to_heap_;
  using base::try_construct_stack_n_unchecked_;
  using base::construct_heap_n_unchecked_;
  using base::append_uncounted_range_;

  using base::copy_construct_from_;
  using base::move_construct_from_;
  using base::copy_assignment_from_;
  using base::move_assignment_from_;

  static constexpr size_type blocks_for_bits_(size_type bit_count) noexcept
  {
    return bit_count / bits_per_block + (bit_count % bits_per_block != 0);
  }

  static constexpr size_type checked_block_count_(size_type block_count, allocator_type const &alloc)
  {
    if (block_count > max_size_(alloc) / bits_per_block) {
      throw_overflow_error_();
    }
    return block_count;
  }

  static constexpr block_type bit_mask_(size_type bit_pos) noexcept
  {
    return static_cast<block_type>(block_type { 1 } << (bit_pos % bits_per_block));
  }

  static constexpr block_type low_bits_mask_(size_type n) noexcept
  {
    return static_cast<block_type>((block_type { 1 } << n) - block_type { 1 });
  }

  constexpr void clear_tail_bits_(size_type nbits, size_type nblocks) noexcept
  {
    if (size_type const r = nbits % bits_per_block; r != 0) {
      data()[nblocks - 1] &= low_bits_mask_(r);
    }
  }

  constexpr void clear_tail_bits_(size_type nbits) noexcept
  {
    clear_tail_bits_(nbits, blocks_for_bits_(nbits));
  }

public:
  // ---------- five rules ----------
  constexpr inlined_bitset() noexcept : base { allocator_type {} }  { }

  constexpr inlined_bitset(inlined_bitset const &other)
      : base { allocator_traits::select_on_container_copy_construction(other.get_allocator()) }
  {
    copy_construct_from_(other);
    bit_size_ = other.bit_size_;
  }

  constexpr inlined_bitset(inlined_bitset const& other, allocator_type const& alloc)
      : base { alloc }
  {
    copy_construct_from_(other);
    bit_size_ = other.bit_size_;
  }

  constexpr inlined_bitset(inlined_bitset &&other) noexcept(noexcept(move_construct_from_(std::move(other))))
      : base { std::move(other.alloc_) }
  {
    if (move_construct_from_(std::move(other)) == base::move_outcome::heap_steal) {
      bit_size_ = std::exchange(other.bit_size_, size_type {});
    } else {
      bit_size_ = other.bit_size_;
    }
  }

  constexpr inlined_bitset(inlined_bitset &&other, allocator_type const &alloc)
      : base { alloc }
  {
    if (move_construct_from_(std::move(other), alloc) == base::move_outcome::heap_steal) {
      bit_size_ = std::exchange(other.bit_size_, size_type {});
    } else {
      bit_size_ = other.bit_size_;
    }
  }

  constexpr inlined_bitset& operator= (inlined_bitset const& other)
  {
    copy_assignment_from_(other);
    bit_size_ = other.bit_size_;
    return *this;
  }

  constexpr inlined_bitset& operator= (inlined_bitset&& other)
  {
    if (move_assignment_from_(std::move(other)) == base::move_outcome::heap_steal) {
      bit_size_ = std::exchange(other.bit_size_, size_type {});
    } else {
      bit_size_ = other.bit_size_;
    }
    return *this;
  }

  // ---------- init ----------
  constexpr explicit inlined_bitset(allocator_type const &alloc)  : base { alloc }  { }

  constexpr explicit inlined_bitset(size_type nbits, allocator_type const &alloc = allocator_type {})
      : base { blocks_for_bits_(nbits), alloc }
      , bit_size_ { nbits }
  {
  }

  constexpr inlined_bitset(from_blocks_t, size_type nblocks, allocator_type const &alloc = allocator_type {})
      : base { checked_block_count_(nblocks, alloc), alloc }
      , bit_size_ { nblocks * bits_per_block }
  {
  }

  constexpr inlined_bitset(size_type nbits, bool init_val, allocator_type const &alloc = allocator_type {})
      : base { blocks_for_bits_(nbits), block_type { 0 } - static_cast<block_type>(init_val), alloc }
      , bit_size_ { nbits }
  {
    if (init_val) clear_tail_bits_(nbits, block_count());
  }

  constexpr inlined_bitset(from_blocks_t, size_type nblocks, block_type init_val, allocator_type const &alloc = allocator_type {})
      : base { checked_block_count_(nblocks, alloc), init_val, alloc }
      , bit_size_ { nblocks * bits_per_block }
  {
  }

private:
  /*
  std::uint64_t pack64_avx2(const std::uint8_t* src) {
      __m256i a = _mm256_loadu_si256((const __m256i*)src);
      __m256i b = _mm256_loadu_si256((const __m256i*)(src + 32));
      a = _mm256_slli_epi16(a, 7);
      b = _mm256_slli_epi16(b, 7);
      return std::uint64_t(_mm256_movemask_epi8(a)) | (std::uint64_t(_mm256_movemask_epi8(b)) << 32);
  }
  */
  template <std::input_iterator InputIt>
  requires std::convertible_to<std::iter_reference_t<InputIt>, bool>
  static constexpr block_type pack_bits_to_block_(InputIt &src, size_type const nbits_in_block)
  {
    block_type block {};
    for (size_type b = 0; b < nbits_in_block; ++b, ++src) {
      block |= static_cast<block_type>(static_cast<bool>(*src)) << b;
    }
    return block;
  }

  template <std::input_iterator InputIt>
  requires std::convertible_to<std::iter_reference_t<InputIt>, bool>
  constexpr bool try_construct_stack_from_bits_unchecked_(InputIt src, size_type nbits)
  {
    if constexpr (inline_capacity == 0) {
      return false;
    } else {
      size_type const nblocks = blocks_for_bits_(nbits);
      if (nblocks > inline_capacity) return false;

      // Stack union members are not operator-new-backed, so P0593 implicit
      // creation does not apply: always construct_at (folds to a store below).
      // 
      // TODO
      // Maybe in C++26: once std::start_lifetime is available, we can begin the
      // lifetime of the union's block storage directly and enable a fast path
      // mirroring the heap branch, skipping explicit construction.
      uninitialized_backout<block_type *> block_guard { this->stack_.elements_ };
      size_type const full_blocks = nbits / bits_per_block;
      size_type const tail_bits   = nbits % bits_per_block;
      for (size_type b = 0; b < full_blocks; ++b) {
        block_guard.emplace_back(pack_bits_to_block_(src, bits_per_block));
      }
      if (tail_bits != 0) {
        block_guard.emplace_back(pack_bits_to_block_(src, tail_bits));
      }
      block_guard.release();

      set_size_(nblocks, base::stack_flag);
      bit_size_ = nbits;
      return true;
    }
  }

  template <std::input_iterator InputIt>
  requires std::convertible_to<std::iter_reference_t<InputIt>, bool>
  constexpr void construct_heap_from_bits_unchecked_(InputIt src, size_type nbits)
  {
    size_type const nblocks = blocks_for_bits_(nbits);
    auto [new_block, new_capacity] = allocate_unchecked_(nblocks);
    typename base::allocation_guard alloc_guard { new_block, new_capacity, this->alloc_ };
    block_type *const raw_new_block = std::to_address(new_block);

    size_type const full_blocks = nbits / bits_per_block;
    size_type const tail_bits   = nbits % bits_per_block;

    // P0593R6 fast path: implicit-lifetime Block in allocate() storage
    // needs no explicit construct at run time with a default allocator.
    if ((!has_custom_construct<allocator_type, block_type *, block_type>) && !std::is_constant_evaluated()) {
      for (size_type b = 0; b < full_blocks; ++b) {
        raw_new_block[b] = pack_bits_to_block_(src, bits_per_block);
      }
      if (tail_bits != 0) {
        raw_new_block[full_blocks] = pack_bits_to_block_(src, tail_bits);
      }
    } else {
      // General path: custom allocator construct, and constant evaluation.
      // RAII backout rolls back the constructed prefix if *src throws.
      uninitialized_backout_al<allocator_type, block_type *> block_guard { raw_new_block, this->alloc_ };
      for (size_type b = 0; b < full_blocks; ++b) {
        block_guard.emplace_back(pack_bits_to_block_(src, bits_per_block));
      }
      if (tail_bits != 0) {
        block_guard.emplace_back(pack_bits_to_block_(src, tail_bits));
      }
      (void) block_guard.release();
    }
    alloc_guard.dismiss();

    this->heap_.elements_ptr_ = new_block;
    this->heap_.capacity_     = new_capacity;
    set_size_(nblocks, base::heap_flag);
    bit_size_ = nbits;
  }

  template <class CharT, class Traits>
  static constexpr block_type pack_string_to_block_(
      std::basic_string_view<CharT, Traits> str,
      typename std::basic_string_view<CharT, Traits>::size_type pos,
      typename std::basic_string_view<CharT, Traits>::size_type len,
      size_type const block_index, CharT const zero, CharT const one)
  {
    using view_size = typename std::basic_string_view<CharT, Traits>::size_type;
    block_type value {};
    size_type const lo = block_index * bits_per_block;
    size_type const hi = (std::min) (lo + bits_per_block, static_cast<size_type>(len));
    for (size_type i = lo; i < hi; ++i) {
      view_size const k = static_cast<view_size>(i);
      CharT const c     = str[pos + (len - view_size { 1 } - k)];
      if (c != zero && c != one) throw_invalid_argument_();
      value |= static_cast<block_type>(c == one) << (i - lo);
    }
    return value;
  }

  template <class CharT, class Traits>
  constexpr bool try_construct_stack_from_string_unchecked_(
      std::basic_string_view<CharT, Traits> str,
      typename std::basic_string_view<CharT, Traits>::size_type pos,
      typename std::basic_string_view<CharT, Traits>::size_type len, CharT const zero, CharT const one)
  {
    if constexpr (inline_capacity == 0) {
      return false;
    } else {
      size_type const nbits   = static_cast<size_type>(len);
      size_type const nblocks = blocks_for_bits_(nbits);
      if (nblocks > inline_capacity) return false;

      uninitialized_backout<block_type *> block_guard { this->stack_.elements_ };
      for (size_type b = 0; b < nblocks; ++b) {
        block_guard.emplace_back(pack_string_to_block_(str, pos, len, b, zero, one));
      }
      (void) block_guard.release();

      set_size_(nblocks, base::stack_flag);
      bit_size_ = nbits;
      return true;
    }
  }

  template <class CharT, class Traits>
  constexpr void construct_heap_from_string_unchecked_(
      std::basic_string_view<CharT, Traits> str,
      typename std::basic_string_view<CharT, Traits>::size_type pos,
      typename std::basic_string_view<CharT, Traits>::size_type len, CharT const zero, CharT const one)
  {
    size_type const nbits   = static_cast<size_type>(len);
    size_type const nblocks = blocks_for_bits_(nbits);
    auto [new_block, new_capacity] = allocate_unchecked_(nblocks);
    typename base::allocation_guard alloc_guard { new_block, new_capacity, this->alloc_ };
    block_type *const raw_new_block = std::to_address(new_block);

    // Fast path stays throw-safe without a backout: pack throws during the
    // value computation strictly before its block's store, so the stored
    // prefix holds whole implicitly-created trivial objects needing no
    // destruction, and alloc_guard frees the block on unwind.
    if ((!has_custom_construct<allocator_type, block_type *, block_type>) && !std::is_constant_evaluated()) {
      for (size_type b = 0; b < nblocks; ++b) {
        raw_new_block[b] = pack_string_to_block_(str, pos, len, b, zero, one);
      }
    } else {
      uninitialized_backout_al<allocator_type, block_type *> block_guard { raw_new_block, this->alloc_ };
      for (size_type b = 0; b < nblocks; ++b) {
        block_guard.emplace_back(pack_string_to_block_(str, pos, len, b, zero, one));
      }
      (void) block_guard.release();
    }
    alloc_guard.dismiss();

    this->heap_.elements_ptr_ = new_block;
    this->heap_.capacity_     = new_capacity;
    set_size_(nblocks, base::heap_flag);
    bit_size_ = nbits;
  }

public:
  // ---------- iterator-range (bit-wise) ----------
  template <std::input_iterator InputIt>
  requires std::convertible_to<std::iter_reference_t<InputIt>, bool>
  constexpr inlined_bitset(InputIt first, InputIt last, allocator_type const &alloc = allocator_type {})
      : base { alloc }
  {
    if constexpr (std::forward_iterator<InputIt>) {
      auto const nbits = static_cast<size_type>(std::distance(first, last));
      if (nbits > max_size()) [[unlikely]] throw_length_error_();
      if (nbits == 0) [[unlikely]] return;

      if constexpr (inline_capacity != 0) {
        if (try_construct_stack_from_bits_unchecked_(first, nbits)) return;
        promote_to_heap_();
      }
      construct_heap_from_bits_unchecked_(first, nbits);
    } else {
      for (; first != last; ++first) {
        push_back(static_cast<bool>(*first));
      }
    }
  }

  // ---------- initializer_list (bit-wise) ----------
  template <class T>
  requires std::convertible_to<std::remove_cv_t<T>, bool>
  constexpr inlined_bitset(std::initializer_list<T> ilist, allocator_type const &alloc = allocator_type {})
      : inlined_bitset { ilist.begin(), ilist.end(), alloc }
  {
  }

#if defined(__cpp_lib_containers_ranges)
  // ---------- from_range (bit-wise) ----------
  template <std::ranges::input_range Rng>
  requires std::convertible_to<std::ranges::range_reference_t<Rng>, bool>
  constexpr inlined_bitset(std::from_range_t, Rng&& rng, allocator_type const& alloc = allocator_type{})
      : inlined_bitset { std::ranges::begin(rng), std::ranges::end(rng), alloc }
  {
  }
#endif

  // ---------- iterator-range (block-wise) ----------
  template <std::input_iterator InputIt>
  requires std::unsigned_integral<std::remove_cvref_t<std::iter_value_t<InputIt>>> 
        && std::convertible_to<std::iter_reference_t<InputIt>, block_type>
  constexpr inlined_bitset(from_blocks_t, InputIt first, InputIt last, allocator_type const &alloc = allocator_type {})
      : base { alloc }
  {
    if constexpr (std::forward_iterator<InputIt>) {
      auto const n = static_cast<size_type>(std::distance(first, last));
      if (checked_block_count_(n) == 0) [[unlikely]] return;

      if constexpr (inline_capacity != 0) {
        if (try_construct_stack_n_unchecked_(n, first, last)) return;
        promote_to_heap_();
      }
      construct_heap_n_unchecked_(n, first, last);
      bit_size_ = n * bits_per_block;
    } else {
      append_uncounted_range_(first, last);
      bit_size_ = block_count() * bits_per_block;
    }
  }

  // ---------- initializer_list (block-wise) ----------
  template <std::unsigned_integral T>
  requires std::convertible_to<T, block_type>
  constexpr inlined_bitset(from_blocks_t, std::initializer_list<T> ilist, allocator_type const &alloc = allocator_type {})
      : inlined_bitset { from_blocks, ilist.begin(), ilist.end(), alloc }
  {
  }

  // ---------- from_range (block-wise) ----------
#if defined(__cpp_lib_ranges)
  template <std::ranges::input_range Rng>
  requires std::unsigned_integral<std::remove_cvref_t<std::ranges::range_value_t<Rng>>> && std::convertible_to<std::ranges::range_reference_t<Rng>, block_type>
  constexpr inlined_bitset(from_blocks_t, Rng &&rng, allocator_type const &alloc = allocator_type {})
      : inlined_bitset { from_blocks, std::ranges::begin(rng), std::ranges::end(rng), alloc }
  {
  }
#endif

  // ---------- string (std::bitset shape: last char is bit 0) ----------
  template <class CharT, class Traits>
  constexpr explicit inlined_bitset(
      std::basic_string_view<CharT, Traits> str,
      typename std::basic_string_view<CharT, Traits>::size_type pos = 0,
      typename std::basic_string_view<CharT, Traits>::size_type n   = std::basic_string_view<CharT, Traits>::npos,
      CharT zero                                                    = CharT('0'),
      CharT one                                                     = CharT('1'),
      allocator_type const &alloc                                   = allocator_type {})
      : base { alloc }
  {
    using view_size = typename std::basic_string_view<CharT, Traits>::size_type;
    if (pos > str.size()) throw_out_of_range_();

    view_size const len = (std::min) (n, str.size() - pos);
    if (len > static_cast<view_size>(max_size())) throw_length_error_();
    if (len == 0) return;

    if constexpr (inline_capacity != 0) {
      if (try_construct_stack_from_string_unchecked_(str, pos, len, zero, one)) return;
      promote_to_heap_();
    }
    construct_heap_from_string_unchecked_(str, pos, len, zero, one);
  }

  template <class CharT, class Traits, class StrAlloc>
  constexpr explicit inlined_bitset(
      std::basic_string<CharT, Traits, StrAlloc> const &str,
      typename std::basic_string<CharT, Traits, StrAlloc>::size_type pos = 0,
      typename std::basic_string<CharT, Traits, StrAlloc>::size_type n   = std::basic_string<CharT, Traits, StrAlloc>::npos,
      CharT zero                                                         = CharT('0'),
      CharT one                                                          = CharT('1'),
      allocator_type const &alloc                                        = allocator_type {})
      : inlined_bitset { std::basic_string_view<CharT, Traits> { str.data(), str.size() }, pos, n, zero, one, alloc }
  {
  }

  template <class CharT, class Traits = std::char_traits<CharT>>
  constexpr explicit inlined_bitset(
      CharT const *str,
      typename std::basic_string<CharT>::size_type pos = 0,
      typename std::basic_string<CharT>::size_type n   = std::basic_string<CharT>::npos,
      CharT zero                                       = CharT('0'),
      CharT one                                        = CharT('1'),
      allocator_type const &alloc                      = allocator_type {})
      : inlined_bitset { std::basic_string_view<CharT, Traits> { str, Traits::length(str) }, pos, n, zero, one, alloc }
  {
  }

  template <class T>
  requires std::same_as<T, bool>
  constexpr inlined_bitset &operator= (std::initializer_list<T> list)
  {
    assign(list.begin(), list.end());
    return *this;
  }

  template <std::unsigned_integral T>
  requires (!std::same_as<T, bool>)
  constexpr inlined_bitset &operator= (std::initializer_list<T> list)
  {
    assign(from_blocks, list.begin(), list.end());
    return *this;
  }

  [[nodiscard]] constexpr iterator begin() noexcept;
  [[nodiscard]] constexpr const_iterator begin() const noexcept;
  [[nodiscard]] constexpr iterator end() noexcept;
  [[nodiscard]] constexpr const_iterator end() const noexcept;
  [[nodiscard]] constexpr reverse_iterator rbegin() noexcept;
  [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept;
  [[nodiscard]] constexpr reverse_iterator rend() noexcept;
  [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept;
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept;
  [[nodiscard]] constexpr const_iterator cend() const noexcept;
  [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept;
  [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept;

  [[nodiscard]] constexpr block_iterator block_begin() noexcept { return base::begin(); }
  [[nodiscard]] constexpr block_const_iterator block_begin() const noexcept { return base::begin(); }
  [[nodiscard]] constexpr block_iterator block_end() noexcept { return base::end(); }
  [[nodiscard]] constexpr block_const_iterator block_end() const noexcept { return base::end(); }
  [[nodiscard]] constexpr block_reverse_iterator block_rbegin() noexcept { return base::rbegin(); }
  [[nodiscard]] constexpr block_const_reverse_iterator block_rbegin() const noexcept { return base::rbegin(); }
  [[nodiscard]] constexpr block_reverse_iterator block_rend() noexcept { return base::rend(); }
  [[nodiscard]] constexpr block_const_reverse_iterator block_rend() const noexcept { return base::rend(); }
  [[nodiscard]] constexpr block_const_iterator block_cbegin() const noexcept { return base::cbegin(); }
  [[nodiscard]] constexpr block_const_iterator block_cend() const noexcept { return base::cend(); }
  [[nodiscard]] constexpr block_const_reverse_iterator block_crbegin() const noexcept { return base::crbegin(); }
  [[nodiscard]] constexpr block_const_reverse_iterator block_crend() const noexcept { return base::crend(); }

 public:
  using base::data;
  using base::get_allocator;

  using base::is_heap;
  using base::is_stack;

  using base::shrink_to_fit;

  [[nodiscard]] constexpr size_type block_count() const noexcept { return base::size(); }
  [[nodiscard]] constexpr size_type block_capacity() const noexcept { return base::capacity(); }
  [[nodiscard]] constexpr size_type block_max_size() const noexcept { return base::max_size(); }

  [[nodiscard]] constexpr bool empty() const noexcept { return bit_size_ == 0; }
  [[nodiscard]] constexpr size_type size() const noexcept { return bit_size_; }
  [[nodiscard]] constexpr size_type capacity() const noexcept { return block_capacity() * bits_per_block; }
  [[nodiscard]] constexpr size_type max_size() const noexcept 
  {
    size_type const max_blocks   = block_max_size();
    constexpr size_type max_bits = std::numeric_limits<size_type>::max();

    return max_blocks > (max_bits / bits_per_block) ? max_bits : max_blocks * bits_per_block;
  }

  [[nodiscard]] constexpr reference operator[] (size_type pos) noexcept
  {
    return reference(data() + pos / bits_per_block, pos);
  }

  [[nodiscard]] constexpr bool operator[] (size_type pos) const noexcept
  {
    return (data()[pos / bits_per_block] & bit_mask_(pos)) != 0;
  }

  [[nodiscard]] constexpr reference at(size_type pos)
  {
    if (pos >= bit_size_) throw_out_of_range_();
    return reference(data() + pos / bits_per_block, pos);
  }

  [[nodiscard]] constexpr bool at(size_type pos) const
  {
    if (pos >= bit_size_) throw_out_of_range_();
    return (data()[pos / bits_per_block] & bit_mask_(pos)) != 0;
  }

  [[nodiscard]] constexpr reference front() noexcept { return reference(data(), size_type { 0 }); }
  [[nodiscard]] constexpr bool front() const noexcept { return (data()[0] & block_type { 1 }) != 0; }

  [[nodiscard]] constexpr reference back() noexcept
  {
    return reference(data() + (bit_size_ - 1) / bits_per_block, bit_size_ - 1);
  }

  [[nodiscard]] constexpr bool back() const noexcept
  {
    return (data()[(bit_size_ - 1) / bits_per_block] & bit_mask_(bit_size_ - 1)) != 0;
  }

  [[nodiscard]] constexpr bool test(size_type pos) const
  {
    if (pos >= bit_size_) throw_out_of_range_();
    return (data()[pos / bits_per_block] & bit_mask_(pos)) != 0;
  }

  [[nodiscard]] constexpr bool test_set(size_type pos, bool value = true)
  {
    if (pos >= bit_size_) throw_out_of_range_();
    block_type *const block = data() + pos / bits_per_block;
    block_type const mask   = bit_mask_(pos);
    bool const old          = (*block & mask) != 0;
    if (value) {
      *block |= mask;
    } else {
      *block &= static_cast<block_type>(~mask);
    }
    return old;
  }

  [[nodiscard]] constexpr bool all() const noexcept
  {
    block_type const *const blocks = data();
    size_type const full           = bit_size_ / bits_per_block;
    for (size_type i = 0; i < full; ++i) {
      if (blocks[i] != static_cast<block_type>(~block_type { 0 })) return false;
    }
    if (size_type const r = bit_size_ % bits_per_block; r != 0) {
      if ((blocks[full] & low_bits_mask_(r)) != low_bits_mask_(r)) return false;
    }
    return true;
  }

  [[nodiscard]] constexpr bool any() const noexcept
  {
    // Padding is always zero, so the tail block needs no masking: nonzero
    // anywhere (full or tail) implies a set bit.
    block_type const *const blocks   = data();
    size_type const block_count_value = block_count();
    for (size_type i = 0; i < block_count_value; ++i) {
      if (blocks[i] != block_type { 0 }) return true;
    }
    return false;
  }
  [[nodiscard]] constexpr bool none() const noexcept { return !any(); }

  [[nodiscard]] constexpr size_type count() const noexcept
  {
    block_type const *const blocks = data();
    size_type total                = size_type { 0 };
    size_type const full           = bit_size_ / bits_per_block;
    for (size_type i = 0; i < full; ++i) {
      total += static_cast<size_type>(std::popcount(blocks[i]));
    }
    if (size_type const r = bit_size_ % bits_per_block; r != 0) {
      total += static_cast<size_type>(std::popcount(static_cast<block_type>(blocks[full] & low_bits_mask_(r))));
    }
    return total;
  }

private:
  /// Applies op(block, mask) over [pos, pos + len). Requires pos + len <= bit_size_;
  /// len == 0 is a no-op. Masks are whole-block for interior blocks.
  template <class Op>
  constexpr void apply_range_unchecked_(size_type pos, size_type len, Op op)
  {
    if (len == 0) [[unlikely]] return;
    block_type *const blocks    = data();
    size_type const first       = pos / bits_per_block;
    size_type const last        = (pos + len - 1) / bits_per_block;
    size_type const first_shift = pos % bits_per_block;
    size_type const last_r      = (pos + len) % bits_per_block;
    block_type const all_ones   = static_cast<block_type>(~block_type { 0 });
    block_type const first_mask = static_cast<block_type>(all_ones << first_shift);
    if (first == last) {
      op(blocks[first], last_r == 0 ? first_mask : static_cast<block_type>(first_mask & low_bits_mask_(last_r)));
      return;
    }
    op(blocks[first], first_mask);
    for (size_type i = first + 1; i < last; ++i) {
      op(blocks[i], all_ones);
    }
    op(blocks[last], last_r == 0 ? all_ones : low_bits_mask_(last_r));
  }

public:
  constexpr inlined_bitset &set() noexcept
  {
    size_type const n = block_count();
    if (n == 0) [[unlikely]] return *this;
    block_type *const blocks = data();
    std::fill_n(blocks, n - 1, static_cast<block_type>(~block_type { 0 }));
    size_type const r = bit_size_ % bits_per_block;
    blocks[n - 1]     = (r == 0) ? static_cast<block_type>(~block_type { 0 }) : low_bits_mask_(r);
    return *this;
  }

  constexpr inlined_bitset &set(size_type pos, bool value = true)
  {
    if (pos >= bit_size_) throw_out_of_range_();
    (*this)[pos] = value;
    return *this;
  }

  constexpr inlined_bitset &set(size_type pos, size_type len, bool value)
  {
    if (pos > bit_size_ || len > bit_size_ - pos) throw_out_of_range_();

    if (value) {
      apply_range_unchecked_(pos, len, [](block_type &block, block_type mask) constexpr noexcept { 
        block |= mask; 
      });
    } else {
      apply_range_unchecked_(pos, len, [](block_type &block, block_type mask) constexpr noexcept { 
        block &= static_cast<block_type>(~mask); 
      });
    }
    return *this;
  }

  constexpr inlined_bitset &reset() noexcept
  {
    std::fill_n(data(), block_count(), block_type { 0 });
    return *this;
  }

  constexpr inlined_bitset &reset(size_type pos)
  {
    if (pos >= bit_size_) throw_out_of_range_();
    (*this)[pos].reset();
    return *this;
  }

  constexpr inlined_bitset &reset(size_type pos, size_type len)
  {
    if (pos > bit_size_ || len > bit_size_ - pos) throw_out_of_range_();
    apply_range_unchecked_(pos, len, [](block_type &block, block_type mask) constexpr noexcept {
      block &= static_cast<block_type>(~mask);
    });
    return *this;
  }

  constexpr inlined_bitset &flip() noexcept
  {
    size_type const n = block_count();
    if (n == 0) [[unlikely]] return *this;

    block_type *const blocks = data();
    for (size_type i = 0; i < n - 1; ++i) {
      blocks[i] = static_cast<block_type>(~blocks[i]);
    }

    size_type const r = bit_size_ % bits_per_block;
    blocks[n - 1]     = (r == 0) ? static_cast<block_type>(~blocks[n - 1])
                                 : static_cast<block_type>(~blocks[n - 1] & low_bits_mask_(r));
    return *this;
  }

  constexpr inlined_bitset &flip(size_type pos)
  {
    if (pos >= bit_size_) throw_out_of_range_();
    (*this)[pos].flip();
    return *this;
  }

  constexpr inlined_bitset &flip(size_type pos, size_type len)
  {
    if (pos > bit_size_ || len > bit_size_ - pos) throw_out_of_range_();
    apply_range_unchecked_(pos, len, [](block_type &block, block_type mask) constexpr noexcept { 
      block ^= mask; 
    });
    return *this;
  }

  template <class... Args>
  requires std::constructible_from<bool, Args...>
  constexpr iterator emplace(size_type pos, Args &&...args);

  template <class... Args>
  requires std::constructible_from<bool, Args...>
  constexpr reference emplace_back(Args &&...args);

  constexpr void push_back(bool value);
  constexpr void pop_back() noexcept;

  constexpr void clear() noexcept
  {
    base::clear();
    bit_size_ = size_type {};
  }

  constexpr void reserve(size_type bit_capacity)
  {
    if (bit_capacity > max_size()) [[unlikely]] throw_length_error_();
    base::reserve(blocks_for_bits_(bit_capacity));
  }

  constexpr void resize(size_type bit_count, bool value = false)
  {
    if (bit_count > max_size()) [[unlikely]] throw_length_error_();

    size_type const old_bits   = bit_size_;
    size_type const old_blocks = block_count();
    size_type const new_blocks = blocks_for_bits_(bit_count);
    if (bit_count == old_bits) return;

    if (size_type const old_r; bit_count > old_bits && value && (old_r = old_bits % bits_per_block) != 0) {
      // Bits [old_bits, new tail start) live in the old tail block, which the
      // block-granular growth below never touches; OR them in now. Padding is
      // always zero, so growing with false needs no corresponding clear.
      size_type const new_r    = bit_count % bits_per_block;
      size_type const fill_end = (new_blocks == old_blocks && new_r != 0) ? new_r : bits_per_block;
      data()[old_blocks - 1] |= static_cast<block_type>(((block_type { 1 } << (fill_end - old_r)) - block_type { 1 }) << old_r);
    }

    base::resize(new_blocks, block_type { 0 } - static_cast<block_type>(value));

    // The new tail block may carry ones past bit_count (fill with true sets the
    // whole block; trimming exposes stale bits); mask them off so padding stays
    // zero and whole-block comparison stays valid.
    clear_tail_bits_(bit_count, new_blocks);

    bit_size_ = bit_count;
  }

  constexpr void append(std::initializer_list<bool> bits) { (void) insert(size(), bits); }

  template <std::input_iterator InputIt>
  requires std::convertible_to<std::iter_reference_t<InputIt>, bool>
  constexpr void append(InputIt first, InputIt last) { (void) insert(size(), first, last); }

#if defined(__cpp_lib_ranges)
  template <std::ranges::input_range Rng>
  requires std::convertible_to<std::ranges::range_reference_t<Rng>, bool>
  constexpr void append_range(Rng &&rng) { (void) insert_range(size(), std::forward<Rng>(rng)); }
#endif

  constexpr iterator insert(size_type pos, bool value);
  constexpr iterator insert(size_type pos, size_type count, bool value);

  template <std::input_iterator InputIt>
  requires std::convertible_to<std::iter_reference_t<InputIt>, bool>
  constexpr iterator insert(size_type pos, InputIt first, InputIt last);

  constexpr iterator insert(size_type pos, std::initializer_list<bool> ilist);

#if defined(__cpp_lib_ranges)
  template <std::ranges::input_range Rng>
  requires std::convertible_to<std::ranges::range_reference_t<Rng>, bool>
  constexpr iterator insert_range(size_type pos, Rng &&rng);
#endif

  constexpr void assign(size_type bit_count, bool value);
  constexpr void assign(std::initializer_list<bool> ilist);

  template <std::input_iterator InputIt>
  requires std::convertible_to<std::iter_reference_t<InputIt>, bool>
  constexpr void assign(InputIt first, InputIt last);

#if defined(__cpp_lib_ranges)
  template <std::ranges::input_range Rng>
  requires std::convertible_to<std::ranges::range_reference_t<Rng>, bool>
  constexpr void assign_range(Rng &&rng);
#endif

  constexpr iterator erase(size_type pos) noexcept;
  constexpr iterator erase(size_type first, size_type last) noexcept;

public:
  constexpr void resize(from_blocks_t, size_type nblocks, block_type value = block_type {})
  {
    if (nblocks > max_size() / bits_per_block) [[unlikely]] throw_length_error_();
    base::resize(nblocks, value);
    bit_size_ = nblocks * bits_per_block;
  }

  constexpr void append(from_blocks_t, block_type block) { (void) insert(from_blocks, block_count(), block); }

  template <std::unsigned_integral T>
  requires std::convertible_to<T, block_type>
  constexpr void append(from_blocks_t, std::initializer_list<T> blocks) { (void) insert(from_blocks, block_count(), blocks); }

  template <std::input_iterator InputIt>
  requires std::unsigned_integral<std::remove_cvref_t<std::iter_value_t<InputIt>>>
        && std::convertible_to<std::iter_reference_t<InputIt>, block_type>
  constexpr void append(from_blocks_t, InputIt first, InputIt last) { (void) insert(from_blocks, block_count(), first, last); }

#if defined(__cpp_lib_ranges)
  template <std::ranges::input_range Rng>
  requires std::unsigned_integral<std::remove_cvref_t<std::ranges::range_value_t<Rng>>>
        && std::convertible_to<std::ranges::range_reference_t<Rng>, block_type>
  constexpr void append_range(from_blocks_t, Rng &&rng) { (void) insert_range(from_blocks, block_count(), std::forward<Rng>(rng)); }
#endif
        
  constexpr block_iterator insert(from_blocks_t, size_type block_pos, block_type block);
  constexpr block_iterator insert(from_blocks_t, size_type block_pos, size_type block_count, block_type block);
  
  template <std::unsigned_integral T>
  requires std::convertible_to<T, block_type>
  constexpr block_iterator insert(from_blocks_t, size_type block_pos, std::initializer_list<T> ilist);

  template <std::input_iterator InputIt>
  requires std::unsigned_integral<std::remove_cvref_t<std::iter_value_t<InputIt>>> 
        && std::convertible_to<std::iter_reference_t<InputIt>, block_type>
  constexpr block_iterator insert(from_blocks_t, size_type block_pos, InputIt first, InputIt last);
        
#if defined(__cpp_lib_ranges)
  template <std::ranges::input_range Rng>
  requires std::unsigned_integral<std::remove_cvref_t<std::ranges::range_value_t<Rng>>> 
        && std::convertible_to<std::ranges::range_reference_t<Rng>, block_type>
  constexpr block_iterator insert_range(from_blocks_t, size_type block_pos, Rng &&rng);
#endif

  constexpr void assign(from_blocks_t, size_type block_count, block_type value);

  template <std::unsigned_integral T>
  requires std::convertible_to<T, block_type>
  constexpr void assign(from_blocks_t, std::initializer_list<T> ilist);
  
  template <std::input_iterator InputIt>
  requires std::unsigned_integral<std::remove_cvref_t<std::iter_value_t<InputIt>>> 
        && std::convertible_to<std::iter_reference_t<InputIt>, block_type>
  constexpr void assign(from_blocks_t, InputIt first, InputIt last);

#if defined(__cpp_lib_ranges)
  template <std::ranges::input_range Rng>
  requires std::unsigned_integral<std::remove_cvref_t<std::ranges::range_value_t<Rng>>> 
        && std::convertible_to<std::ranges::range_reference_t<Rng>, block_type>
  constexpr void assign_range(from_blocks_t, Rng &&rng);
#endif

  constexpr block_iterator erase(from_blocks_t, size_type block_pos, size_type block_count = 1) noexcept;

public:
  template <class CharT = char, class Traits = std::char_traits<CharT>, class StrAlloc = std::allocator<CharT>>
  [[nodiscard]] constexpr auto to_string(CharT zero = CharT('0'), CharT one = CharT('1')) const
      -> std::basic_string<CharT, Traits, StrAlloc>;

  [[nodiscard]] constexpr unsigned long to_ulong() const;
  [[nodiscard]] constexpr unsigned long long to_ullong() const;

  [[nodiscard]] constexpr bool is_subset_of(inlined_bitset const &other) const noexcept;
  [[nodiscard]] constexpr bool is_proper_subset_of(inlined_bitset const &other) const noexcept;
  [[nodiscard]] constexpr bool intersects(inlined_bitset const &other) const noexcept;

  [[nodiscard]] constexpr size_type find_first() const noexcept;             
  [[nodiscard]] constexpr size_type find_next(size_type prev) const noexcept; 
  [[nodiscard]] constexpr size_type find_last() const noexcept;              
  [[nodiscard]] constexpr size_type find_prev(size_type next) const noexcept;

  [[nodiscard]] constexpr size_type find_first_zero() const noexcept;
  [[nodiscard]] constexpr size_type find_next_zero(size_type prev) const noexcept;
  [[nodiscard]] constexpr size_type find_last_zero() const noexcept;
  [[nodiscard]] constexpr size_type find_prev_zero(size_type next) const noexcept;

  template <std::output_iterator<block_type> OutIt>
  constexpr OutIt copy_blocks(OutIt out) const;

  //[[nodiscard]] constexpr auto set_bits() const noexcept;   // view of positions where bit==1
  //[[nodiscard]] constexpr auto unset_bits() const noexcept; // view of positions where bit==0

public:
  [[nodiscard]] constexpr inlined_bitset operator& (inlined_bitset const &) const;
  [[nodiscard]] constexpr inlined_bitset operator| (inlined_bitset const &) const;
  [[nodiscard]] constexpr inlined_bitset operator^ (inlined_bitset const &) const;
  [[nodiscard]] constexpr inlined_bitset operator-() const;
  [[nodiscard]] constexpr inlined_bitset operator~() const;

  [[nodiscard]] constexpr inlined_bitset operator<< (size_type n) const;
  [[nodiscard]] constexpr inlined_bitset operator>> (size_type n) const;
  constexpr inlined_bitset &operator<<= (size_type n);
  constexpr inlined_bitset &operator>>= (size_type n);

  constexpr inlined_bitset &operator&= (inlined_bitset const &);
  constexpr inlined_bitset &operator|= (inlined_bitset const &);
  constexpr inlined_bitset &operator^= (inlined_bitset const &);

  constexpr inlined_bitset &operator-= (inlined_bitset const &);

  constexpr void swap(inlined_bitset &other) noexcept(noexcept(std::declval<base &>().swap(std::declval<base &>())))
  {
    base::swap(other);
    std::ranges::swap(bit_size_, other.bit_size_);
  }

  friend constexpr void swap(inlined_bitset &lhs, inlined_bitset &rhs) noexcept(noexcept(lhs.swap(rhs)))
  {
    lhs.swap(rhs);
  }

  friend constexpr bool operator== (inlined_bitset const &lhs, inlined_bitset const &rhs) noexcept
  {
    if (lhs.bit_size_ != rhs.bit_size_) return false;
    return lhs.storage_equal_(rhs);
  }

private:
  [[noreturn]] static void throw_overflow_error_()
  {
#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
    throw std::overflow_error("overflow in arithmetic type.");
#else
    std::abort();
#endif
  }

  [[noreturn]] static void throw_out_of_range_()
  {
#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
    throw std::out_of_range("invalid bitset position");
#else
    std::abort();
#endif
  }

  [[noreturn]] static void throw_invalid_argument_()
  {
#if defined(_CPPUNWIND) || defined(__EXCEPTIONS)
    throw std::invalid_argument("invalid bitset char");
#else
    std::abort();
#endif
  }

private:
  size_type bit_size_ {};
};

template <class CharT, class Traits, std::unsigned_integral Block, size_t N, class Allocator>
std::basic_ostream<CharT, Traits> &operator<< (std::basic_ostream<CharT, Traits> &os, inlined_bitset<Block, N, Allocator> const &bitset);

template <class CharT, class Traits, std::unsigned_integral Block, size_t N, class Allocator>
std::basic_istream<CharT, Traits> &operator>> (std::basic_istream<CharT, Traits> &is, inlined_bitset<Block, N, Allocator> &bitset);
} // namespace stdex
