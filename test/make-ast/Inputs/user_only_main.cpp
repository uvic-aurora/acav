// user_only_main.cpp - Source file that only includes user headers
// This test verifies that query-dependencies correctly handles:
// 1. Source files with no system headers
// 2. Direct user includes (user_only_header2.h)
// 3. Indirect user includes (user_only_header1.h via user_only_header2.h)

#include "user_only_header2.h"

int main() {
  Data d;
  d.value = 42;
  d.score = 3.14;

  DataProcessor processor;
  processor.process(d);

  return 0;
}
