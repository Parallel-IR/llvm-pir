#include <iostream>
#include <omp.h>
#include <sstream>
#include "utils.h"

long long init_a();

int main(int argc, char **argv) {
  long long a = 100;
  int n;
  float *b;

  n = init_size(argc, argv);
  b = alloc_arr(n);

#pragma omp parallel for firstprivate(a)
  for (int i = 0; i < n; i++) {
    b[i] = a;
  }

  for (int i = 0; i < n; ++i) {
    std::cout << b[i] << ", ";
  }

  std::cout << std::endl;
  std::cout << "a = " << a << std::endl;

  return 0;
}
