/*
 * Copyright (c) 2025 LinnJz
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

#ifndef LINN_UTILS_H_
#define LINN_UTILS_H_

#if defined(__clang__)
#  define LINN_ALWAYS_INLINE   inline __attribute__((always_inline))
#  define LINN_PRETTY_FUNCTION __PRETTY_FUNCTION__
#  define LINN_RESTRICT        __restrict__
#  define LINN_UNREACHABLE()   __builtin_unreachable()
#elif defined(__GNUC__)
#  define LINN_ALWAYS_INLINE   inline __attribute__((always_inline))
#  define LINN_PRETTY_FUNCTION __PRETTY_FUNCTION__
#  define LINN_RESTRICT        __restrict__
#  define LINN_UNREACHABLE()   __builtin_unreachable()
#elif defined(_MSC_VER)
#  define LINN_ALWAYS_INLINE   inline __forceinline
#  define LINN_PRETTY_FUNCTION __FUNCSIG__
#  define LINN_RESTRICT        __restrict
#  define LINN_UNREACHABLE()   __assume(false)
#else
#  define LINN_ALWAYS_INLINE   inline
#  define LINN_PRETTY_FUNCTION __func__
#  define LINN_RESTRICT
#  define LINN_UNREACHABLE() std::abort()
#endif

#if defined(__has_attribute)
#  if __has_attribute(flatten)
#    define LINN_FLATTEN __attribute__((flatten))
#  endif
#endif

#if !defined(LINN_FLATTEN)
#  if defined(_MSC_VER)
#    if _MSC_VER >= 1930 // VS 2022 17.0+
#      define LINN_FLATTEN [[msvc::flatten]]
#    else
#      define LINN_FLATTEN
#    endif
#  else
#    define LINN_FLATTEN
#  endif
#endif

#if defined(__clang__)
/// Clang support clang loop unroll_count or unroll
#  define LINN_LOOP_UNROLL(n) _Pragma(LINN_EXPAND_AND_STRINGIFY(clang loop unroll_count(n)))
#elif defined(__GNUC__) && !defined(__INTEL_COMPILER) && __GNUC__ >= 8
/// GCC 8+ support #pragma GCC unroll
#  define LINN_LOOP_UNROLL(n) _Pragma(LINN_EXPAND_AND_STRINGIFY(GCC unroll n))
#elif defined(_MSC_VER) && _MSC_VER >= 1920
/// MSVC 2019+ support __pragma(unroll)
#  define LINN_LOOP_UNROLL(n) //__pragma(unroll(n))
#else
#  define LINN_LOOP_UNROLL(n)
#endif

#define LINN_EXPAND(x)      x
#define LINN_STRINGIFY_I(x) #x
#define LINN_STRINGIFY(x)   LINN_STRINGIFY_I(x)

#define LINN_CAT_I(a, b)                       a##b
#define LINN_CAT(a, b)                         LINN_CAT_I(a, b)
#define LINN_CAT3_I(a, b, c)                   a##b##c
#define LINN_CAT3(a, b, c)                     LINN_CAT3_I(a, b, c)
#define LINN_CAT4_I(a, b, c, d)                a##b##c##d
#define LINN_CAT4(a, b, c, d)                  LINN_CAT4_I(a, b, c, d)
#define LINN_CAT5_I(a, b, c, d, e)             a##b##c##d##e
#define LINN_CAT5(a, b, c, d, e)               LINN_CAT5_I(a, b, c, d, e)
#define LINN_CAT6_I(a, b, c, d, e, f)          a##b##c##d##e##f
#define LINN_CAT6(a, b, c, d, e, f)            LINN_CAT6_I(a, b, c, d, e, f)
#define LINN_CAT7_I(a, b, c, d, e, f, g)       a##b##c##d##e##f##g
#define LINN_CAT7(a, b, c, d, e, f, g)         LINN_CAT7_I(a, b, c, d, e, f, g)
#define LINN_CAT8_I(a, b, c, d, e, f, g, h)    a##b##c##d##e##f##g##h
#define LINN_CAT8(a, b, c, d, e, f, g, h)      LINN_CAT8_I(a, b, c, d, e, f, g, h)
#define LINN_CAT9_I(a, b, c, d, e, f, g, h, i) a##b##c##d##e##f##g##h##i
#define LINN_CAT9(a, b, c, d, e, f, g, h, i)   LINN_CAT9_I(a, b, c, d, e, f, g, h, i)

#define LINN_IF(cond)   LINN_CAT(LINN_IF_, cond)
#define LINN_IF_1(t, f) t
#define LINN_IF_0(t, f) f

#define LINN_CHECK_N(x, n, ...) n
#define LINN_CHECK(...)         LINN_CHECK_N(__VA_ARGS__, 0)

#define LINN_PROBE(x)            x, 1
#define LINN_IS_PAREN_PROBE(...) ~, 1
#define LINN_IS_PAREN(x)         LINN_CHECK(LINN_IS_PAREN_PROBE x)

#define LINN_UNPAREN(...)   LINN_UNPAREN_I __VA_ARGS__
#define LINN_UNPAREN_I(...) __VA_ARGS__

#define LINN_OR(a, b) LINN_CAT3(LINN_OR_, a, b)
#define LINN_OR_00    0
#define LINN_OR_01    1
#define LINN_OR_10    1
#define LINN_OR_11    1

#define LINN_AND(a, b) LINN_CAT3(LINN_AND_, a, b)
#define LINN_AND_00    0
#define LINN_AND_01    0
#define LINN_AND_10    0
#define LINN_AND_11    1

#define LINN_PAIR(a, b)          (a, b)
#define LINN_PAIR_FIRST_I(a, b)  a
#define LINN_PAIR_FIRST(pair)    LINN_PAIR_FIRST_I pair
#define LINN_PAIR_SECOND_I(a, b) b
#define LINN_PAIR_SECOND(pair)   LINN_PAIR_SECOND_I pair

#define LINN_TRIPLE(a, b, c)          (a, b, c)
#define LINN_TRIPLE_FIRST_I(a, b, c)  a
#define LINN_TRIPLE_FIRST(triple)     LINN_TRIPLE_FIRST_I triple
#define LINN_TRIPLE_SECOND_I(a, b, c) b
#define LINN_TRIPLE_SECOND(triple)    LINN_TRIPLE_SECOND_I triple
#define LINN_TRIPLE_THIRD_I(a, b, c)  c
#define LINN_TRIPLE_THIRD(triple)     LINN_TRIPLE_THIRD_I triple

#define LINN_QUAD(a, b, c, d)          (a, b, c, d)
#define LINN_QUAD_FIRST_I(a, b, c, d)  a
#define LINN_QUAD_FIRST(t)             LINN_QUAD_FIRST_I t
#define LINN_QUAD_SECOND_I(a, b, c, d) b
#define LINN_QUAD_SECOND(t)            LINN_QUAD_SECOND_I t
#define LINN_QUAD_THIRD_I(a, b, c, d)  c
#define LINN_QUAD_THIRD(t)             LINN_QUAD_THIRD_I t
#define LINN_QUAD_FOURTH_I(a, b, c, d) d
#define LINN_QUAD_FOURTH(t)            LINN_QUAD_FOURTH_I t

#define LINN_QUINT(a, b, c, d, e)          (a, b, c, d, e)
#define LINN_QUINT_FIRST_I(a, b, c, d, e)  a
#define LINN_QUINT_FIRST(t)                LINN_QUINT_FIRST_I t
#define LINN_QUINT_SECOND_I(a, b, c, d, e) b
#define LINN_QUINT_SECOND(t)               LINN_QUINT_SECOND_I t
#define LINN_QUINT_THIRD_I(a, b, c, d, e)  c
#define LINN_QUINT_THIRD(t)                LINN_QUINT_THIRD_I t
#define LINN_QUINT_FOURTH_I(a, b, c, d, e) d
#define LINN_QUINT_FOURTH(t)               LINN_QUINT_FOURTH_I t
#define LINN_QUINT_FIFTH_I(a, b, c, d, e)  e
#define LINN_QUINT_FIFTH(t)                LINN_QUINT_FIFTH_I t

#define LINN_SEXT(a, b, c, d, e, f)          (a, b, c, d, e, f)
#define LINN_SEXT_FIRST_I(a, b, c, d, e, f)  a
#define LINN_SEXT_FIRST(t)                   LINN_SEXT_FIRST_I t
#define LINN_SEXT_SECOND_I(a, b, c, d, e, f) b
#define LINN_SEXT_SECOND(t)                  LINN_SEXT_SECOND_I t
#define LINN_SEXT_THIRD_I(a, b, c, d, e, f)  c
#define LINN_SEXT_THIRD(t)                   LINN_SEXT_THIRD_I t
#define LINN_SEXT_FOURTH_I(a, b, c, d, e, f) d
#define LINN_SEXT_FOURTH(t)                  LINN_SEXT_FOURTH_I t
#define LINN_SEXT_FIFTH_I(a, b, c, d, e, f)  e
#define LINN_SEXT_FIFTH(t)                   LINN_SEXT_FIFTH_I t
#define LINN_SEXT_SIXTH_I(a, b, c, d, e, f)  f
#define LINN_SEXT_SIXTH(t)                   LINN_SEXT_SIXTH_I t

#define LINN_SEPT(a, b, c, d, e, f, g)           (a, b, c, d, e, f, g)
#define LINN_SEPT_FIRST_I(a, b, c, d, e, f, g)   a
#define LINN_SEPT_FIRST(t)                       LINN_SEPT_FIRST_I t
#define LINN_SEPT_SECOND_I(a, b, c, d, e, f, g)  b
#define LINN_SEPT_SECOND(t)                      LINN_SEPT_SECOND_I t
#define LINN_SEPT_THIRD_I(a, b, c, d, e, f, g)   c
#define LINN_SEPT_THIRD(t)                       LINN_SEPT_THIRD_I t
#define LINN_SEPT_FOURTH_I(a, b, c, d, e, f, g)  d
#define LINN_SEPT_FOURTH(t)                      LINN_SEPT_FOURTH_I t
#define LINN_SEPT_FIFTH_I(a, b, c, d, e, f, g)   e
#define LINN_SEPT_FIFTH(t)                       LINN_SEPT_FIFTH_I t
#define LINN_SEPT_SIXTH_I(a, b, c, d, e, f, g)   f
#define LINN_SEPT_SIXTH(t)                       LINN_SEPT_SIXTH_I t
#define LINN_SEPT_SEVENTH_I(a, b, c, d, e, f, g) g
#define LINN_SEPT_SEVENTH(t)                     LINN_SEPT_SEVENTH_I t

#define LINN_OCT(a, b, c, d, e, f, g, h)           (a, b, c, d, e, f, g, h)
#define LINN_OCT_FIRST_I(a, b, c, d, e, f, g, h)   a
#define LINN_OCT_FIRST(t)                          LINN_OCT_FIRST_I t
#define LINN_OCT_SECOND_I(a, b, c, d, e, f, g, h)  b
#define LINN_OCT_SECOND(t)                         LINN_OCT_SECOND_I t
#define LINN_OCT_THIRD_I(a, b, c, d, e, f, g, h)   c
#define LINN_OCT_THIRD(t)                          LINN_OCT_THIRD_I t
#define LINN_OCT_FOURTH_I(a, b, c, d, e, f, g, h)  d
#define LINN_OCT_FOURTH(t)                         LINN_OCT_FOURTH_I t
#define LINN_OCT_FIFTH_I(a, b, c, d, e, f, g, h)   e
#define LINN_OCT_FIFTH(t)                          LINN_OCT_FIFTH_I t
#define LINN_OCT_SIXTH_I(a, b, c, d, e, f, g, h)   f
#define LINN_OCT_SIXTH(t)                          LINN_OCT_SIXTH_I t
#define LINN_OCT_SEVENTH_I(a, b, c, d, e, f, g, h) g
#define LINN_OCT_SEVENTH(t)                        LINN_OCT_SEVENTH_I t
#define LINN_OCT_EIGHTH_I(a, b, c, d, e, f, g, h)  h
#define LINN_OCT_EIGHTH(t)                         LINN_OCT_EIGHTH_I t

#define LINN_NON(a, b, c, d, e, f, g, h, i)           (a, b, c, d, e, f, g, h, i)
#define LINN_NON_FIRST_I(a, b, c, d, e, f, g, h, i)   a
#define LINN_NON_FIRST(t)                             LINN_NON_FIRST_I t
#define LINN_NON_SECOND_I(a, b, c, d, e, f, g, h, i)  b
#define LINN_NON_SECOND(t)                            LINN_NON_SECOND_I t
#define LINN_NON_THIRD_I(a, b, c, d, e, f, g, h, i)   c
#define LINN_NON_THIRD(t)                             LINN_NON_THIRD_I t
#define LINN_NON_FOURTH_I(a, b, c, d, e, f, g, h, i)  d
#define LINN_NON_FOURTH(t)                            LINN_NON_FOURTH_I t
#define LINN_NON_FIFTH_I(a, b, c, d, e, f, g, h, i)   e
#define LINN_NON_FIFTH(t)                             LINN_NON_FIFTH_I t
#define LINN_NON_SIXTH_I(a, b, c, d, e, f, g, h, i)   f
#define LINN_NON_SIXTH(t)                             LINN_NON_SIXTH_I t
#define LINN_NON_SEVENTH_I(a, b, c, d, e, f, g, h, i) g
#define LINN_NON_SEVENTH(t)                           LINN_NON_SEVENTH_I t
#define LINN_NON_EIGHTH_I(a, b, c, d, e, f, g, h, i)  h
#define LINN_NON_EIGHTH(t)                            LINN_NON_EIGHTH_I t
#define LINN_NON_NINTH_I(a, b, c, d, e, f, g, h, i)   i
#define LINN_NON_NINTH(t)                             LINN_NON_NINTH_I t

#define LINN_NARGS_SEQ()                                                                                              \
  256, 255, 254, 253, 252, 251, 250, 249, 248, 247, 246, 245, 244, 243, 242, 241, 240, 239, 238, 237, 236, 235, 234,  \
      233, 232, 231, 230, 229, 228, 227, 226, 225, 224, 223, 222, 221, 220, 219, 218, 217, 216, 215, 214, 213, 212,   \
      211, 210, 209, 208, 207, 206, 205, 204, 203, 202, 201, 200, 199, 198, 197, 196, 195, 194, 193, 192, 191, 190,   \
      189, 188, 187, 186, 185, 184, 183, 182, 181, 180, 179, 178, 177, 176, 175, 174, 173, 172, 171, 170, 169, 168,   \
      167, 166, 165, 164, 163, 162, 161, 160, 159, 158, 157, 156, 155, 154, 153, 152, 151, 150, 149, 148, 147, 146,   \
      145, 144, 143, 142, 141, 140, 139, 138, 137, 136, 135, 134, 133, 132, 131, 130, 129, 128, 127, 126, 125, 124,   \
      123, 122, 121, 120, 119, 118, 117, 116, 115, 114, 113, 112, 111, 110, 109, 108, 107, 106, 105, 104, 103, 102,   \
      101, 100, 99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 77, 76, 75,   \
      74, 73, 72, 71, 70, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, \
      46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, \
      18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0

#define LINN_NARGS_SELECT(                                                                                             \
    _0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, \
    _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, \
    _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, _62, _63, _64, _65, _66, _67, _68, _69, _70, \
    _71, _72, _73, _74, _75, _76, _77, _78, _79, _80, _81, _82, _83, _84, _85, _86, _87, _88, _89, _90, _91, _92, _93, \
    _94, _95, _96, _97, _98, _99, _100, _101, _102, _103, _104, _105, _106, _107, _108, _109, _110, _111, _112, _113,  \
    _114, _115, _116, _117, _118, _119, _120, _121, _122, _123, _124, _125, _126, _127, _128, _129, _130, _131, _132,  \
    _133, _134, _135, _136, _137, _138, _139, _140, _141, _142, _143, _144, _145, _146, _147, _148, _149, _150, _151,  \
    _152, _153, _154, _155, _156, _157, _158, _159, _160, _161, _162, _163, _164, _165, _166, _167, _168, _169, _170,  \
    _171, _172, _173, _174, _175, _176, _177, _178, _179, _180, _181, _182, _183, _184, _185, _186, _187, _188, _189,  \
    _190, _191, _192, _193, _194, _195, _196, _197, _198, _199, _200, _201, _202, _203, _204, _205, _206, _207, _208,  \
    _209, _210, _211, _212, _213, _214, _215, _216, _217, _218, _219, _220, _221, _222, _223, _224, _225, _226, _227,  \
    _228, _229, _230, _231, _232, _233, _234, _235, _236, _237, _238, _239, _240, _241, _242, _243, _244, _245, _246,  \
    _247, _248, _249, _250, _251, _252, _253, _254, _255, _256, N, ...)                                                \
  N

#if __cplusplus >= 202002L
/// C++20 with __VA_OPT__ support.
#  define LINN_NARGS(...) LINN_NARGS_SELECT(                                                                             \
      0 __VA_OPT__(, ) __VA_ARGS__,                                                                                      \
      256, 255, 254, 253, 252, 251, 250, 249, 248, 247, 246, 245, 244, 243, 242, 241, 240, 239, 238, 237, 236, 235, 234, \
      233, 232, 231, 230, 229, 228, 227, 226, 225, 224, 223, 222, 221, 220, 219, 218, 217, 216, 215, 214, 213, 212,      \
      211, 210, 209, 208, 207, 206, 205, 204, 203, 202, 201, 200, 199, 198, 197, 196, 195, 194, 193, 192, 191, 190,      \
      189, 188, 187, 186, 185, 184, 183, 182, 181, 180, 179, 178, 177, 176, 175, 174, 173, 172, 171, 170, 169, 168,      \
      167, 166, 165, 164, 163, 162, 161, 160, 159, 158, 157, 156, 155, 154, 153, 152, 151, 150, 149, 148, 147, 146,      \
      145, 144, 143, 142, 141, 140, 139, 138, 137, 136, 135, 134, 133, 132, 131, 130, 129, 128, 127, 126, 125, 124,      \
      123, 122, 121, 120, 119, 118, 117, 116, 115, 114, 113, 112, 111, 110, 109, 108, 107, 106, 105, 104, 103, 102,      \
      101, 100, 99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 77, 76, 75,      \
      74, 73, 72, 71, 70, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47,    \
      46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19,    \
      18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#elif defined(_MSC_VER) || defined(__GNUC__) || defined(__clang__)
/// MSVC: require standard preprocessor mode (/Zc:preprocessor)
#  if defined(_MSC_VER) && !defined(__clang__)
#    if !(_MSC_VER >= 1925 && defined(_MSVC_TRADITIONAL) && _MSVC_TRADITIONAL == 0)
#      error "For MSVC, please use the standard preprocessor compilation option /Zc:preprocessor or /experimental:preprocessor. For details, see https://learn.microsoft.com/en-us/cpp/build/reference/zc-preprocessor?view=msvc-170"
#    endif
#  endif

#  define LINN_NARGS(...) LINN_NARGS_SELECT(                                                                             \
      0, ##__VA_ARGS__,                                                                                                  \
      256, 255, 254, 253, 252, 251, 250, 249, 248, 247, 246, 245, 244, 243, 242, 241, 240, 239, 238, 237, 236, 235, 234, \
      233, 232, 231, 230, 229, 228, 227, 226, 225, 224, 223, 222, 221, 220, 219, 218, 217, 216, 215, 214, 213, 212,      \
      211, 210, 209, 208, 207, 206, 205, 204, 203, 202, 201, 200, 199, 198, 197, 196, 195, 194, 193, 192, 191, 190,      \
      189, 188, 187, 186, 185, 184, 183, 182, 181, 180, 179, 178, 177, 176, 175, 174, 173, 172, 171, 170, 169, 168,      \
      167, 166, 165, 164, 163, 162, 161, 160, 159, 158, 157, 156, 155, 154, 153, 152, 151, 150, 149, 148, 147, 146,      \
      145, 144, 143, 142, 141, 140, 139, 138, 137, 136, 135, 134, 133, 132, 131, 130, 129, 128, 127, 126, 125, 124,      \
      123, 122, 121, 120, 119, 118, 117, 116, 115, 114, 113, 112, 111, 110, 109, 108, 107, 106, 105, 104, 103, 102,      \
      101, 100, 99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 77, 76, 75,      \
      74, 73, 72, 71, 70, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47,    \
      46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19,    \
      18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#else
/*
* Macro LINN_IS_EMPTY_IMPL(_0, _1, _2, _3), the role of four parameters:
* _0: Does __VA_ARGS__ contain a top-level comma?
*     HAS_COMMA()        -> 0    (empty)
*     HAS_COMMA(x)       -> 0    (single argument)
*     HAS_COMMA(x, y)    -> 1    (multiple arguments, top-level comma present)
*     Purpose: directly detect ">= 2 arguments"
*
* _1: Put a "comma trigger" before __VA_ARGS__
*     TRIGGER is a function-like macro (defined as ','); it expands only
*     when immediately followed by '('.
*     empty / starts with non-'(' -> TRIGGER remains unchanged -> 0
*     starts with '(' (e.g. (), (x), (a,b)) -> TRIGGER '(' ... ')' is invoked
*         -> becomes ',' -> 1
*     Purpose: detect "argument starts with '('"
*
* _2: Append '()' after __VA_ARGS__
*     empty -> '()' (only one empty paren group, no comma) -> 0
*     x     -> 'x()' (no top-level comma)                 -> 0
*     x, y  -> 'x, y()' (top-level comma remains)        -> 1
*     Purpose: complement _0, ensuring "empty" and "multiple arguments"
*     are distinguished by _2
*
* _3: Add both the trigger and the appended '()' -- key probe
*     empty: TRIGGER () -> TRIGGER immediately followed by '()'
*         -> expands to ',' -> 1
*     starts with '(': TRIGGER (...) -> expands to ',' -> 1
*     other single argument: TRIGGER x () -> TRIGGER unchanged -> 0
*     top-level comma: x, y () -> comma remains -> 1
*     Purpose: the only probe that can identify "empty" by itself
*     (empty always yields 1)
* 16 Cases Table:
| #   | `_0` | `_1` | `_2` | `_3` | Pattern | Reachable | Corresponding input                                         | `IS_EMPTY`           |
| --- | ---- | ---- | ---- | ---- | ------- | --------- | ----------------------------------------------------------- | -------------------- |
| 0   | 0    | 0    | 0    | 0    | `0000`  | Yes       | `x`, `x()`, `x y`, `x (y)`, `x (y,z)`                       | **0**                |
| 1   | 0    | 0    | 0    | 1    | `0001`  | Yes       | **empty `()`**                                              | **1** (unique match) |
| 2   | 0    | 0    | 1    | 0    | `0010`  | No        | `_2=1` requires top-level comma -> `_0=1`, contradiction    | **0**                |
| 3   | 0    | 0    | 1    | 1    | `0011`  | No        | Same as above                                               | **0**                |
| 4   | 0    | 1    | 0    | 0    | `0100`  | No        | `_1=1` requires starting with `(` -> `_3=1`, contradiction  | **0**                |
| 5   | 0    | 1    | 0    | 1    | `0101`  | Yes       | `()`; `(x)`; `((x))`; `(a,b)`; `(x) y`; `(x) (y)`; `(,)`    | **0**                |
| 6   | 0    | 1    | 1    | 0    | `0110`  | No        | `_2=1` -> `_0=1`, contradiction                             | **0**                |
| 7   | 0    | 1    | 1    | 1    | `0111`  | No        | Same as above                                               | **0**                |
| 8   | 1    | 0    | 0    | 0    | `1000`  | No        | `_0=1` (has top-level comma) -> `_1=_2=_3=1`, contradiction | **0**                |
| 9   | 1    | 0    | 0    | 1    | `1001`  | No        | Same as above                                               | **0**                |
| 10  | 1    | 0    | 1    | 0    | `1010`  | No        | Same as above                                               | **0**                |
| 11  | 1    | 0    | 1    | 1    | `1011`  | No        | Same as above                                               | **0**                |
| 12  | 1    | 1    | 0    | 0    | `1100`  | No        | Same as above                                               | **0**                |
| 13  | 1    | 1    | 0    | 1    | `1101`  | No        | Same as above                                               | **0**                |
| 14  | 1    | 1    | 1    | 0    | `1110`  | No        | Same as above                                               | **0**                |
| 15  | 1    | 1    | 1    | 1    | `1111`  | Yes       | `x, y`; `(a), (b)`; `(), x`; `x, (y)`; `x, y, z`            | **0**                |
*/
#  define LINN_HAS_COMMA_I(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, ...) _15

#  define LINN_HAS_COMMA(...) LINN_HAS_COMMA_I(__VA_ARGS__, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0)

#  define LINN_IS_EMPTY_CASE_UNIQUE_ID(_0, _1, _2, _3, _4) _0##_1##_2##_3##_4
#  define LINN_IS_EMPTY_IMPL(_0, _1, _2, _3)               LINN_HAS_COMMA(LINN_IS_EMPTY_CASE_UNIQUE_ID(LINN_IS_EMPTY_CASE_, _0, _1, _2, _3))
/// 16 IDs
// define LINN_IS_EMPTY_CASE_0000
#  define LINN_IS_EMPTY_CASE_0001                          , // Empty ID
// define LINN_IS_EMPTY_CASE_0010
// ...
// define LINN_IS_EMPTY_CASE_1111
#  define LINN_TRIGGER_PARENTHESIS(...)                    ,

#  define LINN_IS_EMPTY(...)                                  \
    LINN_IS_EMPTY_IMPL(                                       \
        LINN_HAS_COMMA(__VA_ARGS__),                          \
        LINN_HAS_COMMA(LINN_TRIGGER_PARENTHESIS __VA_ARGS__), \
        LINN_HAS_COMMA(__VA_ARGS__()),                        \
        LINN_HAS_COMMA(LINN_TRIGGER_PARENTHESIS __VA_ARGS__()))
#  define LINN_NARGS_EMPTY_1(...) 0
#  define LINN_NARGS_EMPTY_0(...) LINN_NARGS_SELECT(                                                                     \
      __VA_ARGS__,                                                                                                       \
      256, 255, 254, 253, 252, 251, 250, 249, 248, 247, 246, 245, 244, 243, 242, 241, 240, 239, 238, 237, 236, 235, 234, \
      233, 232, 231, 230, 229, 228, 227, 226, 225, 224, 223, 222, 221, 220, 219, 218, 217, 216, 215, 214, 213, 212,      \
      211, 210, 209, 208, 207, 206, 205, 204, 203, 202, 201, 200, 199, 198, 197, 196, 195, 194, 193, 192, 191, 190,      \
      189, 188, 187, 186, 185, 184, 183, 182, 181, 180, 179, 178, 177, 176, 175, 174, 173, 172, 171, 170, 169, 168,      \
      167, 166, 165, 164, 163, 162, 161, 160, 159, 158, 157, 156, 155, 154, 153, 152, 151, 150, 149, 148, 147, 146,      \
      145, 144, 143, 142, 141, 140, 139, 138, 137, 136, 135, 134, 133, 132, 131, 130, 129, 128, 127, 126, 125, 124,      \
      123, 122, 121, 120, 119, 118, 117, 116, 115, 114, 113, 112, 111, 110, 109, 108, 107, 106, 105, 104, 103, 102,      \
      101, 100, 99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 77, 76, 75,      \
      74, 73, 72, 71, 70, 69, 68, 67, 66, 65, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47,    \
      46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19,    \
      18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0)
#  define LINN_NARGS(...) LINN_CAT(LINN_NARGS_EMPTY_, LINN_IS_EMPTY(__VA_ARGS__))(__VA_ARGS__)
#endif
#endif // !LINN_UTILS_H_
