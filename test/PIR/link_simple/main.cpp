#include <iostream>
#include <omp.h>
#include <sstream>

extern "C" {
void simpleParallelRegion();

void bar() {
  std::stringstream ss;
  ss << "bar executed by thread: " << omp_get_thread_num() << std::endl;
  std::cout << ss.str();
}

void foo(int x, int *y) {
  std::stringstream ss;
  ss << "foo executed by thread: " << omp_get_thread_num() << std::endl;
  ss << "x " << x << ", y " << *y << std::endl;
  std::cout << ss.str();
}

void foo2(int x, float y) {
  std::stringstream ss;
  ss << "foo2 executed by thread: " << omp_get_thread_num() << std::endl;
  ss << "x " << x << ", y " << y << std::endl;
  std::cout << ss.str();
}

int read_int() {
  int x;
  std::cin >> x;
  return x;
}

int read_float() {
  float x;
  std::cin >> x;
  return x;
}
}
int main() {
  simpleParallelRegion();
  return 0;
}
