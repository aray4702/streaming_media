// Minimal, dependency-free test framework. No exceptions, no RTTI, so it
// runs everywhere the library does (desktop, mobile, Wasm under Node).
//
//   SM_TEST(Suite, Name) { SM_EXPECT_EQ(1 + 1, 2); SM_ASSERT_TRUE(ok); }
//
// Run with --filter=<substring> (e.g. --filter=NackTracker.) and --list.
#pragma once

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace sm::test {

struct TestCase {
  const char* suite;
  const char* name;
  void (*fn)();
};

std::vector<TestCase>& Registry();
void ReportFailure(const char* file, int line, const std::string& msg);
bool CurrentFailed();

struct Registrar {
  Registrar(const char* suite, const char* name, void (*fn)()) { Registry().push_back({suite, name, fn}); }
};

template <class T>
std::string Str(const T& v) {
  if constexpr (requires(std::ostream& os, const T& x) { os << x; }) {
    std::ostringstream os;
    os << v;
    return os.str();
  } else {
    return "<value>";
  }
}

}  // namespace sm::test

#define SM_TEST(suite, name)                                                         \
  static void suite##_##name##_impl();                                               \
  static ::sm::test::Registrar suite##_##name##_reg(#suite, #name, &suite##_##name##_impl); \
  static void suite##_##name##_impl()

#define SM_CHECK_IMPL(cond, msg, on_fail)                                  \
  do {                                                                     \
    if (!(cond)) {                                                         \
      ::sm::test::ReportFailure(__FILE__, __LINE__, msg);                  \
      on_fail;                                                             \
    }                                                                      \
  } while (0)

#define SM_CMP_IMPL(a, b, op, on_fail)                                                   \
  do {                                                                                   \
    const auto sm_a_ = (a);                                                              \
    const auto sm_b_ = (b);                                                              \
    if (!(sm_a_ op sm_b_)) {                                                             \
      ::sm::test::ReportFailure(__FILE__, __LINE__,                                      \
                                std::string(#a " " #op " " #b " (") + ::sm::test::Str(sm_a_) + \
                                    " vs " + ::sm::test::Str(sm_b_) + ")");              \
      on_fail;                                                                           \
    }                                                                                    \
  } while (0)

#define SM_EXPECT_TRUE(c) SM_CHECK_IMPL(c, "expected true: " #c, (void)0)
#define SM_EXPECT_FALSE(c) SM_CHECK_IMPL(!(c), "expected false: " #c, (void)0)
#define SM_ASSERT_TRUE(c) SM_CHECK_IMPL(c, "assertion failed: " #c, return)
#define SM_ASSERT_FALSE(c) SM_CHECK_IMPL(!(c), "assertion failed: !" #c, return)
#define SM_EXPECT_EQ(a, b) SM_CMP_IMPL(a, b, ==, (void)0)
#define SM_EXPECT_NE(a, b) SM_CMP_IMPL(a, b, !=, (void)0)
#define SM_EXPECT_LT(a, b) SM_CMP_IMPL(a, b, <, (void)0)
#define SM_EXPECT_LE(a, b) SM_CMP_IMPL(a, b, <=, (void)0)
#define SM_EXPECT_GT(a, b) SM_CMP_IMPL(a, b, >, (void)0)
#define SM_EXPECT_GE(a, b) SM_CMP_IMPL(a, b, >=, (void)0)
#define SM_ASSERT_EQ(a, b) SM_CMP_IMPL(a, b, ==, return)
#define SM_ASSERT_GE(a, b) SM_CMP_IMPL(a, b, >=, return)
#define SM_ASSERT_GT(a, b) SM_CMP_IMPL(a, b, >, return)
#define SM_ASSERT_LE(a, b) SM_CMP_IMPL(a, b, <=, return)
#define SM_EXPECT_NEAR(a, b, eps)                                                          \
  do {                                                                                     \
    double sm_a_ = static_cast<double>(a), sm_b_ = static_cast<double>(b);                 \
    if (!(std::fabs(sm_a_ - sm_b_) <= (eps))) {                                            \
      ::sm::test::ReportFailure(__FILE__, __LINE__,                                        \
                                std::string("|" #a " - " #b "| <= " #eps " (") +           \
                                    std::to_string(sm_a_) + " vs " + std::to_string(sm_b_) + ")"); \
    }                                                                                      \
  } while (0)

// Lets a test print a measured value into the results log (picked up by the
// documentation generator): "[metric] suite.name key=value".
#define SM_RECORD(key, value) \
  std::printf("[metric] %s=%.6g\n", key, static_cast<double>(value))
