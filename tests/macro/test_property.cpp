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

#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#if defined(QT_DEBUG) || defined(QT_NO_DEBUG) || defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#  include <QCoreApplication>
#  include <QMetaMethod>
#  include <QMetaObject>
#  include <QMetaProperty>
#  include <QObject>
#  include <QString>
#  include <QVariant>
#endif

#include "LinnProperty.h"

#define TEST_EMIT

namespace
{

int g_checks               = 0;
int g_failures             = 0;
int g_custom_handler_calls = 0;

#define CHECK(expr)                                               \
  do {                                                            \
    ++g_checks;                                                   \
    if (!(expr)) {                                                \
      ++g_failures;                                               \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    }                                                             \
  } while (false)

void custom_handler(char const *, char const *, int, char const *) noexcept
{
  ++g_custom_handler_calls;
}

struct Tracker
{
  inline static int copies = 0;
  inline static int moves  = 0;

  int value = 0;

  Tracker() = default;

  explicit Tracker(int v)
      : value(v)
  {
  }

  Tracker(Tracker const &other)
      : value(other.value)
  {
    ++copies;
  }

  Tracker(Tracker &&other) noexcept
      : value(other.value)
  {
    other.value = -1;
    ++moves;
  }

  Tracker &operator= (Tracker const &other)
  {
    value = other.value;
    ++copies;
    return *this;
  }

  Tracker &operator= (Tracker &&other) noexcept
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
  LINN_PROPERTY_CREATE(LPS_MEMBER(int, Count, 7), LPS_SETTER(int), LPS_GETTER(int))
  LINN_PROPERTY_CREATE(LPS_MEMBER(std::string, Name, "guest"), LPS_SETTER(std::string const &), LPS_GETTER(std::string))
  LINN_PROPERTY_CREATE(LPS_MEMBER(std::string, Title), LPS_SETTER(std::string &&), LPS_GETTER(std::string const &))
  LINN_PROPERTY_CREATE(LPS_MEMBER(std::string, Label, "label"), LPS_SETTER(std::string_view), LPS_GETTER(std::string_view))
  LINN_PROPERTY_CREATE(LPS_MEMBER(std::string, RefLabel, "ref"), LPS_SETTER(std::string const &), LPS_GETTER_REF_QUALIFIED_OVERLOAD)
  LINN_PROPERTY_CREATE(LPS_MEMBER(std::string, ValueRef, "value-ref"), LPS_SETTER(std::string), LPS_GETTER_REF_QUALIFIED_OVERLOAD)
  LINN_PROPERTY_CREATE(LPS_MEMBER(std::string, NoDefault), LPS_SETTER(std::string const &), LPS_GETTER(std::string))
  LINN_PROPERTY_CREATE(LPS_MEMBER(Tracker, TrackerCopy, Tracker { 1 }), LPS_SETTER(Tracker const &), LPS_GETTER(Tracker))
  LINN_PROPERTY_CREATE(LPS_MEMBER(Tracker, TrackerMove, Tracker { 2 }), LPS_SETTER(Tracker &&), LPS_GETTER(Tracker))
  LINN_PROPERTY_CREATE(LPS_MEMBER(Tracker, TrackerValue, Tracker { 3 }), LPS_SETTER(Tracker), LPS_GETTER(Tracker))
};

class GuardedProps
{
public:
  LINN_PROPERTY_CREATE(LPS_MEMBER(int, Always, 1), LPS_SETTER(int, LPS_PRE_CONTRACT(Always > 0)), LPS_GETTER(int))
  LINN_PROPERTY_CREATE(LPS_MEMBER(int, DebugOnly, 1), LPS_SETTER(int, LPS_PRE_CONTRACTD(DebugOnly > 0)), LPS_GETTER(int))
  LINN_PROPERTY_CREATE(LPS_MEMBER(int, Custom, 1), LPS_SETTER(int, LPS_PRE_CONTRACT(Custom > 0, custom_handler)), LPS_GETTER(int))
  LINN_PROPERTY_CREATE(
      LPS_MEMBER(int, Parenthesized, 1),
      LPS_SETTER(int, LPS_PRE_CONTRACT((std::is_same_v<int, int>) )),
      LPS_GETTER(int))
};

class PostProps
{
public:
  int update_calls   = 0;
  int plain_calls    = 0;
  int notified_value = -1;
  int observed_value = -1;

  void update() noexcept { ++update_calls; }

  void plain() { ++plain_calls; }

  void notify(int value) noexcept { notified_value = value; }

  void capture() noexcept { observed_value = getAfterAssign(); }

  LINN_PROPERTY_CREATE(LPS_MEMBER(int, PostOnly, 1), LPS_SETTER(int, LPS_POST_ACTION(this->update())), LPS_GETTER(int))
  LINN_PROPERTY_CREATE(LPS_MEMBER(int, ContractAndPost, 1), LPS_SETTER(int, LPS_PRE_CONTRACT(ContractAndPost > 0, custom_handler), LPS_POST_ACTION(this->update())), LPS_GETTER(int))
  LINN_PROPERTY_CREATE(LPS_MEMBER(int, MultiPost, 1), LPS_SETTER(int, LPS_POST_ACTION(this->update(), this->notify(MultiPost))), LPS_GETTER(int))
  LINN_PROPERTY_CREATE(LPS_MEMBER(int, SignalPost, 1), LPS_SETTER(int, LPS_POST_ACTION(TEST_EMIT this->notify(SignalPost))), LPS_GETTER(int))
  LINN_PROPERTY_CREATE(LPS_MEMBER(int, ThrowingPost, 1), LPS_SETTER(int, LPS_POST_ACTION(this->plain())), LPS_GETTER(int))
  LINN_PROPERTY_CREATE(LPS_MEMBER(int, DefaultContractPost, 1), LPS_SETTER(int, LPS_PRE_CONTRACT(DefaultContractPost > 0), LPS_POST_ACTION(this->update())), LPS_GETTER(int))
  LINN_PROPERTY_CREATE(LPS_MEMBER(int, AfterAssign, 1), LPS_SETTER(int, LPS_POST_ACTION(this->capture())), LPS_GETTER(int))
};

class SplitProps
{
public:
  int touches = 0;

  void touch() noexcept { ++touches; }

  LINN_PROPERTY_CREATE_H(LPS_MEMBER(int, Count), LPS_SETTER(int), LPS_GETTER(int))
  LINN_PROPERTY_CREATE_H(LPS_MEMBER(std::string, Name), LPS_SETTER(std::string const &), LPS_GETTER(std::string))
  LINN_PROPERTY_CREATE_H(LPS_MEMBER(std::string, RefName), LPS_SETTER(std::string const &), LPS_GETTER_REF_QUALIFIED_OVERLOAD)
  LINN_PROPERTY_CREATE_H(LPS_MEMBER(int, Guarded), LPS_SETTER(int), LPS_GETTER(int))
  LINN_PROPERTY_CREATE_H(LPS_MEMBER(int, Posted), LPS_SETTER(int), LPS_GETTER(int))

  LINN_PROPERTY_CREATE_D(int, Count, 7)
  LINN_PROPERTY_CREATE_D(std::string, Name, "guest")
  LINN_PROPERTY_CREATE_D(std::string, RefName, "ref")
  LINN_PROPERTY_CREATE_D(int, Guarded, 1)
  LINN_PROPERTY_CREATE_D(int, Posted, 1)
};

// MSVC evaluates `this->member()` in an in-class exception specification as
// potentially-throwing, but the out-of-class definition as noexcept. Split-mode
// post actions therefore use an unqualified member call.
LINN_PROPERTY_CREATE_CPP(SplitProps, LPS_MEMBER(int, Count), LPS_SETTER(int), LPS_GETTER(int))
LINN_PROPERTY_CREATE_CPP(SplitProps, LPS_MEMBER(std::string, Name), LPS_SETTER(std::string const &), LPS_GETTER(std::string))
LINN_PROPERTY_CREATE_CPP(SplitProps, LPS_MEMBER(std::string, RefName), LPS_SETTER(std::string const &), LPS_GETTER_REF_QUALIFIED_OVERLOAD)
LINN_PROPERTY_CREATE_CPP(SplitProps, LPS_MEMBER(int, Guarded), LPS_SETTER(int, LPS_PRE_CONTRACT(Guarded > 0, custom_handler)), LPS_GETTER(int))
LINN_PROPERTY_CREATE_CPP(SplitProps, LPS_MEMBER(int, Posted), LPS_SETTER(int, LPS_POST_ACTION(touch())), LPS_GETTER(int))

#if defined(QT_DEBUG) || defined(QT_NO_DEBUG) || defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
namespace qt_test
{

class QtInlineProps : public QObject
{
  Q_OBJECT

public:
  using QObject::QObject;
  Q_PROPERTY_CREATE(LPS_MEMBER(int, Count, 7), LPS_SETTER(int, LPS_POST_ACTION(Q_EMIT this->signalCount())), LPS_GETTER(int))
  Q_PROPERTY_CREATE(LPS_MEMBER(QString, Name, QString("guest")), LPS_SETTER(QString const &, LPS_POST_ACTION(Q_EMIT this->signalName())), LPS_GETTER(QString))
};

class QtSplitProps : public QObject
{
  Q_OBJECT

public:
  using QObject::QObject;
  Q_PROPERTY_CREATE_H(LPS_MEMBER(int, Count), LPS_SETTER(int), LPS_GETTER(int))
  Q_PROPERTY_CREATE_H(LPS_MEMBER(QString, Name), LPS_SETTER(QString const &), LPS_GETTER(QString))

  LINN_PROPERTY_CREATE_D(int, Count, 7)
  LINN_PROPERTY_CREATE_D(QString, Name, QString("guest"))
};

Q_PROPERTY_CREATE_CPP(QtSplitProps, LPS_MEMBER(int, Count), LPS_SETTER(int, LPS_POST_ACTION(Q_EMIT this->signalCount())), LPS_GETTER(int))
Q_PROPERTY_CREATE_CPP(QtSplitProps, LPS_MEMBER(QString, Name), LPS_SETTER(QString const &, LPS_POST_ACTION(Q_EMIT this->signalName())), LPS_GETTER(QString))

} // namespace qt_test

/// Verifies the meta-object metadata that moc generated for a property created
/// with LPS_QT: registered type, READ/WRITE capability, and the NOTIFY signal.
/// Signal is the generated notify member, so the direct-connection overload can
/// be used instead of a string-based SIGNAL() lookup.
template <class Object, class Signal>
void check_count_meta(Object *object, QMetaObject const *meta, Signal signal)
{
  int const index = meta->indexOfProperty("Count");
  CHECK(index >= 0);
  if (index < 0) {
    return;
  }

  QMetaProperty property = meta->property(index);
  CHECK(property.isValid());
  CHECK(property.typeName() == QByteArray("int"));
  CHECK(property.hasNotifySignal());
  CHECK(property.notifySignal().name() == QByteArray("signalCount"));
  CHECK(property.isReadable());
  CHECK(property.isWritable());

  int emissions = 0;
  auto const connection =
      QObject::connect(object, signal, object, [&emissions] { ++emissions; }, Qt::DirectConnection);

  CHECK(object->setProperty("Count", 42));
  CHECK(object->property("Count").toInt() == 42);
  CHECK(emissions == 1);

  CHECK(property.write(object, QVariant(7)));
  CHECK(object->property("Count").toInt() == 7);
  CHECK(emissions == 2);

  QObject::disconnect(connection);
}

template <class Object, class Signal>
void check_name_meta(Object *object, QMetaObject const *meta, Signal signal)
{
  int const index = meta->indexOfProperty("Name");
  CHECK(index >= 0);
  if (index < 0) {
    return;
  }

  QMetaProperty property = meta->property(index);
  CHECK(property.isValid());
  CHECK(property.typeName() == QByteArray("QString"));
  CHECK(property.hasNotifySignal());
  CHECK(property.notifySignal().name() == QByteArray("signalName"));
  CHECK(property.isReadable());
  CHECK(property.isWritable());

  int emissions = 0;
  auto const connection =
      QObject::connect(object, signal, object, [&emissions] { ++emissions; }, Qt::DirectConnection);

  CHECK(object->setProperty("Name", QStringLiteral("alice")));
  CHECK(object->property("Name").toString() == QLatin1String("alice"));
  CHECK(emissions == 1);

  CHECK(property.write(object, QVariant(QStringLiteral("bob"))));
  CHECK(object->property("Name").toString() == QLatin1String("bob"));
  CHECK(emissions == 2);

  QObject::disconnect(connection);
}
#endif
} // namespace

int main(int argc, char *argv[])
{
#if !(defined(QT_DEBUG) || defined(QT_NO_DEBUG) || defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT))
  (void) argc;
  (void) argv;
#endif
  static_assert(std::is_same_v<decltype(std::declval<BasicProps const &>().getCount()), int>);
  static_assert(std::is_same_v<decltype(std::declval<BasicProps const &>().getName()), std::string>);
  static_assert(std::is_same_v<decltype(std::declval<BasicProps const &>().getTitle()), std::string const &>);
  static_assert(std::is_same_v<decltype(std::declval<BasicProps const &>().getLabel()), std::string_view>);
  static_assert(std::is_same_v<decltype(std::declval<BasicProps const &>().getRefLabel()), std::string const &>);
  static_assert(std::is_same_v<decltype(std::declval<BasicProps &&>().getRefLabel()), std::string>);
  static_assert(std::is_same_v<decltype(std::declval<BasicProps const &>().getValueRef()), std::string const &>);
  static_assert(std::is_same_v<decltype(std::declval<BasicProps &&>().getValueRef()), std::string>);
  static_assert(std::is_same_v<decltype(std::declval<BasicProps const &>().getTrackerCopy()), Tracker>);

  static_assert(noexcept(std::declval<BasicProps const &>().getCount()));
  static_assert(!noexcept(std::declval<BasicProps const &>().getName()));
  static_assert(noexcept(std::declval<BasicProps const &>().getTitle()));
  static_assert(noexcept(std::declval<BasicProps const &>().getLabel()));
  static_assert(noexcept(std::declval<BasicProps const &>().getRefLabel()));
  static_assert(noexcept(std::declval<BasicProps &&>().getRefLabel()));
  static_assert(!noexcept(std::declval<BasicProps const &>().getTrackerCopy()));

  static_assert(noexcept(std::declval<BasicProps &>().setCount(0)));
  static_assert(noexcept(std::declval<BasicProps &>().setValueRef(std::declval<std::string>())));
  static_assert(!noexcept(std::declval<BasicProps &>().setName(std::declval<std::string const &>())));
  static_assert(noexcept(std::declval<BasicProps &>().setTrackerMove(std::declval<Tracker &&>())));
  static_assert(noexcept(std::declval<BasicProps &>().setTrackerValue(std::declval<Tracker>())));
  static_assert(!noexcept(std::declval<BasicProps &>().setTrackerCopy(std::declval<Tracker const &>())));
  static_assert(!noexcept(std::declval<GuardedProps &>().setAlways(0)));
#if defined(DEBUG) || defined(_DEBUG)
  static_assert(!noexcept(std::declval<GuardedProps &>().setDebugOnly(0)));
#else
  static_assert(noexcept(std::declval<GuardedProps &>().setDebugOnly(0)));
#endif
  static_assert(noexcept(std::declval<GuardedProps &>().setCustom(0)));

  static_assert(noexcept(std::declval<PostProps &>().setPostOnly(0)));
  static_assert(noexcept(std::declval<PostProps &>().setContractAndPost(0)));
  static_assert(noexcept(std::declval<PostProps &>().setMultiPost(0)));
  static_assert(noexcept(std::declval<PostProps &>().setSignalPost(0)));
  static_assert(noexcept(std::declval<PostProps &>().setAfterAssign(0)));
  static_assert(!noexcept(std::declval<PostProps &>().setThrowingPost(0)));
  static_assert(!noexcept(std::declval<PostProps &>().setDefaultContractPost(0)));

  static_assert(std::is_same_v<decltype(std::declval<SplitProps const &>().getCount()), int>);
  static_assert(!noexcept(std::declval<SplitProps &>().setName(std::declval<std::string const &>())));
  static_assert(!noexcept(std::declval<SplitProps &>().setCount(0)));
  static_assert(!noexcept(std::declval<SplitProps &>().setGuarded(0)));
  static_assert(!noexcept(std::declval<SplitProps &>().setPosted(0)));
  static_assert(std::is_same_v<decltype(std::declval<SplitProps const &>().getRefName()), std::string const &>);
  static_assert(std::is_same_v<decltype(std::declval<SplitProps &&>().getRefName()), std::string>);

  BasicProps basic;

  CHECK(basic.getCount() == 7);
  basic.setCount(9);
  CHECK(basic.getCount() == 9);

  CHECK(basic.getName() == "guest");
  basic.setName("alice");
  CHECK(basic.getName() == "alice");

  CHECK(basic.getTitle().empty());
  std::string title_source = "title";
  basic.setTitle(std::move(title_source));
  CHECK(basic.getTitle() == "title");

  CHECK(basic.getLabel() == "label");
  basic.setLabel("custom-label");
  CHECK(basic.getLabel() == "custom-label");

  CHECK(basic.getRefLabel() == "ref");
  basic.setRefLabel("custom-ref");
  CHECK(basic.getRefLabel() == "custom-ref");

  CHECK(basic.getValueRef() == "value-ref");
  basic.setValueRef("value-ref-2");
  CHECK(basic.getValueRef() == "value-ref-2");

  BasicProps moved_from;
  std::string ref_label = std::move(moved_from).getRefLabel();
  CHECK(ref_label == "ref");

  CHECK(basic.getNoDefault().empty());
  basic.setNoDefault("no-default");
  CHECK(basic.getNoDefault() == "no-default");

  CHECK(basic.getTrackerCopy().value == 1);
  CHECK(basic.getTrackerMove().value == 2);
  CHECK(basic.getTrackerValue().value == 3);

  Tracker copy_source { 42 };
  Tracker::copies = 0;
  basic.setTrackerCopy(copy_source);
  CHECK(basic.getTrackerCopy().value == 42);
  CHECK(copy_source.value == 42);
  CHECK(Tracker::copies > 0);

  Tracker move_source { 43 };
  Tracker::moves = 0;
  basic.setTrackerMove(std::move(move_source));
  CHECK(basic.getTrackerMove().value == 43);
  CHECK(move_source.value == -1);
  CHECK(Tracker::moves > 0);

  Tracker value_source { 44 };
  Tracker::copies = 0;
  Tracker::moves  = 0;
  basic.setTrackerValue(std::move(value_source));
  CHECK(basic.getTrackerValue().value == 44);
  CHECK(value_source.value == -1);
  CHECK(Tracker::moves > 0);

  Tracker value_copy_source { 45 };
  Tracker::copies = 0;
  Tracker::moves  = 0;
  basic.setTrackerValue(value_copy_source);
  CHECK(basic.getTrackerValue().value == 45);
  CHECK(value_copy_source.value == 45);
  CHECK(Tracker::copies > 0);
  CHECK(Tracker::moves > 0);

  GuardedProps guarded;
  guarded.setAlways(2);
  guarded.setDebugOnly(3);
  guarded.setParenthesized(4);
  CHECK(guarded.getAlways() == 2);
  CHECK(guarded.getDebugOnly() == 3);
  CHECK(guarded.getParenthesized() == 4);

  guarded.setCustom(-1);
  CHECK(guarded.getCustom() == -1);
  CHECK(g_custom_handler_calls == 1);

#if !defined(DEBUG) && !defined(_DEBUG)
  guarded.setDebugOnly(-1);
  CHECK(guarded.getDebugOnly() == -1);
#endif

  PostProps post;
  post.setPostOnly(2);
  CHECK(post.getPostOnly() == 2);
  CHECK(post.update_calls == 1);

  post.setContractAndPost(3);
  CHECK(post.getContractAndPost() == 3);
  CHECK(post.update_calls == 2);

  post.setMultiPost(6);
  CHECK(post.getMultiPost() == 6);
  CHECK(post.update_calls == 3);
  CHECK(post.notified_value == 6);

  post.setSignalPost(10);
  CHECK(post.getSignalPost() == 10);
  CHECK(post.notified_value == 10);

  post.setThrowingPost(7);
  CHECK(post.getThrowingPost() == 7);
  CHECK(post.plain_calls == 1);
  CHECK(post.update_calls == 3);

  post.setDefaultContractPost(8);
  CHECK(post.getDefaultContractPost() == 8);
  CHECK(post.update_calls == 4);

  post.setAfterAssign(9);
  CHECK(post.getAfterAssign() == 9);
  CHECK(post.observed_value == 9);

  SplitProps split;

  CHECK(split.getCount() == 7);
  split.setCount(11);
  CHECK(split.getCount() == 11);

  CHECK(split.getName() == "guest");
  split.setName("bob");
  CHECK(split.getName() == "bob");

  CHECK(split.getRefName() == "ref");
  split.setRefName("ref-2");
  CHECK(split.getRefName() == "ref-2");

  SplitProps split_moved;
  std::string moved_ref = std::move(split_moved).getRefName();
  CHECK(moved_ref == "ref");

  split.setGuarded(5);
  CHECK(split.getGuarded() == 5);

  split.setPosted(6);
  CHECK(split.getPosted() == 6);
  CHECK(split.touches == 1);

#if defined(QT_DEBUG) || defined(QT_NO_DEBUG) || defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)

  QCoreApplication app(argc, argv);

  {
    qt_test::QtInlineProps object;
    CHECK(object.getCount() == 7);
    CHECK(object.getName() == QLatin1String("guest"));
    check_count_meta(&object, &qt_test::QtInlineProps::staticMetaObject, &qt_test::QtInlineProps::signalCount);
    check_name_meta(&object, &qt_test::QtInlineProps::staticMetaObject, &qt_test::QtInlineProps::signalName);
    object.setCount(9);
    CHECK(object.getCount() == 9);
    object.setName(QStringLiteral("carol"));
    CHECK(object.getName() == QLatin1String("carol"));
  }

  {
    qt_test::QtSplitProps object;
    CHECK(object.getCount() == 7);
    CHECK(object.getName() == QLatin1String("guest"));
    check_count_meta(&object, &qt_test::QtSplitProps::staticMetaObject, &qt_test::QtSplitProps::signalCount);
    check_name_meta(&object, &qt_test::QtSplitProps::staticMetaObject, &qt_test::QtSplitProps::signalName);
    object.setCount(11);
    CHECK(object.getCount() == 11);
    object.setName(QStringLiteral("dave"));
    CHECK(object.getName() == QLatin1String("dave"));
  }
#endif

  std::printf("checks=%d failures=%d\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}

#if defined(QT_DEBUG) || defined(QT_NO_DEBUG) || defined(QT_CORE_LIB) || defined(QT_WIDGETS_LIB) || defined(Q_OBJECT)
#  include "test_property.moc"
#endif
