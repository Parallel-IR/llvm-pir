#ifndef UTILS_H
#define UTILS_H

#include <iostream>
#include <string>

int init_size(int argc, char **argv) {
  if (argc > 1) {
    return std::stoi(argv[1]);
  }

  return 32;
}
float *alloc_arr(int size) { return new float[size]; }

#endif
