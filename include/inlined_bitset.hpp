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
#include <cassert>          // assert
#include <concepts>         // std::convertible_to, std::unsigned_integral
#include <cstdint>          // std::uint32_t, std::uint64_t
#include <cstdlib>          // std::abort
#include <initializer_list> // std::initializer_list
#include <iosfwd>           // std::basic_istream, std::basic_ostream
#include <iterator>         // std::input_iterator, std::iter_reference_t, std::iter_value_t,
                            // std::random_access_iterator_tag, std::reverse_iterator
#include <limits>           // std::numeric_limits
#include <memory>           // std::allocator
#include <ranges>           // std::from_range_t, std::ranges::input_range, std::ranges::range_reference_t, std::ranges::range_value_t
#include <stdexcept>        // std::overflow_error
#include <string>           // std::basic_string, std::char_traits
#include <string_view>      // std::basic_string_view
#include <type_traits>      // std::remove_cv_t, std::remove_cvref_t
#include <utility>          // std::forward

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86) && _M_IX86_FP >= 2))
#  include <immintrin.h> // MSVC: every intrinsic is usable without /arch; dispatch is the caller's job
#  include <intrin.h>    // MSVC: __cpuid/__cpuidex/_xgetbv for runtime dispatch
#elif defined(__AVX2__)
#  include <immintrin.h> // _mm256_*: AVX2 target implies SSSE3 shuffle
#else
#  if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2)
#    include <emmintrin.h> // _mm_*: x64 always provides SSE2
#  endif
#  if defined(__SSSE3__)
#    include <tmmintrin.h> // _mm_shuffle_epi8
#  endif
#endif
#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && !defined(__ARM_BIG_ENDIAN)
#  include <arm_neon.h> // vld1q_u8, vceqq_u8, vcntq_u8
#endif

// https://learn.microsoft.com/en-us/cpp/build/reference/arch-x64?view=msvc-170
// https://learn.microsoft.com/en-us/cpp/preprocessor/predefined-macros?view=msvc-170
// MSVC never defines __SSE3__/__SSSE3__/__SSE4_* (not even with /arch:SSE4.2;
// only __AVX__/__AVX2__ exist, and only with /arch:AVX or /arch:AVX2), so code
// below that needs SSSE3/AVX2 selects its path at run time on MSVC x86/x64.
// Clang(-cl) and clang-based compilers also define _MSC_VER/_M_X64 on Windows
// but, unlike MSVC, reject intrinsics whose target feature is not enabled at
// compile time ("requires target feature 'avx2'"); they are excluded here and
// keep the __AVX2__/__SSSE3__ compile-time paths (their runtime-dispatch
// spelling would be __attribute__((target(...))), deliberately not used here).
#if defined(_MSC_VER) && !defined(__clang__) \
    && (defined(_M_X64) || defined(_M_AMD64) || (defined(_M_IX86) && _M_IX86_FP >= 2))
#  define STDEX_MSVC_RT_INSTRUCTION_SET_ARCHITECTURE_DISPATCH_ 1
#endif

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

template <std::unsigned_integral Block = std::uint64_t, std::size_t N = 0, class Allocator = std::allocator<Block>>
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

  class iterator;
  class const_iterator;

  using const_reference = bool;
  class reference
  {
    friend inlined_bitset;
    friend class iterator;

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
    friend class const_iterator;
    friend inlined_bitset;

  public:
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept  = std::random_access_iterator_tag;
    using value_type        = bool;
    using difference_type   = typename base::difference_type;
    using pointer           = void;
    using reference         = inlined_bitset::reference;

    constexpr iterator() noexcept = default;

    [[nodiscard]] constexpr reference operator*() const noexcept { return reference(blocks_ + pos_ / bits_per_block, pos_); }
    [[nodiscard]] constexpr reference operator[] (difference_type n) const noexcept { return *(*this + n); }

    constexpr iterator &operator++ () noexcept
    {
      ++pos_;
      return *this;
    }

    constexpr iterator operator++ (int) noexcept
    {
      iterator tmp = *this;
      ++pos_;
      return tmp;
    }

    constexpr iterator &operator-- () noexcept
    {
      --pos_;
      return *this;
    }

    constexpr iterator operator-- (int) noexcept
    {
      iterator tmp = *this;
      --pos_;
      return tmp;
    }

    constexpr iterator &operator+= (difference_type n) noexcept
    {
      pos_ = static_cast<size_type>(static_cast<difference_type>(pos_) + n);
      return *this;
    }

    constexpr iterator &operator-= (difference_type n) noexcept { return *this += -n; }

    [[nodiscard]] constexpr iterator operator+ (difference_type n) const noexcept
    {
      iterator tmp = *this;
      tmp += n;
      return tmp;
    }

    [[nodiscard]] constexpr iterator operator- (difference_type n) const noexcept
    {
      iterator tmp = *this;
      tmp -= n;
      return tmp;
    }

    [[nodiscard]] constexpr difference_type operator- (iterator const &other) const noexcept
    {
      return static_cast<difference_type>(pos_) - static_cast<difference_type>(other.pos_);
    }

    [[nodiscard]] friend constexpr iterator operator+ (difference_type n, iterator it) noexcept
    {
      it += n;
      return it;
    }

    constexpr bool operator== (iterator const &) const noexcept  = default;
    constexpr auto operator<=> (iterator const &) const noexcept = default;

  private:
    constexpr iterator(block_type *blocks, size_type pos) noexcept
        : blocks_ { blocks }
        , pos_ { pos }
    {
    }

    block_type *blocks_ = nullptr;
    size_type pos_      = size_type { 0 };
  };

  class const_iterator
  {
  public:
    using iterator_category = std::random_access_iterator_tag;
    using iterator_concept  = std::random_access_iterator_tag;
    using value_type        = bool;
    using difference_type   = typename base::difference_type;
    using pointer           = void;
    using reference         = bool;

    constexpr const_iterator() noexcept = default;

    constexpr const_iterator(iterator const &other) noexcept
        : blocks_ { other.blocks_ }
        , pos_ { other.pos_ }
    {
    }

    [[nodiscard]] constexpr reference operator* () const noexcept
    {
      return (blocks_[pos_ / bits_per_block] & bit_mask_(pos_)) != 0;
    }

    [[nodiscard]] constexpr reference operator[] (difference_type n) const noexcept { return *(*this + n); }

    constexpr const_iterator &operator++ () noexcept
    {
      ++pos_;
      return *this;
    }

    constexpr const_iterator operator++ (int) noexcept
    {
      const_iterator tmp = *this;
      ++pos_;
      return tmp;
    }

    constexpr const_iterator &operator-- () noexcept
    {
      --pos_;
      return *this;
    }

    constexpr const_iterator operator-- (int) noexcept
    {
      const_iterator tmp = *this;
      --pos_;
      return tmp;
    }

    constexpr const_iterator &operator+= (difference_type n) noexcept
    {
      pos_ = static_cast<size_type>(static_cast<difference_type>(pos_) + n);
      return *this;
    }

    constexpr const_iterator &operator-= (difference_type n) noexcept { return *this += -n; }

    [[nodiscard]] constexpr const_iterator operator+ (difference_type n) const noexcept
    {
      const_iterator tmp = *this;
      tmp += n;
      return tmp;
    }

    [[nodiscard]] constexpr const_iterator operator- (difference_type n) const noexcept
    {
      const_iterator tmp = *this;
      tmp -= n;
      return tmp;
    }

    [[nodiscard]] constexpr difference_type operator- (const_iterator const &other) const noexcept
    {
      return static_cast<difference_type>(pos_) - static_cast<difference_type>(other.pos_);
    }

    [[nodiscard]] friend constexpr const_iterator operator+ (difference_type n, const_iterator it) noexcept
    {
      it += n;
      return it;
    }

    constexpr bool operator== (const_iterator const &) const noexcept  = default;
    constexpr auto operator<=> (const_iterator const &) const noexcept = default;

  private:
    friend inlined_bitset;

    constexpr const_iterator(block_type const *blocks, size_type pos) noexcept
        : blocks_ { blocks }
        , pos_ { pos }
    {
    }

    block_type const *blocks_ = nullptr;
    size_type pos_            = size_type { 0 };
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
  using base::allocate_unchecked_;
  using base::append_uncounted_range_;
  using base::assign_counted_range_;
  using base::assign_range_;
  using base::assign_uncounted_range_;
  using base::construct_elements_unchecked_;
  using base::construct_heap_n_unchecked_;
  using base::destroy_element_unchecked_;
  using base::destroy_elements_unchecked_;
  using base::emplace_one_at_back_;
  using base::get_flag_;
  using base::insert_counted_range_;
  using base::insert_range_;
  using base::insert_uncounted_range_;
  using base::max_size_;
  using base::promote_to_heap_;
  using base::set_size_;
  using base::throw_length_error_;
  using base::try_construct_stack_n_unchecked_;

  using base::copy_assignment_from_;
  using base::copy_construct_from_;
  using base::move_assignment_from_;
  using base::move_construct_from_;

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

private:
  // ---------- SIMD byte kernels for any/all/count/pack ----------
  //
  // OR/AND/popcount summation are width-agnostic, so the hot loops run over the
  // raw byte stream (block_count * sizeof(block_type)); only tail masking and
  // bit deposit depend on bits_per_block. Every load is unaligned: stack
  // storage carries only alignof(block_type) alignment.
  //
  // Each kernel keeps the scalar loop for std::is_constant_evaluated()
  // (intrinsics are not constant expressions) and for targets without the ISA
  // extension, so any/all/count stay constexpr and pack keeps its throw.

#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && !defined(__ARM_BIG_ENDIAN)
  // NEON has no movemask: fold the 16 high bits with pairwise adds
  static int neon_movemask_(uint8x16_t v) noexcept
  {
    static constexpr unsigned char weights[16] = {
      1u << 0, 1u << 1, 1u << 2, 1u << 3, 1u << 4, 1u << 5, 1u << 6, 1u << 7,
      1u << 0, 1u << 1, 1u << 2, 1u << 3, 1u << 4, 1u << 5, 1u << 6, 1u << 7,
    };
    uint8x16_t const weighted = vandq_u8(vld1q_u8(weights), v);
    uint8x8x2_t const zipped  = vzip_u8(vget_low_u8(weighted), vget_high_u8(weighted));
    uint16x8_t const combined = vreinterpretq_u16_u8(vcombine_u8(zipped.val[0], zipped.val[1]));
#  if defined(__ARM_ARCH_ISA_A64)
    return static_cast<int>(vaddvq_u16(combined));
#  else
    std::uint64x2_t const summed = vpaddlq_u32(vpaddlq_u16(combined));
    return static_cast<int>(vgetq_lane_u64(summed, 0) + vgetq_lane_u64(summed, 1));
#  endif
  }
#endif

  // Bit j of a movemask addresses ascending byte j, while block bit (rel + W-1-j)
  // addresses ascending char s+j: the ones mask needs a bit reversal per chunk.
  static constexpr unsigned reverse_bits16_(unsigned v) noexcept
  {
    v = ((v & 0x55'55u) << 1u) | ((v >> 1u) & 0x55'55u);
    v = ((v & 0x33'33u) << 2u) | ((v >> 2u) & 0x33'33u);
    v = ((v & 0x0F'0Fu) << 4u) | ((v >> 4u) & 0x0F'0Fu);
    // v lives in a 32-bit lane: mask the byte swap down to 16 bits.
    return ((v << 8u) & 0xFF'00u) | ((v >> 8u) & 0x00'FFu);
  }

  static constexpr std::uint32_t reverse_bits32_(std::uint32_t v) noexcept
  {
    v = ((v & 0x55'55'55'55u) << 1u) | ((v >> 1u) & 0x55'55'55'55u);
    v = ((v & 0x33'33'33'33u) << 2u) | ((v >> 2u) & 0x33'33'33'33u);
    v = ((v & 0x0F'0F'0F'0Fu) << 4u) | ((v >> 4u) & 0x0F'0F'0F'0Fu);
    v = ((v & 0x00'FF'00'FFu) << 8u) | ((v >> 8u) & 0x00'FF'00'FFu);
    return (v << 16u) | (v >> 16u);
  }

#if defined(__AVX2__) || defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2)
  // Loads 16 chars, throws on any char outside {zero, one}, and reports the
  // ones mask (bit j set iff byte j equals one).
  static void pack16_sse_(unsigned char const *s, unsigned char zero, unsigned char one, unsigned &ones)
  {
    __m128i const v   = _mm_loadu_si128(reinterpret_cast<__m128i const *>(s));
    __m128i const z   = _mm_set1_epi8(static_cast<char>(zero));
    __m128i const o   = _mm_set1_epi8(static_cast<char>(one));
    unsigned const mz = static_cast<unsigned>(_mm_movemask_epi8(_mm_cmpeq_epi8(v, z)));
    unsigned const mo = static_cast<unsigned>(_mm_movemask_epi8(_mm_cmpeq_epi8(v, o)));
    if (((mz | mo) & 0xFF'FFu) != 0xFF'FFu) throw_invalid_argument_();
    ones = mo & 0xFF'FFu;
  }
#endif

#if defined(__AVX2__)
  static void pack32_avx2_(unsigned char const *s, unsigned char zero, unsigned char one, std::uint32_t &ones)
  {
    __m256i const v   = _mm256_loadu_si256(reinterpret_cast<__m256i const *>(s));
    __m256i const z   = _mm256_set1_epi8(static_cast<char>(zero));
    __m256i const o   = _mm256_set1_epi8(static_cast<char>(one));
    std::uint32_t const mz = static_cast<std::uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, z)));
    std::uint32_t const mo = static_cast<std::uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, o)));
    if ((mz | mo) != 0xFF'FF'FF'FFu) throw_invalid_argument_();
    ones = mo;
  }
#endif

#if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && !defined(__ARM_BIG_ENDIAN)
  static void pack16_neon_(unsigned char const *s, unsigned char zero, unsigned char one, unsigned &ones)
  {
    uint8x16_t const v  = vld1q_u8(s);
    uint8x16_t const mz = vceqq_u8(v, vdupq_n_u8(zero));
    uint8x16_t const mo = vceqq_u8(v, vdupq_n_u8(one));
    if ((static_cast<unsigned>(neon_movemask_(vorrq_u8(mz, mo))) & 0xFF'FFu) != 0xFF'FFu) throw_invalid_argument_();
    ones = static_cast<unsigned>(neon_movemask_(mo)) & 0xFF'FFu;
  }
#endif

  static constexpr bool any_blocks_scalar_(block_type const *blocks, size_type nblocks) noexcept
  {
    // Padding is always zero, so the tail block needs no masking: nonzero
    // anywhere (full or tail) implies a set bit.
    for (size_type i = 0; i < nblocks; ++i) {
      if (blocks[i] != block_type { 0 }) return true;
    }
    return false;
  }

  static constexpr bool any_blocks_(block_type const *blocks, size_type nblocks) noexcept
  {
    if (nblocks == 0) return false;
    if (!std::is_constant_evaluated()) {
      // Below ~256B the SIMD setup (gate math, broadcasts, movemasks) costs more
      // than the scalar scan it replaces (measured break-even ~150-200B on
      // MSVC/AVX2; 256B leaves margin); stay scalar.
      if (nblocks * static_cast<size_type>(sizeof(block_type)) < size_type { 256 }) {
        return any_blocks_scalar_(blocks, nblocks);
      }
      // Scalar peek at the first block: early-true inputs return without
      // paying any SIMD setup. The full scan below resumes past it.
      if (blocks[0] != block_type { 0 }) return true;
      ++blocks;
      --nblocks;
      if (nblocks == 0) return false;
#if defined(__AVX2__)
      unsigned char const *b = reinterpret_cast<unsigned char const *>(blocks);
      size_type n            = nblocks * static_cast<size_type>(sizeof(block_type));
      __m256i const zero256  = _mm256_setzero_si256();
      __m128i const zero128  = _mm_setzero_si128();
      while (n >= size_type { 32 }) {
        __m256i const v = _mm256_loadu_si256(reinterpret_cast<__m256i const *>(b));
        if (static_cast<std::uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, zero256))) != 0xFF'FF'FF'FFu) {
          return true;
        }
        b += 32;
        n -= size_type { 32 };
      }
      while (n >= size_type { 16 }) {
        __m128i const v = _mm_loadu_si128(reinterpret_cast<__m128i const *>(b));
        if (static_cast<std::uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(v, zero128))) != 0xFF'FFu) {
          return true;
        }
        b += 16;
        n -= size_type { 16 };
      }
      for (size_type i = 0; i < n; ++i) {
        if (b[i] != 0) return true;
      }
      return false;
#elif defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2)
      unsigned char const *b = reinterpret_cast<unsigned char const *>(blocks);
      size_type n            = nblocks * static_cast<size_type>(sizeof(block_type));
      __m128i const zero     = _mm_setzero_si128();
      while (n >= size_type { 16 }) {
        __m128i const v = _mm_loadu_si128(reinterpret_cast<__m128i const *>(b));
        if (static_cast<std::uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(v, zero))) != 0xFF'FFu) {
          return true;
        }
        b += 16;
        n -= size_type { 16 };
      }
      for (size_type i = 0; i < n; ++i) {
        if (b[i] != 0) return true;
      }
      return false;
#elif (defined(__ARM_NEON) || defined(__ARM_NEON__)) && !defined(__ARM_BIG_ENDIAN)
      unsigned char const *b = reinterpret_cast<unsigned char const *>(blocks);
      size_type n            = nblocks * static_cast<size_type>(sizeof(block_type));
      uint8x16_t const zero  = vdupq_n_u8(0);
      while (n >= size_type { 16 }) {
        if ((static_cast<unsigned>(neon_movemask_(vceqq_u8(vld1q_u8(b), zero))) & 0xFF'FFu) != 0xFF'FFu) {
          return true;
        }
        b += 16;
        n -= size_type { 16 };
      }
      for (size_type i = 0; i < n; ++i) {
        if (b[i] != 0) return true;
      }
      return false;
#else
      return any_blocks_scalar_(blocks, nblocks);
#endif
    }
    return any_blocks_scalar_(blocks, nblocks);
  }

  static constexpr bool all_blocks_(block_type const *blocks, size_type bit_size) noexcept
  {
    size_type const orig_full = bit_size / bits_per_block;
    size_type full            = orig_full;
    block_type const *p       = blocks;
    if (!std::is_constant_evaluated()) {
      // Sub-vector inputs stay scalar (all wins from 64B up, measured).
      // The gate implies orig_full >= 1, so p[0] below is a whole block and
      // the scalar peek is exact; the scan resumes past it (p[full] still
      // addresses the original tail block for the masked check at the end).
      if (orig_full * static_cast<size_type>(sizeof(block_type)) >= size_type { 64 }) {
        if (p[0] != static_cast<block_type>(~block_type { 0 })) return false;
        ++p;
        --full;
#if defined(__AVX2__)
        // Early exit per chunk keeps the sparse-false case as cheap as scalar.
        unsigned char const *b = reinterpret_cast<unsigned char const *>(p);
        size_type n            = full * static_cast<size_type>(sizeof(block_type));
        __m256i const all256   = _mm256_set1_epi8(static_cast<char>(0xFF));
        while (n >= size_type { 32 }) {
          __m256i const v = _mm256_loadu_si256(reinterpret_cast<__m256i const *>(b));
          if (static_cast<std::uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(v, all256))) != 0xFF'FF'FF'FFu) {
            return false;
          }
          b += 32;
          n -= size_type { 32 };
        }
        // Drained 32B chunks may leave a 16B remainder; SSE2 is always present.
        __m128i const all128 = _mm_set1_epi8(static_cast<char>(0xFF));
        while (n >= size_type { 16 }) {
          __m128i const v = _mm_loadu_si128(reinterpret_cast<__m128i const *>(b));
          if (static_cast<std::uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(v, all128))) != 0xFF'FFu) return false;
          b += 16;
          n -= size_type { 16 };
        }
        size_type const consumed = (full * static_cast<size_type>(sizeof(block_type)) - n) / static_cast<size_type>(sizeof(block_type));
        p += consumed;
        full -= consumed;
#elif defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2)
        unsigned char const *b = reinterpret_cast<unsigned char const *>(p);
        size_type n            = full * static_cast<size_type>(sizeof(block_type));
        __m128i const all      = _mm_set1_epi8(static_cast<char>(0xFF));
        while (n >= size_type { 16 }) {
          __m128i const v = _mm_loadu_si128(reinterpret_cast<__m128i const *>(b));
          if (static_cast<std::uint32_t>(_mm_movemask_epi8(_mm_cmpeq_epi8(v, all))) != 0xFF'FFu) return false;
          b += 16;
          n -= size_type { 16 };
        }
        size_type const consumed = (full * static_cast<size_type>(sizeof(block_type)) - n) / static_cast<size_type>(sizeof(block_type));
        p += consumed;
        full -= consumed;
#elif (defined(__ARM_NEON) || defined(__ARM_NEON__)) && !defined(__ARM_BIG_ENDIAN)
        unsigned char const *b = reinterpret_cast<unsigned char const *>(p);
        size_type n            = full * static_cast<size_type>(sizeof(block_type));
        uint8x16_t const all   = vdupq_n_u8(0xFFu);
        while (n >= size_type { 16 }) {
          if ((static_cast<unsigned>(neon_movemask_(vceqq_u8(vld1q_u8(b), all))) & 0xFF'FFu) != 0xFF'FFu) return false;
          b += 16;
          n -= size_type { 16 };
        }
        size_type const consumed = (full * static_cast<size_type>(sizeof(block_type)) - n) / static_cast<size_type>(sizeof(block_type));
        p += consumed;
        full -= consumed;
#endif
      }
    }
    for (size_type i = 0; i < full; ++i) {
      if (p[i] != static_cast<block_type>(~block_type { 0 })) return false;
    }
    if (size_type const r = bit_size % bits_per_block; r != 0) {
      if ((p[full] & low_bits_mask_(r)) != low_bits_mask_(r)) return false;
    }
    return true;
  }

#if defined(STDEX_MSVC_RT_INSTRUCTION_SET_ARCHITECTURE_DISPATCH_)
  // MSVC exposes no SSSE3/AVX2 step through __isa_available (its enum is
  // X86/SSE2/SSE42/AVX/ENFSTRG/AVX2/AVX512 only), so probe CPUID directly.
  // Cached process-wide; both helpers run before any vector instruction.
  static bool ssse3_available_() noexcept
  {
    // CPUID.1:ECX bit 9. No extended CPU/OS state involved, exact.
    static int const ecx = [] {
      int info[4] {};
      __cpuid(info, 1);
      return info[2];
    }();
    return (ecx & 0x2'00) != 0;
  }

  static bool avx2_available_() noexcept
  {
    // CPU gate (CPUID.1 ECX[27]=XSAVE, [28]=AVX; CPUID.7 EBX[5]=AVX2) plus OS
    // gate (XCR0[2:1]: XMM+YMM saved), mirroring the CRT initialization.
    static bool const available = [] {
      int info[4] {};
      __cpuid(info, 1);
      if ((info[2] & 0x18'00'00'00) != 0x18'00'00'00) return false;
      if ((_xgetbv(0) & 0x6u) != 0x6u) return false;
      __cpuidex(info, 7, 0);
      return (info[1] & 0x20) != 0;
    }();
    return available;
  }
#endif

#if defined(__AVX2__) || defined(__SSSE3__) || defined(STDEX_MSVC_RT_INSTRUCTION_SET_ARCHITECTURE_DISPATCH_) || \
    ((defined(__ARM_NEON) || defined(__ARM_NEON__)) && !defined(__ARM_BIG_ENDIAN) && defined(__ARM_ARCH_ISA_A64))
  // Nibble-shuffle popcount over whole-block bytes; the caller keeps the masked
  // tail scalar. AVX2 implies SSSE3 on GCC/Clang (signalled via __AVX2__); on
  // MSVC neither macro ever appears (default /O2), so the widest available
  // width is picked at run time (see ssse3/avx2_available_ above).
#  if defined(__AVX2__) || defined(STDEX_MSVC_RT_INSTRUCTION_SET_ARCHITECTURE_DISPATCH_)
  // Drains 32B chunks, advancing (b, n); the horizontal sum is the caller's.
  static void count_shuffle32_(unsigned char const *&b, size_type &n, __m256i &acc) noexcept
  {
    __m256i const lut = _mm256_broadcastsi128_si256(_mm_setr_epi8(0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4));
    __m256i const low = _mm256_set1_epi8(0x0F);
    __m256i const z   = _mm256_setzero_si256();
    acc               = _mm256_setzero_si256();
    while (n >= size_type { 32 }) {
      __m256i const v = _mm256_loadu_si256(reinterpret_cast<__m256i const *>(b));
      __m256i const c = _mm256_add_epi8(
          _mm256_shuffle_epi8(lut, _mm256_and_si256(v, low)),
          _mm256_shuffle_epi8(lut, _mm256_and_si256(_mm256_srli_epi16(v, 4), low)));
      acc             = _mm256_add_epi64(acc, _mm256_sad_epu8(c, z));
      b += 32;
      n -= size_type { 32 };
    }
    // The runtime-dispatch build around here is SSE2 codegen with inline VEX;
    // close the upper lane so later legacy-SSE code pays no transition stall.
    _mm256_zeroupper();
  }

  static size_type horiz_sum256_(__m256i acc) noexcept
  {
    alignas(32) std::uint64_t lanes[4] {};
    _mm256_store_si256(reinterpret_cast<__m256i *>(lanes), acc);
    return static_cast<size_type>(lanes[0] + lanes[1] + lanes[2] + lanes[3]);
  }
#  endif
#  if defined(__AVX2__) || defined(__SSSE3__) || defined(STDEX_MSVC_RT_INSTRUCTION_SET_ARCHITECTURE_DISPATCH_)
  // Drains 16B chunks, advancing (b, n); the horizontal sum is the caller's.
  static void count_shuffle16_(unsigned char const *&b, size_type &n, __m128i &acc) noexcept
  {
    __m128i const lut = _mm_setr_epi8(0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4);
    __m128i const low = _mm_set1_epi8(0x0F);
    __m128i const z   = _mm_setzero_si128();
    acc               = _mm_setzero_si128();
    while (n >= size_type { 16 }) {
      __m128i const v = _mm_loadu_si128(reinterpret_cast<__m128i const *>(b));
      __m128i const c = _mm_add_epi8(
          _mm_shuffle_epi8(lut, _mm_and_si128(v, low)),
          _mm_shuffle_epi8(lut, _mm_and_si128(_mm_srli_epi16(v, 4), low)));
      acc             = _mm_add_epi64(acc, _mm_sad_epu8(c, z));
      b += 16;
      n -= size_type { 16 };
    }
  }

  static size_type horiz_sum128_(__m128i acc) noexcept
  {
    alignas(16) std::uint64_t lanes[2] {};
    _mm_store_si128(reinterpret_cast<__m128i *>(lanes), acc);
    return static_cast<size_type>(lanes[0] + lanes[1]);
  }
#  endif

  static size_type count_full_bytes_(unsigned char const *b, size_type n) noexcept
  {
    size_type total = size_type { 0 };
#  if defined(__AVX2__)
    {
      __m256i acc;
      count_shuffle32_(b, n, acc);
      total += horiz_sum256_(acc);
    }
    {
      __m128i acc;
      count_shuffle16_(b, n, acc);
      total += horiz_sum128_(acc);
    }
#  elif defined(STDEX_MSVC_RT_INSTRUCTION_SET_ARCHITECTURE_DISPATCH_)
    if (avx2_available_()) {
      __m256i acc;
      count_shuffle32_(b, n, acc);
      total += horiz_sum256_(acc);
    }
    if (ssse3_available_()) {
      __m128i acc;
      count_shuffle16_(b, n, acc);
      total += horiz_sum128_(acc);
    }
#  elif defined(__SSSE3__)
    {
      __m128i acc;
      count_shuffle16_(b, n, acc);
      total += horiz_sum128_(acc);
    }
#  endif
#  if (defined(__ARM_NEON) || defined(__ARM_NEON__)) && !defined(__ARM_BIG_ENDIAN) && defined(__ARM_ARCH_ISA_A64)
    while (n >= size_type { 16 }) {
      total += static_cast<size_type>(vaddlvq_u8(vcntq_u8(vld1q_u8(b))));
      b += 16;
      n -= size_type { 16 };
    }
#  endif
    for (size_type i = 0; i < n; ++i) {
      total += static_cast<size_type>(std::popcount(b[i]));
    }
    return total;
  }
#endif

  static constexpr size_type count_blocks_(block_type const *blocks, size_type bit_size) noexcept
  {
    size_type const full = bit_size / bits_per_block;
    size_type total      = size_type { 0 };
    if (!std::is_constant_evaluated()) {
#if defined(__AVX2__) || defined(__SSSE3__) || defined(STDEX_MSVC_RT_INSTRUCTION_SET_ARCHITECTURE_DISPATCH_) || \
    ((defined(__ARM_NEON) || defined(__ARM_NEON__)) && !defined(__ARM_BIG_ENDIAN) && defined(__ARM_ARCH_ISA_A64))
      // Same sub-vector gate idea as any_blocks_, but count's shuffle setup plus
      // the horizontal lane sum break even later (~150-200B measured); gate 256B.
      if (full * static_cast<size_type>(sizeof(block_type)) >= size_type { 256 }) {
        total = count_full_bytes_(reinterpret_cast<unsigned char const *>(blocks), full * static_cast<size_type>(sizeof(block_type)));
      } else {
        for (size_type i = 0; i < full; ++i) {
          total += static_cast<size_type>(std::popcount(blocks[i]));
        }
      }
#else
      for (size_type i = 0; i < full; ++i) {
        total += static_cast<size_type>(std::popcount(blocks[i]));
      }
#endif
    } else {
      for (size_type i = 0; i < full; ++i) {
        total += static_cast<size_type>(std::popcount(blocks[i]));
      }
    }
    if (size_type const r = bit_size % bits_per_block; r != 0) {
      total += static_cast<size_type>(std::popcount(static_cast<block_type>(blocks[full] & low_bits_mask_(r))));
    }
    return total;
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

  constexpr inlined_bitset(inlined_bitset const &other, allocator_type const &alloc)
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

  constexpr inlined_bitset &operator= (inlined_bitset const &other)
  {
    copy_assignment_from_(other);
    bit_size_ = other.bit_size_;
    return *this;
  }

  constexpr inlined_bitset &operator= (inlined_bitset &&other)
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
      size_type const block_index,
      CharT const zero,
      CharT const one)
  {
    using view_size = typename std::basic_string_view<CharT, Traits>::size_type;
    block_type value {};
    size_type const lo = block_index * bits_per_block;
    size_type const hi = (std::min) (lo + bits_per_block, static_cast<size_type>(len));
    size_type i        = lo;
    // Chars arrive last-char-first (bit 0 is the string tail), so each chunk is
    // validated with a parallel compare and deposited with a bit reversal.
    if constexpr (sizeof(CharT) == 1 && bits_per_block >= 16) {
      if (!std::is_constant_evaluated()) {
        unsigned char const *const bytes = reinterpret_cast<unsigned char const *>(str.data());
        unsigned char const z            = static_cast<unsigned char>(zero);
        unsigned char const o            = static_cast<unsigned char>(one);
        size_type const pos_size         = static_cast<size_type>(pos);
        size_type const len_size         = static_cast<size_type>(len);
#if defined(__AVX2__)
        for (; i + size_type { 32 } <= hi; i += size_type { 32 }) {
          std::uint32_t ones = 0;
          pack32_avx2_(bytes + (pos_size + (len_size - (i + size_type { 32 }))), z, o, ones);
          value |= static_cast<block_type>(static_cast<block_type>(reverse_bits32_(ones)) << (i - lo));
        }
#endif
#if defined(__AVX2__) || defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64) || (_M_IX86_FP >= 2)
        for (; i + size_type { 16 } <= hi; i += size_type { 16 }) {
          unsigned ones = 0;
          pack16_sse_(bytes + (pos_size + (len_size - (i + size_type { 16 }))), z, o, ones);
          value |= static_cast<block_type>(static_cast<block_type>(reverse_bits16_(ones)) << (i - lo));
        }
#elif (defined(__ARM_NEON) || defined(__ARM_NEON__)) && !defined(__ARM_BIG_ENDIAN)
        for (; i + size_type { 16 } <= hi; i += size_type { 16 }) {
          unsigned ones = 0;
          pack16_neon_(bytes + (pos_size + (len_size - (i + size_type { 16 }))), z, o, ones);
          value |= static_cast<block_type>(static_cast<block_type>(reverse_bits16_(ones)) << (i - lo));
        }
#endif
      }
    }
    for (; i < hi; ++i) {
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
      typename std::basic_string_view<CharT, Traits>::size_type len,
      CharT const zero,
      CharT const one)
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
      typename std::basic_string_view<CharT, Traits>::size_type len,
      CharT const zero,
      CharT const one)
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
  template <std::input_iterator InputIt, std::sentinel_for<InputIt> InSent>
  requires std::convertible_to<std::iter_reference_t<InputIt>, bool>
  constexpr inlined_bitset(InputIt first, InSent last, allocator_type const &alloc = allocator_type {})
      : base { alloc }
  {
    if constexpr (std::forward_iterator<InputIt>) {
      auto const nbits = static_cast<size_type>(std::ranges::distance(first, last));
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
  constexpr inlined_bitset(std::from_range_t, Rng &&rng, allocator_type const &alloc = allocator_type {})
      : inlined_bitset { std::ranges::begin(rng), std::ranges::end(rng), alloc }
  {
  }
#endif

  // ---------- iterator-range (block-wise) ----------
  template <std::input_iterator InputIt, std::sentinel_for<InputIt> InSent>
  requires std::unsigned_integral<std::remove_cvref_t<std::iter_value_t<InputIt>>>
        && std::convertible_to<std::iter_reference_t<InputIt>, block_type>
  constexpr inlined_bitset(from_blocks_t, InputIt first, InSent last, allocator_type const &alloc = allocator_type {})
      : base { alloc }
  {
    if constexpr (std::forward_iterator<InputIt>) {
      auto const n = static_cast<size_type>(std::ranges::distance(first, last));
      if (checked_block_count_(n, alloc) == 0) [[unlikely]] return;

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
  requires std::unsigned_integral<std::remove_cvref_t<std::ranges::range_value_t<Rng>>> 
        && std::convertible_to<std::ranges::range_reference_t<Rng>, block_type>
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

  [[nodiscard]] constexpr iterator begin() noexcept { return iterator(data(), size_type { 0 }); }
  [[nodiscard]] constexpr const_iterator begin() const noexcept { return const_iterator(data(), size_type { 0 }); }
  [[nodiscard]] constexpr iterator end() noexcept { return iterator(data(), bit_size_); }
  [[nodiscard]] constexpr const_iterator end() const noexcept { return const_iterator(data(), bit_size_); }
  [[nodiscard]] constexpr reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  [[nodiscard]] constexpr reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
  [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  [[nodiscard]] constexpr const_iterator cbegin() const noexcept { return begin(); }
  [[nodiscard]] constexpr const_iterator cend() const noexcept { return end(); }
  [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept { return rend(); }

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

  [[nodiscard]] constexpr bool all() const noexcept { return all_blocks_(data(), bit_size_); }
  [[nodiscard]] constexpr bool any() const noexcept { return any_blocks_(data(), block_count()); }
  [[nodiscard]] constexpr bool none() const noexcept { return !any(); }
  [[nodiscard]] constexpr size_type count() const noexcept { return count_blocks_(data(), bit_size_); }

private:
  /// Applies op(block, mask) over [pos, pos + len). Requires pos + len <= bit_size_;
  /// len == 0 is a no-op. Masks are whole-block for interior blocks.
  template <class Op>
  constexpr void apply_range_unchecked_(size_type const pos, size_type const len, Op op)
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

  constexpr inlined_bitset &set(size_type pos, bool value)
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
  constexpr iterator emplace(size_type pos, Args &&...args)
  {
    return insert(pos, bool(std::forward<Args>(args)...));
  }

  template <class... Args>
  requires std::constructible_from<bool, Args...>
  constexpr reference emplace_back(Args &&...args)
  {
    push_back(bool(std::forward<Args>(args)...));
    return back();
  }

  constexpr void push_back(bool value)
  {
    if (bit_size_ >= max_size()) [[unlikely]] throw_length_error_();
    if (bit_size_ % bits_per_block == 0) {
      emplace_one_at_back_(block_type { 0 });
    }
    (*this)[bit_size_] = value;
    ++bit_size_;
  }

  constexpr void pop_back() noexcept
  {
    if (empty()) [[unlikely]] return;

    --bit_size_;
    if (bit_size_ % bits_per_block != 0) {
      (*this)[bit_size_].reset();
    } else {
      destroy_element_unchecked_(data() + block_count() - 1);
      set_size_(block_count() - 1, get_flag_());
    }
  }

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

    if (size_type const old_r = old_bits % bits_per_block; bit_count > old_bits && value && old_r != 0) {
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

  constexpr iterator insert(size_type pos, bool value) { return insert(pos, size_type { 1 }, value); }

  constexpr iterator insert(size_type pos, size_type count, bool value)
  {
    assert(pos <= bit_size_ && "insert position out of range");
    if (count == 0) [[unlikely]] return iterator(data(), pos);
    size_type const old_bits = bit_size_;
    if (count > max_size() - old_bits) [[unlikely]] throw_length_error_();
    if (pos % bits_per_block == 0 && count % bits_per_block == 0) {
      // Whole blocks: block-level fill-insert, no bit surgery.
      (void) insert(from_blocks, pos / bits_per_block, count / bits_per_block,
                    value ? static_cast<block_type>(~block_type { 0 }) : block_type { 0 });
      return iterator(data(), pos);
    }
    // Grow first (zeroed, tail masked); then shift the tail right and fill.
    resize(old_bits + count, false);
    for (size_type i = old_bits; i-- > pos;) {
      (*this)[i + count] = static_cast<bool>((*this)[i]);
    }
    if (value) {
      set(pos, count, true);
    } else {
      reset(pos, count);
    }
    return iterator(data(), pos);
  }

  template <std::input_iterator InIt, std::sentinel_for<InIt> InSent>
  requires std::convertible_to<std::iter_reference_t<InIt>, bool>
  constexpr iterator insert(size_type pos, InIt first, InSent last)
  {
    assert(pos <= bit_size_ && "insert position out of range");
    size_type const old_size = bit_size_;
    if constexpr (std::forward_iterator<InIt>) {
      auto const count = static_cast<size_type>(std::ranges::distance(first, last));
      if (count > max_size() - old_size) [[unlikely]] throw_length_error_();
    }
    // Append-then-rotate (the insert_uncounted_range_ shape): push_back grows
    // (reallocation included); iterators are taken fresh afterwards.
    for (; first != last; ++first) {
      push_back(static_cast<bool>(*first));
    }
    std::rotate(begin() + pos, begin() + old_size, end());
    return iterator(data(), pos);
  }

  constexpr iterator insert(size_type pos, std::initializer_list<bool> ilist)
  {
    return insert(pos, ilist.begin(), ilist.end());
  }

#if defined(__cpp_lib_ranges)
  template <std::ranges::input_range Rng>
  requires std::convertible_to<std::ranges::range_reference_t<Rng>, bool>
  constexpr iterator insert_range(size_type pos, Rng &&rng)
  {
    return insert(pos, std::ranges::begin(rng), std::ranges::end(rng));
  }
#endif

  constexpr void assign(size_type bit_count, bool value)
  {
    if (bit_count > max_size()) [[unlikely]] throw_length_error_();
    size_type const old_bits = bit_size_;
    // resize grows with value (old-tail OR included) and masks the new tail;
    // only the kept prefix still holds stale bits.
    resize(bit_count, value);
    // NOTE: set takes (pos, len, value); reset takes (pos, len) — a two-arg
    // set call would reinterpret len as the bit value.
    if (value) {
      set(size_type { 0 }, (std::min) (old_bits, bit_count), true);
    } else {
      reset(size_type { 0 }, (std::min) (old_bits, bit_count));
    }
  }

  constexpr void assign(std::initializer_list<bool> ilist) { assign(ilist.begin(), ilist.end()); }

  template <std::input_iterator InIt, std::sentinel_for<InIt> InSent>
  requires std::convertible_to<std::iter_reference_t<InIt>, bool>
  constexpr void assign(InIt first, InSent last)
  {
    size_type nbits = size_type { 0 };
    if constexpr (std::forward_iterator<InIt>) {
      nbits = static_cast<size_type>(std::ranges::distance(first, last));
      if (nbits > max_size()) [[unlikely]] throw_length_error_();
    }
    iterator cur = begin();
    for (iterator const old_end = end(); first != last && cur != old_end; ++first, ++cur) {
      *cur = static_cast<bool>(*first);
    }
    if (first == last) {
      resize(static_cast<size_type>(cur - begin()));
    } else {
      reserve(nbits);
      for (; first != last; ++first) {
        push_back(static_cast<bool>(*first));
      }
    }
  }

#if defined(__cpp_lib_ranges)
  template <std::ranges::input_range Rng>
  requires std::convertible_to<std::ranges::range_reference_t<Rng>, bool>
  constexpr void assign_range(Rng &&rng)
  {
    assign(std::ranges::begin(rng), std::ranges::end(rng));
  }
#endif

  constexpr iterator erase(size_type pos) noexcept { return erase(pos, pos + 1); }

  constexpr iterator erase(size_type first, size_type last) noexcept
  {
    assert(first <= last && "erase range must not be inverted");
    assert(last <= bit_size_ && "erase range out of bounds");
    if (first == last) [[unlikely]] return iterator(data(), first);
    if (first % bits_per_block == 0 && last % bits_per_block == 0) {
      // Whole blocks: block-level erase moves storage, no bit surgery.
      (void) erase(from_blocks, first / bits_per_block, last / bits_per_block);
      return iterator(data(), first);
    }
    size_type const count = last - first;
    // Left shift, strictly forward: every read stays ahead of its write.
    for (size_type i = last; i < bit_size_; ++i) {
      (*this)[i - count] = static_cast<bool>((*this)[i]);
    }
    // Trim the tail (pop_back shape, range-sized): destroy surplus blocks and
    // mask the new partial tail so padding stays zero.
    size_type const new_bits   = bit_size_ - count;
    size_type const new_blocks = blocks_for_bits_(new_bits);
    destroy_elements_unchecked_(data() + new_blocks, block_count() - new_blocks);
    set_size_(new_blocks, get_flag_());
    clear_tail_bits_(new_bits, new_blocks);
    bit_size_ = new_bits;
    return iterator(data(), first);
  }

public:
  constexpr void resize(from_blocks_t, size_type nblocks, block_type value = block_type {})
  {
    if (nblocks > max_size() / bits_per_block) [[unlikely]] throw_overflow_error_();
    base::resize(nblocks, value);
    bit_size_ = nblocks * bits_per_block;
  }

  constexpr void append(from_blocks_t, block_type block) { (void) insert(from_blocks, block_count(), block); }

  template <std::input_iterator InputIt>
  requires std::unsigned_integral<std::remove_cvref_t<std::iter_value_t<InputIt>>> 
        && std::convertible_to<std::iter_reference_t<InputIt>, block_type>
  constexpr void append(from_blocks_t, InputIt first, InputIt last)
  {
    (void) insert(from_blocks, block_count(), first, last);
  }

  template <std::unsigned_integral T>
  requires std::convertible_to<T, block_type>
  constexpr void append(from_blocks_t, std::initializer_list<T> blocks)
  {
    (void) insert(from_blocks, block_count(), blocks);
  }

#if defined(__cpp_lib_ranges)
  template <std::ranges::input_range Rng>
  requires std::unsigned_integral<std::remove_cvref_t<std::ranges::range_value_t<Rng>>> 
        && std::convertible_to<std::ranges::range_reference_t<Rng>, block_type>
  constexpr void append_range(from_blocks_t, Rng &&rng)
  {
    (void) insert_range(from_blocks, block_count(), std::forward<Rng>(rng));
  }
#endif

  constexpr block_iterator insert(from_blocks_t, size_type block_pos, block_type block)
  {
    return insert(from_blocks, block_pos, size_type { 1 }, block);
  }

  constexpr block_iterator insert(from_blocks_t, size_type block_pos, size_type count, block_type value)
  {
    assert(block_pos <= this->block_count() && "insert position out of range");
    if (count == 0) [[unlikely]] return block_iterator(data() + block_pos);
    size_type const old_count = this->block_count();
    if (count > max_size() / bits_per_block - old_count) [[unlikely]] throw_overflow_error_();
    // Append-then-rotate (the insert_uncounted_range_ shape): resize fills the
    // tail with value, rotate slides it into the gap.
    base::resize(old_count + count, value);
    std::rotate(data() + block_pos, data() + old_count, data() + old_count + count);
    bit_size_ += count * bits_per_block;
    return block_iterator(data() + block_pos);
  }

  template <std::input_iterator InIt, std::sentinel_for<InIt> InSent>
  requires std::unsigned_integral<std::remove_cvref_t<std::iter_value_t<InIt>>>
        && std::convertible_to<std::iter_reference_t<InIt>, block_type>
  constexpr block_iterator insert(from_blocks_t, size_type block_pos, InIt first, InSent last)
  {
    assert(block_pos <= block_count() && "insert position out of range");
    size_type const old_count = block_count();
    if constexpr (std::forward_iterator<InIt>) {
      size_type const count = static_cast<size_type>(std::ranges::distance(first, last));
      if (count > max_size() / bits_per_block - old_count) [[unlikely]] throw_overflow_error_();
    }
    insert_range_(block_pos, first, last);
    size_type const new_count = block_count();
    if constexpr (!std::forward_iterator<InIt>) {
      if (new_count > max_size() / bits_per_block) [[unlikely]] throw_overflow_error_();
    }
    // Post-op delta: no upfront distance needed, single-pass sources stay valid.
    bit_size_ += (new_count - old_count) * bits_per_block;
    return block_iterator(data() + block_pos);
  }

  template <std::unsigned_integral T>
  requires std::convertible_to<T, block_type>
  constexpr block_iterator insert(from_blocks_t, size_type block_pos, std::initializer_list<T> ilist)
  {
    return insert(from_blocks, block_pos, ilist.begin(), ilist.end());
  }

#if defined(__cpp_lib_ranges)
  template <std::ranges::input_range Rng>
  requires std::unsigned_integral<std::remove_cvref_t<std::ranges::range_value_t<Rng>>>
        && std::convertible_to<std::ranges::range_reference_t<Rng>, block_type>
  constexpr block_iterator insert_range(from_blocks_t, size_type block_pos, Rng &&rng)
  {
    return insert(from_blocks, block_pos, std::ranges::begin(rng), std::ranges::end(rng));
  }
#endif

  constexpr void assign(from_blocks_t, size_type count, block_type value)
  {
    if (count > max_size() / bits_per_block) [[unlikely]] throw_overflow_error_();
    if (count > block_capacity()) {
      base::clear();
      base::resize(count, value);
    } else {
      size_type const old_count = block_count();
      base::resize(count, value);
      std::fill_n(data(), (std::min) (old_count, count), value);
    }
    bit_size_ = count * bits_per_block;
  }

  template <std::input_iterator InIt, std::sentinel_for<InIt> InSent>
  requires std::unsigned_integral<std::remove_cvref_t<std::iter_value_t<InIt>>>
        && std::convertible_to<std::iter_reference_t<InIt>, block_type>
  constexpr void assign(from_blocks_t, InIt first, InSent last)
  {
    if constexpr (std::forward_iterator<InIt>) {
      size_type const count = static_cast<size_type>(std::ranges::distance(first, last));
      if (count > max_size() / bits_per_block) [[unlikely]] throw_overflow_error_();
    }
    assign_range_(first, last);
    if constexpr (!std::forward_iterator<InIt>) {
      if (block_count() > max_size() / bits_per_block) [[unlikely]] throw_overflow_error_();
    }
    bit_size_ = block_count() * bits_per_block;
  }

  template <std::unsigned_integral T>
  requires std::convertible_to<T, block_type>
  constexpr void assign(from_blocks_t, std::initializer_list<T> ilist)
  {
    assign(from_blocks, ilist.begin(), ilist.end());
  }

#if defined(__cpp_lib_ranges)
  template <std::ranges::input_range Rng>
  requires std::unsigned_integral<std::remove_cvref_t<std::ranges::range_value_t<Rng>>>
        && std::convertible_to<std::ranges::range_reference_t<Rng>, block_type>
  constexpr void assign_range(from_blocks_t, Rng &&rng)
  {
    assign(from_blocks, std::ranges::begin(rng), std::ranges::end(rng));
  }
#endif

  constexpr block_iterator erase(from_blocks_t, size_type pos) noexcept
  {
    return erase(from_blocks, pos, pos + 1);
  }

  constexpr block_iterator erase(from_blocks_t, size_type first, size_type last) noexcept
  {
    assert(first <= last && last <= block_count() && "erase range out of bounds");
    if (first != last) {
      block_type *const blocks  = data();
      size_type const old_count = block_count();
      size_type const removed   = last - first;
      std::move(blocks + last, blocks + old_count, blocks + first);
      destroy_elements_unchecked_(blocks + old_count - removed, removed);
      set_size_(old_count - removed, get_flag_());
      bit_size_ -= removed * bits_per_block;
    }
    return block_iterator(data() + first);
  }

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
  [[nodiscard]] constexpr inlined_bitset operator- () const;
  [[nodiscard]] constexpr inlined_bitset operator~ () const;

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

template <class CharT, class Traits, std::unsigned_integral Block, std::size_t N, class Allocator>
std::basic_ostream<CharT, Traits> &operator<< (std::basic_ostream<CharT, Traits> &os, inlined_bitset<Block, N, Allocator> const &bitset);

template <class CharT, class Traits, std::unsigned_integral Block, std::size_t N, class Allocator>
std::basic_istream<CharT, Traits> &operator>> (std::basic_istream<CharT, Traits> &is, inlined_bitset<Block, N, Allocator> &bitset);
} // namespace stdex
