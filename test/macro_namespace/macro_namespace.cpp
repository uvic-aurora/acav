#include "macro_namespace.h"

QT_BEGIN_NAMESPACE

struct Widget {
  int value = 0;
};

int add(int a, int b) { return a + b; }

QT_END_NAMESPACE

int main() { return qt::add(1, 2) + qt::Widget{}.value; }
