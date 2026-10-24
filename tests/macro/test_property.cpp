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

#define LINN_PROPERTY_ENABLE_SHORT_MACROS

#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "LinnProperty.h"

namespace
{

int g_checks   = 0;
int g_failures = 0;

static_assert(LINN_NARGS() == 0);
static_assert(LINN_NARGS(1) == 1);
static_assert(LINN_NARGS(1, 2) == 2);
static_assert(LINN_NARGS(1, 2, 3) == 3);

#define CHECK(expr)                                                \
  do {                                                             \
    ++g_checks;                                                    \
    if (!(expr)) {                                                 \
      ++g_failures;                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    }                                                              \
  } while (false)

struct Tracker
{
  static inline int copies = 0;
  static inline int moves  = 0;

  int value = 0;

  Tracker() = default;
  explicit Tracker(int v) : value(v) {}

  Tracker(Tracker const &other) : value(other.value) { ++copies; }

  Tracker(Tracker &&other) noexcept : value(other.value)
  {
    other.value = -1;
    ++moves;
  }

  Tracker &operator=(Tracker const &other)
  {
    value = other.value;
    ++copies;
    return *this;
  }

  Tracker &operator=(Tracker &&other) noexcept
  {
    value       = other.value;
    other.value = -1;
    ++moves;
    return *this;
  }
};

class BasicProps
{
public:
  LINN_PROPERTY_CREATE(int, Count, LPS_DEFAULT_VAL(7))
  LINN_PROPERTY_CREATE(std::string, Name, LPS_DEFAULT_VAL("guest"))
  LINN_PROPERTY_CREATE(std::string, LPS_ASSIGN_MOVE(Title))
  LINN_PROPERTY_CREATE(std::string, LPS_ASSIGN_COPY(TitleCopy))
  LINN_PROPERTY_CREATE_2(const std::string &, std::string, Label, LPS_DEFAULT_VAL("label"))
  LINN_PROPERTY_CREATE_2(const std::string &, const std::string &, RefLabel, LPS_DEFAULT_VAL("ref"))
  LINN_PROPERTY_CREATE_2(const std::string &, std::string_view, ViewLabel, LPS_DEFAULT_VAL("view"))
  LINN_PROPERTY_CREATE_2(const std::string &, std::string, NoDefaultLabel)
  LINN_PROPERTY_CREATE(Tracker, TrackerCopy, LPS_DEFAULT_VAL(Tracker{1}))
  LINN_PROPERTY_CREATE(Tracker, LPS_ASSIGN_MOVE(TrackerMove), LPS_DEFAULT_VAL(Tracker{2}))
  LINN_PROPERTY_CREATE(Tracker, LPS_ASSIGN_COPY(TrackerCopyExplicit), LPS_DEFAULT_VAL(Tracker{3}))
};

class GuardedProps
{
public:
  LINN_PROPERTY_CREATE(int, Always, LPS_DEFAULT_VAL(1), LPS_SETTER_CONTRACT(Always > 0))
  LINN_PROPERTY_CREATE(int, DebugOnly, LPS_DEFAULT_VAL(1), LPS_SETTER_CONTRACT_D(DebugOnly > 0))
  LINN_PROPERTY_CREATE(int, CommaExpr, LPS_DEFAULT_VAL(1), LPS_SETTER_CONTRACT(std::is_same_v<int, int>))
  LINN_PROPERTY_CREATE(int, Three, LPS_DEFAULT_VAL(1), LPS_SETTER_CONTRACT(Three > 0), LPS_SETTER_CONTRACT_D(Three < 100))
};

class ShortProps
{
public:
  PROPERTY_CREATE(int, Count, DEFAULT_VAL(5))
  PROPERTY_CREATE(std::string, ASSIGN_MOVE(Name), DEFAULT_VAL("short"))
  PROPERTY_CREATE(std::string, ASSIGN_COPY(Plain))
  PROPERTY_CREATE_2(const std::string &, std::string, Label, DEFAULT_VAL("short-label"))
  PROPERTY_CREATE_2(const std::string &, std::string, NoDefault)
  PROPERTY_CREATE(int, Guarded, DEFAULT_VAL(1), SETTER_CONTRACT(Guarded > 0))
  PROPERTY_CREATE(int, DebugOnly, DEFAULT_VAL(1), SETTER_CONTRACT_D(DebugOnly > 0))
  PROPERTY_CREATE(int, CommaShort, DEFAULT_VAL(1), SETTER_CONTRACT(std::is_same_v<int, int>))
};

} // namespace

int main()
{
  static_assert(std::is_same_v<decltype(std::declval<BasicProps const &>().GetCount()), int>);
  static_assert(std::is_same_v<decltype(std::declval<BasicProps const &>().GetName()), std::string>);
  static_assert(std::is_same_v<decltype(std::declval<BasicProps const &>().GetLabel()), std::string>);
  static_assert(std::is_same_v<decltype(std::declval<BasicProps const &>().GetRefLabel()), const std::string &>);
  static_assert(std::is_same_v<decltype(std::declval<BasicProps const &>().GetViewLabel()), std::string_view>);
  static_assert(std::is_same_v<decltype(std::declval<BasicProps const &>().GetTrackerCopy()), Tracker>);

  static_assert(noexcept(std::declval<BasicProps const &>().GetCount()));
  static_assert(!noexcept(std::declval<BasicProps const &>().GetName()));
  static_assert(!noexcept(std::declval<BasicProps const &>().GetLabel()));
  static_assert(noexcept(std::declval<BasicProps const &>().GetRefLabel()));
  static_assert(noexcept(std::declval<BasicProps const &>().GetViewLabel()));
  static_assert(!noexcept(std::declval<BasicProps const &>().GetTrackerCopy()));

  BasicProps basic;

  CHECK(basic.GetCount() == 7);
  basic.SetCount(9);
  CHECK(basic.GetCount() == 9);

  CHECK(basic.GetName() == "guest");
  basic.SetName("alice");
  CHECK(basic.GetName() == "alice");

  CHECK(basic.GetTitle().empty());
  basic.SetTitle("title");
  CHECK(basic.GetTitle() == "title");

  std::string title_source = "copy-title";
  basic.SetTitle(title_source);
  CHECK(basic.GetTitle() == "copy-title");
  CHECK(title_source == "copy-title");

  CHECK(basic.GetTitleCopy().empty());
  basic.SetTitleCopy("title-copy");
  CHECK(basic.GetTitleCopy() == "title-copy");

  CHECK(basic.GetLabel() == "label");
  basic.SetLabel("custom-label");
  CHECK(basic.GetLabel() == "custom-label");

  CHECK(basic.GetRefLabel() == "ref");
  basic.SetRefLabel("custom-ref");
  CHECK(basic.GetRefLabel() == "custom-ref");

  CHECK(basic.GetViewLabel() == "view");
  basic.SetViewLabel("custom-view");
  CHECK(basic.GetViewLabel() == "custom-view");

  CHECK(basic.GetNoDefaultLabel().empty());
  basic.SetNoDefaultLabel("no-default");
  CHECK(basic.GetNoDefaultLabel() == "no-default");

  CHECK(basic.GetTrackerCopy().value == 1);
  CHECK(basic.GetTrackerMove().value == 2);
  CHECK(basic.GetTrackerCopyExplicit().value == 3);

  Tracker copy_source{42};
  Tracker::copies = 0;
  basic.SetTrackerCopy(copy_source);
  CHECK(basic.GetTrackerCopy().value == 42);
  CHECK(copy_source.value == 42);
  CHECK(Tracker::copies > 0);

  Tracker::copies = 0;
  basic.SetTrackerCopyExplicit(copy_source);
  CHECK(basic.GetTrackerCopyExplicit().value == 42);
  CHECK(copy_source.value == 42);
  CHECK(Tracker::copies > 0);

  Tracker move_source{43};
  Tracker::moves = 0;
  basic.SetTrackerMove(std::move(move_source));
  CHECK(basic.GetTrackerMove().value == 43);
  CHECK(move_source.value == -1);
  CHECK(Tracker::moves > 0);

  GuardedProps guarded;
  guarded.SetAlways(2);
  guarded.SetDebugOnly(3);
  guarded.SetCommaExpr(4);
  guarded.SetThree(5);
  CHECK(guarded.GetAlways() == 2);
  CHECK(guarded.GetDebugOnly() == 3);
  CHECK(guarded.GetCommaExpr() == 4);
  CHECK(guarded.GetThree() == 5);

  ShortProps short_props;
  CHECK(short_props.GetCount() == 5);
  short_props.SetCount(6);
  CHECK(short_props.GetCount() == 6);

  CHECK(short_props.GetName() == "short");
  short_props.SetName("short-name");
  CHECK(short_props.GetName() == "short-name");

  CHECK(short_props.GetPlain().empty());
  short_props.SetPlain("plain");
  CHECK(short_props.GetPlain() == "plain");

  CHECK(short_props.GetLabel() == "short-label");
  short_props.SetLabel("short-label-2");
  CHECK(short_props.GetLabel() == "short-label-2");

  CHECK(short_props.GetNoDefault().empty());
  short_props.SetNoDefault("short-no-default");
  CHECK(short_props.GetNoDefault() == "short-no-default");

  short_props.SetGuarded(2);
  short_props.SetDebugOnly(2);
  short_props.SetCommaShort(2);
  CHECK(short_props.GetGuarded() == 2);
  CHECK(short_props.GetDebugOnly() == 2);
  CHECK(short_props.GetCommaShort() == 2);

  std::printf("checks=%d failures=%d\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
