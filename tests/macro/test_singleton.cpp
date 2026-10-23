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

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <thread>
#include <type_traits>
#include <vector>

#include "LinnSingleton.h"
#include "singleton_test_support.hpp"

namespace
{

int g_checks   = 0;
int g_failures = 0;

#define CHECK(expr)                                                \
  do {                                                             \
    ++g_checks;                                                    \
    if (!(expr)) {                                                 \
      ++g_failures;                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    }                                                              \
  } while (false)

template <class T>
inline constexpr bool is_locked_down =
    !std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T> &&
    !std::is_move_constructible_v<T> && !std::is_move_assignable_v<T>;

// ---------------------------------------------------------------------------
// UNIQUE x 3 destroy modes
// ---------------------------------------------------------------------------

class UniqueNoDestroy
{
  LINN_SINGLETON_CREATE(LSS_UNIQUE(UniqueNoDestroy))

public:
  [[nodiscard]] int value() const noexcept
  {
    return m_value;
  }

  static int constructions() noexcept
  {
    return s_constructions;
  }

private:
  explicit UniqueNoDestroy(int value) noexcept
      : m_value(value)
  {
    ++s_constructions;
  }

  ~UniqueNoDestroy() = default;

  int m_value;
  inline static int s_constructions = 0;
};

class UniqueEnableDestroy
{
  LINN_SINGLETON_CREATE(LSS_UNIQUE(UniqueEnableDestroy), LSS_DESTROY_NOLOCK)

public:
  [[nodiscard]] int value() const noexcept
  {
    return m_value;
  }

  static int constructions() noexcept
  {
    return s_constructions;
  }

  static int destructions() noexcept
  {
    return s_destructions;
  }

private:
  explicit UniqueEnableDestroy(int value) noexcept
      : m_value(value)
  {
    ++s_constructions;
  }

  ~UniqueEnableDestroy()
  {
    ++s_destructions;
  }

  int m_value;
  inline static int s_constructions = 0;
  inline static int s_destructions  = 0;
};

class UniqueLockDestroy
{
  LINN_SINGLETON_CREATE(LSS_UNIQUE(UniqueLockDestroy), LSS_DESTROY_LOCKED)

public:
  [[nodiscard]] int value() const noexcept
  {
    return m_value;
  }

  static int constructions() noexcept
  {
    return s_constructions;
  }

  static int destructions() noexcept
  {
    return s_destructions;
  }

private:
  explicit UniqueLockDestroy(int value) noexcept
      : m_value(value)
  {
    ++s_constructions;
  }

  ~UniqueLockDestroy()
  {
    ++s_destructions;
  }

  int m_value;
  inline static int s_constructions = 0;
  inline static int s_destructions  = 0;
};

// ---------------------------------------------------------------------------
// SHARED x 3 destroy modes
// ---------------------------------------------------------------------------

class SharedNoDestroy
{
  LINN_SINGLETON_CREATE(LSS_SHARED(SharedNoDestroy))

public:
  [[nodiscard]] int value() const noexcept
  {
    return m_value;
  }

  static int constructions() noexcept
  {
    return s_constructions;
  }

private:
  explicit SharedNoDestroy(int value) noexcept
      : m_value(value)
  {
    ++s_constructions;
  }

  ~SharedNoDestroy() = default;

  int m_value;
  inline static int s_constructions = 0;
};

class SharedEnableDestroy
{
  LINN_SINGLETON_CREATE(LSS_SHARED(SharedEnableDestroy), LSS_DESTROY_NOLOCK)

public:
  [[nodiscard]] int value() const noexcept
  {
    return m_value;
  }

  static int constructions() noexcept
  {
    return s_constructions;
  }

  static int destructions() noexcept
  {
    return s_destructions;
  }

private:
  explicit SharedEnableDestroy(int value) noexcept
      : m_value(value)
  {
    ++s_constructions;
  }

  ~SharedEnableDestroy()
  {
    ++s_destructions;
  }

  int m_value;
  inline static int s_constructions = 0;
  inline static int s_destructions  = 0;
};

class SharedLockDestroy
{
  LINN_SINGLETON_CREATE(LSS_SHARED(SharedLockDestroy), LSS_DESTROY_LOCKED)

public:
  [[nodiscard]] int value() const noexcept
  {
    return m_value;
  }

  static int constructions() noexcept
  {
    return s_constructions;
  }

  static int destructions() noexcept
  {
    return s_destructions;
  }

private:
  explicit SharedLockDestroy(int value) noexcept
      : m_value(value)
  {
    ++s_constructions;
  }

  ~SharedLockDestroy()
  {
    ++s_destructions;
  }

  int m_value;
  inline static int s_constructions = 0;
  inline static int s_destructions  = 0;
};

// ---------------------------------------------------------------------------
// MEYERS x 3 destroy modes (destroy variants degrade to UNIQUE)
// ---------------------------------------------------------------------------

class MeyersNoDestroy
{
  LINN_SINGLETON_CREATE(LSS_MEYERS(MeyersNoDestroy))

public:
  [[nodiscard]] int value() const noexcept
  {
    return 31;
  }

  static int constructions() noexcept
  {
    return s_constructions;
  }

private:
  MeyersNoDestroy() noexcept
  {
    ++s_constructions;
  }

  ~MeyersNoDestroy() = default;

  inline static int s_constructions = 0;
};

class MeyersEnableDestroy
{
  LINN_SINGLETON_CREATE(LSS_MEYERS(MeyersEnableDestroy), LSS_DESTROY_NOLOCK)

public:
  [[nodiscard]] int value() const noexcept
  {
    return 37;
  }

  static int constructions() noexcept
  {
    return s_constructions;
  }

  static int destructions() noexcept
  {
    return s_destructions;
  }

private:
  MeyersEnableDestroy() noexcept
  {
    ++s_constructions;
  }

  ~MeyersEnableDestroy()
  {
    ++s_destructions;
  }

  inline static int s_constructions = 0;
  inline static int s_destructions  = 0;
};

class MeyersLockDestroy
{
  LINN_SINGLETON_CREATE(LSS_MEYERS(MeyersLockDestroy), LSS_DESTROY_LOCKED)

public:
  [[nodiscard]] int value() const noexcept
  {
    return 41;
  }

  static int constructions() noexcept
  {
    return s_constructions;
  }

  static int destructions() noexcept
  {
    return s_destructions;
  }

private:
  MeyersLockDestroy() noexcept
  {
    ++s_constructions;
  }

  ~MeyersLockDestroy()
  {
    ++s_destructions;
  }

  inline static int s_constructions = 0;
  inline static int s_destructions  = 0;
};

// ---------------------------------------------------------------------------
// Reentrant destruction: the destroyed flag must be set before the object dies
// ---------------------------------------------------------------------------

class ReentrantUniqueNoLock
{
  LINN_SINGLETON_CREATE(LSS_UNIQUE(ReentrantUniqueNoLock), LSS_DESTROY_NOLOCK)

public:
  static int constructions() noexcept
  {
    return s_constructions;
  }

  static bool reentered() noexcept
  {
    return s_reentered;
  }

  static bool sawDestroyed() noexcept
  {
    return s_sawDestroyed;
  }

private:
  ReentrantUniqueNoLock() noexcept
  {
    ++s_constructions;
  }

  ~ReentrantUniqueNoLock()
  {
    s_reentered    = true;
    s_sawDestroyed = (getInstance() == nullptr);
  }

  inline static int s_constructions = 0;
  inline static bool s_reentered    = false;
  inline static bool s_sawDestroyed = false;
};

class ReentrantSharedNoLock
{
  LINN_SINGLETON_CREATE(LSS_SHARED(ReentrantSharedNoLock), LSS_DESTROY_NOLOCK)

public:
  static int constructions() noexcept
  {
    return s_constructions;
  }

  static bool reentered() noexcept
  {
    return s_reentered;
  }

  static bool sawDestroyed() noexcept
  {
    return s_sawDestroyed;
  }

private:
  ReentrantSharedNoLock() noexcept
  {
    ++s_constructions;
  }

  ~ReentrantSharedNoLock()
  {
    s_reentered    = true;
    s_sawDestroyed = (getInstance() == nullptr);
  }

  inline static int s_constructions = 0;
  inline static bool s_reentered    = false;
  inline static bool s_sawDestroyed = false;
};

// ---------------------------------------------------------------------------
// Final class: the factory must not derive from CLASS
// ---------------------------------------------------------------------------

class FinalUnique final
{
  LINN_SINGLETON_CREATE(LSS_UNIQUE(FinalUnique))

public:
  [[nodiscard]] int value() const noexcept
  {
    return 43;
  }

private:
  FinalUnique() = default;
  ~FinalUnique() = default;
};

class FinalShared final
{
  LINN_SINGLETON_CREATE(LSS_SHARED(FinalShared))

public:
  [[nodiscard]] int value() const noexcept
  {
    return 47;
  }

private:
  FinalShared() = default;
  ~FinalShared() = default;
};

// ---------------------------------------------------------------------------
// Threaded variants
// ---------------------------------------------------------------------------

class ThreadedUniqueNoDestroy
{
  LINN_SINGLETON_CREATE(LSS_UNIQUE(ThreadedUniqueNoDestroy))

public:
  static int constructions() noexcept
  {
    return s_constructions;
  }

private:
  ThreadedUniqueNoDestroy() noexcept
  {
    ++s_constructions;
  }

  ~ThreadedUniqueNoDestroy() = default;

  inline static int s_constructions = 0;
};

class ThreadedUniqueLockDestroy
{
  LINN_SINGLETON_CREATE(LSS_UNIQUE(ThreadedUniqueLockDestroy), LSS_DESTROY_LOCKED)

public:
  static int constructions() noexcept
  {
    return s_constructions;
  }

  static int destructions() noexcept
  {
    return s_destructions;
  }

private:
  ThreadedUniqueLockDestroy() noexcept
  {
    ++s_constructions;
  }

  ~ThreadedUniqueLockDestroy()
  {
    ++s_destructions;
  }

  inline static int s_constructions = 0;
  inline static int s_destructions  = 0;
};

class ThreadedSharedNoDestroy
{
  LINN_SINGLETON_CREATE(LSS_SHARED(ThreadedSharedNoDestroy))

public:
  static int constructions() noexcept
  {
    return s_constructions;
  }

private:
  ThreadedSharedNoDestroy() noexcept
  {
    ++s_constructions;
  }

  ~ThreadedSharedNoDestroy() = default;

  inline static int s_constructions = 0;
};

class ThreadedSharedLockDestroy
{
  LINN_SINGLETON_CREATE(LSS_SHARED(ThreadedSharedLockDestroy), LSS_DESTROY_LOCKED)

public:
  static int constructions() noexcept
  {
    return s_constructions;
  }

  static int destructions() noexcept
  {
    return s_destructions;
  }

private:
  ThreadedSharedLockDestroy() noexcept
  {
    ++s_constructions;
  }

  ~ThreadedSharedLockDestroy()
  {
    ++s_destructions;
  }

  inline static int s_constructions = 0;
  inline static int s_destructions  = 0;
};

class ThreadedSharedMixed
{
  LINN_SINGLETON_CREATE(LSS_SHARED(ThreadedSharedMixed), LSS_DESTROY_LOCKED)

public:
  [[nodiscard]] int value() const noexcept
  {
    return 29;
  }

  static int constructions() noexcept
  {
    return s_constructions;
  }

  static int destructions() noexcept
  {
    return s_destructions;
  }

private:
  ThreadedSharedMixed() noexcept
  {
    ++s_constructions;
  }

  ~ThreadedSharedMixed()
  {
    ++s_destructions;
  }

  inline static int s_constructions = 0;
  inline static int s_destructions  = 0;
};

class ThreadedMeyersNoDestroy
{
  LINN_SINGLETON_CREATE(LSS_MEYERS(ThreadedMeyersNoDestroy))

public:
  static int constructions() noexcept
  {
    return s_constructions;
  }

private:
  ThreadedMeyersNoDestroy() noexcept
  {
    ++s_constructions;
  }

  ~ThreadedMeyersNoDestroy() = default;

  inline static int s_constructions = 0;
};

// ---------------------------------------------------------------------------
// Trait / signature checks
// ---------------------------------------------------------------------------

static_assert(is_locked_down<UniqueNoDestroy>);
static_assert(is_locked_down<UniqueEnableDestroy>);
static_assert(is_locked_down<UniqueLockDestroy>);
static_assert(is_locked_down<SharedNoDestroy>);
static_assert(is_locked_down<SharedEnableDestroy>);
static_assert(is_locked_down<SharedLockDestroy>);
static_assert(is_locked_down<MeyersNoDestroy>);
static_assert(is_locked_down<MeyersEnableDestroy>);
static_assert(is_locked_down<MeyersLockDestroy>);
static_assert(is_locked_down<CrossTuUnique>);
static_assert(is_locked_down<FinalUnique>);
static_assert(is_locked_down<FinalShared>);

static_assert(std::is_same_v<decltype(UniqueNoDestroy::getInstance(0)), UniqueNoDestroy *>);
static_assert(std::is_same_v<decltype(UniqueEnableDestroy::getInstance(0)), UniqueEnableDestroy *>);
static_assert(std::is_same_v<decltype(UniqueLockDestroy::getInstance(0)), UniqueLockDestroy *>);
static_assert(std::is_same_v<decltype(SharedNoDestroy::getInstance(0)), std::shared_ptr<SharedNoDestroy>>);
static_assert(std::is_same_v<decltype(SharedEnableDestroy::getInstance(0)), std::shared_ptr<SharedEnableDestroy>>);
static_assert(std::is_same_v<decltype(SharedLockDestroy::getInstance(0)), std::shared_ptr<SharedLockDestroy>>);
static_assert(std::is_same_v<decltype(MeyersNoDestroy::getInstance()), MeyersNoDestroy &>);
static_assert(std::is_same_v<decltype(MeyersEnableDestroy::getInstance()), MeyersEnableDestroy *>);
static_assert(std::is_same_v<decltype(MeyersLockDestroy::getInstance()), MeyersLockDestroy *>);
static_assert(std::is_same_v<decltype(FinalShared::getInstance()), std::shared_ptr<FinalShared>>);

// ---------------------------------------------------------------------------
// Single-threaded behaviour
// ---------------------------------------------------------------------------

void test_unique_no_destroy()
{
  auto *first  = UniqueNoDestroy::getInstance(1);
  auto *second = UniqueNoDestroy::getInstance(2);

  CHECK(first != nullptr);
  CHECK(first == second);
  CHECK(first != nullptr && first->value() == 1);
  CHECK(UniqueNoDestroy::constructions() == 1);
}

void test_unique_enable_destroy()
{
  auto *instance = UniqueEnableDestroy::getInstance(5);

  CHECK(instance != nullptr);
  CHECK(instance != nullptr && instance->value() == 5);
  CHECK(!UniqueEnableDestroy::isInstanceDestroyed());
  CHECK(UniqueEnableDestroy::constructions() == 1);

  UniqueEnableDestroy::destroyInstance();

  CHECK(UniqueEnableDestroy::isInstanceDestroyed());
  CHECK(UniqueEnableDestroy::destructions() == 1);
  CHECK(UniqueEnableDestroy::getInstance(6) == nullptr);
  CHECK(UniqueEnableDestroy::constructions() == 1);

  UniqueEnableDestroy::destroyInstance();
  CHECK(UniqueEnableDestroy::destructions() == 1);
}

void test_unique_lock_destroy()
{
  auto *instance = UniqueLockDestroy::getInstance(7);

  CHECK(instance != nullptr);
  CHECK(instance != nullptr && instance->value() == 7);
  CHECK(!UniqueLockDestroy::isInstanceDestroyed());
  CHECK(UniqueLockDestroy::constructions() == 1);

  UniqueLockDestroy::destroyInstance();

  CHECK(UniqueLockDestroy::isInstanceDestroyed());
  CHECK(UniqueLockDestroy::destructions() == 1);
  CHECK(UniqueLockDestroy::getInstance(8) == nullptr);
  CHECK(UniqueLockDestroy::constructions() == 1);

  UniqueLockDestroy::destroyInstance();
  CHECK(UniqueLockDestroy::destructions() == 1);
}

void test_shared_no_destroy()
{
  auto a = SharedNoDestroy::getInstance(9);
  auto b = SharedNoDestroy::getInstance(10);

  CHECK(a != nullptr);
  CHECK(b != nullptr);
  CHECK(a.get() == b.get());
  CHECK(a != nullptr && a->value() == 9);
  CHECK(SharedNoDestroy::constructions() == 1);
  CHECK(a.use_count() >= 2);
}

void test_shared_enable_destroy()
{
  auto owner = SharedEnableDestroy::getInstance(11);

  CHECK(owner != nullptr);
  CHECK(owner != nullptr && owner->value() == 11);
  CHECK(SharedEnableDestroy::constructions() == 1);

  SharedEnableDestroy::destroyInstance();

  CHECK(SharedEnableDestroy::isInstanceDestroyed());
  CHECK(SharedEnableDestroy::destructions() == 0);
  CHECK(SharedEnableDestroy::getInstance(12) == nullptr);
  CHECK(SharedEnableDestroy::constructions() == 1);

  owner.reset();
  CHECK(SharedEnableDestroy::destructions() == 1);
}

void test_shared_lock_destroy()
{
  auto owner = SharedLockDestroy::getInstance(13);

  CHECK(owner != nullptr);
  CHECK(owner != nullptr && owner->value() == 13);
  CHECK(SharedLockDestroy::constructions() == 1);

  SharedLockDestroy::destroyInstance();

  CHECK(SharedLockDestroy::isInstanceDestroyed());
  CHECK(SharedLockDestroy::destructions() == 0);
  CHECK(SharedLockDestroy::getInstance(14) == nullptr);
  CHECK(SharedLockDestroy::constructions() == 1);

  owner.reset();
  CHECK(SharedLockDestroy::destructions() == 1);
}

void test_meyers_no_destroy()
{
  auto &first  = MeyersNoDestroy::getInstance();
  auto &second = MeyersNoDestroy::getInstance();

  CHECK(&first == &second);
  CHECK(first.value() == 31);
  CHECK(MeyersNoDestroy::constructions() == 1);
}

void test_meyers_enable_destroy()
{
  auto *instance = MeyersEnableDestroy::getInstance();

  CHECK(instance != nullptr);
  CHECK(instance != nullptr && instance->value() == 37);
  CHECK(MeyersEnableDestroy::constructions() == 1);

  MeyersEnableDestroy::destroyInstance();

  CHECK(MeyersEnableDestroy::isInstanceDestroyed());
  CHECK(MeyersEnableDestroy::destructions() == 1);
  CHECK(MeyersEnableDestroy::getInstance() == nullptr);
  CHECK(MeyersEnableDestroy::constructions() == 1);
}

void test_meyers_lock_destroy()
{
  auto *instance = MeyersLockDestroy::getInstance();

  CHECK(instance != nullptr);
  CHECK(instance != nullptr && instance->value() == 41);
  CHECK(MeyersLockDestroy::constructions() == 1);

  MeyersLockDestroy::destroyInstance();

  CHECK(MeyersLockDestroy::isInstanceDestroyed());
  CHECK(MeyersLockDestroy::destructions() == 1);
  CHECK(MeyersLockDestroy::getInstance() == nullptr);
  CHECK(MeyersLockDestroy::constructions() == 1);
}

void test_reentrant_unique_destroy_nolock()
{
  CHECK(ReentrantUniqueNoLock::getInstance() != nullptr);
  CHECK(ReentrantUniqueNoLock::constructions() == 1);

  ReentrantUniqueNoLock::destroyInstance();

  CHECK(ReentrantUniqueNoLock::reentered());
  CHECK(ReentrantUniqueNoLock::sawDestroyed());
  CHECK(ReentrantUniqueNoLock::constructions() == 1);
  CHECK(ReentrantUniqueNoLock::getInstance() == nullptr);
}

void test_reentrant_shared_destroy_nolock()
{
  CHECK(ReentrantSharedNoLock::getInstance() != nullptr);
  CHECK(ReentrantSharedNoLock::constructions() == 1);

  ReentrantSharedNoLock::destroyInstance();

  CHECK(ReentrantSharedNoLock::reentered());
  CHECK(ReentrantSharedNoLock::sawDestroyed());
  CHECK(ReentrantSharedNoLock::constructions() == 1);
  CHECK(ReentrantSharedNoLock::getInstance() == nullptr);
}

void test_final_unique()
{
  auto *first  = FinalUnique::getInstance();
  auto *second = FinalUnique::getInstance();

  CHECK(first != nullptr);
  CHECK(first == second);
  CHECK(first != nullptr && first->value() == 43);
}

void test_final_shared()
{
  auto first  = FinalShared::getInstance();
  auto second = FinalShared::getInstance();

  CHECK(first != nullptr);
  CHECK(second != nullptr);
  CHECK(first.get() == second.get());
  CHECK(first != nullptr && first->value() == 47);
}

void test_cross_tu_identity()
{
  auto *from_a = cross_tu_unique_from_a();
  auto *from_b = cross_tu_unique_from_b();

  CHECK(from_a != nullptr);
  CHECK(from_a == from_b);
  CHECK(from_a != nullptr && from_a->value() == 17);
}

void test_independent_instances()
{
  auto *unique = UniqueNoDestroy::getInstance(1);
  auto &meyers = MeyersNoDestroy::getInstance();

  CHECK(static_cast<void *>(unique) != static_cast<void *>(&meyers));
}

// ---------------------------------------------------------------------------
// Multi-threaded creation / destruction
// ---------------------------------------------------------------------------

constexpr std::size_t kThreads = 16;

void test_threaded_unique_no_destroy()
{
  std::vector<ThreadedUniqueNoDestroy *> instances(kThreads, nullptr);
  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (std::size_t i = 0; i < kThreads; ++i) {
    threads.emplace_back([&instances, i] { instances[i] = ThreadedUniqueNoDestroy::getInstance(); });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  CHECK(ThreadedUniqueNoDestroy::constructions() == 1);
  CHECK(instances[0] != nullptr);
  for (std::size_t i = 1; i < kThreads; ++i) {
    CHECK(instances[i] == instances[0]);
  }
}

void test_threaded_unique_lock_destroy()
{
  std::vector<ThreadedUniqueLockDestroy *> instances(kThreads, nullptr);
  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (std::size_t i = 0; i < kThreads; ++i) {
    threads.emplace_back([&instances, i] { instances[i] = ThreadedUniqueLockDestroy::getInstance(); });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  CHECK(ThreadedUniqueLockDestroy::constructions() == 1);
  CHECK(instances[0] != nullptr);
  for (std::size_t i = 1; i < kThreads; ++i) {
    CHECK(instances[i] == instances[0]);
  }

  ThreadedUniqueLockDestroy::destroyInstance();
  CHECK(ThreadedUniqueLockDestroy::isInstanceDestroyed());
  CHECK(ThreadedUniqueLockDestroy::destructions() == 1);

  std::vector<ThreadedUniqueLockDestroy *> after(kThreads, nullptr);
  threads.clear();
  for (std::size_t i = 0; i < kThreads; ++i) {
    threads.emplace_back([&after, i] { after[i] = ThreadedUniqueLockDestroy::getInstance(); });
  }
  for (auto &thread : threads) {
    thread.join();
  }
  for (auto *instance : after) {
    CHECK(instance == nullptr);
  }
  CHECK(ThreadedUniqueLockDestroy::constructions() == 1);
}

void test_threaded_shared_no_destroy()
{
  std::vector<std::shared_ptr<ThreadedSharedNoDestroy>> instances(kThreads);
  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (std::size_t i = 0; i < kThreads; ++i) {
    threads.emplace_back([&instances, i] { instances[i] = ThreadedSharedNoDestroy::getInstance(); });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  CHECK(ThreadedSharedNoDestroy::constructions() == 1);
  CHECK(instances[0] != nullptr);
  for (std::size_t i = 1; i < kThreads; ++i) {
    CHECK(instances[i].get() == instances[0].get());
  }
}

void test_threaded_shared_lock_destroy()
{
  std::vector<std::shared_ptr<ThreadedSharedLockDestroy>> instances(kThreads);
  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (std::size_t i = 0; i < kThreads; ++i) {
    threads.emplace_back([&instances, i] { instances[i] = ThreadedSharedLockDestroy::getInstance(); });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  CHECK(ThreadedSharedLockDestroy::constructions() == 1);
  CHECK(instances[0] != nullptr);
  for (std::size_t i = 1; i < kThreads; ++i) {
    CHECK(instances[i].get() == instances[0].get());
  }

  ThreadedSharedLockDestroy::destroyInstance();
  CHECK(ThreadedSharedLockDestroy::isInstanceDestroyed());
  CHECK(ThreadedSharedLockDestroy::destructions() == 0);
  CHECK(ThreadedSharedLockDestroy::getInstance() == nullptr);
  CHECK(ThreadedSharedLockDestroy::constructions() == 1);

  instances.clear();
  CHECK(ThreadedSharedLockDestroy::destructions() == 1);
}

void test_threaded_shared_get_and_destroy()
{
  auto warm = ThreadedSharedMixed::getInstance();

  CHECK(warm != nullptr);
  CHECK(ThreadedSharedMixed::constructions() == 1);
  warm.reset();

  std::atomic<bool> running {true};
  std::atomic<int> mismatches {0};
  std::atomic<long> observed {0};
  std::atomic<long> nulls {0};
  std::vector<std::thread> workers;
  workers.reserve(kThreads);

  for (std::size_t i = 0; i < kThreads; ++i) {
    workers.emplace_back([&running, &mismatches, &observed, &nulls] {
      while (running.load(std::memory_order_relaxed)) {
        auto instance = ThreadedSharedMixed::getInstance();
        if (instance == nullptr) {
          nulls.fetch_add(1, std::memory_order_relaxed);
          continue;
        }
        if (instance->value() != 29) {
          mismatches.fetch_add(1, std::memory_order_relaxed);
        }
        observed.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }

  while (observed.load(std::memory_order_relaxed) == 0) {
    std::this_thread::yield();
  }

  ThreadedSharedMixed::destroyInstance();

  while (nulls.load(std::memory_order_relaxed) == 0) {
    std::this_thread::yield();
  }

  running.store(false, std::memory_order_relaxed);
  for (auto &worker : workers) {
    worker.join();
  }

  CHECK(mismatches.load() == 0);
  CHECK(observed.load() > 0);
  CHECK(nulls.load() > 0);
  CHECK(ThreadedSharedMixed::isInstanceDestroyed());
  CHECK(ThreadedSharedMixed::getInstance() == nullptr);
  CHECK(ThreadedSharedMixed::constructions() == 1);
  CHECK(ThreadedSharedMixed::destructions() == 1);
}

struct StorageProbe
{
  explicit StorageProbe(int v) noexcept
      : value(v)
  {
  }

  int value;
};

void test_unique_storage_wrapper()
{
  using Storage = linn_singleton_detail::UniqueStorage<StorageProbe>;

  static_assert(!std::is_copy_constructible_v<Storage>);
  static_assert(!std::is_copy_assignable_v<Storage>);

  Storage storage;
  CHECK(storage.get() == nullptr);

  storage = std::unique_ptr<StorageProbe>(new StorageProbe(1));
  CHECK(storage.get() != nullptr);
  CHECK(storage.get()->value == 1);

  storage = new StorageProbe(2);
  CHECK(storage.get() != nullptr);
  CHECK(storage.get()->value == 2);

  storage = nullptr;
  CHECK(storage.get() == nullptr);
}

void test_threaded_meyers_no_destroy()
{
  std::vector<ThreadedMeyersNoDestroy *> instances(kThreads, nullptr);
  std::vector<std::thread> threads;
  threads.reserve(kThreads);

  for (std::size_t i = 0; i < kThreads; ++i) {
    threads.emplace_back([&instances, i] { instances[i] = &ThreadedMeyersNoDestroy::getInstance(); });
  }
  for (auto &thread : threads) {
    thread.join();
  }

  CHECK(ThreadedMeyersNoDestroy::constructions() == 1);
  CHECK(instances[0] != nullptr);
  for (std::size_t i = 1; i < kThreads; ++i) {
    CHECK(instances[i] == instances[0]);
  }
}

} // namespace

int main()
{
  std::printf("=== LinnSingleton tests ===\n\n");

  test_unique_no_destroy();
  test_unique_enable_destroy();
  test_unique_lock_destroy();
  test_shared_no_destroy();
  test_shared_enable_destroy();
  test_shared_lock_destroy();
  test_meyers_no_destroy();
  test_meyers_enable_destroy();
  test_meyers_lock_destroy();
  test_reentrant_unique_destroy_nolock();
  test_reentrant_shared_destroy_nolock();
  test_final_unique();
  test_final_shared();
  test_cross_tu_identity();
  test_independent_instances();
  test_unique_storage_wrapper();

  test_threaded_unique_no_destroy();
  test_threaded_unique_lock_destroy();
  test_threaded_shared_no_destroy();
  test_threaded_shared_lock_destroy();
  test_threaded_shared_get_and_destroy();
  test_threaded_meyers_no_destroy();

  g_failures += run_short_macro_tests();

  std::printf("\n=== Results: %d checks, %d failed ===\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
