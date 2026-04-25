// Test file with user-defined header
#include "user_header.h"
#include <iostream>

int main() {
  MyClass obj;
  std::cout << obj.getName() << std::endl;
  return 0;
}
