// Example: Simple C++ test file for ACAV
// This file demonstrates various C++ constructs that ACAV can analyze

#include "calculator.hpp"

// Global variable
int globalCounter = 0;

// Type alias
typedef int Integer;

// Enum declaration
enum Status { OK = 0, ERROR = 1, PENDING = 2 };

// Class with various members
class Calculator {
private:
    int value_;
    
public:
    Calculator() : value_(0) {}
    explicit Calculator(int initial) : value_(initial) {}
    
    int getValue() const { return value_; }
    void setValue(int v) { value_ = v; }
    
    int add(int x) { 
        value_ += x;
        return value_;
    }
    
    int subtract(int x) {
        value_ -= x;
        return value_;
    }
};

// Namespace example
namespace math {
    int square(int x) { return x * x; }
    
    template<typename T>
    T abs(T value) {
        return value < 0 ? -value : value;
    }
}

// Function with control flow
int fibonacci(int n) {
    if (n <= 1) {
        return n;
    }
    
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) {
        int temp = a + b;
        a = b;
        b = temp;
    }
    return b;
}

// Template function
template<typename T>
T max(T a, T b) {
    return (a > b) ? a : b;
}

// Main function
int main() {
    Calculator calc(10);
    calc.add(5);
    calc.subtract(3);
    
    int fib10 = fibonacci(10);
    int squared = math::square(5);
    int maxVal = max(10, 20);
    
    globalCounter++;
    
    return calc.getValue();
}
