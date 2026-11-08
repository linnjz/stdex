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

// test_fancy_pointer_iterator.cpp
//
// Verifies include/fancy_pointer_iterator.hpp: concept-driven iterator
// category selection, noexcept propagation, arithmetic synthesis for pointer
// types that only provide '<' and '==', and contiguous iteration.

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <memory>
#include <ranges>
#include <type_traits>
#include <utility>

#include "fancy_pointer_iterator.hpp"

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (cond) {                                                              \
      ++g_pass;                                                              \
    } else {                                                                 \
      ++g_fail;                                                              \
      if (g_fail < 20) {                                                     \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
      }                                                                      \
    }                                                                        \
  } while (0)
} // namespace

namespace test
{
template <class T>
class fancy_ptr
{
public:
  using element_type    = T;
  using difference_type = std::ptrdiff_t;

  constexpr fancy_ptr() noexcept = default;
  constexpr fancy_ptr(std::nullptr_t) noexcept {}
  explicit constexpr fancy_ptr(T *ptr) noexcept
      : ptr_ { ptr }
  {
  }

  template <class U>
  requires std::convertible_to<U *, T *>
  constexpr fancy_ptr(fancy_ptr<U> const &other) noexcept
      : ptr_ { other.get() }
  {
  }

  [[nodiscard]] constexpr T *get() const noexcept { return ptr_; }
  [[nodiscard]] constexpr T &operator* () const noexcept { return *ptr_; }
  [[nodiscard]] constexpr T *operator->() const noexcept { return ptr_; }

  constexpr fancy_ptr &operator++ () noexcept
  {
    ++ptr_;
    return *this;
  }

  constexpr fancy_ptr operator++ (int) noexcept
  {
    auto tmp = *this;
    ++ptr_;
    return tmp;
  }

  constexpr fancy_ptr &operator-- () noexcept
  {
    --ptr_;
    return *this;
  }

  constexpr fancy_ptr operator-- (int) noexcept
  {
    auto tmp = *this;
    --ptr_;
    return tmp;
  }

  constexpr fancy_ptr &operator+= (difference_type n) noexcept
  {
    ptr_ += n;
    return *this;
  }

  constexpr fancy_ptr &operator-= (difference_type n) noexcept
  {
    ptr_ -= n;
    return *this;
  }

  [[nodiscard]] constexpr fancy_ptr operator+ (difference_type n) const noexcept
  {
    return fancy_ptr { ptr_ + n };
  }

  [[nodiscard]] constexpr fancy_ptr operator- (difference_type n) const noexcept
  {
    return fancy_ptr { ptr_ - n };
  }

  [[nodiscard]] constexpr difference_type operator- (fancy_ptr const &other) const noexcept
  {
    return ptr_ - other.ptr_;
  }

  [[nodiscard]] constexpr T &operator[] (difference_type n) const noexcept { return ptr_[n]; }

  friend constexpr bool operator== (fancy_ptr const &, fancy_ptr const &) noexcept = default;
  friend constexpr auto operator<=> (fancy_ptr const &, fancy_ptr const &) noexcept = default;

private:
  T *ptr_ = nullptr;
};

// Deliberately lacks >, <=, >=, n + p, and returns compound assignment by
// value: the adapter has to synthesize the missing operations itself.
template <class T>
class legacy_ptr
{
public:
  using element_type    = T;
  using difference_type = std::ptrdiff_t;

  constexpr legacy_ptr() noexcept = default;
  explicit constexpr legacy_ptr(T *ptr) noexcept
      : ptr_ { ptr }
  {
  }

  [[nodiscard]] constexpr T &operator* () const noexcept { return *ptr_; }

  constexpr legacy_ptr &operator++ () noexcept
  {
    ++ptr_;
    return *this;
  }

  constexpr legacy_ptr operator++ (int) noexcept
  {
    auto tmp = *this;
    ++ptr_;
    return tmp;
  }

  constexpr legacy_ptr &operator-- () noexcept
  {
    --ptr_;
    return *this;
  }

  constexpr legacy_ptr operator-- (int) noexcept
  {
    auto tmp = *this;
    --ptr_;
    return tmp;
  }

  constexpr legacy_ptr operator+= (difference_type n) noexcept
  {
    ptr_ += n;
    return *this;
  }

  constexpr legacy_ptr operator-= (difference_type n) noexcept
  {
    ptr_ -= n;
    return *this;
  }

  [[nodiscard]] constexpr legacy_ptr operator+ (difference_type n) const noexcept
  {
    return legacy_ptr { ptr_ + n };
  }

  [[nodiscard]] constexpr legacy_ptr operator- (difference_type n) const noexcept
  {
    return legacy_ptr { ptr_ - n };
  }

  [[nodiscard]] constexpr difference_type operator- (legacy_ptr const &other) const noexcept
  {
    return ptr_ - other.ptr_;
  }

  friend constexpr bool operator== (legacy_ptr const &a, legacy_ptr const &b) noexcept
  {
    return a.ptr_ == b.ptr_;
  }

  friend constexpr bool operator< (legacy_ptr const &a, legacy_ptr const &b) noexcept
  {
    return a.ptr_ < b.ptr_;
  }

private:
  T *ptr_ = nullptr;
};

// Input-only: no equality and no decrement, so the adapter must report input.
template <class T>
class input_ptr
{
public:
  using element_type    = T;
  using difference_type = std::ptrdiff_t;

  constexpr input_ptr() noexcept = default;
  explicit constexpr input_ptr(T *ptr) noexcept
      : ptr_ { ptr }
  {
  }

  [[nodiscard]] constexpr T &operator* () const noexcept { return *ptr_; }

  constexpr input_ptr &operator++ () noexcept
  {
    ++ptr_;
    return *this;
  }

  constexpr input_ptr operator++ (int) noexcept
  {
    auto tmp = *this;
    ++ptr_;
    return tmp;
  }

private:
  T *ptr_ = nullptr;
};

// Every arithmetic operation is potentially throwing. There is deliberately
// no operator->, so the type cannot claim contiguity.
template <class T>
class throwing_ptr
{
public:
  using element_type    = T;
  using difference_type = std::ptrdiff_t;

  constexpr throwing_ptr() noexcept = default;
  explicit constexpr throwing_ptr(T *ptr) noexcept
      : ptr_ { ptr }
  {
  }

  [[nodiscard]] T &operator* () const { return *ptr_; }

  throwing_ptr &operator++ ()
  {
    ++ptr_;
    return *this;
  }

  throwing_ptr operator++ (int)
  {
    auto tmp = *this;
    ++ptr_;
    return tmp;
  }

  throwing_ptr &operator-- ()
  {
    --ptr_;
    return *this;
  }

  throwing_ptr operator-- (int)
  {
    auto tmp = *this;
    --ptr_;
    return tmp;
  }

  throwing_ptr &operator+= (difference_type n)
  {
    ptr_ += n;
    return *this;
  }

  throwing_ptr &operator-= (difference_type n)
  {
    ptr_ -= n;
    return *this;
  }

  [[nodiscard]] throwing_ptr operator+ (difference_type n) const
  {
    return throwing_ptr { ptr_ + n };
  }

  [[nodiscard]] throwing_ptr operator- (difference_type n) const
  {
    return throwing_ptr { ptr_ - n };
  }

  [[nodiscard]] difference_type operator- (throwing_ptr const &other) const
  {
    return ptr_ - other.ptr_;
  }

  friend bool operator== (throwing_ptr const &a, throwing_ptr const &b)
  {
    return a.ptr_ == b.ptr_;
  }

  friend bool operator< (throwing_ptr const &a, throwing_ptr const &b)
  {
    return a.ptr_ < b.ptr_;
  }

private:
  T *ptr_ = nullptr;
};
} // namespace test

namespace std
{
template <class T>
struct pointer_traits<test::fancy_ptr<T>>
{
  using pointer         = test::fancy_ptr<T>;
  using element_type    = T;
  using difference_type = std::ptrdiff_t;

  [[nodiscard]] static constexpr element_type *to_address(pointer const &p) noexcept
  {
    return p.get();
  }
};
} // namespace std

namespace
{
using raw_iter      = stdex::fancy_pointer_iterator<int, int *>;
using fancy_iter    = stdex::fancy_pointer_iterator<int, test::fancy_ptr<int>>;
using fancy_citer   = stdex::fancy_pointer_iterator<int, test::fancy_ptr<const int>>;
using legacy_iter   = stdex::fancy_pointer_iterator<int, test::legacy_ptr<int>>;
using input_iter    = stdex::fancy_pointer_iterator<int, test::input_ptr<int>>;
using throwing_iter = stdex::fancy_pointer_iterator<int, test::throwing_ptr<int>>;

static_assert(stdex::pointer_contiguous<int *>);
static_assert(stdex::pointer_contiguous<test::fancy_ptr<int>>);
static_assert(stdex::pointer_contiguous<test::fancy_ptr<const int>>);
static_assert(stdex::pointer_random_access<test::legacy_ptr<int>>);
static_assert(!stdex::pointer_contiguous<test::legacy_ptr<int>>);
static_assert(!stdex::pointer_contiguous<test::throwing_ptr<int>>);
static_assert(!stdex::pointer_random_access<test::input_ptr<int>>);

static_assert(std::contiguous_iterator<raw_iter>);
static_assert(std::contiguous_iterator<fancy_iter>);
static_assert(std::contiguous_iterator<fancy_citer>);
static_assert(std::same_as<raw_iter::iterator_concept, std::contiguous_iterator_tag>);
static_assert(std::same_as<raw_iter::iterator_category, std::random_access_iterator_tag>);
static_assert(std::same_as<fancy_iter::iterator_concept, std::contiguous_iterator_tag>);
static_assert(std::same_as<fancy_iter::iterator_category, std::random_access_iterator_tag>);

static_assert(std::random_access_iterator<legacy_iter>);
static_assert(!std::contiguous_iterator<legacy_iter>);
static_assert(std::same_as<legacy_iter::iterator_concept, std::random_access_iterator_tag>);
static_assert(std::same_as<legacy_iter::iterator_category, std::random_access_iterator_tag>);

static_assert(std::random_access_iterator<throwing_iter>);
static_assert(!std::contiguous_iterator<throwing_iter>);
static_assert(std::same_as<throwing_iter::iterator_concept, std::random_access_iterator_tag>);

static_assert(std::input_iterator<input_iter>);
static_assert(!std::forward_iterator<input_iter>);
static_assert(std::same_as<input_iter::iterator_concept, std::input_iterator_tag>);
static_assert(std::same_as<input_iter::iterator_category, std::input_iterator_tag>);

static_assert(std::same_as<std::iter_reference_t<fancy_citer>, const int &>);
static_assert(std::same_as<std::iter_value_t<fancy_citer>, int>);
static_assert(std::same_as<std::iter_reference_t<raw_iter>, int &>);

static_assert(std::same_as<decltype(std::declval<raw_iter const &>().base()), int *const &>);
static_assert(std::same_as<decltype(std::declval<fancy_iter const &>().base()),
                           test::fancy_ptr<int> const &>);

static_assert(std::is_nothrow_default_constructible_v<raw_iter>);
static_assert(std::is_nothrow_copy_constructible_v<raw_iter>);
static_assert(std::is_nothrow_move_constructible_v<raw_iter>);
static_assert(noexcept(*std::declval<raw_iter const &>()));
static_assert(noexcept(++std::declval<raw_iter &>()));
static_assert(noexcept(std::declval<raw_iter &>()++));
static_assert(noexcept(--std::declval<raw_iter &>()));
static_assert(noexcept(std::declval<raw_iter &>()--));
static_assert(noexcept(std::declval<raw_iter &>() += 1));
static_assert(noexcept(std::declval<raw_iter &>() -= 1));
static_assert(noexcept(std::declval<raw_iter const &>() + 1));
static_assert(noexcept(1 + std::declval<raw_iter>()));
static_assert(noexcept(std::declval<raw_iter const &>() - 1));
static_assert(noexcept(std::declval<raw_iter const &>() - std::declval<raw_iter const &>()));
static_assert(noexcept(std::declval<raw_iter const &>()[0]));
static_assert(noexcept(std::to_address(std::declval<raw_iter const &>())));

static_assert(noexcept(*std::declval<fancy_iter const &>()));
static_assert(noexcept(++std::declval<fancy_iter &>()));
static_assert(noexcept(std::declval<fancy_iter &>() += 1));
static_assert(noexcept(std::declval<fancy_iter const &>() + 1));
static_assert(noexcept(std::to_address(std::declval<fancy_iter const &>())));

static_assert(!noexcept(*std::declval<throwing_iter const &>()));
static_assert(!noexcept(++std::declval<throwing_iter &>()));
static_assert(!noexcept(std::declval<throwing_iter &>() += 1));
static_assert(!noexcept(std::declval<throwing_iter &>() -= 1));
static_assert(!noexcept(std::declval<throwing_iter const &>() + 1));
static_assert(!noexcept(std::declval<throwing_iter const &>()[0]));

static_assert(std::same_as<decltype(stdex::fancy_pointer_iterator<int, test::fancy_ptr<const int>> {
                               std::declval<fancy_iter const &>() }),
                           fancy_citer>);

struct fancy_span
{
  fancy_iter first;
  fancy_iter last;

  [[nodiscard]] fancy_iter begin() const { return first; }
  [[nodiscard]] fancy_iter end() const { return last; }
};

static_assert(std::ranges::contiguous_range<fancy_span>);
static_assert(std::ranges::sized_range<fancy_span>);
static_assert(std::ranges::random_access_range<fancy_span>);

template <class It>
void exercise(It first, It last)
{
  CHECK(*first == 3);
  CHECK(first[1] == 1);
  CHECK(first[4] == 5);
  CHECK(*(first + 2) == 4);
  CHECK(*(2 + first) == 4);
  CHECK(*(last - 1) == 5);
  CHECK(last - first == 5);
  CHECK(first - last == -5);

  CHECK(first == first);
  CHECK(first != last);
  CHECK(first < last);
  CHECK(first <= first);
  CHECK(last > first);
  CHECK(last >= last);

  auto it = first;
  CHECK(*it++ == 3);
  CHECK(*it == 1);
  CHECK(*++it == 4);
  CHECK(*it-- == 4);
  CHECK(*it == 1);
  CHECK(*--it == 3);

  it += 3;
  CHECK(*it == 1);
  it -= 2;
  CHECK(*it == 1);
  CHECK(it - first == 1);

  auto it2 = first;
  it2 = it2 + 4;
  CHECK(*it2 == 5);
  it2 = it2 - 3;
  CHECK(*it2 == 1);
}

template <class It>
void exercise_contiguous(It first, int *raw)
{
  CHECK(std::to_address(first) == raw);
  CHECK(std::to_address(first + 2) == raw + 2);
  CHECK(std::pointer_traits<It>::to_address(first + 4) == raw + 4);
}

int data[] = { 3, 1, 4, 1, 5 };
} // namespace

int main()
{
  std::printf("\n=== fancy_pointer_iterator ===\n");

  exercise(raw_iter { data }, raw_iter { data + 5 });
  exercise_contiguous(raw_iter { data }, data);
  CHECK(raw_iter { data }.operator->() == data);

  exercise(fancy_iter { test::fancy_ptr<int> { data } },
           fancy_iter { test::fancy_ptr<int> { data + 5 } });
  exercise_contiguous(fancy_iter { test::fancy_ptr<int> { data } }, data);
  CHECK(*fancy_iter { data } == 3);
  CHECK(fancy_iter { test::fancy_ptr<int> { data } }.operator->().get() == data);

  fancy_iter const fancy_it { test::fancy_ptr<int> { data } };
  fancy_citer const fancy_cit = fancy_it;
  CHECK(*fancy_cit == 3);
  CHECK(fancy_cit.base().get() == data);

  exercise(legacy_iter { test::legacy_ptr<int> { data } },
           legacy_iter { test::legacy_ptr<int> { data + 5 } });

  CHECK(*input_iter { test::input_ptr<int> { data } } == 3);
  input_iter input_it { test::input_ptr<int> { data } };
  CHECK(*++input_it == 1);

  int sortable[] = { 5, 1, 4, 2, 3 };
  std::ranges::sort(fancy_iter { test::fancy_ptr<int> { sortable } },
                    fancy_iter { test::fancy_ptr<int> { sortable + 5 } });
  for (int i = 0; i < 5; ++i) {
    CHECK(sortable[i] == i + 1);
  }

  int legacy_sortable[] = { 5, 1, 4, 2, 3 };
  std::sort(legacy_iter { test::legacy_ptr<int> { legacy_sortable } },
            legacy_iter { test::legacy_ptr<int> { legacy_sortable + 5 } });
  for (int i = 0; i < 5; ++i) {
    CHECK(legacy_sortable[i] == i + 1);
  }

  int reversed[] = { 5, 1, 4, 2, 3 };
  fancy_span const span { fancy_iter { test::fancy_ptr<int> { reversed } },
                          fancy_iter { test::fancy_ptr<int> { reversed + 5 } } };
  CHECK(std::ranges::data(span) == reversed);
  CHECK(std::ranges::size(span) == 5);
  std::ranges::sort(span);
  for (int i = 0; i < 5; ++i) {
    CHECK(reversed[i] == i + 1);
  }

  std::printf("\n===============================\n");
  std::printf("PASS: %d   FAIL: %d\n", g_pass, g_fail);
  std::printf("===============================\n");
  return g_fail == 0 ? 0 : 1;
}
