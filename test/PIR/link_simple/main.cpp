#include <iostream>
#include <sstream>
#include <omp.h>

extern "C" {
void simpleParallelRegion();

void foo() {
  std::stringstream ss;
  ss << "foo executed by thread: " << omp_get_thread_num() << std::endl;
  std::cout << ss.str();
}
}
int main() {
  simpleParallelRegion();
  return 0;
}
