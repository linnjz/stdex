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
 * @file   LinnSingleton.h
 * @brief  Macro-based singleton factory: 3 storage modes x 3 destroy modes.
 *
 * @details
 * `LINN_SINGLETON_CREATE(MODE_PAIR [, DESTROY_MODE])` injects a singleton
 * into a class body. One-time creation is thread-safe in every mode except
 * `LSS_DESTROY_NOLOCK`, which expects `getInstance()`/`destroyInstance()` to
 * stay on a single thread.
 *
 * @note Requires C++17 (inline static data members, `std::scoped_lock`,
 * `std::is_convertible_v`).
 *
 * Arg 1 — storage mode (wrap class):
 *   - `LSS_UNIQUE(CLASS)`  -> `unique_ptr`, `getInstance()` returns `CLASS*`
 *   - `LSS_SHARED(CLASS)`  -> `shared_ptr`, `getInstance()` returns `shared_ptr<CLASS>`
 *   - `LSS_MEYERS(CLASS)`  -> function-local static, `getInstance()` returns `CLASS&` (NO_DESTROY only)
 *
 * Arg 2 — destroy mode (default `LSS_NO_DESTROY`):
 *   - `LSS_NO_DESTROY`           -> lives forever, no `destroyInstance()`
 *   - `LSS_DESTROY_NOLOCK`       -> adds `destroyInstance()` / `isInstanceDestroyed()`
 *   - `LSS_DESTROY_LOCKED`       -> same, but serializes get/destroy
 *
 * @code
 * class Foo {
 *   LINN_SINGLETON_CREATE(LSS_UNIQUE(Foo))                 // no destroy
 * private:
 *   Foo(int a);
 *   ~Foo();
 * };
 *
 * class Bar {
 *   LINN_SINGLETON_CREATE(LSS_SHARED(Bar), LSS_DESTROY_LOCKED)
 * private:
 *   Bar() = default;
 *   ~Bar() = default;
 * };
 *
 * Foo::getInstance(42);            // CLASS*
 * Bar::getInstance();              // shared_ptr<Bar>
 * Bar::destroyInstance();
 * @endcode
 *
 * @note
 * - MEYERS + NO_DESTROY takes no constructor args; UNIQUE, SHARED, and
 *   MEYERS + destroy accept args on the first `getInstance()` call only.
 * - Destruction is one-way: after `destroyInstance()`, `getInstance()`
 *   returns `nullptr` / null `shared_ptr` (never re-created).
 * - `LSS_DESTROY_NOLOCK` performs no synchronization; use it only when all
 *   calls are confined to one thread.
 * - Constructors run inside `call_once`/the lock and must not call
 *   `getInstance()` or `destroyInstance()`. Destructors must not call
 *   `destroyInstance()` or throw; a destructor that re-enters `getInstance()`
 *   observes the destroyed state because the flag is set before release, but
 *   relying on that is discouraged. `unique_ptr`/`shared_ptr` reset are
 *   `noexcept`, so a throwing `~CLASS()` terminates.
 * - Copy/move are disabled. SHARED mode keeps the object alive while external
 *   `shared_ptr` owners exist. SHARED reaches private constructors through a
 *   nested helper (single allocation); final classes fall back to a
 *   two-allocation path.
 * - MEYERS + destroy modes degrade to UNIQUE: `getInstance()` returns `CLASS*`
 *   (not `CLASS&`) and accepts constructor arguments on the first call. Use
 *   MEYERS + `LSS_NO_DESTROY` when a `CLASS&` is required.
 * - MEYERS + NO_DESTROY has the usual function-local-static destruction-order
 *   hazard; do not call `getInstance()` from other static destructors.
 *
 * @warning `LSS_DESTROY_LOCKED` serializes `getInstance()`/`destroyInstance()`
 * only. In UNIQUE mode the returned `CLASS*` is not lifetime-protected:
 * another thread may destroy it immediately after return. Use SHARED when
 * owners can outlive `destroyInstance()`.
 */

#ifndef LINN_SINGLETON_H_
#define LINN_SINGLETON_H_

#include <atomic>      // std::atomic_bool, std::memory_order
#include <cstddef>     // std::nullptr_t
#include <memory>      // std::unique_ptr, std::shared_ptr, std::make_unique, std::make_shared
#include <mutex>       // std::once_flag, std::call_once, std::mutex, std::scoped_lock (C++17)
#include <type_traits> // std::is_final, std::false_type, std::true_type, std::is_convertible_v (C++17)
#include <utility>     // std::forward

#include "LinnPimpl.h"
#include "LinnUtils.h"

namespace linn_singleton_detail
{

// The unique_ptr is a static member so this wrapper's destructor stays trivial:
// `inline static UniqueStorage<CLASS> s_Instance` can then be defined while
// CLASS is still incomplete. The unique_ptr definition is instantiated on first
// use, after CLASS is complete. This is a single-storage handle: do not create
// a second UniqueStorage<T>, because every instance shares s_Pointer.
template <typename T>
struct UniqueStorage
{
  inline static std::unique_ptr<T> s_Pointer { nullptr };

  UniqueStorage() = default;

  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U *, T *>>>
  UniqueStorage &operator= (std::unique_ptr<U> &&other) noexcept
  {
    s_Pointer = std::move(other);
    return *this;
  }

  UniqueStorage &operator= (T *raw) noexcept
  {
    s_Pointer.reset(raw);
    return *this;
  }

  UniqueStorage &operator= (std::nullptr_t) noexcept
  {
    s_Pointer.reset();
    return *this;
  }

  [[nodiscard]] T *get() const noexcept
  {
    return s_Pointer.get();
  }

  UniqueStorage(UniqueStorage const &)             = delete;
  UniqueStorage &operator= (UniqueStorage const &) = delete;
};

///definitions of helper macros
#pragma region LINN_SINGLETON_HELPER_CORE

#define LINN_SINGLETON_HELPER_PP_EQ(a, b) LINN_CAT(LINN_SINGLETON_HELPER_PP_EQ_, LINN_CAT(a, b))
#define LINN_SINGLETON_HELPER_PP_EQ_00    1
#define LINN_SINGLETON_HELPER_PP_EQ_01    0
#define LINN_SINGLETON_HELPER_PP_EQ_02    0
#define LINN_SINGLETON_HELPER_PP_EQ_10    0
#define LINN_SINGLETON_HELPER_PP_EQ_11    1
#define LINN_SINGLETON_HELPER_PP_EQ_12    0
#define LINN_SINGLETON_HELPER_PP_EQ_20    0
#define LINN_SINGLETON_HELPER_PP_EQ_21    0
#define LINN_SINGLETON_HELPER_PP_EQ_22    1

/// UNIQUE cannot hold `inline static std::unique_ptr<CLASS> s_Instance` in the
/// class body: since C++23 ~unique_ptr is constexpr, and clang instantiates
/// constexpr destructors at the point of first reference (the in-class
/// definition) instead of deferring them to the end of the TU. CLASS is still
/// incomplete there, so default_delete<CLASS>'s sizeof check fires
/// (https://github.com/llvm/llvm-project/issues/74963). UniqueStorage keeps the
/// unique_ptr behind a trivially destructible wrapper and instantiates it on
/// first use, after CLASS is complete. SHARED does not need this: ~shared_ptr
/// does not require a complete T.
#define LINN_SINGLETON_HELPER_UNIQUE_FACTORY(CLASS)                        \
  template <typename... Args>                                              \
  static std::unique_ptr<CLASS> CLASS##_InstanceCreateImpl(Args &&...args) \
  {                                                                        \
    return std::unique_ptr<CLASS>(new CLASS(std::forward<Args>(args)...)); \
  }                                                                        \
                                                                           \
  inline static ::linn_singleton_detail::UniqueStorage<CLASS> s_Instance;

/// SHARED storage: make_shared keeps the object and the control block in one
/// allocation. The factory has to derive from CLASS to reach its private
/// constructor; a final CLASS cannot be a base, so that case falls back to the
/// two-allocation raw-pointer form (still well-defined).
#define LINN_SINGLETON_HELPER_SHARED_FACTORY(CLASS)                                                       \
  template <typename Base, typename... Args>                                                              \
  struct LinnSingletonMakeHelper final : public Base                                                      \
  {                                                                                                       \
    explicit LinnSingletonMakeHelper(Args &&...args)                                                      \
        : Base(std::forward<Args>(args)...)                                                               \
    {                                                                                                     \
    }                                                                                                     \
  };                                                                                                      \
                                                                                                          \
  template <typename... Args>                                                                             \
  static std::shared_ptr<CLASS> CLASS##_InstanceCreateShared(std::false_type, Args &&...args)             \
  {                                                                                                       \
    return std::make_shared<LinnSingletonMakeHelper<CLASS, Args...>>(std::forward<Args>(args)...);        \
  }                                                                                                       \
                                                                                                          \
  template <typename... Args>                                                                             \
  static std::shared_ptr<CLASS> CLASS##_InstanceCreateShared(std::true_type, Args &&...args)              \
  {                                                                                                       \
    return std::shared_ptr<CLASS>(new CLASS(std::forward<Args>(args)...), std::default_delete<CLASS> {}); \
  }                                                                                                       \
                                                                                                          \
  template <typename... Args>                                                                             \
  static std::shared_ptr<CLASS> CLASS##_InstanceCreateImpl(Args &&...args)                                \
  {                                                                                                       \
    return CLASS##_InstanceCreateShared(std::is_final<CLASS> {}, std::forward<Args>(args)...);            \
  }

#define LINN_SINGLETON_HELPER_CREATE_BODY_UNIQUE_NO_DESTROY(CLASS)                                             \
                                                                                                               \
public:                                                                                                        \
  LINN_DISABLE_COPY_MOVE(CLASS)                                                                                \
                                                                                                               \
  template <typename... Args>                                                                                  \
  [[nodiscard]] static CLASS *getInstance(Args &&...args)                                                      \
  {                                                                                                            \
    std::call_once(s_InitFlag, [&] { s_Instance = CLASS##_InstanceCreateImpl(std::forward<Args>(args)...); }); \
    return s_Instance.get();                                                                                   \
  }                                                                                                            \
                                                                                                               \
private:                                                                                                       \
  friend std::default_delete<CLASS>;                                                                           \
                                                                                                               \
  LINN_SINGLETON_HELPER_UNIQUE_FACTORY(CLASS)                                                                  \
                                                                                                               \
  inline static std::once_flag s_InitFlag;

#define LINN_SINGLETON_HELPER_CREATE_BODY_UNIQUE_DESTROY_NOLOCK(CLASS)      \
                                                                            \
public:                                                                     \
  LINN_DISABLE_COPY_MOVE(CLASS)                                             \
                                                                            \
  template <typename... Args>                                               \
  [[nodiscard]] static CLASS *getInstance(Args &&...args)                   \
  {                                                                         \
    if (s_Destroyed) {                                                      \
      return nullptr;                                                       \
    }                                                                       \
    if (!s_Instance.get()) {                                                \
      s_Instance = CLASS##_InstanceCreateImpl(std::forward<Args>(args)...); \
    }                                                                       \
    return s_Instance.get();                                                \
  }                                                                         \
                                                                            \
  static void destroyInstance() noexcept                                    \
  {                                                                         \
    s_Destroyed = true;                                                     \
    s_Instance  = nullptr;                                                  \
  }                                                                         \
                                                                            \
  [[nodiscard]] static bool isInstanceDestroyed() noexcept                  \
  {                                                                         \
    return s_Destroyed;                                                     \
  }                                                                         \
                                                                            \
private:                                                                    \
  friend std::default_delete<CLASS>;                                        \
                                                                            \
  LINN_SINGLETON_HELPER_UNIQUE_FACTORY(CLASS)                               \
                                                                            \
  inline static bool s_Destroyed { false };

#define LINN_SINGLETON_HELPER_CREATE_BODY_UNIQUE_DESTROY_LOCKED(CLASS)      \
                                                                            \
public:                                                                     \
  LINN_DISABLE_COPY_MOVE(CLASS)                                             \
                                                                            \
  template <typename... Args>                                               \
  [[nodiscard]] static CLASS *getInstance(Args &&...args)                   \
  {                                                                         \
    if (s_Destroyed.load(std::memory_order_acquire)) {                      \
      return nullptr;                                                       \
    }                                                                       \
    std::scoped_lock lock(s_Mutex);                                         \
    if (s_Destroyed.load(std::memory_order_relaxed)) {                      \
      return nullptr;                                                       \
    }                                                                       \
    if (!s_Instance.get()) {                                                \
      s_Instance = CLASS##_InstanceCreateImpl(std::forward<Args>(args)...); \
    }                                                                       \
    return s_Instance.get();                                                \
  }                                                                         \
                                                                            \
  static void destroyInstance()                                             \
  {                                                                         \
    std::scoped_lock lock(s_Mutex);                                         \
    s_Destroyed.store(true, std::memory_order_release);                     \
    s_Instance = nullptr;                                                   \
  }                                                                         \
                                                                            \
  [[nodiscard]] static bool isInstanceDestroyed() noexcept                  \
  {                                                                         \
    return s_Destroyed.load(std::memory_order_acquire);                     \
  }                                                                         \
                                                                            \
private:                                                                    \
  friend std::default_delete<CLASS>;                                        \
                                                                            \
  LINN_SINGLETON_HELPER_UNIQUE_FACTORY(CLASS)                               \
                                                                            \
  inline static std::mutex s_Mutex;                                         \
  inline static std::atomic_bool s_Destroyed { false };

#define LINN_SINGLETON_HELPER_CREATE_BODY_SHARED_NO_DESTROY(CLASS)                                             \
                                                                                                               \
public:                                                                                                        \
  LINN_DISABLE_COPY_MOVE(CLASS)                                                                                \
                                                                                                               \
  template <typename... Args>                                                                                  \
  [[nodiscard]] static std::shared_ptr<CLASS> getInstance(Args &&...args)                                      \
  {                                                                                                            \
    std::call_once(s_InitFlag, [&] { s_Instance = CLASS##_InstanceCreateImpl(std::forward<Args>(args)...); }); \
    return s_Instance;                                                                                         \
  }                                                                                                            \
                                                                                                               \
private:                                                                                                       \
  friend std::default_delete<CLASS>;                                                                           \
                                                                                                               \
  LINN_SINGLETON_HELPER_SHARED_FACTORY(CLASS)                                                                  \
                                                                                                               \
  inline static std::once_flag s_InitFlag;                                                                     \
  inline static std::shared_ptr<CLASS> s_Instance { nullptr };

#define LINN_SINGLETON_HELPER_CREATE_BODY_SHARED_DESTROY_NOLOCK(CLASS)      \
                                                                            \
public:                                                                     \
  LINN_DISABLE_COPY_MOVE(CLASS)                                             \
                                                                            \
  template <typename... Args>                                               \
  [[nodiscard]] static std::shared_ptr<CLASS> getInstance(Args &&...args)   \
  {                                                                         \
    if (s_Destroyed) {                                                      \
      return {};                                                            \
    }                                                                       \
    if (!s_Instance) {                                                      \
      s_Instance = CLASS##_InstanceCreateImpl(std::forward<Args>(args)...); \
    }                                                                       \
    return s_Instance;                                                      \
  }                                                                         \
                                                                            \
  static void destroyInstance() noexcept                                    \
  {                                                                         \
    s_Destroyed = true;                                                     \
    s_Instance.reset();                                                     \
  }                                                                         \
                                                                            \
  [[nodiscard]] static bool isInstanceDestroyed() noexcept                  \
  {                                                                         \
    return s_Destroyed;                                                     \
  }                                                                         \
                                                                            \
private:                                                                    \
  friend std::default_delete<CLASS>;                                        \
                                                                            \
  LINN_SINGLETON_HELPER_SHARED_FACTORY(CLASS)                               \
                                                                            \
  inline static bool s_Destroyed { false };                                 \
  inline static std::shared_ptr<CLASS> s_Instance { nullptr };

/// SHARED + LOCKED copies s_Instance while holding the lock: s_Instance is a
/// plain shared_ptr, so reading it after unlock would race with destroyInstance().
#define LINN_SINGLETON_HELPER_CREATE_BODY_SHARED_DESTROY_LOCKED(CLASS)      \
                                                                            \
public:                                                                     \
  LINN_DISABLE_COPY_MOVE(CLASS)                                             \
                                                                            \
  template <typename... Args>                                               \
  [[nodiscard]] static std::shared_ptr<CLASS> getInstance(Args &&...args)   \
  {                                                                         \
    if (s_Destroyed.load(std::memory_order_acquire)) {                      \
      return {};                                                            \
    }                                                                       \
    std::scoped_lock lock(s_Mutex);                                         \
    if (s_Destroyed.load(std::memory_order_relaxed)) {                      \
      return {};                                                            \
    }                                                                       \
    if (!s_Instance) {                                                      \
      s_Instance = CLASS##_InstanceCreateImpl(std::forward<Args>(args)...); \
    }                                                                       \
    return s_Instance;                                                      \
  }                                                                         \
                                                                            \
  static void destroyInstance()                                             \
  {                                                                         \
    std::scoped_lock lock(s_Mutex);                                         \
    s_Destroyed.store(true, std::memory_order_release);                     \
    s_Instance.reset();                                                     \
  }                                                                         \
                                                                            \
  [[nodiscard]] static bool isInstanceDestroyed() noexcept                  \
  {                                                                         \
    return s_Destroyed.load(std::memory_order_acquire);                     \
  }                                                                         \
                                                                            \
private:                                                                    \
  friend std::default_delete<CLASS>;                                        \
                                                                            \
  LINN_SINGLETON_HELPER_SHARED_FACTORY(CLASS)                               \
                                                                            \
  inline static std::mutex s_Mutex;                                         \
  inline static std::atomic_bool s_Destroyed { false };                     \
  inline static std::shared_ptr<CLASS> s_Instance { nullptr };

#define LINN_SINGLETON_HELPER_CREATE_BODY_MEYERS_NO_DESTROY(CLASS) \
                                                                   \
public:                                                            \
  LINN_DISABLE_COPY_MOVE(CLASS)                                    \
                                                                   \
  [[nodiscard]] static CLASS &getInstance()                        \
  {                                                                \
    static CLASS instance;                                         \
    return instance;                                               \
  }

#define LINN_SINGLETON_HELPER_CREATE_BODY_MEYERS_DESTROY_NOLOCK(CLASS) LINN_SINGLETON_HELPER_CREATE_BODY_UNIQUE_DESTROY_NOLOCK(CLASS)

#define LINN_SINGLETON_HELPER_CREATE_BODY_MEYERS_DESTROY_LOCKED(CLASS) \
  LINN_SINGLETON_HELPER_CREATE_BODY_UNIQUE_DESTROY_LOCKED(CLASS)

#define LINN_SINGLETON_HELPER_SELECT_DESTROY_UNIQUE(DESTROY_MODE, CLASS)                                           \
  LINN_IF(LINN_SINGLETON_HELPER_PP_EQ(DESTROY_MODE, LINN_SINGLETON_STRATEGY_TAG_DESTROY_MODE_NO_DESTROY))(         \
      LINN_SINGLETON_HELPER_CREATE_BODY_UNIQUE_NO_DESTROY(CLASS),                                                  \
      LINN_IF(LINN_SINGLETON_HELPER_PP_EQ(DESTROY_MODE, LINN_SINGLETON_STRATEGY_TAG_DESTROY_MODE_DESTROY_NOLOCK))( \
          LINN_SINGLETON_HELPER_CREATE_BODY_UNIQUE_DESTROY_NOLOCK(CLASS),                                          \
          LINN_SINGLETON_HELPER_CREATE_BODY_UNIQUE_DESTROY_LOCKED(CLASS)))

#define LINN_SINGLETON_HELPER_SELECT_DESTROY_SHARED(DESTROY_MODE, CLASS)                                           \
  LINN_IF(LINN_SINGLETON_HELPER_PP_EQ(DESTROY_MODE, LINN_SINGLETON_STRATEGY_TAG_DESTROY_MODE_NO_DESTROY))(         \
      LINN_SINGLETON_HELPER_CREATE_BODY_SHARED_NO_DESTROY(CLASS),                                                  \
      LINN_IF(LINN_SINGLETON_HELPER_PP_EQ(DESTROY_MODE, LINN_SINGLETON_STRATEGY_TAG_DESTROY_MODE_DESTROY_NOLOCK))( \
          LINN_SINGLETON_HELPER_CREATE_BODY_SHARED_DESTROY_NOLOCK(CLASS),                                          \
          LINN_SINGLETON_HELPER_CREATE_BODY_SHARED_DESTROY_LOCKED(CLASS)))

#define LINN_SINGLETON_HELPER_SELECT_DESTROY_MEYERS(DESTROY_MODE, CLASS)                                           \
  LINN_IF(LINN_SINGLETON_HELPER_PP_EQ(DESTROY_MODE, LINN_SINGLETON_STRATEGY_TAG_DESTROY_MODE_NO_DESTROY))(         \
      LINN_SINGLETON_HELPER_CREATE_BODY_MEYERS_NO_DESTROY(CLASS),                                                  \
      LINN_IF(LINN_SINGLETON_HELPER_PP_EQ(DESTROY_MODE, LINN_SINGLETON_STRATEGY_TAG_DESTROY_MODE_DESTROY_NOLOCK))( \
          LINN_SINGLETON_HELPER_CREATE_BODY_MEYERS_DESTROY_NOLOCK(CLASS),                                          \
          LINN_SINGLETON_HELPER_CREATE_BODY_MEYERS_DESTROY_LOCKED(CLASS)))

#define LINN_SINGLETON_HELPER_CREATE_FROM_TAG(MODE_TAG, DESTROY_MODE, CLASS)                   \
  LINN_IF(LINN_SINGLETON_HELPER_PP_EQ(MODE_TAG, LINN_SINGLETON_STRATEGY_TAG_MODE_UNIQUE))(     \
      LINN_SINGLETON_HELPER_SELECT_DESTROY_UNIQUE(DESTROY_MODE, CLASS),                        \
      LINN_IF(LINN_SINGLETON_HELPER_PP_EQ(MODE_TAG, LINN_SINGLETON_STRATEGY_TAG_MODE_SHARED))( \
          LINN_SINGLETON_HELPER_SELECT_DESTROY_SHARED(DESTROY_MODE, CLASS),                    \
          LINN_SINGLETON_HELPER_SELECT_DESTROY_MEYERS(DESTROY_MODE, CLASS)))

#define LINN_SINGLETON_HELPER_CREATE_FROM_PAIR(MODE_PAIR, DESTROY_MODE) \
  LINN_SINGLETON_HELPER_CREATE_FROM_TAG(LINN_PAIR_FIRST(MODE_PAIR), DESTROY_MODE, LINN_PAIR_SECOND(MODE_PAIR))

#pragma endregion LINN_SINGLETON_HELPER_CORE

} // namespace linn_singleton_detail

#define LINN_SINGLETON_STRATEGY_TAG_MODE_UNIQUE 0
#define LINN_SINGLETON_STRATEGY_TAG_MODE_SHARED 1
#define LINN_SINGLETON_STRATEGY_TAG_MODE_MEYERS 2

#define LINN_SINGLETON_STRATEGY_TAG_DESTROY_MODE_NO_DESTROY     0
#define LINN_SINGLETON_STRATEGY_TAG_DESTROY_MODE_DESTROY_NOLOCK 1
#define LINN_SINGLETON_STRATEGY_TAG_DESTROY_MODE_DESTROY_LOCKED 2

#define LSS_UNIQUE(CLASS) LINN_PAIR(LINN_SINGLETON_STRATEGY_TAG_MODE_UNIQUE, CLASS)
#define LSS_SHARED(CLASS) LINN_PAIR(LINN_SINGLETON_STRATEGY_TAG_MODE_SHARED, CLASS)
#define LSS_MEYERS(CLASS) LINN_PAIR(LINN_SINGLETON_STRATEGY_TAG_MODE_MEYERS, CLASS)

#define LSS_NO_DESTROY     LINN_SINGLETON_STRATEGY_TAG_DESTROY_MODE_NO_DESTROY
#define LSS_DESTROY_NOLOCK LINN_SINGLETON_STRATEGY_TAG_DESTROY_MODE_DESTROY_NOLOCK
#define LSS_DESTROY_LOCKED LINN_SINGLETON_STRATEGY_TAG_DESTROY_MODE_DESTROY_LOCKED

#define LINN_SINGLETON_CREATE_2(MODE_PAIR, DESTROY_MODE) LINN_SINGLETON_HELPER_CREATE_FROM_PAIR(MODE_PAIR, DESTROY_MODE)
#define LINN_SINGLETON_CREATE_1(MODE_PAIR)               LINN_SINGLETON_CREATE_2(MODE_PAIR, LSS_NO_DESTROY)
#define LINN_SINGLETON_CREATE(...)                       LINN_CAT(LINN_SINGLETON_CREATE_, LINN_NARGS(__VA_ARGS__))(__VA_ARGS__)

#ifdef LINN_SINGLETON_ENABLE_SHORT_MACROS
#  ifndef UNIQUE
#    define UNIQUE(CLASS) LINN_PAIR(LINN_SINGLETON_STRATEGY_TAG_MODE_UNIQUE, CLASS)
#  endif
#  ifndef SHARED
#    define SHARED(CLASS) LINN_PAIR(LINN_SINGLETON_STRATEGY_TAG_MODE_SHARED, CLASS)
#  endif
#  ifndef MEYERS
#    define MEYERS(CLASS) LINN_PAIR(LINN_SINGLETON_STRATEGY_TAG_MODE_MEYERS, CLASS)
#  endif
#  ifndef NO_DESTROY
#    define NO_DESTROY LINN_SINGLETON_STRATEGY_TAG_DESTROY_MODE_NO_DESTROY
#  endif
#  ifndef DESTROY_NOLOCK
#    define DESTROY_NOLOCK LINN_SINGLETON_STRATEGY_TAG_DESTROY_MODE_DESTROY_NOLOCK
#  endif
#  ifndef DESTROY_LOCKED
#    define DESTROY_LOCKED LINN_SINGLETON_STRATEGY_TAG_DESTROY_MODE_DESTROY_LOCKED
#  endif
#  define SINGLETON_CREATE_2(MODE_PAIR, DESTROY_MODE) LINN_SINGLETON_HELPER_CREATE_FROM_PAIR(MODE_PAIR, DESTROY_MODE)
#  define SINGLETON_CREATE_1(MODE_PAIR)               SINGLETON_CREATE_2(MODE_PAIR, LSS_NO_DESTROY)
#  define SINGLETON_CREATE(...)                       LINN_CAT(SINGLETON_CREATE_, LINN_NARGS(__VA_ARGS__))(__VA_ARGS__)
#endif // LINN_SINGLETON_ENABLE_SHORT_MACROS

#if defined(QT_DEBUG) || defined(QT_NO_DEBUG) || defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)

#  define Q_SINGLETON_CREATE_2(MODE_PAIR, DESTROY_MODE) LINN_SINGLETON_CREATE_2(MODE_PAIR, DESTROY_MODE)
#  define Q_SINGLETON_CREATE_1(MODE_PAIR)               LINN_SINGLETON_CREATE_1(MODE_PAIR)
#  define Q_SINGLETON_CREATE(...)                       LINN_SINGLETON_CREATE(__VA_ARGS__)

#  define QSS_UNIQUE(CLASS)  LSS_UNIQUE(CLASS)
#  define QSS_SHARED(CLASS)  LSS_SHARED(CLASS)
#  define QSS_MEYERS(CLASS)  LSS_MEYERS(CLASS)
#  define QSS_NO_DESTROY     LSS_NO_DESTROY
#  define QSS_DESTROY_NOLOCK LSS_DESTROY_NOLOCK
#  define QSS_DESTROY_LOCKED LSS_DESTROY_LOCKED

#endif

#endif // !LINN_SINGLETON_H_
