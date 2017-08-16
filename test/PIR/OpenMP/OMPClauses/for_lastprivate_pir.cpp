#include <iostream>
#include <omp.h>
#include <sstream>
#include "utils.h"

int main(int argc, char **argv) {
  //   long long a = 0;
  int n;

  n = init_size(argc, argv);

  int x = -1;

#pragma omp parallel for lastprivate(x)
  for (int i = 0; i < n - 1; i++) {
    x = i * 2;
  }

  std::cout << "x = " << x << std::endl;

  return 0;
}
