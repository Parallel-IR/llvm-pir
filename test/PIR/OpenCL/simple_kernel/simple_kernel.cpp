#include <iostream>
#include <omp.h>
#include <sstream>
#include "../../OpenMP/OMPClauses/utils.h"

int main(int argc, char **argv) {
  int i;
  int n;
  float *a, *b;

  n = init_size(argc, argv);
  a = alloc_arr(n);
  b = alloc_arr(n);

  for (int i=0 ; i<n ; ++i) {
	  a[i] = i;
  }

#pragma omp parallel for
  for (i = 0; i < n; ++i) /* i is private by default */ {
    b[i] = a[i] + n;
  }

  for (i =0 ; i<n ; ++i) {
    std::cout << b[i] << " ";
  }

  std::cout << std::endl;

  return 0;
}
