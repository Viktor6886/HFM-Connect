// minitest.h - tiny test harness for host-side tests (no dependencies).
#pragma once
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <functional>
#include <vector>

struct MtCase {
  const char* name;
  std::function<void()> fn;
};

inline std::vector<MtCase>& mtCases() {
  static std::vector<MtCase> v;
  return v;
}
inline int& mtFailures() {
  static int f = 0;
  return f;
}
inline int& mtChecks() {
  static int c = 0;
  return c;
}

struct MtReg {
  MtReg(const char* n, std::function<void()> f) { mtCases().push_back({n, f}); }
};

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)
#define TEST(name)                                     \
  static void name();                                  \
  static MtReg MT_CAT(reg_, name)(#name, name);        \
  static void name()

#define CHECK(cond)                                                              \
  do {                                                                           \
    mtChecks()++;                                                                \
    if (!(cond)) {                                                               \
      mtFailures()++;                                                            \
      printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                 \
    }                                                                            \
  } while (0)

#define CHECK_EQ(a, b)                                                                     \
  do {                                                                                     \
    mtChecks()++;                                                                          \
    long long _a = (long long)(a), _b = (long long)(b);                                    \
    if (_a != _b) {                                                                        \
      mtFailures()++;                                                                      \
      printf("    FAIL %s:%d: %s == %s (%lld != %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); \
    }                                                                                      \
  } while (0)

#define CHECK_NEAR(a, b, tol)                                                               \
  do {                                                                                      \
    mtChecks()++;                                                                           \
    double _a = (double)(a), _b = (double)(b);                                              \
    if (fabs(_a - _b) > (tol)) {                                                            \
      mtFailures()++;                                                                       \
      printf("    FAIL %s:%d: %s ~ %s (%.6f vs %.6f)\n", __FILE__, __LINE__, #a, #b, _a, _b); \
    }                                                                                       \
  } while (0)

#define CHECK_STR(a, b)                                                                     \
  do {                                                                                      \
    mtChecks()++;                                                                           \
    if (strcmp((a), (b)) != 0) {                                                            \
      mtFailures()++;                                                                       \
      printf("    FAIL %s:%d: %s == \"%s\" (got \"%s\")\n", __FILE__, __LINE__, #a, (b), (a)); \
    }                                                                                       \
  } while (0)
