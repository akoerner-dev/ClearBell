// test_main.cpp - runs every registered TEST and prints a summary. Exit code 0 only if all checks pass
// AND at least one check ran.
#include <cstdio>

#include "mini_test.h"

int main() {
  int tests = 0;
  for (mt::Test *t = mt::registry(); t; t = t->next) {
    int before = mt::failures();
    t->fn();
    tests++;
    std::printf("%s %s\n", mt::failures() == before ? "ok  " : "FAIL", t->name);
  }
  std::printf("\n%d tests, %d checks, %d failed\n", tests, mt::checks(), mt::failures());
  return (mt::failures() == 0 && mt::checks() > 0) ? 0 : 1;
}
