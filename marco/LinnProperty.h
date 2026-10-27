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

/**
 * @file   LinnProperty.h
 * @brief  Macro-based property generation for inline, split, and Qt-backed members.
 *
 * @details
 * `LINN_PROPERTY_CREATE` emits the member, setter, and getter inline; `_H` and
 * `_CPP` split declarations and definitions; `_D` defines the member separately.
 * Setters support pre/post hooks; getters support value and ref-qualified forms.
 *
 * @note Requires C++17. Qt aliases activate when Qt is detected.
 */

#ifndef LINN_PROPERTY_H_
#define LINN_PROPERTY_H_

#include <cstdlib>     // std::abort
#include <iostream>    // std::cerr
#include <type_traits> // std::is_reference_v, std::is_trivially_copyable_v, std::is_move_assignable_v ...
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


/**
 * @brief Runtime helpers used by generated accessors.
 */
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


/**
 * @brief Accessor prefix selection and descriptor tags.
 */
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

#define LINN_PROPERTY_HELPER_STRATEGY_TAG_EXTRA_QT_PROPERTY             ~
#define LINN_PROPERTY_HELPER_STRATEGY_TAG_MEMBER_DEFAULT_VALUE          ~
#define LINN_PROPERTY_HELPER_STRATEGY_TAG_GETTER_VALUE_TYPE             LPST_GETTER_VALUE_TYPE
#define LINN_PROPERTY_HELPER_STRATEGY_TAG_GETTER_REF_QUALIFIED_OVERLOAD LPST_GETTER_REF_QUALIFIED_OVERLOAD
#define LINN_PROPERTY_HELPER_STRATEGY_TAG_SETTER_PRE_CONTRACT_ALWAYS    LPST_SETTER_PRE_CONTRACT_ALWAYS
#define LINN_PROPERTY_HELPER_STRATEGY_TAG_SETTER_PRE_CONTRACT_DEBUG     LPST_SETTER_PRE_CONTRACT_DEBUG
#define LINN_PROPERTY_HELPER_STRATEGY_TAG_SETTER_POST_ACTION            LPST_SETTER_POST_ACTION


/**
 * @brief Descriptor access and pre/post hook expansion helpers.
 */
#define LINN_PROPERTY_HELPER_FIRST_I(FIRST, ...) FIRST
#define LINN_PROPERTY_HELPER_FIRST(DESC)         LINN_PROPERTY_HELPER_FIRST_I DESC

#define LINN_PROPERTY_HELPER_POST_ACTION_RUN_1(DESC) LINN_PAIR_SECOND(DESC)
#define LINN_PROPERTY_HELPER_POST_ACTION_RUN_0(DESC) ((void) 0)
#define LINN_PROPERTY_HELPER_POST_ACTION_RUN(DESC) \
  LINN_CAT(LINN_PROPERTY_HELPER_POST_ACTION_RUN_, LINN_IS_PAREN(DESC))(DESC)

#define LINN_PROPERTY_HELPER_POST_ACTION_NOEXCEPT_1(DESC) noexcept(LINN_PAIR_SECOND(DESC))
#define LINN_PROPERTY_HELPER_POST_ACTION_NOEXCEPT_0(DESC) true
#define LINN_PROPERTY_HELPER_POST_ACTION_NOEXCEPT(DESC) \
  LINN_CAT(LINN_PROPERTY_HELPER_POST_ACTION_NOEXCEPT_, LINN_IS_PAREN(DESC))(DESC)

#define LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT_1(DESC) \
  LINN_CAT(LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT_MODE_, LINN_PROPERTY_HELPER_FIRST(DESC))(DESC)
#define LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT_0(DESC) true
#define LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT(DESC) \
  LINN_CAT(LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT_, LINN_IS_PAREN(DESC))(DESC)

#define LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT_MODE_LPST_SETTER_PRE_CONTRACT_ALWAYS(DESC) \
  ::linn_property_detail::contract_handler_noexcept_v<decltype(LINN_TRIPLE_THIRD(DESC))>
#if defined(DEBUG) || defined(_DEBUG)
#  define LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT_MODE_LPST_SETTER_PRE_CONTRACT_DEBUG(DESC) \
    ::linn_property_detail::contract_handler_noexcept_v<decltype(LINN_TRIPLE_THIRD(DESC))>
#else
#  define LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT_MODE_LPST_SETTER_PRE_CONTRACT_DEBUG(DESC) true
#endif

#define LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_1(DESC) \
  LINN_CAT(LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_MODE_, LINN_PROPERTY_HELPER_FIRST(DESC))(LINN_TRIPLE_SECOND(DESC), LINN_TRIPLE_THIRD(DESC))
#define LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_0(DESC) ((void) 0)
#define LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK(DESC) \
  LINN_CAT(LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_, LINN_IS_PAREN(DESC))(DESC)

#define LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_MODE_LPST_SETTER_PRE_CONTRACT_ALWAYS(EXPR, HANDLER) \
  do {                                                                                              \
    if (!(EXPR)) {                                                                                  \
      HANDLER(LINN_STRINGIFY(EXPR), __FILE__, __LINE__, LINN_PRETTY_FUNCTION);                      \
    }                                                                                               \
  } while (false)
#if defined(DEBUG) || defined(_DEBUG)
#  define LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_MODE_LPST_SETTER_PRE_CONTRACT_DEBUG(EXPR, HANDLER) \
    LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_MODE_LPST_SETTER_PRE_CONTRACT_ALWAYS(EXPR, HANDLER)
#else
#  define LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK_MODE_LPST_SETTER_PRE_CONTRACT_DEBUG(EXPR, HANDLER) ((void) 0)
#endif


/**
 * @brief Member initializer selection for an optional default value.
 */
#define LINN_PROPERTY_HELPER_MEMBER_INIT_1(MEMBER_DEFAULT_VALUE) { LINN_PAIR_SECOND(MEMBER_DEFAULT_VALUE) }
#define LINN_PROPERTY_HELPER_MEMBER_INIT_0(MEMBER_DEFAULT_VALUE)
#define LINN_PROPERTY_HELPER_MEMBER_INIT_NONE
#define LINN_PROPERTY_HELPER_MEMBER_INIT(MEMBER_DEFAULT_VALUE) \
  LINN_CAT(LINN_PROPERTY_HELPER_MEMBER_INIT_, LINN_IS_PAREN(MEMBER_DEFAULT_VALUE))(MEMBER_DEFAULT_VALUE)


/**
 * @brief Inline property generation and Qt declaration helpers.
 */
#define LINN_PROPERTY_HELPER_CREATE_QT_DECL(MEMBER_TYPE, MEMBER_NAME, SETTER_NAME, GETTER_NAME, SIGNAL_NAME)       \
  Q_PROPERTY(MEMBER_TYPE MEMBER_NAME MEMBER m_##MEMBER_NAME READ GETTER_NAME WRITE SETTER_NAME NOTIFY SIGNAL_NAME) \
                                                                                                                   \
public:                                                                                                            \
  Q_SIGNAL void SIGNAL_NAME();

#define LINN_PROPERTY_HELPER_CREATE_DISPATCH(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  LINN_CAT(LINN_PROPERTY_HELPER_CREATE_DISPATCH_, LINN_IS_PAREN(GETTER_DESC))(                                                                       \
      MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC)

#define LINN_PROPERTY_HELPER_CREATE_DISPATCH_1(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  LINN_CAT(LINN_PROPERTY_HELPER_CREATE_DISPATCH_TAG_, LINN_PAIR_FIRST(GETTER_DESC))(                                                                   \
      MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC)

#define LINN_PROPERTY_HELPER_CREATE_DISPATCH_TAG_LPST_GETTER_VALUE_TYPE(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  LINN_PROPERTY_HELPER_CREATE_BODY(                                                                                                                                             \
      MEMBER_TYPE, MEMBER_NAME, LINN_PROPERTY_HELPER_MEMBER_INIT(MEMBER_DEFAULT_VALUE),                                                                                         \
      SET_TYPE, LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),                                                                                                      \
      LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT(CONTRACT_DESC),                                                                                                                \
      LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK(CONTRACT_DESC),                                                                                                                   \
      LINN_PROPERTY_HELPER_POST_ACTION_NOEXCEPT(POST_ACTION_DESC),                                                                                                              \
      LINN_PROPERTY_HELPER_POST_ACTION_RUN(POST_ACTION_DESC),                                                                                                                   \
      LINN_PAIR_SECOND(GETTER_DESC), LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 1)

#define LINN_PROPERTY_HELPER_CREATE_DISPATCH_TAG_LPST_GETTER_REF_QUALIFIED_OVERLOAD(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  LINN_PROPERTY_HELPER_CREATE_BODY(                                                                                                                                                         \
      MEMBER_TYPE, MEMBER_NAME, LINN_PROPERTY_HELPER_MEMBER_INIT(MEMBER_DEFAULT_VALUE),                                                                                                     \
      SET_TYPE, LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),                                                                                                                  \
      LINN_PROPERTY_HELPER_PRE_CONTRACT_NOEXCEPT(CONTRACT_DESC),                                                                                                                            \
      LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK(CONTRACT_DESC),                                                                                                                               \
      LINN_PROPERTY_HELPER_POST_ACTION_NOEXCEPT(POST_ACTION_DESC),                                                                                                                          \
      LINN_PROPERTY_HELPER_POST_ACTION_RUN(POST_ACTION_DESC),                                                                                                                               \
      MEMBER_TYPE, LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 0)

#define LINN_PROPERTY_HELPER_CREATE_DISPATCH_0(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  static_assert(false, "GETTER must be wrapped in LPS_GETTER(TYPE) or LPS_GETTER_REF_QUALIFIED_OVERLOAD");

#define LINN_PROPERTY_HELPER_CREATE_BODY(MEMBER_TYPE, MEMBER_NAME, MEMBER_INIT, SET_TYPE, SETTER_NAME, PRE_NOEXCEPT, PRE_CHECK, POST_NOEXCEPT, POST_RUN, GET_TYPE, GETTER_NAME, GETTER_KIND) \
                                                                                                                                                                                             \
public:                                                                                                                                                                                      \
  void SETTER_NAME(SET_TYPE MEMBER_NAME) noexcept(                                                                                                                                           \
      (PRE_NOEXCEPT) &&                                                                                                                                                                      \
      ::linn_property_detail::setter_noexcept_v<MEMBER_TYPE, SET_TYPE> &&                                                                                                                    \
      (POST_NOEXCEPT))                                                                                                                                                                       \
  {                                                                                                                                                                                          \
    PRE_CHECK;                                                                                                                                                                               \
    ::linn_property_detail::assign_property<MEMBER_TYPE, SET_TYPE>(m_##MEMBER_NAME, std::forward<SET_TYPE>(MEMBER_NAME));                                                                    \
    POST_RUN;                                                                                                                                                                                \
  }                                                                                                                                                                                          \
LINN_PROPERTY_HELPER_CREATE_GETTER_##GETTER_KIND(MEMBER_TYPE, MEMBER_NAME, GET_TYPE, GETTER_NAME) private : MEMBER_TYPE m_##MEMBER_NAME MEMBER_INIT;

#define LINN_PROPERTY_HELPER_CREATE_GETTER_1(MEMBER_TYPE, MEMBER_NAME, GET_TYPE, GETTER_NAME)         \
  [[nodiscard]] GET_TYPE GETTER_NAME() const noexcept(std::is_nothrow_copy_constructible_v<GET_TYPE>) \
  {                                                                                                   \
    return m_##MEMBER_NAME;                                                                           \
  }

#define LINN_PROPERTY_HELPER_CREATE_GETTER_0(MEMBER_TYPE, MEMBER_NAME, GET_TYPE, GETTER_NAME)            \
  [[nodiscard]] MEMBER_TYPE const &GETTER_NAME() const & noexcept                                        \
  {                                                                                                      \
    return m_##MEMBER_NAME;                                                                              \
  }                                                                                                      \
  [[nodiscard]] MEMBER_TYPE GETTER_NAME() && noexcept(std::is_nothrow_move_constructible_v<MEMBER_TYPE>) \
  {                                                                                                      \
    return std::move(m_##MEMBER_NAME);                                                                   \
  }


/**
 * @brief Header-side split property declaration helpers.
 */
#define LINN_PROPERTY_HELPER_CREATE_H_QT_DECL(MEMBER_TYPE, MEMBER_NAME, SETTER_NAME, GETTER_NAME, SIGNAL_NAME) \
  Q_PROPERTY(MEMBER_TYPE MEMBER_NAME READ GETTER_NAME WRITE SETTER_NAME NOTIFY SIGNAL_NAME)                    \
                                                                                                               \
public:                                                                                                        \
  Q_SIGNAL void SIGNAL_NAME();

#define LINN_PROPERTY_HELPER_CREATE_H_DISPATCH(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  LINN_CAT3(LINN_PROPERTY_HELPER_CREATE_H_DISPATCH_, LINN_IS_PAREN(CONTRACT_DESC), LINN_IS_PAREN(POST_ACTION_DESC))(                                   \
      MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC)

#define LINN_PROPERTY_HELPER_CREATE_H_DISPATCH_00(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  LINN_PROPERTY_HELPER_CREATE_H_GETTER_DISPATCH(                                                                                                          \
      MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, GETTER_DESC)

#define LINN_PROPERTY_HELPER_CREATE_H_DISPATCH_01(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  static_assert(false, "PRE_CONTRACT and POST_ACTION belong in LINN_PROPERTY_CREATE_CPP");

#define LINN_PROPERTY_HELPER_CREATE_H_DISPATCH_10(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  static_assert(false, "PRE_CONTRACT and POST_ACTION belong in LINN_PROPERTY_CREATE_CPP");

#define LINN_PROPERTY_HELPER_CREATE_H_DISPATCH_11(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  static_assert(false, "PRE_CONTRACT and POST_ACTION belong in LINN_PROPERTY_CREATE_CPP");

#define LINN_PROPERTY_HELPER_CREATE_H_GETTER_DISPATCH(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, GETTER_DESC)  \
  LINN_CAT3(LINN_PROPERTY_HELPER_CREATE_H_GETTER_DISPATCH_, LINN_IS_PAREN(GETTER_DESC), LINN_IS_PAREN(MEMBER_DEFAULT_VALUE))( \
      MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, GETTER_DESC)

#define LINN_PROPERTY_HELPER_CREATE_H_GETTER_DISPATCH_00(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, GETTER_DESC) \
  static_assert(false, "GETTER must be wrapped in LPS_GETTER(TYPE) or LPS_GETTER_REF_QUALIFIED_OVERLOAD");

#define LINN_PROPERTY_HELPER_CREATE_H_GETTER_DISPATCH_01(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, GETTER_DESC) \
  static_assert(false, "GETTER must be wrapped in LPS_GETTER(TYPE) or LPS_GETTER_REF_QUALIFIED_OVERLOAD");

#define LINN_PROPERTY_HELPER_CREATE_H_GETTER_DISPATCH_10(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, GETTER_DESC) \
  LINN_CAT(LINN_PROPERTY_HELPER_CREATE_H_GETTER_TAG_, LINN_PAIR_FIRST(GETTER_DESC))(                                            \
      MEMBER_TYPE, MEMBER_NAME, SET_TYPE, GETTER_DESC)

#define LINN_PROPERTY_HELPER_CREATE_H_GETTER_DISPATCH_11(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, GETTER_DESC) \
  static_assert(false, "LPS_MEMBER default value is not used by LINN_PROPERTY_CREATE_H; use LINN_PROPERTY_CREATE_D for split-mode initialization");

#define LINN_PROPERTY_HELPER_CREATE_H_GETTER_TAG_LPST_GETTER_VALUE_TYPE(MEMBER_TYPE, MEMBER_NAME, SET_TYPE, GETTER_DESC) \
  LINN_PROPERTY_HELPER_CREATE_H_BODY(                                                                                    \
      MEMBER_TYPE, MEMBER_NAME,                                                                                          \
      SET_TYPE, LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),                                               \
      LINN_PAIR_SECOND(GETTER_DESC), LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 1)

#define LINN_PROPERTY_HELPER_CREATE_H_GETTER_TAG_LPST_GETTER_REF_QUALIFIED_OVERLOAD(MEMBER_TYPE, MEMBER_NAME, SET_TYPE, GETTER_DESC) \
  LINN_PROPERTY_HELPER_CREATE_H_BODY(                                                                                                \
      MEMBER_TYPE, MEMBER_NAME,                                                                                                      \
      SET_TYPE, LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),                                                           \
      MEMBER_TYPE, LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 0)

#define LINN_PROPERTY_HELPER_CREATE_H_BODY(MEMBER_TYPE, MEMBER_NAME, SET_TYPE, SETTER_NAME, GET_TYPE, GETTER_NAME, GETTER_KIND) \
                                                                                                                                \
public:                                                                                                                         \
  void SETTER_NAME(SET_TYPE MEMBER_NAME);                                                                                       \
  LINN_PROPERTY_HELPER_CREATE_H_GETTER_##GETTER_KIND(MEMBER_TYPE, MEMBER_NAME, GET_TYPE, GETTER_NAME)

#define LINN_PROPERTY_HELPER_CREATE_H_GETTER_1(MEMBER_TYPE, MEMBER_NAME, GET_TYPE, GETTER_NAME) \
  [[nodiscard]] GET_TYPE GETTER_NAME() const noexcept(std::is_nothrow_copy_constructible_v<GET_TYPE>);

#define LINN_PROPERTY_HELPER_CREATE_H_GETTER_0(MEMBER_TYPE, MEMBER_NAME, GET_TYPE, GETTER_NAME) \
  [[nodiscard]] MEMBER_TYPE const &GETTER_NAME() const & noexcept;                              \
  [[nodiscard]] MEMBER_TYPE GETTER_NAME() && noexcept(std::is_nothrow_move_constructible_v<MEMBER_TYPE>);


/**
 * @brief Qt-aware helpers backing the four-argument inline and split-header entry points.
 */
#define LINN_PROPERTY_HELPER_CREATE_QT_4(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  LINN_PROPERTY_HELPER_CREATE_QT_DECL(                                                                                                           \
      MEMBER_TYPE, MEMBER_NAME,                                                                                                                  \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),                                                                                 \
      LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME),                                                                                 \
      LINN_CAT(LINN_PROPERTY_HELPER_SIGNAL_PREFIX, MEMBER_NAME))                                                                                 \
  LINN_PROPERTY_HELPER_CREATE_DISPATCH(                                                                                                          \
      MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC)

#define LINN_PROPERTY_HELPER_CREATE_H_QT_4(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  LINN_PROPERTY_HELPER_CREATE_H_QT_DECL(                                                                                                           \
      MEMBER_TYPE, MEMBER_NAME,                                                                                                                    \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),                                                                                   \
      LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME),                                                                                   \
      LINN_CAT(LINN_PROPERTY_HELPER_SIGNAL_PREFIX, MEMBER_NAME))                                                                                   \
  LINN_PROPERTY_HELPER_CREATE_H_DISPATCH(                                                                                                          \
      MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC)

#define LINN_PROPERTY_HELPER_CREATE_QT_SIMPLE_0(TYPE, MEMBER_NAME)       \
  LINN_PROPERTY_HELPER_CREATE_QT_DECL(                                   \
      TYPE, MEMBER_NAME,                                                 \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),         \
      LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME),         \
      LINN_CAT(LINN_PROPERTY_HELPER_SIGNAL_PREFIX, MEMBER_NAME))         \
  LINN_PROPERTY_HELPER_CREATE_BODY(                                      \
      TYPE, MEMBER_NAME, LINN_PROPERTY_HELPER_MEMBER_INIT_NONE, TYPE,    \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),         \
      true, ((void) 0), true, ((void) 0),                                \
      TYPE, LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 1)

#define LINN_PROPERTY_HELPER_CREATE_QT_SIMPLE_1(PAIR, MEMBER_NAME)                      \
  LINN_PROPERTY_HELPER_CREATE_QT_DECL(                                                  \
      LINN_PAIR_FIRST(PAIR), MEMBER_NAME,                                               \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),                        \
      LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME),                        \
      LINN_CAT(LINN_PROPERTY_HELPER_SIGNAL_PREFIX, MEMBER_NAME))                        \
  LINN_PROPERTY_HELPER_CREATE_BODY(                                                     \
      LINN_PAIR_FIRST(PAIR), MEMBER_NAME, LINN_PROPERTY_HELPER_MEMBER_INIT_NONE,        \
      LINN_PAIR_SECOND(PAIR),                                                           \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),                        \
      true, ((void) 0), true, ((void) 0),                                               \
      LINN_PAIR_FIRST(PAIR), LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 1)

#define LINN_PROPERTY_HELPER_CREATE_H_QT_SIMPLE_0(TYPE, MEMBER_NAME)  \
  LINN_PROPERTY_HELPER_CREATE_H_QT_DECL(                              \
      TYPE, MEMBER_NAME,                                              \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),      \
      LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME),      \
      LINN_CAT(LINN_PROPERTY_HELPER_SIGNAL_PREFIX, MEMBER_NAME))      \
  LINN_PROPERTY_HELPER_CREATE_H_BODY(                                 \
      TYPE, MEMBER_NAME, TYPE,                                        \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),      \
      TYPE, LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 1)

#define LINN_PROPERTY_HELPER_CREATE_H_QT_SIMPLE_1(PAIR, MEMBER_NAME)   \
  LINN_PROPERTY_HELPER_CREATE_H_QT_DECL(                               \
      LINN_PAIR_FIRST(PAIR), MEMBER_NAME,                              \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),       \
      LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME),       \
      LINN_CAT(LINN_PROPERTY_HELPER_SIGNAL_PREFIX, MEMBER_NAME))       \
  LINN_PROPERTY_HELPER_CREATE_H_BODY(                                  \
      LINN_PAIR_FIRST(PAIR), MEMBER_NAME, LINN_PAIR_SECOND(PAIR),      \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),       \
      LINN_PAIR_FIRST(PAIR), LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 1)


/**
 * @brief Source-side split property definition helpers.
 */
#define LINN_PROPERTY_HELPER_CREATE_CPP_DISPATCH(CLASS, MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  LINN_CAT3(LINN_PROPERTY_HELPER_CREATE_CPP_DISPATCH_, LINN_IS_PAREN(GETTER_DESC), LINN_IS_PAREN(MEMBER_DEFAULT_VALUE))(                                        \
      CLASS, MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC)

#define LINN_PROPERTY_HELPER_CREATE_CPP_DISPATCH_00(CLASS, MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  static_assert(false, "GETTER must be wrapped in LPS_GETTER(TYPE) or LPS_GETTER_REF_QUALIFIED_OVERLOAD");

#define LINN_PROPERTY_HELPER_CREATE_CPP_DISPATCH_01(CLASS, MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  static_assert(false, "GETTER must be wrapped in LPS_GETTER(TYPE) or LPS_GETTER_REF_QUALIFIED_OVERLOAD");

#define LINN_PROPERTY_HELPER_CREATE_CPP_DISPATCH_10(CLASS, MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  LINN_CAT(LINN_PROPERTY_HELPER_CREATE_CPP_TAG_, LINN_PAIR_FIRST(GETTER_DESC))(                                                                                    \
      CLASS, MEMBER_TYPE, MEMBER_NAME, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC)

#define LINN_PROPERTY_HELPER_CREATE_CPP_DISPATCH_11(CLASS, MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  static_assert(false, "LPS_MEMBER default value is not used by LINN_PROPERTY_CREATE_CPP; use LINN_PROPERTY_CREATE_D for split-mode initialization");

#define LINN_PROPERTY_HELPER_CREATE_CPP_TAG_LPST_GETTER_VALUE_TYPE(CLASS, MEMBER_TYPE, MEMBER_NAME, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  LINN_PROPERTY_HELPER_CREATE_CPP_BODY(                                                                                                                     \
      CLASS, MEMBER_TYPE, MEMBER_NAME,                                                                                                                      \
      SET_TYPE, LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),                                                                                  \
      LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK(CONTRACT_DESC),                                                                                               \
      LINN_PROPERTY_HELPER_POST_ACTION_RUN(POST_ACTION_DESC),                                                                                               \
      LINN_PAIR_SECOND(GETTER_DESC), LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 1)

#define LINN_PROPERTY_HELPER_CREATE_CPP_TAG_LPST_GETTER_REF_QUALIFIED_OVERLOAD(CLASS, MEMBER_TYPE, MEMBER_NAME, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  LINN_PROPERTY_HELPER_CREATE_CPP_BODY(                                                                                                                                 \
      CLASS, MEMBER_TYPE, MEMBER_NAME,                                                                                                                                  \
      SET_TYPE, LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),                                                                                              \
      LINN_PROPERTY_HELPER_PRE_CONTRACT_CHECK(CONTRACT_DESC),                                                                                                           \
      LINN_PROPERTY_HELPER_POST_ACTION_RUN(POST_ACTION_DESC),                                                                                                           \
      MEMBER_TYPE, LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 0)

#define LINN_PROPERTY_HELPER_CREATE_CPP_DISPATCH_0(CLASS, MEMBER_TYPE, MEMBER_NAME, SET_TYPE, CONTRACT_DESC, POST_ACTION_DESC, GETTER_DESC) \
  static_assert(false, "GETTER must be wrapped in LPS_GETTER(TYPE) or LPS_GETTER_REF_QUALIFIED_OVERLOAD");

#define LINN_PROPERTY_HELPER_CREATE_CPP_BODY(CLASS, MEMBER_TYPE, MEMBER_NAME, SET_TYPE, SETTER_NAME, PRE_CHECK, POST_RUN, GET_TYPE, GETTER_NAME, GETTER_KIND) \
  void CLASS::SETTER_NAME(SET_TYPE MEMBER_NAME)                                                                                                               \
  {                                                                                                                                                           \
    PRE_CHECK;                                                                                                                                                \
    ::linn_property_detail::assign_property<MEMBER_TYPE, SET_TYPE>(m_##MEMBER_NAME, std::forward<SET_TYPE>(MEMBER_NAME));                                     \
    POST_RUN;                                                                                                                                                 \
  }                                                                                                                                                           \
  LINN_PROPERTY_HELPER_CPP_GETTER_##GETTER_KIND(CLASS, MEMBER_TYPE, MEMBER_NAME, GET_TYPE, GETTER_NAME)

#define LINN_PROPERTY_HELPER_CPP_GETTER_1(CLASS, MEMBER_TYPE, MEMBER_NAME, GET_TYPE, GETTER_NAME) \
  GET_TYPE CLASS::GETTER_NAME() const noexcept(std::is_nothrow_copy_constructible_v<GET_TYPE>)    \
  {                                                                                               \
    return m_##MEMBER_NAME;                                                                       \
  }

#define LINN_PROPERTY_HELPER_CPP_GETTER_0(CLASS, MEMBER_TYPE, MEMBER_NAME, GET_TYPE, GETTER_NAME) \
  MEMBER_TYPE const &CLASS::GETTER_NAME() const & noexcept                                        \
  {                                                                                               \
    return m_##MEMBER_NAME;                                                                       \
  }                                                                                               \
  MEMBER_TYPE CLASS::GETTER_NAME() && noexcept(std::is_nothrow_move_constructible_v<MEMBER_TYPE>) \
  {                                                                                               \
    return std::move(m_##MEMBER_NAME);                                                            \
  }


#pragma endregion LINN_PROPERTY_HELPER_CORE

} // namespace linn_property_detail

/**
 * @brief Arity selectors for the public variadic macros.
 */
#define LINN_PROPERTY_HELPER_SELECT_1_2(_1, _2, NAME, ...)           NAME
#define LINN_PROPERTY_HELPER_SELECT_1_2_3(_1, _2, _3, NAME, ...)     NAME
#define LINN_PROPERTY_HELPER_SELECT_2_3(_1, _2, _3, NAME, ...)       NAME
#define LINN_PROPERTY_HELPER_SELECT_2_3_4(_1, _2, _3, _4, NAME, ...) NAME
#define LINN_PROPERTY_HELPER_SELECT_3_4(_1, _2, _3, _4, NAME, ...)   NAME


/**
 * @brief Member, getter, setter, and hook descriptors.
 */
#define LPS_MEMBER_3(TYPE, NAME, VALUE) (TYPE, NAME, (LINN_PROPERTY_HELPER_STRATEGY_TAG_MEMBER_DEFAULT_VALUE, VALUE))
#define LPS_MEMBER_2(TYPE, NAME)        (TYPE, NAME, MEMBER_NO_DEFAULT_VALUE)
#define LPS_MEMBER(...)                 LINN_PROPERTY_HELPER_SELECT_2_3(__VA_ARGS__, LPS_MEMBER_3, LPS_MEMBER_2)(__VA_ARGS__)

#define LPS_SCREF(TYPE) (TYPE, TYPE const &)

#define LPS_GETTER(GET_TYPE)              (LINN_PROPERTY_HELPER_STRATEGY_TAG_GETTER_VALUE_TYPE, GET_TYPE)
#define LPS_GETTER_REF_QUALIFIED_OVERLOAD (LINN_PROPERTY_HELPER_STRATEGY_TAG_GETTER_REF_QUALIFIED_OVERLOAD, ~)

#define LPS_SETTER_3(PARAM_TYPE, PRE_CONTRACT, POST_ACTION) \
  LINN_CAT(LPS_SETTER_3_POST_, LINN_IS_PAREN(POST_ACTION))(PARAM_TYPE, PRE_CONTRACT, POST_ACTION)
#define LPS_SETTER_3_POST_1(PARAM_TYPE, PRE_CONTRACT, POST_ACTION) (PARAM_TYPE, PRE_CONTRACT, POST_ACTION)
#define LPS_SETTER_3_POST_0(PARAM_TYPE, PRE_CONTRACT, POST_ACTION) \
  (PARAM_TYPE, PRE_CONTRACT, (LINN_PROPERTY_HELPER_STRATEGY_TAG_SETTER_POST_ACTION, (POST_ACTION)))
#define LPS_SETTER_2(PARAM_TYPE, SECOND) \
  LINN_CAT(LPS_SETTER_2_PAREN_, LINN_IS_PAREN(SECOND))(PARAM_TYPE, SECOND)
#define LPS_SETTER_2_PAREN_1(PARAM_TYPE, SECOND) \
  LINN_CAT(LPS_SETTER_2_TAG_, LINN_PROPERTY_HELPER_FIRST(SECOND))(PARAM_TYPE, SECOND)
#define LPS_SETTER_2_PAREN_0(PARAM_TYPE, SECOND) \
  (PARAM_TYPE, SETTER_NO_PRE_CONTRACT, (LINN_PROPERTY_HELPER_STRATEGY_TAG_SETTER_POST_ACTION, (SECOND)))
#define LPS_SETTER_2_TAG_LPST_SETTER_PRE_CONTRACT_ALWAYS(PARAM_TYPE, SECOND) (PARAM_TYPE, SECOND, SETTER_NO_POST_ACTION)
#define LPS_SETTER_2_TAG_LPST_SETTER_PRE_CONTRACT_DEBUG(PARAM_TYPE, SECOND)  (PARAM_TYPE, SECOND, SETTER_NO_POST_ACTION)
#define LPS_SETTER_2_TAG_LPST_SETTER_POST_ACTION(PARAM_TYPE, SECOND)         (PARAM_TYPE, SETTER_NO_PRE_CONTRACT, SECOND)
#define LPS_SETTER_1(PARAM_TYPE)                                             (PARAM_TYPE, SETTER_NO_PRE_CONTRACT, SETTER_NO_POST_ACTION)
#define LPS_SETTER(...)                                                      LINN_PROPERTY_HELPER_SELECT_1_2_3(__VA_ARGS__, LPS_SETTER_3, LPS_SETTER_2, LPS_SETTER_1)(__VA_ARGS__)

#define LPS_PRE_CONTRACT_2(EXPR, HANDLER) (LINN_PROPERTY_HELPER_STRATEGY_TAG_SETTER_PRE_CONTRACT_ALWAYS, EXPR, HANDLER)
#define LPS_PRE_CONTRACT_1(EXPR)          (LINN_PROPERTY_HELPER_STRATEGY_TAG_SETTER_PRE_CONTRACT_ALWAYS, EXPR, ::linn_property_detail::contract_violation)
#define LPS_PRE_CONTRACT(...)             LINN_PROPERTY_HELPER_SELECT_1_2(__VA_ARGS__, LPS_PRE_CONTRACT_2, LPS_PRE_CONTRACT_1)(__VA_ARGS__)

#define LPS_PRE_CONTRACTD_2(EXPR, HANDLER) (LINN_PROPERTY_HELPER_STRATEGY_TAG_SETTER_PRE_CONTRACT_DEBUG, EXPR, HANDLER)
#define LPS_PRE_CONTRACTD_1(EXPR)          (LINN_PROPERTY_HELPER_STRATEGY_TAG_SETTER_PRE_CONTRACT_DEBUG, EXPR, ::linn_property_detail::contract_violation)
#define LPS_PRE_CONTRACTD(...)             LINN_PROPERTY_HELPER_SELECT_1_2(__VA_ARGS__, LPS_PRE_CONTRACTD_2, LPS_PRE_CONTRACTD_1)(__VA_ARGS__)

#define LPS_POST_ACTION(...) (LINN_PROPERTY_HELPER_STRATEGY_TAG_SETTER_POST_ACTION, (__VA_ARGS__))


/**
 * @brief Inline property creation and its two-argument fast paths.
 */
#define LINN_PROPERTY_HELPER_CREATE_SIMPLE_0(TYPE, MEMBER_NAME)       \
  LINN_PROPERTY_HELPER_CREATE_BODY(                                   \
      TYPE, MEMBER_NAME, LINN_PROPERTY_HELPER_MEMBER_INIT_NONE, TYPE, \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),      \
      true, ((void) 0), true, ((void) 0),                             \
      TYPE, LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 1)
#define LINN_PROPERTY_HELPER_CREATE_SIMPLE_1(PAIR, MEMBER_NAME)      \
  LINN_PROPERTY_HELPER_CREATE_BODY(                                  \
      LINN_PAIR_FIRST(PAIR), MEMBER_NAME,                            \
      LINN_PROPERTY_HELPER_MEMBER_INIT_NONE, LINN_PAIR_SECOND(PAIR), \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),     \
      true, ((void) 0), true, ((void) 0),                            \
      LINN_PAIR_FIRST(PAIR), LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 1)

#define LINN_PROPERTY_CREATE(...) LINN_PROPERTY_HELPER_SELECT_2_3_4(__VA_ARGS__, LINN_PROPERTY_CREATE_4, LINN_PROPERTY_CREATE_3, LINN_PROPERTY_CREATE_2)(__VA_ARGS__)
#define LINN_PROPERTY_CREATE_2(TYPE_OR_SCREF, MEMBER_NAME) \
  LINN_CAT(LINN_PROPERTY_HELPER_CREATE_SIMPLE_, LINN_IS_PAREN(TYPE_OR_SCREF))(TYPE_OR_SCREF, MEMBER_NAME)
#define LINN_PROPERTY_CREATE_3(MEMBER_DESC, SETTER_DESC, GETTER_DESC)                                  \
  LINN_PROPERTY_HELPER_CREATE_DISPATCH(                                                                \
      LINN_TRIPLE_FIRST(MEMBER_DESC), LINN_TRIPLE_SECOND(MEMBER_DESC), LINN_TRIPLE_THIRD(MEMBER_DESC), \
      LINN_TRIPLE_FIRST(SETTER_DESC), LINN_TRIPLE_SECOND(SETTER_DESC), LINN_TRIPLE_THIRD(SETTER_DESC), \
      GETTER_DESC)
#define LINN_PROPERTY_CREATE_4(MEMBER_DESC, SETTER_DESC, GETTER_DESC, EXTRA_QT_PROP)                   \
  LINN_PROPERTY_HELPER_CREATE_QT_4(                                                                    \
      LINN_TRIPLE_FIRST(MEMBER_DESC), LINN_TRIPLE_SECOND(MEMBER_DESC), LINN_TRIPLE_THIRD(MEMBER_DESC), \
      LINN_TRIPLE_FIRST(SETTER_DESC), LINN_TRIPLE_SECOND(SETTER_DESC), LINN_TRIPLE_THIRD(SETTER_DESC), \
      GETTER_DESC)


/**
 * @brief Split header-side property declarations and their fast paths.
 */
#define LINN_PROPERTY_HELPER_CREATE_H_SIMPLE_0(TYPE, MEMBER_NAME) \
  LINN_PROPERTY_HELPER_CREATE_H_BODY(                             \
      TYPE, MEMBER_NAME, TYPE,                                    \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),  \
      TYPE, LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 1)
#define LINN_PROPERTY_HELPER_CREATE_H_SIMPLE_1(PAIR, MEMBER_NAME) \
  LINN_PROPERTY_HELPER_CREATE_H_BODY(                             \
      LINN_PAIR_FIRST(PAIR), MEMBER_NAME, LINN_PAIR_SECOND(PAIR), \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),  \
      LINN_PAIR_FIRST(PAIR), LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 1)

#define LINN_PROPERTY_CREATE_H(...) LINN_PROPERTY_HELPER_SELECT_2_3_4(__VA_ARGS__, LINN_PROPERTY_CREATE_H_4, LINN_PROPERTY_CREATE_H_3, LINN_PROPERTY_CREATE_H_2)(__VA_ARGS__)
#define LINN_PROPERTY_CREATE_H_2(TYPE_OR_SCREF, MEMBER_NAME) \
  LINN_CAT(LINN_PROPERTY_HELPER_CREATE_H_SIMPLE_, LINN_IS_PAREN(TYPE_OR_SCREF))(TYPE_OR_SCREF, MEMBER_NAME)
#define LINN_PROPERTY_CREATE_H_3(MEMBER_DESC, SETTER_DESC, GETTER_DESC)                                \
  LINN_PROPERTY_HELPER_CREATE_H_DISPATCH(                                                              \
      LINN_TRIPLE_FIRST(MEMBER_DESC), LINN_TRIPLE_SECOND(MEMBER_DESC), LINN_TRIPLE_THIRD(MEMBER_DESC), \
      LINN_TRIPLE_FIRST(SETTER_DESC), LINN_TRIPLE_SECOND(SETTER_DESC), LINN_TRIPLE_THIRD(SETTER_DESC), GETTER_DESC)
#define LINN_PROPERTY_CREATE_H_4(MEMBER_DESC, SETTER_DESC, GETTER_DESC, EXTRA_QT_PROP)        \
  LINN_CAT(LINN_PROPERTY_CREATE_H_4_DEFAULT_, LINN_IS_PAREN(LINN_TRIPLE_THIRD(MEMBER_DESC)))( \
      MEMBER_DESC, SETTER_DESC, GETTER_DESC, EXTRA_QT_PROP)
#define LINN_PROPERTY_CREATE_H_4_DEFAULT_0(MEMBER_DESC, SETTER_DESC, GETTER_DESC, EXTRA_QT_PROP)       \
  LINN_PROPERTY_HELPER_CREATE_H_QT_4(                                                                  \
      LINN_TRIPLE_FIRST(MEMBER_DESC), LINN_TRIPLE_SECOND(MEMBER_DESC), LINN_TRIPLE_THIRD(MEMBER_DESC), \
      LINN_TRIPLE_FIRST(SETTER_DESC), LINN_TRIPLE_SECOND(SETTER_DESC), LINN_TRIPLE_THIRD(SETTER_DESC), GETTER_DESC)
#define LINN_PROPERTY_CREATE_H_4_DEFAULT_1(MEMBER_DESC, SETTER_DESC, GETTER_DESC, EXTRA_QT_PROP) \
  static_assert(false, "LPS_MEMBER default value is not used by LINN_PROPERTY_CREATE_H; use LINN_PROPERTY_CREATE_D for split-mode initialization");


/**
 * @brief Split source-side property definitions and their fast paths.
 */
#define LINN_PROPERTY_HELPER_CREATE_CPP_SIMPLE_0(CLASS, TYPE, MEMBER_NAME) \
  LINN_PROPERTY_HELPER_CREATE_CPP_BODY(                                    \
      CLASS, TYPE, MEMBER_NAME, TYPE,                                      \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),           \
      ((void) 0), ((void) 0),                                              \
      TYPE, LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 1)
#define LINN_PROPERTY_HELPER_CREATE_CPP_SIMPLE_1(CLASS, PAIR, MEMBER_NAME) \
  LINN_PROPERTY_HELPER_CREATE_CPP_BODY(                                    \
      CLASS, LINN_PAIR_FIRST(PAIR), MEMBER_NAME, LINN_PAIR_SECOND(PAIR),   \
      LINN_CAT(LINN_PROPERTY_HELPER_SETTER_PREFIX, MEMBER_NAME),           \
      ((void) 0), ((void) 0),                                              \
      LINN_PAIR_FIRST(PAIR), LINN_CAT(LINN_PROPERTY_HELPER_GETTER_PREFIX, MEMBER_NAME), 1)

#define LINN_PROPERTY_CREATE_CPP(...) LINN_PROPERTY_HELPER_SELECT_3_4(__VA_ARGS__, LINN_PROPERTY_CREATE_CPP_4, LINN_PROPERTY_CREATE_CPP_3)(__VA_ARGS__)
#define LINN_PROPERTY_CREATE_CPP_3(CLASS, TYPE_OR_SCREF, MEMBER_NAME) \
  LINN_CAT(LINN_PROPERTY_HELPER_CREATE_CPP_SIMPLE_, LINN_IS_PAREN(TYPE_OR_SCREF))(CLASS, TYPE_OR_SCREF, MEMBER_NAME)
#define LINN_PROPERTY_CREATE_CPP_4(CLASS, MEMBER_DESC, SETTER_DESC, GETTER_DESC)                       \
  LINN_PROPERTY_HELPER_CREATE_CPP_DISPATCH(                                                            \
      CLASS,                                                                                           \
      LINN_TRIPLE_FIRST(MEMBER_DESC), LINN_TRIPLE_SECOND(MEMBER_DESC), LINN_TRIPLE_THIRD(MEMBER_DESC), \
      LINN_TRIPLE_FIRST(SETTER_DESC), LINN_TRIPLE_SECOND(SETTER_DESC), LINN_TRIPLE_THIRD(SETTER_DESC), GETTER_DESC)


/**
 * @brief Standalone split-member definition with optional initializer.
 */
#define LINN_PROPERTY_CREATE_D(...) LINN_PROPERTY_HELPER_SELECT_2_3(__VA_ARGS__, LINN_PROPERTY_CREATE_D_3, LINN_PROPERTY_CREATE_D_2)(__VA_ARGS__)
#define LINN_PROPERTY_CREATE_D_2(MEMBER_TYPE, MEMBER_NAME) \
                                                           \
private:                                                   \
  MEMBER_TYPE m_##MEMBER_NAME;
#define LINN_PROPERTY_CREATE_D_3(MEMBER_TYPE, MEMBER_NAME, MEMBER_DEFAULT_VALUE) \
                                                                                 \
private:                                                                         \
  MEMBER_TYPE m_##MEMBER_NAME { MEMBER_DEFAULT_VALUE };


/**
 * @brief Optional short aliases controlled by LINN_PROPERTY_ENABLE_SHORT_MACROS.
 */
#ifdef LINN_PROPERTY_ENABLE_SHORT_MACROS
#  ifndef MEMBER
#    define MEMBER(...) LINN_PROPERTY_HELPER_SELECT_2_3(__VA_ARGS__, LPS_MEMBER_3, LPS_MEMBER_2)(__VA_ARGS__)
#  endif
#  ifndef SETTER
#    define SETTER(...) LINN_PROPERTY_HELPER_SELECT_1_2_3(__VA_ARGS__, LPS_SETTER_3, LPS_SETTER_2, LPS_SETTER_1)(__VA_ARGS__)
#  endif
#  ifndef GETTER
#    define GETTER(...) (LINN_PROPERTY_HELPER_STRATEGY_TAG_GETTER_VALUE_TYPE, GET_TYPE)
#  endif
#  ifndef GETTER_REF_QUALIFIED_OVERLOAD
#    define GETTER_REF_QUALIFIED_OVERLOAD (LINN_PROPERTY_HELPER_STRATEGY_TAG_GETTER_REF_QUALIFIED_OVERLOAD, ~)
#  endif
#  ifndef PRE_CONTRACT
#    define PRE_CONTRACT(...) LINN_PROPERTY_HELPER_SELECT_1_2(__VA_ARGS__, LPS_PRE_CONTRACT_2, LPS_PRE_CONTRACT_1)(__VA_ARGS__)
#  endif
#  ifndef PRE_CONTRACTD
#    define PRE_CONTRACTD(...) LINN_PROPERTY_HELPER_SELECT_1_2(__VA_ARGS__, LPS_PRE_CONTRACTD_2, LPS_PRE_CONTRACTD_1)(__VA_ARGS__)
#  endif
#  ifndef POST_ACTION
#    define POST_ACTION(...) (LINN_PROPERTY_HELPER_STRATEGY_TAG_SETTER_POST_ACTION, (__VA_ARGS__))
#  endif

#  ifndef PROPERTY_CREATE
#    define PROPERTY_CREATE(...) LINN_PROPERTY_HELPER_SELECT_2_3_4(__VA_ARGS__, LINN_PROPERTY_CREATE_4, LINN_PROPERTY_CREATE_3, LINN_PROPERTY_CREATE_2)(__VA_ARGS__)
#  endif

#  ifndef PROPERTY_CREATE_H
#    define PROPERTY_CREATE_H(...) LINN_PROPERTY_HELPER_SELECT_2_3_4(__VA_ARGS__, LINN_PROPERTY_CREATE_H_4, LINN_PROPERTY_CREATE_H_3, LINN_PROPERTY_CREATE_H_2)(__VA_ARGS__)
#  endif

#  ifndef PROPERTY_CREATE_CPP
#    define PROPERTY_CREATE_CPP(...) LINN_PROPERTY_HELPER_SELECT_3_4(__VA_ARGS__, LINN_PROPERTY_CREATE_CPP_4, LINN_PROPERTY_CREATE_CPP_3)(__VA_ARGS__)
#  endif

#  ifndef PROPERTY_CREATE_D
#    define PROPERTY_CREATE_D(...) LINN_PROPERTY_HELPER_SELECT_2_3(__VA_ARGS__, LINN_PROPERTY_CREATE_D_3, LINN_PROPERTY_CREATE_D_2)(__VA_ARGS__)
#  endif
#endif // LINN_PROPERTY_ENABLE_SHORT_MACROS


/**
 * @brief Qt/moc aliases for property and enum generation.
 */
#if defined(QT_DEBUG) || defined(QT_NO_DEBUG) || defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#  define QPS_MEMBER(...)        LINN_PROPERTY_HELPER_SELECT_2_3(__VA_ARGS__, LPS_MEMBER_3, LPS_MEMBER_2)(__VA_ARGS__)
#  define QPS_SCREF(TYPE)        (TYPE, TYPE const &)
#  define QPS_GETTER(GET_TYPE)   (LINN_PROPERTY_HELPER_STRATEGY_TAG_GETTER_VALUE_TYPE, GET_TYPE)
#  define QPS_SETTER(...)        LINN_PROPERTY_HELPER_SELECT_1_2_3(__VA_ARGS__, LPS_SETTER_3, LPS_SETTER_2, LPS_SETTER_1)(__VA_ARGS__)
#  define QPS_PRE_CONTRACTD(...) LINN_PROPERTY_HELPER_SELECT_1_2(__VA_ARGS__, LPS_PRE_CONTRACTD_2, LPS_PRE_CONTRACTD_1)(__VA_ARGS__)
#  define QPS_PRE_CONTRACT(...)  LINN_PROPERTY_HELPER_SELECT_1_2(__VA_ARGS__, LPS_PRE_CONTRACT_2, LPS_PRE_CONTRACT_1)(__VA_ARGS__)
#  define QPS_POST_ACTION(...)   (LINN_PROPERTY_HELPER_STRATEGY_TAG_SETTER_POST_ACTION, (__VA_ARGS__))

#  define QPS_GETTER_REF_QUALIFIED_OVERLOAD (LINN_PROPERTY_HELPER_STRATEGY_TAG_GETTER_REF_QUALIFIED_OVERLOAD, ~)

#  define QPS_POST_ACTION_EMIT(MEMBER_NAME)      (LINN_PROPERTY_HELPER_STRATEGY_TAG_SETTER_POST_ACTION, (Q_EMIT this->LINN_CAT(LINN_PROPERTY_HELPER_SIGNAL_PREFIX, MEMBER_NAME)))
#  define QPS_POST_ACTION_UPDATE()               (LINN_PROPERTY_HELPER_STRATEGY_TAG_SETTER_POST_ACTION, (this->update()))
#  define QPS_POST_ACTION_UPDATEMIT(MEMBER_NAME) (LINN_PROPERTY_HELPER_STRATEGY_TAG_SETTER_POST_ACTION, (this->update(), Q_EMIT this->LINN_CAT(LINN_PROPERTY_HELPER_SIGNAL_PREFIX, MEMBER_NAME)))

#  define Q_PRIVATE_CREATE(...)     LINN_PROPERTY_HELPER_SELECT_2_3(__VA_ARGS__, LINN_PROPERTY_CREATE_3, LINN_PROPERTY_CREATE_2)(__VA_ARGS__)
#  define Q_PRIVATE_CREATE_H(...)   LINN_PROPERTY_HELPER_SELECT_2_3(__VA_ARGS__, LINN_PROPERTY_CREATE_H_3, LINN_PROPERTY_CREATE_H_2)(__VA_ARGS__)
#  define Q_PRIVATE_CREATE_CPP(...) LINN_PROPERTY_HELPER_SELECT_3_4(__VA_ARGS__, LINN_PROPERTY_CREATE_CPP_4, LINN_PROPERTY_CREATE_CPP_3)(__VA_ARGS__)
#  define Q_PRIVATE_CREATE_D(...)   LINN_PROPERTY_HELPER_SELECT_2_3(__VA_ARGS__, LINN_PROPERTY_CREATE_D_3, LINN_PROPERTY_CREATE_D_2)(__VA_ARGS__)

#  define Q_PROPERTY_CREATE(...)                                     LINN_PROPERTY_HELPER_SELECT_2_3(__VA_ARGS__, Q_PROPERTY_CREATE_3, Q_PROPERTY_CREATE_2)(__VA_ARGS__)
#  define Q_PROPERTY_CREATE_2(TYPE_OR_SCREF, MEMBER_NAME)            LINN_CAT(LINN_PROPERTY_HELPER_CREATE_QT_SIMPLE_, LINN_IS_PAREN(TYPE_OR_SCREF))(TYPE_OR_SCREF, MEMBER_NAME)
#  define Q_PROPERTY_CREATE_3(MEMBER_DESC, SETTER_DESC, GETTER_DESC) LINN_PROPERTY_CREATE_4(MEMBER_DESC, SETTER_DESC, GETTER_DESC, (LINN_PROPERTY_HELPER_STRATEGY_TAG_EXTRA_QT_PROPERTY))

#  define Q_PROPERTY_CREATE_H(...)                                     LINN_PROPERTY_HELPER_SELECT_2_3(__VA_ARGS__, Q_PROPERTY_CREATE_H_3, Q_PROPERTY_CREATE_H_2)(__VA_ARGS__)
#  define Q_PROPERTY_CREATE_H_2(TYPE_OR_SCREF, MEMBER_NAME)            LINN_CAT(LINN_PROPERTY_HELPER_CREATE_H_QT_SIMPLE_, LINN_IS_PAREN(TYPE_OR_SCREF))(TYPE_OR_SCREF, MEMBER_NAME)
#  define Q_PROPERTY_CREATE_H_3(MEMBER_DESC, SETTER_DESC, GETTER_DESC) LINN_PROPERTY_CREATE_H_4(MEMBER_DESC, SETTER_DESC, GETTER_DESC, (LINN_PROPERTY_HELPER_STRATEGY_TAG_EXTRA_QT_PROPERTY))
#  define Q_PROPERTY_CREATE_CPP(...)                                   LINN_PROPERTY_HELPER_SELECT_3_4(__VA_ARGS__, LINN_PROPERTY_CREATE_CPP_4, LINN_PROPERTY_CREATE_CPP_3)(__VA_ARGS__)
#  define Q_PROPERTY_CREATE_D(...)                                     LINN_PROPERTY_HELPER_SELECT_2_3(__VA_ARGS__, LINN_PROPERTY_CREATE_D_3, LINN_PROPERTY_CREATE_D_2)(__VA_ARGS__)

#  define Q_BEGIN_ENUM_CREATE_IMPL(_1, _2, NAME, ...) NAME
#  define Q_BEGIN_ENUM_CREATE_ARGS(args)              Q_BEGIN_ENUM_CREATE_IMPL args
#  define Q_BEGIN_ENUM_CREATE(...)                    Q_BEGIN_ENUM_CREATE_ARGS((__VA_ARGS__, Q_BEGIN_ENUM_CREATE_2, Q_BEGIN_ENUM_CREATE_1))(__VA_ARGS__)

// Enum export helper for older Qt 5 versions.
#  if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#    define Q_ENUM_CREATE(CLASS) Q_ENUM_NS(CLASS)

#    define Q_BEGIN_ENUM_CREATE_1(CLASS) \
      namespace CLASS                    \
      {                                  \
      Q_NAMESPACE_EXPORT()

#    define Q_BEGIN_ENUM_CREATE_2(CLASS, EXPORT) \
      namespace CLASS                            \
      {                                          \
      Q_NAMESPACE_EXPORT(EXPORT)

#    define Q_END_ENUM_CREATE(CLASS) }
#  else
#    define Q_ENUM_CREATE(CLASS) Q_ENUM(CLASS)

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
#  endif // QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
#endif   // defined(QT_DEBUG) || defined(QT_NO_DEBUG) || defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)

#endif // !LINN_PROPERTY_H_
