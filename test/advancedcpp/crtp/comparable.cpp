#include <format>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>
template <typename Derived> class Comparable {
public:
  bool operator==(const Derived &other) const {
    return static_cast<const Derived *>(this)->compare(other) == 0;
  }

  bool operator!=(const Derived &other) const { return !(*this == other); }

  bool operator<(const Derived &other) const {
    return static_cast<const Derived *>(this)->compare(other) < 0;
  }
};

class Person : public Comparable<Person> {
public:
  explicit Person(std::string name, int age) : name_(name), age_(age) {}

  std::string getName() const { return name_; }
  int getAge() const { return age_; }
  int compare(const Person &other) const {
    auto result = std::tie(name_, age_) <=>
                  std::make_tuple(other.getName(), other.getAge());
    return result < 0 ? -1 : result > 0 ? 1 : 0;
  }

  std::string toString() const {
    return std::format("Name: {} Age: {}", name_, age_);
  }

private:
  std::string name_;
  int age_;
};

int main(int argc, char *argv[]) {
  Person min("Min", 10);
  Person hana("Hana", 12);
  std::cout << min.toString() << "\n";
  std::cout << "compare: " << (min == hana ? "equal" : "not equal") << "\n";

  std::vector<Person> people = {Person("Min", 30), Person("Min", 28),
                                Person("Hana", 30)};

  for (const auto &p : people) {
    std::cout << p.toString() << "\n";
  }

  return 0;
}
