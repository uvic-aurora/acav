#include "foo.hpp"

int foo(int condition) {
  {
    if (condition) {
      return 1;
    }
    return 0;
  }

  for (int i = 0; i < 5; i++) {
    int x = i + 1;
  }

  int y = 0;
  while (y < 3) {
    y++;
  }
}

int globalVar = 42;

typedef int MyInt;

MyInt myIntVar = 100;

class MyClass {
  int memberVar;

public:
  int getMember() { return memberVar; }
};

enum Color { Red = 0, Blue = 1 };

namespace MyNamespace {
int nsVar = 10;
}

int add(int a, int b) { return a + b; }

int useVar() {
  int localVar = 5;
  return localVar;
}

int castTest(int value) {
  bool flag = value;
  return flag ? 1 : 0;
}

int *getPointer() {
  int local = 10;
  return &local;
}

void useArray(int arr[5]) {
  int sum = 0;
  for (int i = 0; i < 5; i++) {
    sum += arr[i];
  }
}

template <typename T> T addTemplate(T a, T b) { return a + b; }

template <typename T> void useTemplate() { T value = T(); }

int main() {
  int result = addTemplate<int>(3, 4);
  addTemplate<double>(5.5, 6.5);

  int unusedParam = 0;
  return result;
}
