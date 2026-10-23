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

#ifndef STDEX_TESTS_SINGLETON_TEST_SUPPORT_HPP_
#define STDEX_TESTS_SINGLETON_TEST_SUPPORT_HPP_

#include "LinnSingleton.h"

// Defined in this header, instantiated from two TUs: exercises the C++17
// inline static members and the private ctor/dtor path across TUs.
class CrossTuUnique
{
  LINN_SINGLETON_CREATE(LSS_UNIQUE(CrossTuUnique))

public:
  [[nodiscard]] int value() const noexcept
  {
    return 17;
  }

private:
  CrossTuUnique() = default;
  ~CrossTuUnique() = default;
};

CrossTuUnique *cross_tu_unique_from_a();
CrossTuUnique *cross_tu_unique_from_b();

int run_short_macro_tests();

#endif // !STDEX_TESTS_SINGLETON_TEST_SUPPORT_HPP_
