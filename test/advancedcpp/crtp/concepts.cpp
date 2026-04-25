#include <concepts>
#include <iostream>

template <typename T>
concept HasHash = requires(T a) {
  { a.hash() } -> std::convertible_to<std::size_t>;
};

template <typename Derived> struct Hasher {
public:
  void hasHash() const
    requires HasHash<Derived>
  {
    std::cout << "Hash is :" << static_cast<const Derived *>(this)->hash()
              << "\n";
  }

  void hasHash() const
    requires(!HasHash<Derived>)
  {
    std::cout << "not hashable!\n";
  }
};

class A : public Hasher<A> {
public:
  std::size_t hash() const { return 111111111; }
};

class B : public Hasher<B> {};

int main(int argc, char *argv[]) {
  A a;
  B b;
  a.hasHash();
  b.hasHash();
  return 0;
}
