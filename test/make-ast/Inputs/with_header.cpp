// Test file with user header
#include "with_header.h"
#include <string>

int main() {
    TestData data{42, "test"};
    std::string message = data.name;
    return data.value;
}
