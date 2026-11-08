#include "minitest.h"

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  int run = 0;
  for (auto& c : mtCases()) {
    if (filter && !strstr(c.name, filter)) continue;
    int before = mtFailures();
    c.fn();
    run++;
    printf("%s %s\n", mtFailures() == before ? "[ OK ]" : "[FAIL]", c.name);
  }
  printf("\n%d tests, %d checks, %d failures\n", run, mtChecks(), mtFailures());
  return mtFailures() ? 1 : 0;
}
