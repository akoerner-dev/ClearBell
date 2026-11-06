// mini_test.h - a dependency-free unit test harness for the PC tests (no gtest/Unity download needed).
//
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); }
//
// Every TEST registers itself; test_main.cpp runs them all, prints one line per failed check and a
// summary with the number of tests and checks - so a run that tests nothing is visible as "0 checks".
#pragma once

#include <cstdio>

namespace mt {

struct Test {
  const char *name;
  void (*fn)();
  Test *next;
};

inline Test *&registry() {
  static Test *head = nullptr;
  return head;
}

inline int &checks() {
  static int n = 0;
  return n;
}

inline int &failures() {
  static int n = 0;
  return n;
}

struct Registrar {
  Registrar(Test *t) {
    // Append so tests run in source order.
    Test **p = &registry();
    while (*p) p = &(*p)->next;
    *p = t;
  }
};

}  // namespace mt

#define TEST(name)                                         \
  static void name();                                      \
  static mt::Test name##_test = {#name, name, nullptr};    \
  static mt::Registrar name##_reg(&name##_test);           \
  static void name()

#define CHECK(cond)                                                          \
  do {                                                                       \
    mt::checks()++;                                                          \
    if (!(cond)) {                                                           \
      mt::failures()++;                                                      \
      std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
    }                                                                        \
  } while (0)

#define CHECK_EQ(a, b)                                                                         \
  do {                                                                                         \
    mt::checks()++;                                                                            \
    long long va_ = (long long)(a), vb_ = (long long)(b);                                      \
    if (va_ != vb_) {                                                                          \
      mt::failures()++;                                                                        \
      std::printf("  FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, va_, vb_); \
    }                                                                                          \
  } while (0)
