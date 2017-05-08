/*
  Copyright (C) 2014 Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in
    the documentation and/or other materials provided with the
    distribution.
  * Neither the name of Intel Corporation nor the names of its
    contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
  OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
  AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
  WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.
*/
// clang++ -O3 -std=c++11 -fopenmp OMPMergeSort2.cpp OMPMergeSort2Serial.cpp
// test.cpp -I${CLANG_BIN_DIR}/../projects/openmp/runtime/src/ -o stable.out
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS

#include "merge_sort.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <iterator>
const size_t N_MAX = 10000000;

// static Key Array[N_MAX];
static int Array[N_MAX];
static int Copy1[N_MAX];
static int Copy2[N_MAX];

// Initialize Array with n elements.
void CreateDataSet(size_t n) {
  // Keys will be in [0..m-1].  The limit almost ensures that some duplicate
  // keys will occur.
  int m = 2 * n;
  for (size_t i = 0; i < n; ++i) {
    Array[i] = rand() % m;
    // Array[i].value = rand() % m;
    // Array[i].index = i;
  }

  std::memcpy(Copy1, Array, n * sizeof(int));
  std::memcpy(Copy2, Array, n * sizeof(int));
}

void CheckIsSorted(size_t n) {
  for (size_t i = 0; i < n; ++i) {
    if (Copy1[i] != Copy2[i]) {
      std::cout << "ERROR";
      break;
    }
  }
}

//! Test sort for n items
void Test(size_t n) {
  CreateDataSet(n);

  auto ParallelStart = std::chrono::high_resolution_clock::now();
  parallel_stable_sort(Copy1, Copy1 + n);
  auto ParallelEnd = std::chrono::high_resolution_clock::now();
  auto SerialStart = std::chrono::high_resolution_clock::now();
  std::stable_sort(Copy2, Copy2 + n);
  auto SerialEnd = std::chrono::high_resolution_clock::now();

  auto ParallelDuration = std::chrono::duration_cast<std::chrono::microseconds>(
      ParallelEnd - ParallelStart);
  auto SerialDuration = std::chrono::duration_cast<std::chrono::microseconds>(
      SerialEnd - SerialStart);
  std::cout << ", Parallel: " << std::setw(10) << ParallelDuration.count();
  std::cout << ", Serial: " << std::setw(10) << SerialDuration.count();
  double SpeedUp =
      (double)SerialDuration.count() / (double)ParallelDuration.count();
  std::cout << ", SpeedUp: " << std::setw(10) << SpeedUp << std::endl;
  // Check that keys were sorted
  CheckIsSorted(n);
}

int main() {
  for (int n = 0; n <= N_MAX; n = n < 10 ? n + 1 : n * 1.618f) {
    std::cout << "Testing for n = " << std::setw(10) << n;
    Test(n);
  }
  printf("\n");
  return 0;
}
