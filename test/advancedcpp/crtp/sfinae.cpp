#include <iostream>
#include <string>
#include <type_traits>

template <typename T, typename = void>
// default version: without HasToString
struct HasToString : std::false_type {};

template <typename T>
struct HasToString<T, std::void_t<decltype(std::declval<T>().toString())>>
    : std::true_type {};

struct A {
  std::string toString() const { return "Struct A"; }
};

struct B {
  void reset() {}
};

int main(int argc, char *argv[]) {
  std::cout << "class A: " << HasToString<A>::value << "\n";
  std::cout << "class B: " << HasToString<B>::value << "\n";
  return 0;
}
