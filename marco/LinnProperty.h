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

#ifndef LINN_PROPERTY_H_
#define LINN_PROPERTY_H_

#include <cstdlib>     // std::abort
#include <iostream>    // std::cerr
#include <type_traits> // std::remove_cvref_t
#include <utility>     // std::move

#if defined(QT_DEBUG) || defined(QT_NO_DEBUG) || defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#  include <QDebug>
#endif

#include "LinnPimpl.h"
#include "LinnUtils.h"

#if (defined(LINN_PROPERTY_ACCESSOR_PREFIX_PASCAL) + defined(LINN_PROPERTY_ACCESSOR_PREFIX_CAMEL) + defined(LINN_PROPERTY_ACCESSOR_PREFIX_SNAKE)) > 1
#  error "Only one LINN_PROPERTY_ACCESSOR_PREFIX_* may be defined at a time"
#endif

#if !defined(LINN_PROPERTY_ACCESSOR_PREFIX_PASCAL) && !defined(LINN_PROPERTY_ACCESSOR_PREFIX_CAMEL) && \
    !defined(LINN_PROPERTY_ACCESSOR_PREFIX_SNAKE)
#  define LINN_PROPERTY_ACCESSOR_PREFIX_CAMEL
#endif

namespace linn_property_detail
{
[[noreturn]] inline void contract_violation(char const *expression, char const *file, int line, char const *function)
{
#if defined(QT_DEBUG) || defined(QT_NO_DEBUG) || defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
  qCritical()
#else
  std::cerr
#endif
      << "[CRITICAL CONTRACT VIOLATION] " << expression << '\n'
      << "  File: " << file << ':' << line << '\n'
      << "  Function: " << function << '\n';
  std::abort();
}

template <typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename Member, typename SetType>
inline constexpr bool setter_uses_move_v =
    std::is_rvalue_reference_v<SetType> ||
    (!std::is_reference_v<SetType> && !std::is_trivially_copyable_v<Member> && std::is_move_assignable_v<Member>);

template <typename Member, typename SetType>
inline constexpr bool setter_noexcept_v =
    std::is_nothrow_assignable_v<
        Member &,
        std::conditional_t<
            setter_uses_move_v<Member, SetType>,
            std::remove_reference_t<SetType> &&,
            std::remove_reference_t<SetType> &>>;

template <typename Handler>
inline constexpr bool contract_handler_noexcept_v =
    std::is_nothrow_invocable_v<Handler, char const *, char const *, int, char const *>;

template <typename Member, typename SetType>
LINN_ALWAYS_INLINE void assign_property(Member &member, SetType &&value) noexcept(setter_noexcept_v<Member, SetType>)
{
  if constexpr (setter_uses_move_v<Member, SetType>) {
    member = std::move(value);
  } else {
    member = value;
  }
}

/// definitions of helper macros
#pragma region LINN_PROPERTY_HELPER_CORE

#if defined(LINN_PROPERTY_ACCESSOR_PREFIX_PASCAL)
#  define LINN_PROPERTY_HELPER_SETTER_PREFIX Set
#  define LINN_PROPERTY_HELPER_GETTER_PREFIX Get
#  define LINN_PROPERTY_HELPER_SIGNAL_PREFIX Signal
#elif defined(LINN_PROPERTY_ACCESSOR_PREFIX_CAMEL)
#  define LINN_PROPERTY_HELPER_SETTER_PREFIX set
#  define LINN_PROPERTY_HELPER_GETTER_PREFIX get
#  define LINN_PROPERTY_HELPER_SIGNAL_PREFIX signal
#elif defined(LINN_PROPERTY_ACCESSOR_PREFIX_SNAKE)
#  define LINN_PROPERTY_HELPER_SETTER_PREFIX set_
#  define LINN_PROPERTY_HELPER_GETTER_PREFIX get_
#  define LINN_PROPERTY_HELPER_SIGNAL_PREFIX signal_
#endif

#define LINN_PROPERTY_HELPER_ACCESSOR_NAME_I(prefix, name) prefix##name
#define LINN_PROPERTY_HELPER_ACCESSOR_NAME(prefix, name)   LINN_PROPERTY_HELPER_ACCESSOR_NAME_I(prefix, name)

#define LINN_PROPERTY_HELPER_FIRST_I(first, ...) first
#define LINN_PROPERTY_HELPER_FIRST(DESC)         LINN_PROPERTY_HELPER_FIRST_I DESC


/// Extra Qt Property
/// moc records the alias spelling but resolves the runtime metatype through
/// TypeAndForceComplete, so the project alias keeps this header C++17-safe.
#define LINN_PROPERTY_HELPER_HAS_EXTRA_QT_PROPERTY(EXTRA_QT_PROP) LINN_IS_PAREN(LINN_EXPAND(EXTRA_QT_PROP))

#define LINN_PROPERTY_HELPER_CREATE_EXTRA_QT_PROPERTY(MEMBER_TYPE, MEMBER_NAME, EXTRA_QT_PROP) \
  LINN_CAT(LINN_PROPERTY_HELPER_CREATE_EXTRA_QT_PROPERTY_, LINN_PROPERTY_HELPER_HAS_EXTRA_QT_PROPERTY(EXTRA_QT_PROP))(MEMBER_TYPE, MEMBER_NAME)

#define LINN_PROPERTY_HELPER_CREATE_EXTRA_QT_PROPERTY_1(MEMBER_TYPE, MEMBER_NAME)                                                                                               \
  Q_PROPERTY(::linn_property_detail::remove_cvref_t<MEMBER_TYPE> MEMBER_NAME MEMBER LINN_CAT(m_, MEMBER_NAME) NOTIFY LINN_CAT(LINN_PROPERTY_HELPER_SIGNAL_PREFIX, MEMBER_NAME)) \
                                                                                                                                                                                \
public:                                                                                                                                                                         \
  Q_SIGNAL void LINN_CAT(LINN_PROPERTY_HELPER_SIGNAL_PREFIX, MEMBER_NAME)();

#define LINN_PROPERTY_HELPER_CREATE_EXTRA_QT_PROPERTY_0(MEMBER_TYPE, MEMBER_NAME)

#define LINN_PROPERTY_HELPER_CREATE_H_EXTRA_QT_PROPERTY(MEMBER_TYPE, MEMBER_NAME, EXTRA_QT_PROP) \
  LINN_CAT(LINN_PROPERTY_HELPER_CREATE_H_EXTRA_QT_PROPERTY_, LINN_PROPERTY_HELPER_HAS_EXTRA_QT_PROPERTY(EXTRA_QT_PROP))(MEMBER_TYPE, MEMBER_NAME)

#define LINN_PROPERTY_HELPER_CREATE_H_EXTRA_QT_PROPERTY_1(MEMBER_TYPE, MEMBER_NAME)                     \
  Q_PROPERTY(                                                                                           \
      ::linn_property_detail::remove_cvref_t<MEMBER_TYPE> MEMBER_NAME                                   \
          READ LINN_PROPERTY_HELPER_ACCESSOR_NAME(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME)      \
              WRITE LINN_PROPERTY_HELPER_ACCESSOR_NAME(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME) \
                  NOTIFY LINN_CAT(LINN_PROPERTY_HELPER_SIGNAL_PREFIX, MEMBER_NAME))                     \
                                                                                                        \
public:                                                                                                 \
  Q_SIGNAL void LINN_CAT(LINN_PROPERTY_HELPER_SIGNAL_PREFIX, MEMBER_NAME)();

#define LINN_PROPERTY_HELPER_CREATE_H_EXTRA_QT_PROPERTY_0(MEMBER_TYPE, MEMBER_NAME)


/// Member Init
#define LINN_PROPERTY_HELPER_GETTER_IS_VALUE_TYPE(GETTER)        LINN_IS_PAREN(LINN_EXPAND(GETTER))
#define LINN_PROPERTY_HELPER_MEMBER_INIT_1(MEMBER_DEFAULT_VALUE) { LINN_PAIR_SECOND(MEMBER_DEFAULT_VALUE) }
#define LINN_PROPERTY_HELPER_MEMBER_INIT_0(MEMBER_DEFAULT_VALUE)
#define LINN_PROPERTY_HELPER_MEMBER_INIT(MEMBER_DEFAULT_VALUE) \
  LINN_CAT(LINN_PROPERTY_HELPER_MEMBER_INIT_, LINN_IS_PAREN(LINN_EXPAND(MEMBER_DEFAULT_VALUE)))(MEMBER_DEFAULT_VALUE)


/// Setter Pre Contract
#define LINN_PROPERTY_HELPER_DEFAULT_PRE_CONTRACT_HANDLER    ::linn_property_detail::contract_violation
#define LINN_PROPERTY_HELPER_HAS_PRE_CONTRACT(CONTRACT_DESC) LINN_IS_PAREN(LINN_EXPAND(CONTRACT_DESC))

#define LINN_PROPERTY_HELPER_PRE_CONTRACT_MODE(CONTRACT_DESC)    LINN_TRIPLE_FIRST(CONTRACT_DESC)
#define LINN_PROPERTY_HELPER_PRE_CONTRACT_EXPR(CONTRACT_DESC)    LINN_TRIPLE_SECOND(CONTRACT_DESC)
#define LINN_PROPERTY_HELPER_PRE_CONTRACT_HANDLER(CONTRACT_DESC) LINN_TRIPLE_THIRD(CONTRACT_DESC)

#define LINN_PROPERTY_HELPER_PRE_CONTRACT_MODE_LPST_SETTER_PRE_CONTRACT_ALWAYS LINN_PROBE(~)
#define LINN_PROPERTY_HELPER_PRE_CONTRACT_MODE_LPST_SETTER_PRE_CONTRACT_DEBUG  LINN_PROBE(~)

#define LINN_PROPERTY_HELPER_IS_PRE_CONTRACT(DESC)   LINN_CAT(LINN_PROPERTY_HELPER_IS_PRE_CONTRACT_, LINN_IS_PAREN(LINN_EXPAND(DESC)))(DESC)
#define LINN_PROPERTY_HELPER_IS_PRE_CONTRACT_0(DESC) 0
#define LINN_PROPERTY_HELPER_IS_PRE_CONTRACT_1(DESC) LINN_CHECK(LINN_CAT(LINN_PROPERTY_HELPER_PRE_CONTRACT_MODE_, LINN_PROPERTY_HELPER_FIRST(DESC)))

#define LINN_PROPERTY_HELPER_SETTER_2_CONTRACT(SECOND)   LINN_CAT(LINN_PROPERTY_HELPER_SETTER_2_CONTRACT_, LINN_PROPERTY_HELPER_IS_PRE_CONTRACT(SECOND))(SECOND)
#define LINN_PROPERTY_HELPER_SETTER_2_CONTRACT_1(SECOND) SECOND
#define LINN_PROPERTY_HELPER_SETTER_2_CONTRACT_0(SECOND) LPS_SETTER_NO_PRE_CONTRACT

#define LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT(DESC)   LINN_CAT(LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT_, LINN_PROPERTY_HELPER_HAS_PRE_CONTRACT(DESC))(DESC)
#define LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT_0(DESC) true
#define LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT_1(DESC) LINN_CAT(LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT_MODE_, LINN_PROPERTY_HELPER_PRE_CONTRACT_MODE(DESC))(DESC)

#define LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT_MODE_LPST_SETTER_PRE_CONTRACT_ALWAYS(DESC) \
  ::linn_property_detail::contract_handler_noexcept_v<decltype(LINN_PROPERTY_HELPER_PRE_CONTRACT_HANDLER(DESC))>
#if defined(DEBUG) || defined(_DEBUG)
#  define LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT_MODE_LPST_SETTER_PRE_CONTRACT_DEBUG(DESC) \
    ::linn_property_detail::contract_handler_noexcept_v<decltype(LINN_PROPERTY_HELPER_PRE_CONTRACT_HANDLER(DESC))>
#else
#  define LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT_MODE_LPST_SETTER_PRE_CONTRACT_DEBUG(DESC) true
#endif

#define LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK(MODE, EXPR, HANDLER) LINN_CAT(LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_, MODE)(EXPR, HANDLER)
#define LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_LPST_SETTER_PRE_CONTRACT_ALWAYS(EXPR, HANDLER) \
  do {                                                                                         \
    if (!(EXPR)) {                                                                             \
      HANDLER(LINN_STRINGIFY(EXPR), __FILE__, __LINE__, LINN_PRETTY_FUNCTION);                 \
    }                                                                                          \
  } while (0)

#if defined(DEBUG) || defined(_DEBUG)
#  define LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_LPST_SETTER_PRE_CONTRACT_DEBUG(EXPR, HANDLER) \
    LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_LPST_SETTER_PRE_CONTRACT_ALWAYS(EXPR, HANDLER)
#else
#  define LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_LPST_SETTER_PRE_CONTRACT_DEBUG(EXPR, HANDLER) ((void) 0)
#endif

#define LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_IF_1(MODE, EXPR, HANDLER) LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK(MODE, EXPR, HANDLER)
#define LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_IF_0(MODE, EXPR, HANDLER) ((void) 0)
#define LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_IF(COND, MODE, EXPR, HANDLER) \
  LINN_CAT(LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_IF_, COND)(MODE, EXPR, HANDLER)


/// Setter Post Action
#define LINN_PROPERTY_HELPER_HAS_POST_ACTION(DESC)  LINN_PROPERTY_HELPER_IS_POST_ACTION(DESC)
#define LINN_PROPERTY_HELPER_POST_ACTION_EXPR(DESC) LINN_PAIR_SECOND(DESC)
#define LINN_PROPERTY_HELPER_POST_ACTION_RUN(DESC)  LINN_PROPERTY_HELPER_POST_ACTION_EXPR(DESC)

#define LINN_PROPERTY_HELPER_POST_ACTION_TAG_LPST_SETTER_POST_ACTION LINN_PROBE(~)

#define LINN_PROPERTY_HELPER_IS_POST_ACTION(DESC)   LINN_CAT(LINN_PROPERTY_HELPER_IS_POST_ACTION_, LINN_IS_PAREN(LINN_EXPAND(DESC)))(DESC)
#define LINN_PROPERTY_HELPER_IS_POST_ACTION_0(DESC) 0
#define LINN_PROPERTY_HELPER_IS_POST_ACTION_1(DESC) LINN_CHECK(LINN_CAT(LINN_PROPERTY_HELPER_POST_ACTION_TAG_, LINN_PROPERTY_HELPER_FIRST(DESC)))

#define LINN_PROPERTY_HELPER_POST_ACTION_NOEXCEPT(DESC)   LINN_CAT(LINN_PROPERTY_HELPER_POST_ACTION_NOEXCEPT_, LINN_PROPERTY_HELPER_HAS_POST_ACTION(DESC))(DESC)
#define LINN_PROPERTY_HELPER_POST_ACTION_NOEXCEPT_0(DESC) true
#define LINN_PROPERTY_HELPER_POST_ACTION_NOEXCEPT_1(DESC) noexcept(LINN_PROPERTY_HELPER_POST_ACTION_EXPR(DESC))

#define LINN_PROPERTY_HELPER_POST_ACTION_IF(COND, DESC) LINN_CAT(LINN_PROPERTY_HELPER_POST_ACTION_IF_, COND)(DESC)
#define LINN_PROPERTY_HELPER_POST_ACTION_IF_0(DESC)     ((void) 0)
#define LINN_PROPERTY_HELPER_POST_ACTION_IF_1(DESC)     LINN_PROPERTY_HELPER_POST_ACTION_RUN(DESC)

#define LINN_PROPERTY_HELPER_POST_ACTION_DESC(POST_ACTION)   LINN_CAT(LINN_PROPERTY_HELPER_POST_ACTION_DESC_, LINN_PROPERTY_HELPER_IS_POST_ACTION(POST_ACTION))(POST_ACTION)
#define LINN_PROPERTY_HELPER_POST_ACTION_DESC_1(POST_ACTION) POST_ACTION
#define LINN_PROPERTY_HELPER_POST_ACTION_DESC_0(POST_ACTION) (LINN_PROPERTY_STRATEGY_TAG_SETTER_POST_ACTION_INVALID, (POST_ACTION))

#define LINN_PROPERTY_HELPER_IS_INVALID_POST_ACTION(DESC)   LINN_CAT(LINN_PROPERTY_HELPER_IS_INVALID_POST_ACTION_, LINN_IS_PAREN(LINN_EXPAND(DESC)))(DESC)
#define LINN_PROPERTY_HELPER_IS_INVALID_POST_ACTION_0(DESC) 0
#define LINN_PROPERTY_HELPER_IS_INVALID_POST_ACTION_1(DESC) \
  LINN_CHECK(LINN_CAT(LINN_PROPERTY_HELPER_INVALID_POST_ACTION_TAG_, LINN_PROPERTY_HELPER_FIRST(DESC)))
#define LINN_PROPERTY_HELPER_INVALID_POST_ACTION_TAG_LPST_SETTER_POST_ACTION_INVALID LINN_PROBE(~)

#define LINN_PROPERTY_HELPER_VALIDATE_POST_ACTION(DESC) \
  static_assert(!LINN_PROPERTY_HELPER_IS_INVALID_POST_ACTION(DESC), "POST_ACTION must be wrapped in LPS_POST_ACTION(...)")

#define LINN_PROPERTY_HELPER_VALIDATE_H_SETTER(SETTER_DESC)                                                                                   \
  static_assert(!LINN_PROPERTY_HELPER_HAS_PRE_CONTRACT(LINN_TRIPLE_SECOND(SETTER_DESC)), "PRE_CONTRACT belongs in LINN_PROPERTY_CREATE_CPP"); \
  static_assert(!LINN_PROPERTY_HELPER_HAS_POST_ACTION(LINN_TRIPLE_THIRD(SETTER_DESC)), "POST_ACTION belongs in LINN_PROPERTY_CREATE_CPP")

#define LINN_PROPERTY_HELPER_SETTER_2_POST_ACTION(SECOND)   LINN_CAT(LINN_PROPERTY_HELPER_SETTER_2_POST_ACTION_, LINN_PROPERTY_HELPER_IS_PRE_CONTRACT(SECOND))(SECOND)
#define LINN_PROPERTY_HELPER_SETTER_2_POST_ACTION_1(SECOND) LPS_SETTER_NO_POST_ACTION
#define LINN_PROPERTY_HELPER_SETTER_2_POST_ACTION_0(SECOND) LINN_PROPERTY_HELPER_POST_ACTION_DESC(SECOND)


/// LINN_PROPERTY_CREATER Implement
#define LINN_PROPERTY_HELPER_CREATE_DISPATCH(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC, EXTRA_QT_PROP) \
  LINN_CAT(LINN_PROPERTY_HELPER_CREATE_GETTER_, LINN_PROPERTY_HELPER_GETTER_IS_VALUE_TYPE(GETTER_DESC))(                                                            \
      MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC, EXTRA_QT_PROP)

#define LINN_PROPERTY_HELPER_CREATE_GETTER_1(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC, EXTRA_QT_PROP) \
  LINN_PROPERTY_HELPER_CREATE_BODY(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, LINN_PAIR_SECOND(GETTER_DESC), 1, EXTRA_QT_PROP)

#define LINN_PROPERTY_HELPER_CREATE_GETTER_0(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC, EXTRA_QT_PROP) \
  LINN_PROPERTY_HELPER_CREATE_BODY(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, MEMBER_TYPE, 0, EXTRA_QT_PROP)

#define LINN_PROPERTY_HELPER_CREATE_BODY(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GET_TYPE, GETTER_KIND, EXTRA_QT_PROP) \
                                                                                                                                                                          \
  LINN_PROPERTY_HELPER_VALIDATE_POST_ACTION(POST_ACTION_DESC);                                                                                                            \
  LINN_PROPERTY_HELPER_CREATE_EXTRA_QT_PROPERTY(MEMBER_TYPE, MEMBER_NAME, EXTRA_QT_PROP)                                                                                  \
                                                                                                                                                                          \
public:                                                                                                                                                                   \
  void LINN_PROPERTY_HELPER_ACCESSOR_NAME(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME)(SET_TYPE MEMBER_NAME) noexcept(                                                \
      LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT(CONTRACT_DESC) &&                                                                                                        \
      ::linn_property_detail::setter_noexcept_v<::linn_property_detail::remove_cvref_t<MEMBER_TYPE>, SET_TYPE> &&                                                         \
      LINN_PROPERTY_HELPER_POST_ACTION_NOEXCEPT(POST_ACTION_DESC))                                                                                                        \
  {                                                                                                                                                                       \
    LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_IF(                                                                                                                           \
        LINN_PROPERTY_HELPER_HAS_PRE_CONTRACT(CONTRACT_DESC),                                                                                                             \
        LINN_PROPERTY_HELPER_PRE_CONTRACT_MODE(CONTRACT_DESC),                                                                                                            \
        LINN_PROPERTY_HELPER_PRE_CONTRACT_EXPR(CONTRACT_DESC),                                                                                                            \
        LINN_PROPERTY_HELPER_PRE_CONTRACT_HANDLER(CONTRACT_DESC));                                                                                                        \
    ::linn_property_detail::assign_property<::linn_property_detail::remove_cvref_t<MEMBER_TYPE>, SET_TYPE>(                                                               \
        LINN_CAT(m_, MEMBER_NAME), std::forward<SET_TYPE>(MEMBER_NAME));                                                                                                  \
    LINN_PROPERTY_HELPER_POST_ACTION_IF(LINN_PROPERTY_HELPER_HAS_POST_ACTION(POST_ACTION_DESC), POST_ACTION_DESC);                                                        \
  }                                                                                                                                                                       \
  LINN_PROPERTY_HELPER_GETTER(GETTER_KIND, MEMBER_TYPE, GET_TYPE, MEMBER_NAME)                                                                                            \
                                                                                                                                                                          \
private:                                                                                                                                                                  \
  ::linn_property_detail::remove_cvref_t<MEMBER_TYPE> LINN_CAT(m_, MEMBER_NAME) LINN_PROPERTY_HELPER_MEMBER_INIT(MEMBER_DEFAULT_VALUE);

#define LINN_PROPERTY_HELPER_GETTER(KIND, MEMBER_TYPE, GET_TYPE, MEMBER_NAME) LINN_CAT(LINN_PROPERTY_HELPER_GETTER_, KIND)(MEMBER_TYPE, GET_TYPE, MEMBER_NAME)
#define LINN_PROPERTY_HELPER_GETTER_1(MEMBER_TYPE, GET_TYPE, MEMBER_NAME)                                                                                                     \
  [[nodiscard]] GET_TYPE LINN_PROPERTY_HELPER_ACCESSOR_NAME(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME)() const noexcept(std::is_nothrow_copy_constructible_v<GET_TYPE>) \
  {                                                                                                                                                                           \
    return LINN_CAT(m_, MEMBER_NAME);                                                                                                                                         \
  }

#define LINN_PROPERTY_HELPER_GETTER_0(MEMBER_TYPE, GET_TYPE, MEMBER_NAME)                                                                                                                      \
  [[nodiscard]] ::linn_property_detail::remove_cvref_t<MEMBER_TYPE> const &                                                                                                                    \
  LINN_PROPERTY_HELPER_ACCESSOR_NAME(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME)() const & noexcept                                                                                       \
  {                                                                                                                                                                                            \
    return LINN_CAT(m_, MEMBER_NAME);                                                                                                                                                          \
  }                                                                                                                                                                                            \
  [[nodiscard]] ::linn_property_detail::remove_cvref_t<MEMBER_TYPE>                                                                                                                            \
  LINN_PROPERTY_HELPER_ACCESSOR_NAME(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME)() && noexcept(std::is_nothrow_move_constructible_v<::linn_property_detail::remove_cvref_t<MEMBER_TYPE>>) \
  {                                                                                                                                                                                            \
    return std::move(LINN_CAT(m_, MEMBER_NAME));                                                                                                                                               \
  }


/// Split header
#define LINN_PROPERTY_HELPER_CREATE_H_DISPATCH(MEMBER_TYPE, MEMBER_NAME, SET_TYPE, GETTER_DESC, EXTRA_QT_PROP) \
  LINN_CAT(LINN_PROPERTY_HELPER_CREATE_H_DISPATCH_, LINN_PROPERTY_HELPER_GETTER_IS_VALUE_TYPE(GETTER_DESC))(   \
      MEMBER_TYPE, MEMBER_NAME, SET_TYPE, GETTER_DESC, EXTRA_QT_PROP)

#define LINN_PROPERTY_HELPER_CREATE_H_DISPATCH_1(MEMBER_TYPE, MEMBER_NAME, SET_TYPE, GETTER_DESC, EXTRA_QT_PROP) \
  LINN_PROPERTY_HELPER_CREATE_H_BODY(MEMBER_TYPE, MEMBER_NAME, SET_TYPE, LINN_PAIR_SECOND(GETTER_DESC), 1, EXTRA_QT_PROP)

#define LINN_PROPERTY_HELPER_CREATE_H_DISPATCH_0(MEMBER_TYPE, MEMBER_NAME, SET_TYPE, GETTER_DESC, EXTRA_QT_PROP) \
  LINN_PROPERTY_HELPER_CREATE_H_BODY(MEMBER_TYPE, MEMBER_NAME, SET_TYPE, MEMBER_TYPE, 0, EXTRA_QT_PROP)

#define LINN_PROPERTY_HELPER_CREATE_H_BODY(MEMBER_TYPE, MEMBER_NAME, SET_TYPE, GET_TYPE, GETTER_KIND, EXTRA_QT_PROP) \
                                                                                                                     \
  LINN_PROPERTY_HELPER_CREATE_H_EXTRA_QT_PROPERTY(MEMBER_TYPE, MEMBER_NAME, EXTRA_QT_PROP)                           \
                                                                                                                     \
public:                                                                                                              \
  void LINN_PROPERTY_HELPER_ACCESSOR_NAME(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME)(SET_TYPE MEMBER_NAME);    \
  LINN_PROPERTY_HELPER_CREATE_H_GETTER(GETTER_KIND, MEMBER_TYPE, GET_TYPE, MEMBER_NAME)

#define LINN_PROPERTY_HELPER_CREATE_H_GETTER(KIND, MEMBER_TYPE, GET_TYPE, MEMBER_NAME) LINN_CAT(LINN_PROPERTY_HELPER_CREATE_H_GETTER_, KIND)(MEMBER_TYPE, GET_TYPE, MEMBER_NAME)
#define LINN_PROPERTY_HELPER_CREATE_H_GETTER_1(MEMBER_TYPE, GET_TYPE, MEMBER_NAME) \
  [[nodiscard]] GET_TYPE LINN_PROPERTY_HELPER_ACCESSOR_NAME(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME)() const noexcept(std::is_nothrow_copy_constructible_v<GET_TYPE>);

#define LINN_PROPERTY_HELPER_CREATE_H_GETTER_0(MEMBER_TYPE, GET_TYPE, MEMBER_NAME)                            \
  [[nodiscard]] ::linn_property_detail::remove_cvref_t<MEMBER_TYPE> const &                                   \
      LINN_PROPERTY_HELPER_ACCESSOR_NAME(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME)() const & noexcept; \
  [[nodiscard]] ::linn_property_detail::remove_cvref_t<MEMBER_TYPE>                                           \
      LINN_PROPERTY_HELPER_ACCESSOR_NAME(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME)() && noexcept(std::is_nothrow_move_constructible_v<::linn_property_detail::remove_cvref_t<MEMBER_TYPE>>);


/// Split Cpp
#define LINN_PROPERTY_HELPER_CREATE_CPP_DISPATCH(CLASS, MEMBER_TYPE, MEMBER_NAME, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  LINN_CAT(LINN_PROPERTY_HELPER_CREATE_CPP_GETTER_, LINN_PROPERTY_HELPER_GETTER_IS_VALUE_TYPE(GETTER_DESC))(                              \
      CLASS, MEMBER_TYPE, MEMBER_NAME, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC)

#define LINN_PROPERTY_HELPER_CREATE_CPP_GETTER_1(CLASS, MEMBER_TYPE, MEMBER_NAME, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  LINN_PROPERTY_HELPER_CREATE_CPP_BODY(CLASS, MEMBER_TYPE, MEMBER_NAME, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, LINN_PAIR_SECOND(GETTER_DESC), 1)

#define LINN_PROPERTY_HELPER_CREATE_CPP_GETTER_0(CLASS, MEMBER_TYPE, MEMBER_NAME, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  LINN_PROPERTY_HELPER_CREATE_CPP_BODY(CLASS, MEMBER_TYPE, MEMBER_NAME, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, MEMBER_TYPE, 0)

#define LINN_PROPERTY_HELPER_CREATE_CPP_BODY(CLASS, MEMBER_TYPE, MEMBER_NAME, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GET_TYPE, GETTER_KIND) \
  LINN_PROPERTY_HELPER_VALIDATE_POST_ACTION(POST_ACTION_DESC);                                                                                  \
  LINN_PROPERTY_HELPER_CPP_SETTER(CLASS, MEMBER_TYPE, MEMBER_NAME, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC)                                   \
  LINN_PROPERTY_HELPER_CPP_GETTER(GETTER_KIND, CLASS, MEMBER_TYPE, GET_TYPE, MEMBER_NAME)

#define LINN_PROPERTY_HELPER_CPP_SETTER(CLASS, MEMBER_TYPE, MEMBER_NAME, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC)     \
  void CLASS::LINN_PROPERTY_HELPER_ACCESSOR_NAME(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME)(SET_TYPE MEMBER_NAME) \
  {                                                                                                                     \
    LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_IF(                                                                         \
        LINN_PROPERTY_HELPER_HAS_PRE_CONTRACT(CONTRACT_DESC),                                                           \
        LINN_PROPERTY_HELPER_PRE_CONTRACT_MODE(CONTRACT_DESC),                                                          \
        LINN_PROPERTY_HELPER_PRE_CONTRACT_EXPR(CONTRACT_DESC),                                                          \
        LINN_PROPERTY_HELPER_PRE_CONTRACT_HANDLER(CONTRACT_DESC));                                                      \
    ::linn_property_detail::assign_property<::linn_property_detail::remove_cvref_t<MEMBER_TYPE>, SET_TYPE>(             \
        LINN_CAT(m_, MEMBER_NAME), std::forward<SET_TYPE>(MEMBER_NAME));                                                \
    LINN_PROPERTY_HELPER_POST_ACTION_IF(LINN_PROPERTY_HELPER_HAS_POST_ACTION(POST_ACTION_DESC), POST_ACTION_DESC);      \
  }

#define LINN_PROPERTY_HELPER_CPP_GETTER(KIND, CLASS, MEMBER_TYPE, GET_TYPE, MEMBER_NAME) LINN_CAT(LINN_PROPERTY_HELPER_CPP_GETTER_, KIND)(CLASS, MEMBER_TYPE, GET_TYPE, MEMBER_NAME)
#define LINN_PROPERTY_HELPER_CPP_GETTER_1(CLASS, MEMBER_TYPE, GET_TYPE, MEMBER_NAME)                                                                                   \
  GET_TYPE CLASS::LINN_PROPERTY_HELPER_ACCESSOR_NAME(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME)() const noexcept(std::is_nothrow_copy_constructible_v<GET_TYPE>) \
  {                                                                                                                                                                    \
    return LINN_CAT(m_, MEMBER_NAME);                                                                                                                                  \
  }

#define LINN_PROPERTY_HELPER_CPP_GETTER_0(CLASS, MEMBER_TYPE, GET_TYPE, MEMBER_NAME)                                                                                                                  \
  ::linn_property_detail::remove_cvref_t<MEMBER_TYPE> const &                                                                                                                                         \
  CLASS::LINN_PROPERTY_HELPER_ACCESSOR_NAME(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME)() const & noexcept                                                                                       \
  {                                                                                                                                                                                                   \
    return LINN_CAT(m_, MEMBER_NAME);                                                                                                                                                                 \
  }                                                                                                                                                                                                   \
  ::linn_property_detail::remove_cvref_t<MEMBER_TYPE>                                                                                                                                                 \
  CLASS::LINN_PROPERTY_HELPER_ACCESSOR_NAME(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME)() && noexcept(std::is_nothrow_move_constructible_v<::linn_property_detail::remove_cvref_t<MEMBER_TYPE>>) \
  {                                                                                                                                                                                                   \
    return std::move(LINN_CAT(m_, MEMBER_NAME));                                                                                                                                                      \
  }

#pragma endregion LINN_PROPERTY_HELPER_CORE
} // namespace linn_property_detail

/*
 * Property strategy.
 *
 * LPS_MEMBER / LPS_SETTER / LPS_GETTER describe one property. Optional
 * defaults and contracts are encoded as tagged tuples; LINN_PROPERTY_CREATE
 * dispatches on those descriptors and emits a setter, a getter, and a
 * private member.
 *
 * TYPE, PARAM_TYPE, and contract expressions cannot contain a top-level
 * comma. Wrap such expressions in parentheses or use an alias.
 *
 * Setter post actions must be wrapped in LPS_POST_ACTION(...); a bare
 * expression is rejected at compile time.
 */
#define LINN_PROPERTY_STRATEGY_TAG_MEMBER_DEFAULT_VALUE ~

#define LINN_PROPERTY_STRATEGY_TAG_SETTER_PRE_CONTRACT_ALWAYS LPST_SETTER_PRE_CONTRACT_ALWAYS
#define LINN_PROPERTY_STRATEGY_TAG_SETTER_PRE_CONTRACT_DEBUG  LPST_SETTER_PRE_CONTRACT_DEBUG
#define LINN_PROPERTY_STRATEGY_TAG_SETTER_POST_ACTION         LPST_SETTER_POST_ACTION
#define LINN_PROPERTY_STRATEGY_TAG_SETTER_POST_ACTION_INVALID LPST_SETTER_POST_ACTION_INVALID

#define LINN_PROPERTY_STRATEGY_TAG_GETTER_VALUE_TYPE             ~
#define LINN_PROPERTY_STRATEGY_TAG_GETTER_REF_QUALIFIED_OVERLOAD ~

#define LINN_PROPERTY_STRATEGY_TAG_EXTRA_QT_PROPERTY (~)

///   LPS_MEMBER_NO_DEFAULT_VALUE, LPS_SETTER_NO_PRE_CONTRACT, and LPS_SETTER_NO_POST_ACTION stay
///   undefined on purpose: they are unparenthesized sentinels, and the _0 branches discard them
///   before codegen.

#define LPS_MEMBER_3(TYPE, NAME, VALUE) (TYPE, NAME, (LINN_PROPERTY_STRATEGY_TAG_MEMBER_DEFAULT_VALUE, VALUE))
#define LPS_MEMBER_2(TYPE, NAME)        (TYPE, NAME, LPS_MEMBER_NO_DEFAULT_VALUE)
#define LPS_MEMBER(...)                 LINN_CAT(LPS_MEMBER_, LINN_NARGS(__VA_ARGS__))(__VA_ARGS__)

#define LPS_GETTER(GET_TYPE)              (LINN_PROPERTY_STRATEGY_TAG_GETTER_VALUE_TYPE, GET_TYPE)
#define LPS_GETTER_REF_QUALIFIED_OVERLOAD LINN_PROPERTY_STRATEGY_TAG_GETTER_REF_QUALIFIED_OVERLOAD

#define LPS_SETTER_3(PARAM_TYPE, PRE_CONTRACT, POST_ACTION) (PARAM_TYPE, PRE_CONTRACT, LINN_PROPERTY_HELPER_POST_ACTION_DESC(POST_ACTION))
#define LPS_SETTER_2(PARAM_TYPE, SECOND)                    (PARAM_TYPE, LINN_PROPERTY_HELPER_SETTER_2_CONTRACT(SECOND), LINN_PROPERTY_HELPER_SETTER_2_POST_ACTION(SECOND))
#define LPS_SETTER_1(PARAM_TYPE)                            (PARAM_TYPE, LPS_SETTER_NO_PRE_CONTRACT, LPS_SETTER_NO_POST_ACTION)
#define LPS_SETTER(...)                                     LINN_CAT(LPS_SETTER_, LINN_NARGS(__VA_ARGS__))(__VA_ARGS__)

#define LPS_PRE_CONTRACT_2(EXPR, HANDLER) (LINN_PROPERTY_STRATEGY_TAG_SETTER_PRE_CONTRACT_ALWAYS, EXPR, HANDLER)
#define LPS_PRE_CONTRACT_1(EXPR)          (LINN_PROPERTY_STRATEGY_TAG_SETTER_PRE_CONTRACT_ALWAYS, EXPR, LINN_PROPERTY_HELPER_DEFAULT_PRE_CONTRACT_HANDLER)
#define LPS_PRE_CONTRACT(...)             LINN_CAT(LPS_PRE_CONTRACT_, LINN_NARGS(__VA_ARGS__))(__VA_ARGS__)

#define LPS_PRE_CONTRACTD_2(EXPR, HANDLER) (LINN_PROPERTY_STRATEGY_TAG_SETTER_PRE_CONTRACT_DEBUG, EXPR, HANDLER)
#define LPS_PRE_CONTRACTD_1(EXPR)          (LINN_PROPERTY_STRATEGY_TAG_SETTER_PRE_CONTRACT_DEBUG, EXPR, LINN_PROPERTY_HELPER_DEFAULT_PRE_CONTRACT_HANDLER)
#define LPS_PRE_CONTRACTD(...)             LINN_CAT(LPS_PRE_CONTRACTD_, LINN_NARGS(__VA_ARGS__))(__VA_ARGS__)

#define LPS_POST_ACTION(...) (LINN_PROPERTY_STRATEGY_TAG_SETTER_POST_ACTION, (__VA_ARGS__))


/// Accessor Create

#define LINN_PROPERTY_CREATE(...) LINN_CAT(LINN_PROPERTY_CREATE_, LINN_NARGS(__VA_ARGS__))(__VA_ARGS__)
#define LINN_PROPERTY_CREATE_3(MEMBER_DESC, SETTER_DESC, GETTER_DESC)                                  \
  LINN_PROPERTY_HELPER_CREATE_DISPATCH(                                                                \
      LINN_TRIPLE_FIRST(MEMBER_DESC), LINN_TRIPLE_SECOND(MEMBER_DESC), LINN_TRIPLE_THIRD(MEMBER_DESC), \
      LINN_TRIPLE_FIRST(SETTER_DESC), LINN_TRIPLE_SECOND(SETTER_DESC), LINN_TRIPLE_THIRD(SETTER_DESC), \
      GETTER_DESC, LPS_NO_EXTRA_QT_PROP)
#define LINN_PROPERTY_CREATE_4(MEMBER_DESC, SETTER_DESC, GETTER_DESC, EXTRA_QT_PROP)                   \
  LINN_PROPERTY_HELPER_CREATE_DISPATCH(                                                                \
      LINN_TRIPLE_FIRST(MEMBER_DESC), LINN_TRIPLE_SECOND(MEMBER_DESC), LINN_TRIPLE_THIRD(MEMBER_DESC), \
      LINN_TRIPLE_FIRST(SETTER_DESC), LINN_TRIPLE_SECOND(SETTER_DESC), LINN_TRIPLE_THIRD(SETTER_DESC), \
      GETTER_DESC, EXTRA_QT_PROP)

#define LINN_PROPERTY_CREATE_H(...) LINN_CAT(LINN_PROPERTY_CREATE_H_, LINN_NARGS(__VA_ARGS__))(__VA_ARGS__)
#define LINN_PROPERTY_CREATE_H_3(MEMBER_DESC, SETTER_DESC, GETTER_DESC)      \
  LINN_PROPERTY_HELPER_VALIDATE_POST_ACTION(LINN_TRIPLE_THIRD(SETTER_DESC)); \
  LINN_PROPERTY_HELPER_VALIDATE_H_SETTER(SETTER_DESC);                       \
  LINN_PROPERTY_HELPER_CREATE_H_DISPATCH(                                    \
      LINN_TRIPLE_FIRST(MEMBER_DESC), LINN_TRIPLE_SECOND(MEMBER_DESC),       \
      LINN_TRIPLE_FIRST(SETTER_DESC),                                        \
      GETTER_DESC, LPS_NO_EXTRA_QT_PROP)
#define LINN_PROPERTY_CREATE_H_4(MEMBER_DESC, SETTER_DESC, GETTER_DESC, EXTRA_QT_PROP) \
  LINN_PROPERTY_HELPER_VALIDATE_POST_ACTION(LINN_TRIPLE_THIRD(SETTER_DESC));           \
  LINN_PROPERTY_HELPER_VALIDATE_H_SETTER(SETTER_DESC);                                 \
  LINN_PROPERTY_HELPER_CREATE_H_DISPATCH(                                              \
      LINN_TRIPLE_FIRST(MEMBER_DESC), LINN_TRIPLE_SECOND(MEMBER_DESC),                 \
      LINN_TRIPLE_FIRST(SETTER_DESC),                                                  \
      GETTER_DESC, EXTRA_QT_PROP)

#define LINN_PROPERTY_CREATE_CPP(CLASS, MEMBER_DESC, SETTER_DESC, GETTER_DESC)                         \
  LINN_PROPERTY_HELPER_CREATE_CPP_DISPATCH(                                                            \
      CLASS,                                                                                           \
      LINN_TRIPLE_FIRST(MEMBER_DESC), LINN_TRIPLE_SECOND(MEMBER_DESC),                                 \
      LINN_TRIPLE_FIRST(SETTER_DESC), LINN_TRIPLE_SECOND(SETTER_DESC), LINN_TRIPLE_THIRD(SETTER_DESC), \
      GETTER_DESC)


/// Member Create
#define LINN_PROPERTY_CREATE_D(...) LINN_CAT(LINN_PROPERTY_CREATE_D_, LINN_NARGS(__VA_ARGS__))(__VA_ARGS__)
#define LINN_PROPERTY_CREATE_D_2(MEMBER_TYPE, MEMBER_NAME) \
                                                           \
private:                                                   \
  ::linn_property_detail::remove_cvref_t<MEMBER_TYPE> LINN_CAT(m_, MEMBER_NAME);
#define LINN_PROPERTY_CREATE_D_3(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE) \
                                                                                 \
private:                                                                         \
  ::linn_property_detail::remove_cvref_t<MEMBER_TYPE> LINN_CAT(m_, MEMBER_NAME)  \
      LINN_PROPERTY_HELPER_MEMBER_INIT((LINN_PROPERTY_STRATEGY_TAG_MEMBER_DEFAULT_VALUE, MEMBER_DEFAULT_VALUE));


#ifdef LINN_PROPERTY_ENABLE_SHORT_MACROS
#  ifndef MEMBER
#    define MEMBER(...) LPS_MEMBER(__VA_ARGS__)
#  endif
#  ifndef SETTER
#    define SETTER(...) LPS_SETTER(__VA_ARGS__)
#  endif
#  ifndef GETTER
#    define GETTER(...) LPS_GETTER(__VA_ARGS__)
#  endif
#  ifndef GETTER_REF_QUALIFIED_OVERLOAD
#    define GETTER_REF_QUALIFIED_OVERLOAD LPS_GETTER_REF_QUALIFIED_OVERLOAD
#  endif
#  ifndef PRE_CONTRACT
#    define PRE_CONTRACT(...) LPS_PRE_CONTRACT(__VA_ARGS__)
#  endif
#  ifndef PRE_CONTRACTD
#    define PRE_CONTRACTD(...) LPS_PRE_CONTRACTD(__VA_ARGS__)
#  endif
#  ifndef POST_ACTION
#    define POST_ACTION(...) LPS_POST_ACTION(__VA_ARGS__)
#  endif

#  ifndef PROPERTY_CREATE
#    define PROPERTY_CREATE(MEMBER_DESC, SETTER_DESC, GETTER_DESC) LINN_PROPERTY_CREATE(MEMBER_DESC, SETTER_DESC, GETTER_DESC)
#  endif
#endif // LINN_PROPERTY_ENABLE_SHORT_MACROS


#if defined(QT_DEBUG) || defined(QT_NO_DEBUG) || defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)

#  define Q_PROPERTY_CREATE(MEMBER_DESC, SETTER_DESC, GETTER_DESC) LINN_PROPERTY_CREATE(MEMBER_DESC, SETTER_DESC, GETTER_DESC, LINN_PROPERTY_STRATEGY_TAG_EXTRA_QT_PROPERTY)
#  define Q_PRIVATE_CREATE(MEMBER_DESC, SETTER_DESC, GETTER_DESC)  LINN_PROPERTY_CREATE(MEMBER_DESC, SETTER_DESC, GETTER_DESC)

#  define Q_PROPERTY_CREATE_H(MEMBER_DESC, SETTER_DESC, GETTER_DESC)          LINN_PROPERTY_CREATE_H(MEMBER_DESC, SETTER_DESC, GETTER_DESC, LINN_PROPERTY_STRATEGY_TAG_EXTRA_QT_PROPERTY)
#  define Q_PROPERTY_CREATE_CPP(CLASS, MEMBER_DESC, SETTER_DESC, GETTER_DESC) LINN_PROPERTY_CREATE_CPP(CLASS, MEMBER_DESC, SETTER_DESC, GETTER_DESC)

#  define Q_PRIVATE_CREATE_H(MEMBER_DESC, SETTER_DESC, GETTER_DESC)          LINN_PROPERTY_CREATE_H(MEMBER_DESC, SETTER_DESC, GETTER_DESC)
#  define Q_PRIVATE_CREATE_CPP(CLASS, MEMBER_DESC, SETTER_DESC, GETTER_DESC) LINN_PROPERTY_CREATE_CPP(CLASS, MEMBER_DESC, SETTER_DESC, GETTER_DESC)

#  define Q_BEGIN_ENUM_CREATE_IMPL(_1, _2, NAME, ...) NAME
#  define Q_BEGIN_ENUM_CREATE_ARGS(args)              Q_BEGIN_ENUM_CREATE_IMPL args
#  define Q_BEGIN_ENUM_CREATE(...) \
    Q_BEGIN_ENUM_CREATE_ARGS((__VA_ARGS__, Q_BEGIN_ENUM_CREATE_2, Q_BEGIN_ENUM_CREATE_1))(__VA_ARGS__)

// Enum export helper for older Qt 5 versions.
#  if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#    define Q_BEGIN_ENUM_CREATE_1(CLASS) \
      namespace CLASS                    \
      {                                  \
      Q_NAMESPACE_EXPORT()

#    define Q_BEGIN_ENUM_CREATE_2(CLASS, EXPORT) \
      namespace CLASS                            \
      {                                          \
      Q_NAMESPACE_EXPORT(EXPORT)

#    define Q_END_ENUM_CREATE(CLASS) }

#    define Q_ENUM_CREATE(CLASS) Q_ENUM_NS(CLASS)
#  else
#    define Q_BEGIN_ENUM_CREATE_1(CLASS) \
      class CLASS : public QObject       \
      {                                  \
        Q_OBJECT                         \
                                         \
      public:

#    define Q_BEGIN_ENUM_CREATE_2(CLASS, EXPORT) \
      class EXPORT CLASS : public QObject        \
      {                                          \
        Q_OBJECT                                 \
                                                 \
      public:

#    define Q_END_ENUM_CREATE(CLASS) \
                                     \
    private:                         \
      Q_DISABLE_COPY(CLASS)          \
      }                              \
      ;

#    define Q_ENUM_CREATE(CLASS) Q_ENUM(CLASS)
#  endif // QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#endif   // defined(QT_DEBUG) || defined(QT_NO_DEBUG) || defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)

#endif // !LINN_PROPERTY_H_
