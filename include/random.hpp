#pragma once

#include <cstdint>      // std::uint64_t
#include <limits>       // std::numeric_limits
#include <type_traits>  // std::enable_if_t, std::is_convertible_v,
                        // std::remove_cv_t, std::is_same_v
#include <utility>      // std::declval
#include <ostream>      // std::basic_ostream, operator<<
#include <istream>      // std::basic_istream, operator>>

#include "int128.hpp"

namespace stdex
{
/**
 * @brief splitmix64 pseudorandom number generator.
 *
 * @details
 * The generator advances a 64-bit Weyl state, applies the MurmurHash3
 * finalizer to scramble the state, and returns the mixed result.
 *
 * @par Algorithm
 * @code
 * state += gamma;                                // 64-bit Weyl (additive LCG)
 * z = state;
 * z = (z ^ (z >> 30)) * mix1;
 * z = (z ^ (z >> 27)) * mix2;
 * output = z ^ (z >> 31);                        // final xor-shift avalanche
 * @endcode
 *
 * @details
 * The output stage is the "MurmurHash3 finalizer": a bijective 64-bit mixer
 * chosen because every input bit affects every output bit within a small
 * number of steps. The Weyl sequence supplies a full-period 2^64 state cycle
 * (@c gamma is odd); the finalizer only spreads its bits, never shortens it.
 *
 * @par Period
 * 2^64. This is the state sequence period; the output sequence is a bijection
 * applied to it, so it is also 2^64. Note that 2^64 outputs are insufficient
 * to exhaust a 64-bit output space, so splitmix64 cannot be used as a "true"
 * 64-bit permutation generator, but is BigCrush-clean and is the standard seed
 * expander for xoshiro/xoroshiro-style generators.
 *
 * @par Complexity
 * @c discard is O(1): the additive LCG lets n skipped outputs be folded into a
 * single @c += @c n @c * @c gamma on the state, matching the documented
 * @c discard complexity.
 *
 * @par References
 * - Sebastiano Vigna, "Further scramblings of Marsaglia's xorshift
 *   generators" (https://arxiv.org/abs/1404.0390).
 * - http://zimbry.blogspot.com/2011/09/better-bit-mixing-improving-on.html
 */
class splitmix64
{
public:
  using result_type = std::uint64_t;

  static constexpr result_type default_seed = 0;

  [[nodiscard]] static constexpr result_type(min)() noexcept { return 0; }
  [[nodiscard]] static constexpr result_type(max)() noexcept { return (std::numeric_limits<result_type>::max)(); }

  constexpr splitmix64() noexcept : state_{ default_seed } {}
  constexpr explicit splitmix64(result_type seed) : state_{ seed } {}

  template <class Seedseq,
            std::enable_if_t<
                !std::is_convertible_v<std::remove_cv_t<Seedseq>, result_type> && !std::is_same_v<std::remove_cv_t<Seedseq>, splitmix64>,
                int> = 0>
  explicit splitmix64(Seedseq &seq) noexcept(noexcept(this->seed(seq)))
  {
    seed(seq);
  }

  constexpr void seed(result_type seed = default_seed) noexcept { state_ = seed; }

  template <class Seedseq,
            std::enable_if_t<
                !std::is_convertible_v<std::remove_cv_t<Seedseq>, result_type> && !std::is_same_v<std::remove_cv_t<Seedseq>, splitmix64>,
                int> = 0>
  constexpr void seed(Seedseq &seq) noexcept(noexcept(seq.generate(std::declval<unsigned int *>(), std::declval<unsigned int *>())))
  {
    unsigned int arr[2] {};
    seq.generate(arr, arr + 2);
    state_ = (static_cast<result_type>(arr[0]) << 32) | arr[1];
  }

  constexpr result_type operator() () noexcept
  {
    // http://zimbry.blogspot.com/2011/09/better-bit-mixing-improving-on.html
    result_type z { (state_ += gamma) };
    z = (z ^ (z >> 30)) * mix1;
    z = (z ^ (z >> 27)) * mix2;
    return z ^ (z >> 31);
  }

  constexpr void discard(unsigned long long nskip) noexcept
  {
    state_ += static_cast<result_type>(nskip) * gamma;
  }

  friend constexpr bool operator== (splitmix64 const &a, splitmix64 const &b) noexcept
  {
    return a.state_ == b.state_;
  }

  friend constexpr bool operator!= (splitmix64 const &a, splitmix64 const &b) noexcept
  {
    return !(a == b);
  }

  template <class Elem, class Traits>
  friend std::basic_ostream<Elem, Traits> &operator<< (std::basic_ostream<Elem, Traits> &ostr, splitmix64 const &eng)
  {
    return ostr << eng.state_;
  }

  template <class Elem, class Traits>
  friend std::basic_istream<Elem, Traits> &operator>> (std::basic_istream<Elem, Traits> &istr, splitmix64 &eng)
  {
    return istr >> eng.state_;
  }

private:
  static constexpr result_type gamma = 0x9E'37'79'B9'7F'4A'7C'15ULL;
  static constexpr result_type mix1  = 0xBF'58'47'6D'1C'E4'E5'B9ULL;
  static constexpr result_type mix2  = 0x94'D0'49'BB'13'31'11'EBULL;

  result_type state_ { default_seed };
};

/**
 * @brief wyrand pseudorandom number generator.
 *
 * @details
 * The generator advances a 64-bit Weyl state, then applies a multiply-xorshift
 * mixing step built around a single 64x64->128-bit multiplication.
 * The 128-bit product is folded to 64 bits by xor-ing its high and low halves.
 *
 * @par Algorithm
 * @code
 * state += gamma;                    // 64-bit Weyl (additive LCG)
 * p = state * (state ^ mix);         // 64x64 -> 128
 * output = p_hi ^ p_lo;              // fold 128 bits to 64
 * @endcode
 *
 * @details
 * Proposed by Wang Yi as part of wyhash. The Weyl sequence supplies a
 * full-period 2^64 state cycle (@c gamma is odd); the multiply-xorshift
 * stage only spreads the state's bits and never shortens the cycle.
 *
 * @par Period
 * 2^64. This is the state sequence period, and matches the output sequence
 * period. Passes BigCrush, and is typically faster than splitmix64 on most
 * cores because its inner loop contains a single integer multiply.
 *
 * @par Complexity
 * @c discard is O(1): the additive LCG lets n skipped outputs be folded into
 * a single @c += @c n @c * @c gamma on the state, matching the documented
 * @c discard complexity.
 *
 * @par References
 * - Wang Yi, "wyhash: fast hash & PRNG"
 *   (https://github.com/wangyi-fudan/wyhash).
 */
class wyrand
{
public:
  using result_type = std::uint64_t;

  static constexpr result_type default_seed = 0;

  [[nodiscard]] static constexpr result_type(min)() noexcept { return 0; }
  [[nodiscard]] static constexpr result_type(max)() noexcept { return (std::numeric_limits<result_type>::max)(); }

  constexpr wyrand() noexcept : state_ { default_seed } {}

  constexpr explicit wyrand(result_type seed) noexcept : state_ { seed } {}

  template <class Seedseq,
            std::enable_if_t<
                !std::is_convertible_v<std::remove_cv_t<Seedseq>, result_type> && !std::is_same_v<std::remove_cv_t<Seedseq>, wyrand>,
                int> = 0>
  explicit wyrand(Seedseq &seq) noexcept(noexcept(this->seed(seq)))
  {
    seed(seq);
  }

  constexpr void seed(result_type seed = default_seed) noexcept { state_ = seed; }

  template <class Seedseq,
            std::enable_if_t<
                !std::is_convertible_v<std::remove_cv_t<Seedseq>, result_type> && !std::is_same_v<std::remove_cv_t<Seedseq>, wyrand>,
                int> = 0>
  constexpr void seed(Seedseq &seq) noexcept(noexcept(seq.generate(std::declval<unsigned int *>(), std::declval<unsigned int *>())))
  {
    unsigned int arr[2] {};
    seq.generate(arr, arr + 2);
    state_ = (static_cast<result_type>(arr[0]) << 32) | arr[1];
  }

  constexpr result_type operator() () noexcept
  {
    state_ += gamma;
    std::uint64_t high {};
    std::uint64_t const low = stdex::umul128(state_, state_ ^ mix, high);
    return high ^ low;
  }

  constexpr void discard(unsigned long long nskip) noexcept
  {
    state_ += static_cast<result_type>(nskip) * gamma;
  }

  friend constexpr bool operator== (wyrand const &a, wyrand const &b) noexcept
  {
    return a.state_ == b.state_;
  }

  friend constexpr bool operator!= (wyrand const &a, wyrand const &b) noexcept
  {
    return !(a == b);
  }

  template <class Elem, class Traits>
  friend std::basic_ostream<Elem, Traits> &operator<< (std::basic_ostream<Elem, Traits> &ostr, wyrand const &eng)
  {
    return ostr << eng.state_;
  }

  template <class Elem, class Traits>
  friend std::basic_istream<Elem, Traits> &operator>> (std::basic_istream<Elem, Traits> &istr, wyrand &eng)
  {
    return istr >> eng.state_;
  }

private:
  static constexpr result_type gamma = 0xA0'76'1D'64'78'BD'64'2FULL;
  static constexpr result_type mix   = 0xE7'03'7E'D1'A0'B4'28'DBULL;

  result_type state_ { default_seed };
};

/**
 * @brief lehmer64 pseudorandom number generator.
 *
 * @details
 * The generator is a 128-bit multiplicative LCG with increment c = 0
 * (a Lehmer generator) over the ring of integers modulo 2^128.
 * Only the upper 64 bits of the 128-bit state are returned as output.
 *
 * @par Algorithm
 * @code
 * state = state * gamma  (mod 2^128);  // 128-bit LCG with c = 0
 * output = state_hi;                   // high 64 bits of the state
 * @endcode
 *
 * @details
 * The multiplier is chosen congruent to 1 modulo 4, which maximizes the
 * state sequence period. Because the low 64 bits of a Lehmer generator are
 * weak, the high word is returned as the result.
 *
 * @par Period
 * 2^126, the maximum for a multiplicative LCG modulo 2^128 with a multiplier
 * congruent to 1 mod 4. Note that the low 64 bits of the state are weak
 * (their low bit is fixed), which is precisely why the high word is used as
 * the result.
 *
 * @par Complexity
 * @c discard is O(log n): it is implemented via modular exponentiation of the
 * multiplier, computing gamma^n (mod 2^128) by repeated squaring and applying
 * it to the state in a single multiplication, matching the documented
 * @c discard complexity.
 *
 * @par References
 * - Daniel Lemire, "The fastest conventional random number generator that
 *   can pass Big Crush?"
 *   (https://lemire.me/blog/2019/03/19/the-fastest-conventional-random-number-generator-that-can-pass-big-crush/).
 *   (Archived copy: https://archive.ph/D9Ec5)
 */
class lehmer64
{
public:
  using result_type = std::uint64_t;

  static constexpr result_type default_seed = 0;

  [[nodiscard]] static constexpr result_type(min)() noexcept { return 0; }
  [[nodiscard]] static constexpr result_type(max)() noexcept { return (std::numeric_limits<result_type>::max)(); }

  constexpr lehmer64() noexcept : state_ {} {}

  constexpr explicit lehmer64(result_type seed) noexcept : state_ { stdex::uint128_t::from_u64(seed) } {}

  /// Convenience overload for a full 128-bit seed.
  constexpr lehmer64(result_type lo, result_type hi) noexcept : state_ { lo, hi } {}

  template <class Seedseq,
            std::enable_if_t<
                !std::is_convertible_v<std::remove_cv_t<Seedseq>, result_type> && !std::is_same_v<std::remove_cv_t<Seedseq>, lehmer64>,
                int> = 0>
  explicit lehmer64(Seedseq &seq) noexcept(noexcept(this->seed(seq)))
  {
    seed(seq);
  }

  constexpr void seed(result_type seed = default_seed) noexcept
  {
    state_ = stdex::uint128_t::from_u64(seed);
  }

  template <class Seedseq,
            std::enable_if_t<
                !std::is_convertible_v<std::remove_cv_t<Seedseq>, result_type> && !std::is_same_v<std::remove_cv_t<Seedseq>, lehmer64>,
                int> = 0>
  constexpr void seed(Seedseq &seq) noexcept(noexcept(seq.generate(std::declval<unsigned int *>(), std::declval<unsigned int *>())))
  {
    unsigned int arr[4] {};
    seq.generate(arr, arr + 4);
    std::uint64_t const lo = (static_cast<std::uint64_t>(arr[0]) << 32) | arr[1];
    std::uint64_t const hi = (static_cast<std::uint64_t>(arr[2]) << 32) | arr[3];
    state_                 = stdex::uint128_t { lo, hi };
  }

  constexpr result_type operator() () noexcept
  {
    state_ = state_.wrapping_mul(stdex::uint128_t::from_u64(gamma));
    return state_.hi();
  }

  constexpr void discard(unsigned long long nskip) noexcept
  {
    // Compute gamma^nskip mod 2^128 by repeated squaring, then apply once.
    stdex::uint128_t factor     = stdex::uint128_t::from_u64(gamma);
    stdex::uint128_t multiplier = stdex::uint128_t::from_u64(1);
    unsigned long long n        = nskip;
    while (n != 0) {
      if ((n & 1ULL) != 0) {
        multiplier = multiplier.wrapping_mul(factor);
      }
      n >>= 1;
      if (n != 0) {
        factor = factor.wrapping_mul(factor);
      }
    }
    state_ = state_.wrapping_mul(multiplier);
  }

  friend constexpr bool operator== (lehmer64 const &a, lehmer64 const &b) noexcept
  {
    return a.state_.lo() == b.state_.lo() && a.state_.hi() == b.state_.hi();
  }

  friend constexpr bool operator!= (lehmer64 const &a, lehmer64 const &b) noexcept
  {
    return !(a == b);
  }

  template <class Elem, class Traits>
  friend std::basic_ostream<Elem, Traits> &operator<< (std::basic_ostream<Elem, Traits> &ostr, lehmer64 const &eng)
  {
    return ostr << eng.state_.hi() << ' ' << eng.state_.lo();
  }

  template <class Elem, class Traits>
  friend std::basic_istream<Elem, Traits> &operator>> (std::basic_istream<Elem, Traits> &istr, lehmer64 &eng)
  {
    std::uint64_t hi {}, lo {};
    if (istr >> hi >> lo) {
      eng.state_ = stdex::uint128_t { lo, hi };
    }
    return istr;
  }

private:
  static constexpr result_type gamma = 0xDA'94'20'42'E4'DD'58'B5ULL;

  stdex::uint128_t state_ {};
};
} // namespace stdex
