// math.cppm
// define the interface for the math module
export module math;

export namespace math {

template <typename T> T add(T a, T b) { return a + b; }

int sub(int a, int b);
} // namespace math
