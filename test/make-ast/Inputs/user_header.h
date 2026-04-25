// User-defined header file
#ifndef USER_HEADER_H
#define USER_HEADER_H

#include <string>

class MyClass {
public:
  MyClass() = default;
  std::string getName() const { return name_; }
private:
  std::string name_;
};

#endif // USER_HEADER_H
