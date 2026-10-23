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

#define LINN_SINGLETON_ENABLE_SHORT_MACROS

#include <cstdio>
#include <memory>

#include "singleton_test_support.hpp"

namespace
{

int g_checks   = 0;
int g_failures = 0;

#define SHORT_CHECK(expr)                                          \
  do {                                                             \
    ++g_checks;                                                    \
    if (!(expr)) {                                                 \
      ++g_failures;                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    }                                                              \
  } while (false)

class ShortUnique
{
  SINGLETON_CREATE(UNIQUE(ShortUnique))

public:
  [[nodiscard]] int value() const noexcept
  {
    return 3;
  }

private:
  ShortUnique() = default;
  ~ShortUnique() = default;
};

class ShortShared
{
  SINGLETON_CREATE(SHARED(ShortShared), DESTROY_NOLOCK)

public:
  [[nodiscard]] int value() const noexcept
  {
    return 5;
  }

private:
  ShortShared() = default;
  ~ShortShared() = default;
};

class ShortMeyers
{
  SINGLETON_CREATE_1(MEYERS(ShortMeyers))

public:
  [[nodiscard]] int value() const noexcept
  {
    return 7;
  }

private:
  ShortMeyers() = default;
  ~ShortMeyers() = default;
};

} // namespace

int run_short_macro_tests()
{
  auto *unique = ShortUnique::getInstance();

  SHORT_CHECK(unique != nullptr);
  SHORT_CHECK(unique == ShortUnique::getInstance());
  SHORT_CHECK(unique != nullptr && unique->value() == 3);

  auto shared = ShortShared::getInstance();

  SHORT_CHECK(shared != nullptr);
  SHORT_CHECK(shared != nullptr && shared->value() == 5);

  ShortShared::destroyInstance();
  SHORT_CHECK(ShortShared::isInstanceDestroyed());
  SHORT_CHECK(ShortShared::getInstance() == nullptr);

  auto &meyers = ShortMeyers::getInstance();

  SHORT_CHECK(&meyers == &ShortMeyers::getInstance());
  SHORT_CHECK(meyers.value() == 7);

  std::printf("short-macro checks: %d, failed: %d\n", g_checks, g_failures);
  return g_failures;
}
