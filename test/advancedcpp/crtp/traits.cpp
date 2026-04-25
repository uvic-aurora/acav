#include <format>
#include <iostream>

template <typename T> struct Traits;

class Triangle;
class Squre;

template <> struct Traits<Triangle> {
  static constexpr int dimention = 3;
  static constexpr const char *name = "Triangle";
};

template <> struct Traits<Squre> {
  static constexpr int dimention = 4;
  static constexpr const char *name = "Squre";
};

template <typename Derived> class Shape {
public:
  int dim() const { return Traits<Derived>::dimention; }
  std::string getName() const {
    return std::format("{} : {}", Traits<Derived>::name,
                       Traits<Derived>::dimention);
  }
};

class Triangle : public Shape<Triangle> {};
class Squre : public Shape<Squre> {};

int main(int argc, char *argv[]) {
  Triangle trig;
  Squre squre;

  std::cout << "Triangle dimention: " << trig.dim() << "\n";
  std::cout << "Squre dimention: " << squre.dim() << "\n";
  std::cout << trig.getName() << "\n";
  std::cout << squre.getName() << "\n";
  return 0;
}
