#include "foo.hpp"
int foo() {
  int sum = 0;
  for (int i = 0; i < 100; ++i) {
    sum += i;
  }
  return sum;
}
