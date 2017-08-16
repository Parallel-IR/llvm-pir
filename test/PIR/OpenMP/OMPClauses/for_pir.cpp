#include <iostream>
#include <omp.h>
#include <sstream>
#include "utils.h"

int main(int argc, char **argv) {
  int i;
  int n;
  float *a, *b;

  n = init_size(argc, argv);
  b = alloc_arr(n);

#pragma omp parallel for
  for (i = 0; i < n; ++i) /* i is private by default */ {
    b[i] = i;
    // TODO handle function calls inside a parallel region properly. Clang
    // emits calls as invoke instructions and ParallelRegionInfo doesn't
    // support that yet.
    // b[i] = omp_get_thread_num();
  }

  for (int i=0 ; i< n ; ++i) {
    std::cout << b[i] << ", ";
  }

  std::cout << std::endl;

  return 0;
}
