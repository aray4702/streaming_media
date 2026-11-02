// The gtest/gmock subset that libwebrtc M157's net/dcsctp/ tests use, on
// sm_test.h (#1032, #1060). Only for tests/unit/dcsctp/.
//
// Matchers (#1032): SM_EXPECT_THAT / SM_ASSERT_THAT with ElementsAre,
// SizeIs, IsEmpty and Not, and a plain value, which matches by ==, as
// gmock's implicit Eq does. #1060 adds the matchers the socket, rx, tx and
// timer tests use (_, Eq, Ge, Le, AllOf, Pair, Optional, Property, Field,
// UnorderedElementsAre, ElementsAreArray, HasSubstr, Contains, MATCHER_P,
// MATCHER_P3, ExplainMatchResult), fixtures (testing::Test, SM_TEST_F),
// value-parameterized tests (testing::TestWithParam, SM_TEST_P,
// INSTANTIATE_TEST_SUITE_P, Values), and mocks:
// - MOCK_METHOD is expanded by the port into a FunctionMocker member
//   `gmock_<Name>` and the overriding method that calls it.
// - EXPECT_CALL(obj, Method(matchers...)) and EXPECT_CALL(obj, Method), with
//   Times(n), WillOnce(action) and WillRepeatedly(action); ON_CALL(...)
//   .WillByDefault(action). Actions: Return(v), Return(), ReturnRef(v), or a
//   callable taking the call's arguments or none.
// - gmock's rules: the newest matching expectation takes the call; without
//   Times, the cardinality is 1, or the number of WillOnce (at least that
//   many when WillRepeatedly is set, any number when only WillRepeatedly
//   is); calls beyond it, calls no expectation matches, and expectations
//   called too few times when the mock is destroyed are failures. A call to
//   a method with no expectations is allowed, except on a StrictMock.
//   NiceMock is the default behaviour without the warning.
// - InSequence: expectations made while one lives must be satisfied in
//   order; matching one retires the ones before it.
// - MockFunction<R(Args...)> with Call and AsStdFunction.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "sm_test.h"

namespace testing {

namespace internal {

template <typename T, typename M>
bool MatchValue(const T& value, const M& matcher);

}  // namespace internal

// The listener a MATCHER_P body writes its explanation to; discarded here.
class MatchResultListener {
 public:
  template <typename T>
  MatchResultListener& operator<<(const T&) {
    return *this;
  }
};

template <typename M, typename T>
bool ExplainMatchResult(const M& matcher, const T& value, MatchResultListener*) {
  return internal::MatchValue(value, matcher);
}

struct AnythingMatcher {
  template <typename T>
  bool Matches(const T&) const {
    return true;
  }
};
inline const AnythingMatcher _ = {};

template <typename... Ts>
struct ElementsAreMatcher {
  std::tuple<Ts...> expected;

  template <typename C>
  bool Matches(const C& c) const {
    if (static_cast<size_t>(std::distance(std::begin(c), std::end(c))) != sizeof...(Ts)) return false;
    auto it = std::begin(c);
    bool ok = true;
    std::apply([&](const auto&... e) { ((ok = ok && internal::MatchValue(*it, e), ++it), ...); }, expected);
    return ok;
  }
};

template <typename... Ts>
ElementsAreMatcher<std::decay_t<Ts>...> ElementsAre(Ts&&... ts) {
  return {std::tuple<std::decay_t<Ts>...>(std::forward<Ts>(ts)...)};
}

template <typename E>
struct ElementsAreArrayMatcher {
  std::vector<E> expected;
  template <typename C>
  bool Matches(const C& c) const {
    if (static_cast<size_t>(std::distance(std::begin(c), std::end(c))) != expected.size()) return false;
    size_t i = 0;
    for (const auto& v : c) {
      if (!internal::MatchValue(v, expected[i++])) return false;
    }
    return true;
  }
};

template <typename C>
auto ElementsAreArray(const C& c) {
  using E = std::decay_t<decltype(*std::begin(c))>;
  return ElementsAreArrayMatcher<E>{std::vector<E>(std::begin(c), std::end(c))};
}
template <typename E>
auto ElementsAreArray(std::initializer_list<E> c) {
  return ElementsAreArrayMatcher<E>{std::vector<E>(c.begin(), c.end())};
}

template <typename... Ts>
struct UnorderedElementsAreMatcher {
  std::tuple<Ts...> expected;

  template <typename C>
  bool Matches(const C& c) const {
    const size_t n = static_cast<size_t>(std::distance(std::begin(c), std::end(c)));
    if (n != sizeof...(Ts)) return false;
    // Small sizes only: try every assignment of elements to matchers.
    std::vector<const std::decay_t<decltype(*std::begin(c))>*> elems;
    for (const auto& v : c) elems.push_back(&v);
    std::vector<size_t> perm(n);
    for (size_t i = 0; i < n; ++i) perm[i] = i;
    do {
      size_t i = 0;
      bool ok = true;
      std::apply([&](const auto&... e) { ((ok = ok && internal::MatchValue(*elems[perm[i]], e), ++i), ...); },
                 expected);
      if (ok) return true;
    } while (std::next_permutation(perm.begin(), perm.end()));
    return false;
  }
};

template <typename... Ts>
UnorderedElementsAreMatcher<std::decay_t<Ts>...> UnorderedElementsAre(Ts&&... ts) {
  return {std::tuple<std::decay_t<Ts>...>(std::forward<Ts>(ts)...)};
}

template <typename M>
struct SizeIsMatcher {
  M n;
  template <typename C>
  bool Matches(const C& c) const {
    return internal::MatchValue(static_cast<size_t>(std::distance(std::begin(c), std::end(c))), n);
  }
};

template <typename N>
auto SizeIs(N n) {
  if constexpr (std::is_integral_v<N>) {
    return SizeIsMatcher<size_t>{static_cast<size_t>(n)};
  } else {
    return SizeIsMatcher<N>{n};
  }
}

struct IsEmptyMatcher {
  template <typename C>
  bool Matches(const C& c) const {
    return std::begin(c) == std::end(c);
  }
};

inline IsEmptyMatcher IsEmpty() { return {}; }

template <typename M>
struct NotMatcher {
  M inner;
  template <typename T>
  bool Matches(const T& v) const {
    return !internal::MatchValue(v, inner);
  }
};

template <typename M>
NotMatcher<M> Not(M m) {
  return {std::move(m)};
}

#define SM_GMOCK_CMP_MATCHER(Name, op)               \
  template <typename V>                              \
  struct Name##Matcher {                             \
    V v;                                             \
    template <typename T>                            \
    bool Matches(const T& a) const {                 \
      return a op v;                                 \
    }                                                \
  };                                                 \
  template <typename V>                              \
  Name##Matcher<std::decay_t<V>> Name(V&& v) {       \
    return {std::forward<V>(v)};                     \
  }
SM_GMOCK_CMP_MATCHER(Eq, ==)
SM_GMOCK_CMP_MATCHER(Ne, !=)
SM_GMOCK_CMP_MATCHER(Ge, >=)
SM_GMOCK_CMP_MATCHER(Gt, >)
SM_GMOCK_CMP_MATCHER(Le, <=)
SM_GMOCK_CMP_MATCHER(Lt, <)
#undef SM_GMOCK_CMP_MATCHER

template <typename... Ms>
struct AllOfMatcher {
  std::tuple<Ms...> ms;
  template <typename T>
  bool Matches(const T& v) const {
    return std::apply([&](const auto&... m) { return (internal::MatchValue(v, m) && ...); }, ms);
  }
};
template <typename... Ms>
AllOfMatcher<std::decay_t<Ms>...> AllOf(Ms&&... ms) {
  return {std::tuple<std::decay_t<Ms>...>(std::forward<Ms>(ms)...)};
}

template <typename M1, typename M2>
struct PairMatcher {
  M1 first;
  M2 second;
  template <typename T>
  bool Matches(const T& v) const {
    return internal::MatchValue(v.first, first) && internal::MatchValue(v.second, second);
  }
};
template <typename M1, typename M2>
PairMatcher<std::decay_t<M1>, std::decay_t<M2>> Pair(M1&& a, M2&& b) {
  return {std::forward<M1>(a), std::forward<M2>(b)};
}

template <typename M>
struct OptionalMatcher {
  M m;
  template <typename T>
  bool Matches(const T& v) const {
    return v.has_value() && internal::MatchValue(*v, m);
  }
};
template <typename M>
OptionalMatcher<std::decay_t<M>> Optional(M&& m) {
  return {std::forward<M>(m)};
}

template <typename F, typename M>
struct ResultOfMatcher {
  F f;
  M m;
  template <typename T>
  bool Matches(const T& v) const {
    return internal::MatchValue(f(v), m);
  }
};

template <typename C, typename P, typename M>
auto Property(P (C::*pm)() const, M m) {
  auto f = [pm](const C& c) -> decltype(auto) { return (c.*pm)(); };
  return ResultOfMatcher<decltype(f), M>{f, std::move(m)};
}
template <typename C, typename P, typename M>
auto Property(P (C::*pm)() const&, M m) {
  auto f = [pm](const C& c) -> decltype(auto) { return (c.*pm)(); };
  return ResultOfMatcher<decltype(f), M>{f, std::move(m)};
}
template <typename C, typename P, typename M>
auto Property(std::string_view /* name */, P (C::*pm)() const, M m) {
  return Property(pm, std::move(m));
}
template <typename C, typename P, typename M>
auto Field(P C::*pf, M m) {
  auto f = [pf](const C& c) -> const P& { return c.*pf; };
  return ResultOfMatcher<decltype(f), M>{f, std::move(m)};
}
template <typename C, typename P, typename M>
auto Field(std::string_view /* name */, P C::*pf, M m) {
  return Field(pf, std::move(m));
}

struct HasSubstrMatcher {
  std::string s;
  template <typename T>
  bool Matches(const T& v) const {
    return std::string_view(v).find(s) != std::string_view::npos;
  }
};
inline HasSubstrMatcher HasSubstr(std::string_view s) { return {std::string(s)}; }

template <typename M>
struct ContainsMatcher {
  M m;
  template <typename C>
  bool Matches(const C& c) const {
    for (const auto& v : c) {
      if (internal::MatchValue(v, m)) return true;
    }
    return false;
  }
};
template <typename M>
ContainsMatcher<std::decay_t<M>> Contains(M&& m) {
  return {std::forward<M>(m)};
}

namespace internal {

template <typename T, typename M>
bool MatchValue(const T& value, const M& matcher) {
  if constexpr (requires { matcher.Matches(value); }) {
    return matcher.Matches(value);
  } else {
    return value == matcher;
  }
}

}  // namespace internal

// ---------------------------------------------------------------------------
// Fixtures and parameterized tests.

class Test {
 public:
  virtual ~Test() = default;
  void Run() {
    SetUp();
    if (!::sm::test::CurrentFailed()) TestBody();
    TearDown();
  }

 protected:
  virtual void SetUp() {}
  virtual void TearDown() {}
  virtual void TestBody() = 0;
};

namespace internal {
template <typename T>
const T*& CurrentParam() {
  static const T* p = nullptr;
  return p;
}
template <typename Fixture>
std::vector<typename Fixture::ParamType>& ParamValues() {
  static std::vector<typename Fixture::ParamType> v;
  return v;
}
template <typename T>
void RunParamTest() {
  using P = typename T::ParamType;
  for (const P& p : ParamValues<typename T::FixtureType>()) {
    CurrentParam<P>() = &p;
    T t;
    t.Run();
  }
  CurrentParam<P>() = nullptr;
}
}  // namespace internal

template <typename T>
class WithParamInterface {
 public:
  using ParamType = T;
  virtual ~WithParamInterface() = default;
  static const T& GetParam() { return *internal::CurrentParam<T>(); }
};

template <typename T>
class TestWithParam : public Test, public WithParamInterface<T> {};

template <typename... Ts>
struct ValuesHolder {
  std::tuple<Ts...> values;
  template <typename T>
  std::vector<T> As() const {
    return std::apply([](const auto&... v) { return std::vector<T>{static_cast<T>(v)...}; }, values);
  }
};
template <typename... Ts>
ValuesHolder<std::decay_t<Ts>...> Values(Ts&&... ts) {
  return {std::tuple<std::decay_t<Ts>...>(std::forward<Ts>(ts)...)};
}

// ---------------------------------------------------------------------------
// Mocks.

namespace internal {

enum class Strictness { kNaggy, kNice, kStrict };

class MockerBase;
inline std::vector<MockerBase*>& LiveMockers() {
  static std::vector<MockerBase*> v;
  return v;
}

class ExpectationBase {
 public:
  virtual ~ExpectationBase() = default;
  const char* file = "";
  int line = 0;
  int calls = 0;
  int times = -1;  // explicit Times(n); -1 when not set.
  int once_total = 0;
  bool has_repeated = false;
  bool retired = false;
  ExpectationBase* prerequisite = nullptr;

  int MinCalls() const {
    if (times >= 0) return times;
    if (once_total > 0) return once_total;
    return has_repeated ? 0 : 1;
  }
  int MaxCalls() const {
    if (times >= 0) return times;
    if (has_repeated) return 1 << 30;
    return once_total > 0 ? once_total : 1;
  }
  bool Satisfied() const { return calls >= MinCalls(); }
  bool PrerequisitesSatisfied() const {
    for (const ExpectationBase* p = prerequisite; p != nullptr; p = p->prerequisite) {
      if (!p->retired && !p->Satisfied()) return false;
    }
    return true;
  }
  void RetirePrerequisites() {
    for (ExpectationBase* p = prerequisite; p != nullptr; p = p->prerequisite) p->retired = true;
  }
};

struct SequenceState {
  ExpectationBase* last = nullptr;
};
inline SequenceState*& CurrentSequence() {
  static SequenceState* s = nullptr;
  return s;
}

class MockerBase {
 public:
  explicit MockerBase(const char* name) : name_(name) { LiveMockers().push_back(this); }
  virtual ~MockerBase() {
    auto& v = LiveMockers();
    v.erase(std::remove(v.begin(), v.end(), this), v.end());
  }
  MockerBase(const MockerBase&) = delete;
  MockerBase& operator=(const MockerBase&) = delete;
  Strictness strictness = Strictness::kNaggy;

 protected:
  const char* name_;
};

template <typename V>
struct ReturnAction {
  V value;
};
struct ReturnVoidAction {};
template <typename T>
struct ReturnRefAction {
  T* ref;
};

}  // namespace internal

template <typename V>
internal::ReturnAction<std::decay_t<V>> Return(V&& v) {
  return {std::forward<V>(v)};
}
inline internal::ReturnVoidAction Return() { return {}; }
template <typename T>
internal::ReturnRefAction<T> ReturnRef(T& v) {
  return {&v};
}

namespace internal {

template <typename R, typename... A, typename F>
std::function<R(A...)> MakeAction(F f) {
  if constexpr (requires { f.value; } && !requires { f.ref; }) {
    if constexpr (std::is_void_v<R>) {
      return [](A...) {};
    } else {
      auto v = std::make_shared<decltype(f.value)>(std::move(f.value));
      return [v](A...) -> R { return static_cast<R>(*v); };
    }
  } else if constexpr (std::is_same_v<F, ReturnVoidAction>) {
    return [](A...) {};
  } else if constexpr (requires { f.ref; }) {
    auto* r = f.ref;
    return [r](A...) -> R { return *r; };
  } else if constexpr (std::is_invocable_v<F&, A...>) {
    auto sp = std::make_shared<F>(std::move(f));
    return [sp](A... a) -> R { return static_cast<R>((*sp)(std::forward<A>(a)...)); };
  } else {
    static_assert(std::is_invocable_v<F&>, "action must take the call's arguments or none");
    auto sp = std::make_shared<F>(std::move(f));
    return [sp](A...) -> R { return static_cast<R>((*sp)()); };
  }
}

template <typename Sig>
class FunctionMocker;

template <typename R, typename... A>
class FunctionMocker<R(A...)> : public MockerBase {
 public:
  using ArgMatcher = std::function<bool(const std::remove_reference_t<A>&...)>;

  class Expectation : public ExpectationBase {
   public:
    Expectation& Times(int n) {
      times = n;
      return *this;
    }
    template <typename F>
    Expectation& WillOnce(F f) {
      once.push_back(MakeAction<R, A...>(std::move(f)));
      ++once_total;
      return *this;
    }
    template <typename F>
    Expectation& WillRepeatedly(F f) {
      repeated = MakeAction<R, A...>(std::move(f));
      has_repeated = true;
      return *this;
    }
    ArgMatcher matcher;
    std::deque<std::function<R(A...)>> once;
    std::function<R(A...)> repeated;
  };

  class DefaultSpec {
   public:
    template <typename F>
    DefaultSpec& WillByDefault(F f) {
      action = MakeAction<R, A...>(std::move(f));
      return *this;
    }
    ArgMatcher matcher;
    std::function<R(A...)> action;
  };

  struct CallSpec {
    FunctionMocker* mocker;
    ArgMatcher matcher;
  };

  explicit FunctionMocker(const char* name) : MockerBase(name) {}
  ~FunctionMocker() override {
    for (const auto& e : expectations_) {
      if (e->calls < e->MinCalls()) {
        ::sm::test::ReportFailure(e->file, e->line,
                                  std::string("mock ") + name_ + ": expected at least " +
                                      std::to_string(e->MinCalls()) + " calls, got " + std::to_string(e->calls));
      }
    }
  }

  // EXPECT_CALL(obj, Method(matchers...)) builds a CallSpec through this.
  template <typename... Ms>
    requires(sizeof...(Ms) == sizeof...(A))
  CallSpec operator()(Ms... ms) {
    auto tuple = std::make_shared<std::tuple<Ms...>>(std::move(ms)...);
    return CallSpec{this, [tuple](const std::remove_reference_t<A>&... args) {
                      return std::apply(
                          [&](const auto&... m) { return (MatchValue(args, m) && ...); }, *tuple);
                    }};
  }

  Expectation& AddExpectation(ArgMatcher matcher, const char* file, int line) {
    auto e = std::make_unique<Expectation>();
    e->matcher = matcher ? std::move(matcher) : ArgMatcher([](const std::remove_reference_t<A>&...) { return true; });
    e->file = file;
    e->line = line;
    if (SequenceState* s = CurrentSequence()) {
      e->prerequisite = s->last;
      s->last = e.get();
    }
    expectations_.push_back(std::move(e));
    return *expectations_.back();
  }
  DefaultSpec& AddDefault(ArgMatcher matcher) {
    auto d = std::make_unique<DefaultSpec>();
    d->matcher = matcher ? std::move(matcher) : ArgMatcher([](const std::remove_reference_t<A>&...) { return true; });
    defaults_.push_back(std::move(d));
    return *defaults_.back();
  }

  R Invoke(A... args) {
    Expectation* chosen = nullptr;
    for (auto it = expectations_.rbegin(); it != expectations_.rend(); ++it) {
      Expectation* e = it->get();
      if (e->retired || !e->matcher(args...) || !e->PrerequisitesSatisfied()) continue;
      chosen = e;
      break;
    }
    if (chosen == nullptr) {
      if (!expectations_.empty()) {
        ::sm::test::ReportFailure(__FILE__, __LINE__, std::string("unexpected call to mock ") + name_);
      } else if (strictness == Strictness::kStrict) {
        ::sm::test::ReportFailure(__FILE__, __LINE__, std::string("uninteresting call to strict mock ") + name_);
      }
      return Default(std::forward<A>(args)...);
    }
    ++chosen->calls;
    chosen->RetirePrerequisites();
    if (chosen->calls > chosen->MaxCalls()) {
      ::sm::test::ReportFailure(chosen->file, chosen->line,
                                std::string("mock ") + name_ + ": called more than " +
                                    std::to_string(chosen->MaxCalls()) + " times");
      return Default(std::forward<A>(args)...);
    }
    if (!chosen->once.empty()) {
      auto action = std::move(chosen->once.front());
      chosen->once.pop_front();
      return action(std::forward<A>(args)...);
    }
    if (chosen->repeated) return chosen->repeated(std::forward<A>(args)...);
    return Default(std::forward<A>(args)...);
  }

 private:
  R Default(A... args) {
    for (auto it = defaults_.rbegin(); it != defaults_.rend(); ++it) {
      if ((*it)->action && (*it)->matcher(args...)) return (*it)->action(std::forward<A>(args)...);
    }
    if constexpr (std::is_void_v<R>) {
      return;
    } else if constexpr (std::is_reference_v<R>) {
      ::sm::test::ReportFailure(__FILE__, __LINE__, std::string("mock ") + name_ + " returns a reference, no action");
      std::abort();
    } else {
      return R();
    }
  }

  std::vector<std::unique_ptr<Expectation>> expectations_;
  std::vector<std::unique_ptr<DefaultSpec>> defaults_;
};

template <typename R, typename... A>
typename FunctionMocker<R(A...)>::Expectation& MakeExpectation(FunctionMocker<R(A...)>& m, const char* file,
                                                                int line) {
  return m.AddExpectation(nullptr, file, line);
}
template <typename Spec>
auto& MakeExpectation(Spec spec, const char* file, int line) {
  return spec.mocker->AddExpectation(std::move(spec.matcher), file, line);
}
template <typename R, typename... A>
typename FunctionMocker<R(A...)>::DefaultSpec& MakeDefault(FunctionMocker<R(A...)>& m) {
  return m.AddDefault(nullptr);
}
template <typename Spec>
auto& MakeDefault(Spec spec) {
  return spec.mocker->AddDefault(std::move(spec.matcher));
}

// Marks the mockers a NiceMock/StrictMock's base constructs (base classes
// construct in order, so this runs before T's members).
class StrictnessMarker {
 public:
  StrictnessMarker() : first_(LiveMockers().size()) {}

 protected:
  void Mark(Strictness s) {
    auto& v = LiveMockers();
    for (size_t i = first_; i < v.size(); ++i) v[i]->strictness = s;
  }

 private:
  size_t first_;
};

}  // namespace internal

template <typename T>
class NiceMock : private internal::StrictnessMarker, public T {
 public:
  template <typename... Args>
  explicit NiceMock(Args&&... args) : T(std::forward<Args>(args)...) {
    Mark(internal::Strictness::kNice);
  }
};

template <typename T>
class StrictMock : private internal::StrictnessMarker, public T {
 public:
  template <typename... Args>
  explicit StrictMock(Args&&... args) : T(std::forward<Args>(args)...) {
    Mark(internal::Strictness::kStrict);
  }
};

class InSequence {
 public:
  InSequence() : previous_(internal::CurrentSequence()) { internal::CurrentSequence() = &state_; }
  ~InSequence() { internal::CurrentSequence() = previous_; }
  InSequence(const InSequence&) = delete;
  InSequence& operator=(const InSequence&) = delete;

 private:
  internal::SequenceState state_;
  internal::SequenceState* previous_;
};

template <typename Sig>
class MockFunction;

template <typename R, typename... A>
class MockFunction<R(A...)> {
 public:
  mutable internal::FunctionMocker<R(A...)> gmock_Call{"Call"};
  R Call(A... a) { return gmock_Call.Invoke(std::forward<A>(a)...); }
  std::function<R(A...)> AsStdFunction() {
    return [this](A... a) -> R { return Call(std::forward<A>(a)...); };
  }
};

// MockFunction<std::function<R(Args...)>>, as gmock allows.
template <typename R, typename... A>
class MockFunction<std::function<R(A...)>> : public MockFunction<R(A...)> {};

}  // namespace testing

#define EXPECT_CALL(obj, call) ::testing::internal::MakeExpectation((obj).gmock_##call, __FILE__, __LINE__)
#define ON_CALL(obj, call) ::testing::internal::MakeDefault((obj).gmock_##call)

#define SM_GMOCK_MATCHER_CLASS_BEGIN(name)

#define MATCHER_P(name, p0, description)                                                                  \
  template <typename p0##_type>                                                                           \
  class name##MatcherP {                                                                                  \
   public:                                                                                                \
    explicit name##MatcherP(p0##_type gmock_p0) : p0(std::move(gmock_p0)) {}                              \
    template <typename arg_type>                                                                          \
    bool Matches(const arg_type& arg) const {                                                             \
      ::testing::MatchResultListener l;                                                                   \
      return MatchAndExplain(arg, &l);                                                                    \
    }                                                                                                     \
    template <typename arg_type>                                                                          \
    bool MatchAndExplain(const arg_type& arg, [[maybe_unused]] ::testing::MatchResultListener* result_listener) const; \
    p0##_type p0;                                                                                         \
  };                                                                                                      \
  template <typename p0##_type>                                                                           \
  name##MatcherP<p0##_type> name(p0##_type gmock_p0) {                                                    \
    return name##MatcherP<p0##_type>(std::move(gmock_p0));                                                \
  }                                                                                                       \
  template <typename p0##_type>                                                                           \
  template <typename arg_type>                                                                            \
  bool name##MatcherP<p0##_type>::MatchAndExplain(                                                        \
      const arg_type& arg, [[maybe_unused]] ::testing::MatchResultListener* result_listener) const

#define MATCHER_P3(name, p0, p1, p2, description)                                                         \
  template <typename p0##_type, typename p1##_type, typename p2##_type>                                   \
  class name##MatcherP3 {                                                                                 \
   public:                                                                                                \
    name##MatcherP3(p0##_type gmock_p0, p1##_type gmock_p1, p2##_type gmock_p2)                           \
        : p0(std::move(gmock_p0)), p1(std::move(gmock_p1)), p2(std::move(gmock_p2)) {}                    \
    template <typename arg_type>                                                                          \
    bool Matches(const arg_type& arg) const {                                                             \
      ::testing::MatchResultListener l;                                                                   \
      return MatchAndExplain(arg, &l);                                                                    \
    }                                                                                                     \
    template <typename arg_type>                                                                          \
    bool MatchAndExplain(const arg_type& arg, [[maybe_unused]] ::testing::MatchResultListener* result_listener) const; \
    p0##_type p0;                                                                                         \
    p1##_type p1;                                                                                         \
    p2##_type p2;                                                                                         \
  };                                                                                                      \
  template <typename p0##_type, typename p1##_type, typename p2##_type>                                   \
  name##MatcherP3<p0##_type, p1##_type, p2##_type> name(p0##_type gmock_p0, p1##_type gmock_p1,           \
                                                        p2##_type gmock_p2) {                             \
    return name##MatcherP3<p0##_type, p1##_type, p2##_type>(std::move(gmock_p0), std::move(gmock_p1),     \
                                                            std::move(gmock_p2));                         \
  }                                                                                                       \
  template <typename p0##_type, typename p1##_type, typename p2##_type>                                   \
  template <typename arg_type>                                                                            \
  bool name##MatcherP3<p0##_type, p1##_type, p2##_type>::MatchAndExplain(                                 \
      const arg_type& arg, [[maybe_unused]] ::testing::MatchResultListener* result_listener) const

// TEST_F(Fixture, Name): suite "Dcsctp<Fixture>", as the ported TEST suites.
#define SM_TEST_F(fixture, name)                                                                     \
  class fixture##_##name##_Test : public fixture {                                                   \
   public:                                                                                           \
    void TestBody() override;                                                                        \
  };                                                                                                 \
  static void fixture##_##name##_run() {                                                             \
    fixture##_##name##_Test t;                                                                       \
    t.Run();                                                                                         \
  }                                                                                                  \
  static ::sm::test::Registrar fixture##_##name##_reg("Dcsctp" #fixture, #name, &fixture##_##name##_run); \
  void fixture##_##name##_Test::TestBody()

// TEST_P(Fixture, Name): one test that runs the body for every value of
// INSTANTIATE_TEST_SUITE_P(prefix, Fixture, Values(...)).
#define SM_TEST_P(fixture, name)                                                                     \
  class fixture##_##name##_Test : public fixture {                                                   \
   public:                                                                                           \
    using FixtureType = fixture;                                                                     \
    void TestBody() override;                                                                        \
  };                                                                                                 \
  static void fixture##_##name##_run() { ::testing::internal::RunParamTest<fixture##_##name##_Test>(); } \
  static ::sm::test::Registrar fixture##_##name##_reg("Dcsctp" #fixture, #name, &fixture##_##name##_run); \
  void fixture##_##name##_Test::TestBody()

#define INSTANTIATE_TEST_SUITE_P(prefix, fixture, generator, ...)                                    \
  static const bool prefix##_##fixture##_instantiated = [] {                                         \
    ::testing::internal::ParamValues<fixture>() = (generator).template As<fixture::ParamType>();      \
    return true;                                                                                     \
  }()

#define SM_THAT_IMPL(value, matcher, on_fail)                                                         \
  do {                                                                                                \
    if (!::testing::internal::MatchValue((value), (matcher))) {                                       \
      ::sm::test::ReportFailure(__FILE__, __LINE__, std::string("expected that " #value " matches " #matcher)); \
      on_fail;                                                                                        \
    }                                                                                                 \
  } while (0)

#define SM_EXPECT_THAT(value, matcher) SM_THAT_IMPL(value, matcher, (void)0)
#define SM_ASSERT_THAT(value, matcher) SM_THAT_IMPL(value, matcher, return)
#ifndef SM_ASSERT_NE
#define SM_ASSERT_NE(a, b) SM_CMP_IMPL(a, b, !=, return)
#endif
#ifndef SM_ASSERT_LT
#define SM_ASSERT_LT(a, b) SM_CMP_IMPL(a, b, <, return)
#endif
