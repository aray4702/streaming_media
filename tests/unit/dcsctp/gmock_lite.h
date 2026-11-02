// The gtest/gmock subset that libwebrtc M157's net/dcsctp/ tests use, on
// sm_test.h (#1032): SM_EXPECT_THAT / SM_ASSERT_THAT with the matchers
// ElementsAre, SizeIs, IsEmpty and Not, and a plain value, which matches by
// ==, as gmock's implicit Eq does. Only for tests/unit/dcsctp/.
#pragma once

#include <cstddef>
#include <iterator>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

#include "sm_test.h"

namespace testing {

template <typename... Ts>
struct ElementsAreMatcher {
  std::tuple<Ts...> expected;

  template <typename C>
  bool Matches(const C& c) const {
    if (static_cast<size_t>(std::distance(std::begin(c), std::end(c))) != sizeof...(Ts)) return false;
    auto it = std::begin(c);
    bool ok = true;
    std::apply([&](const auto&... e) { ((ok = ok && (*it == e), ++it), ...); }, expected);
    return ok;
  }
};

template <typename... Ts>
ElementsAreMatcher<std::decay_t<Ts>...> ElementsAre(Ts&&... ts) {
  return {std::tuple<std::decay_t<Ts>...>(std::forward<Ts>(ts)...)};
}

struct SizeIsMatcher {
  size_t n;
  template <typename C>
  bool Matches(const C& c) const {
    return static_cast<size_t>(std::distance(std::begin(c), std::end(c))) == n;
  }
};

template <typename N>
SizeIsMatcher SizeIs(N n) {
  return {static_cast<size_t>(n)};
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
    return !inner.Matches(v);
  }
};

template <typename M>
NotMatcher<M> Not(M m) {
  return {std::move(m)};
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
}  // namespace testing

#define SM_THAT_IMPL(value, matcher, on_fail)                                                         \
  do {                                                                                                \
    if (!::testing::internal::MatchValue((value), (matcher))) {                                       \
      ::sm::test::ReportFailure(__FILE__, __LINE__, std::string("expected that " #value " matches " #matcher)); \
      on_fail;                                                                                        \
    }                                                                                                 \
  } while (0)

#define SM_EXPECT_THAT(value, matcher) SM_THAT_IMPL(value, matcher, (void)0)
#define SM_ASSERT_THAT(value, matcher) SM_THAT_IMPL(value, matcher, return)
