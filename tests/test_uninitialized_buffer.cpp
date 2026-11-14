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

// test_uninitialized_buffer.cpp
// Covers stdex::uninitialized_buffer allocation, accessors, copy and move semantics.
#include <cstddef>
#include <cstdio>
#include <memory>
#include <type_traits>
#include <utility>

#include "uninitialized_buffer.hpp"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond)                                                          \
  do {                                                                       \
    if (cond) {                                                              \
      ++g_pass;                                                              \
    } else {                                                                 \
      ++g_fail;                                                              \
      std::fprintf(stderr, "  [FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond); \
    }                                                                        \
  } while (0)

#define SECTION(name) std::printf("\n=== %s ===\n", name)

using int_buffer = stdex::uninitialized_buffer<int>;

struct non_trivially_copyable
{
  int value;
  non_trivially_copyable() = default;
  non_trivially_copyable(non_trivially_copyable const &other)
      : value { other.value }
  {
  }
};

static_assert(std::is_trivially_default_constructible_v<non_trivially_copyable>);
static_assert(std::is_trivially_destructible_v<non_trivially_copyable>);
static_assert(!std::is_trivially_copyable_v<non_trivially_copyable>);
static_assert(!std::is_copy_constructible_v<stdex::uninitialized_buffer<non_trivially_copyable>>);
static_assert(!std::is_copy_assignable_v<stdex::uninitialized_buffer<non_trivially_copyable>>);

struct deleted_copy_constructor
{
  int value;
  deleted_copy_constructor()                                 = default;
  deleted_copy_constructor(deleted_copy_constructor const &) = delete;
};

static_assert(std::is_trivially_copyable_v<deleted_copy_constructor>);
static_assert(!std::is_copy_constructible_v<deleted_copy_constructor>);
static_assert(std::is_copy_assignable_v<deleted_copy_constructor>);
static_assert(!std::is_copy_constructible_v<stdex::uninitialized_buffer<deleted_copy_constructor>>);
static_assert(!std::is_copy_assignable_v<stdex::uninitialized_buffer<deleted_copy_constructor>>);

struct deleted_copy_assignment
{
  int value;
  deleted_copy_assignment()                                            = default;
  deleted_copy_assignment(deleted_copy_assignment const &)             = default;
  deleted_copy_assignment &operator= (deleted_copy_assignment const &) = delete;
};

static_assert(std::is_trivially_copyable_v<deleted_copy_assignment>);
static_assert(std::is_copy_constructible_v<deleted_copy_assignment>);
static_assert(!std::is_copy_assignable_v<deleted_copy_assignment>);
static_assert(std::is_copy_constructible_v<stdex::uninitialized_buffer<deleted_copy_assignment>>);
static_assert(!std::is_copy_assignable_v<stdex::uninitialized_buffer<deleted_copy_assignment>>);

static_assert(std::is_default_constructible_v<int_buffer>);
static_assert(std::is_move_constructible_v<int_buffer>);
static_assert(std::is_move_assignable_v<int_buffer>);
static_assert(std::is_copy_constructible_v<int_buffer>);
static_assert(std::is_copy_assignable_v<int_buffer>);
static_assert(std::is_same_v<int_buffer::size_type, std::size_t>);
static_assert(std::is_same_v<int_buffer::pointer, int *>);
static_assert(std::is_same_v<int_buffer::allocator_type, std::allocator<int>>);
static_assert(std::is_same_v<stdex::uninitialized_buffer<int, std::allocator<std::byte>>::allocator_type,
                             std::allocator<int>>);

template <class T>
struct fancy_ptr
{
  T *p_ = nullptr;

  using value_type      = T;
  using element_type    = T;
  using difference_type = std::ptrdiff_t;

  fancy_ptr() = default;
  explicit fancy_ptr(T *p) noexcept : p_(p) {}
  T &operator*() const noexcept { return *p_; }
  T *operator->() const noexcept { return p_; }
  bool operator== (fancy_ptr other) const noexcept { return p_ == other.p_; }
  bool operator!= (fancy_ptr other) const noexcept { return p_ != other.p_; }
};

namespace std
{
template <class T>
struct pointer_traits<fancy_ptr<T>>
{
  using pointer         = fancy_ptr<T>;
  using element_type    = T;
  using difference_type = ptrdiff_t;
  template <class U>
  using rebind = fancy_ptr<U>;
  static constexpr T *to_address(pointer p) noexcept { return p.p_; }
};
} // namespace std

template <class T>
struct fancy_alloc
{
  using value_type      = T;
  using pointer         = fancy_ptr<T>;
  using size_type       = std::size_t;
  using difference_type = std::ptrdiff_t;

  fancy_alloc() = default;
  template <class U>
  fancy_alloc(fancy_alloc<U> const &) noexcept
  {
  }

  pointer allocate(size_type n) { return pointer(static_cast<T *>(::operator new(n * sizeof(T)))); }
  void deallocate(pointer p, size_type) noexcept { ::operator delete(p.p_); }

  template <class U>
  struct rebind
  {
    using other = fancy_alloc<U>;
  };
};

static void test_basic_accessors()
{
  SECTION("uninitialized_buffer: basic accessors");
  int_buffer buffer(4);
  CHECK(buffer.size() == 4);
  CHECK(!buffer.empty());
  CHECK(buffer.data() != nullptr);
  CHECK((buffer.end() - buffer.begin()) == 4);
  CHECK((buffer.cend() - buffer.cbegin()) == 4);
  CHECK(buffer.max_size() >= buffer.size());
  CHECK(buffer.size_bytes() == 4 * sizeof(int));

  buffer[0] = 10;
  buffer[1] = 20;
  buffer[2] = 30;
  buffer[3] = 40;
  CHECK(buffer[0] == 10 && buffer[1] == 20 && buffer[2] == 30 && buffer[3] == 40);

  std::allocator<int> allocator = buffer.get_allocator();
  (void) allocator;
}

static void test_fancy_allocator()
{
  SECTION("uninitialized_buffer: fancy pointer allocator");
  fancy_alloc<int> allocator;
  stdex::uninitialized_buffer<int, fancy_alloc<int>> buffer(3, allocator);
  CHECK(buffer.size() == 3);
  CHECK(buffer.data() != nullptr);
  buffer[0] = 7;
  buffer[1] = 8;
  buffer[2] = 9;
  CHECK(buffer[0] == 7 && buffer[1] == 8 && buffer[2] == 9);
}

static void test_default_and_empty()
{
  SECTION("uninitialized_buffer: default-constructed / zero-sized");
  int_buffer empty;
  CHECK(empty.empty());
  CHECK(empty.size() == 0);
  CHECK(empty.data() == nullptr);
  CHECK(empty.begin() == empty.end());
  CHECK(empty.cbegin() == empty.cend());
  CHECK(empty.size_bytes() == 0);

  int_buffer zero(0);
  CHECK(zero.empty());
  CHECK(zero.data() == nullptr);
  CHECK(zero.begin() == zero.end());
}

static void test_move_semantics()
{
  SECTION("uninitialized_buffer: move construction / assignment");
  int_buffer source(3);
  source[0] = 1;
  source[1] = 2;
  source[2] = 3;

  int_buffer moved(std::move(source));
  CHECK(source.empty());
  CHECK(source.data() == nullptr);
  CHECK(moved.size() == 3);
  CHECK(moved[0] == 1 && moved[1] == 2 && moved[2] == 3);

  int_buffer target(1);
  target[0] = 99;
  target = std::move(moved);
  CHECK(moved.empty());
  CHECK(moved.data() == nullptr);
  CHECK(target.size() == 3);
  CHECK(target[0] == 1 && target[1] == 2 && target[2] == 3);
}

static void test_copy_semantics()
{
  SECTION("uninitialized_buffer: copy construction / assignment");
  int_buffer source(3);
  source[0] = 1;
  source[1] = 2;
  source[2] = 3;

  int_buffer copied(source);
  CHECK(copied.size() == 3);
  CHECK(copied[0] == 1 && copied[1] == 2 && copied[2] == 3);
  CHECK(source[0] == 1 && source[1] == 2 && source[2] == 3);

  int_buffer target(1);
  target[0] = 99;
  target = source;
  CHECK(target.size() == 3);
  CHECK(target[0] == 1 && target[1] == 2 && target[2] == 3);
}

int main()
{
  test_basic_accessors();
  test_fancy_allocator();
  test_default_and_empty();
  test_move_semantics();
  test_copy_semantics();

  std::printf("\npass=%d fail=%d\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
