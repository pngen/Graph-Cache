#pragma once
// Lightweight, dependency-free test framework for Graph Cache.
// Copyright 2026 Summon Software Labs. Apache License 2.0.

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gc_test {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}
inline int& failures() {
  static int f = 0;
  return f;
}
inline int& total_assertions() {
  static int a = 0;
  return a;
}
inline std::string& current() {
  static std::string c;
  return c;
}

struct Registrar {
  Registrar(const char* name, std::function<void()> fn) {
    registry().push_back(TestCase{std::string(name), std::move(fn)});
  }
};

inline void report_failure(const char* expr, const char* file, int line, const std::string& msg) {
  std::printf("FAIL [%s] (%s) %s:%d %s\n", current().c_str(), expr, file, line, msg.c_str());
  std::fprintf(stderr, "FAIL [%s] (%s) %s:%d %s\n", current().c_str(), expr, file, line, msg.c_str());
  ++failures();
}

inline int run_all() {
  for (auto& t : registry()) {
    current() = t.name;
    t.fn();
  }
  std::printf("gc_test: %d checks, %d failures\n", total_assertions(), failures());
  return failures() == 0 ? 0 : 1;
}

} // namespace gc_test

#define GC_TEST(name)                                        \
  static void gc_test_fn_##name();                           \
  static ::gc_test::Registrar gc_test_reg_##name(#name, gc_test_fn_##name); \
  static void gc_test_fn_##name()

#define CHECK(x)                                                     \
  do {                                                               \
    ::gc_test::total_assertions()++;                                 \
    if (!(x)) ::gc_test::report_failure(#x, __FILE__, __LINE__, "");  \
  } while (0)

#define REQUIRE(x)                                                         \
  do {                                                                     \
    ::gc_test::total_assertions()++;                                       \
    if (!(x)) {                                                            \
      ::gc_test::report_failure(#x, __FILE__, __LINE__, "REQUIRED");       \
      return;                                                              \
    }                                                                      \
  } while (0)

#define CHECK_EQ(a, b)                                                        \
  do {                                                                        \
    ::gc_test::total_assertions()++;                                          \
    if (!((a) == (b)))                                                        \
      ::gc_test::report_failure(#a " == " #b, __FILE__, __LINE__, "left/right differ"); \
  } while (0)

#define GC_TEST_MAIN int main() { return ::gc_test::run_all(); }
