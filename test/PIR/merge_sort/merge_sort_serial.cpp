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
#include "merge_sort.h"
#include <algorithm>
#include <iostream>
#include <iterator>
#include <omp.h>
#include <utility>

extern "C" {
void stable_sort_base_case(int *xs, int *xe, int *zs, int inplace) {
  std::stable_sort(xs, xe);
  if (inplace != 2) {
    int *ze = zs + (xe - xs);
    if (!inplace)
      // Initialize the temporary buffer and move keys to it.
      for (; zs < ze; ++xs, ++zs)
        *zs = *xs;
  }
}
//! Merge sequences [xs,xe) and [ys,ye) to output sequence [zs,(xe-xs)+(ye-ys)),
void serial_merge(int *xs, int *xe, int *ys, int *ye, int *zs) {
  if (xs != xe) {
    if (ys != ye) {
      for (;;) {
        if (*ys < *xs) {
          *zs = *ys;
          ++zs;
          if (++ys == ye) {
            break;
          }
        } else {
          *zs = *xs;
          ++zs;
          if (++xs == xe) {
            goto movey;
          }
        }
      }
    }
    ys = xs;
    ye = xe;
  }
movey:
  std::copy(ys, ye, zs);
}

//! Raw memory buffer with automatic cleanup.
class raw_buffer {
  void *ptr;

public:
  //! Try to obtain buffer of given size.
  raw_buffer(size_t bytes) : ptr(operator new(bytes, std::nothrow)) {}
  //! True if buffer was successfully obtained, zero otherwise.
  operator bool() const { return ptr; }
  //! Return pointer to buffer, or  NULL if buffer could not be obtained.
  void *get() const { return ptr; }
  //! Destroy buffer
  ~raw_buffer() { operator delete(ptr); }
};

void parallel_stable_sort(int *xs, int *xe) {
  raw_buffer z = raw_buffer(sizeof(int) * (xe - xs));
  parallel_stable_sort_aux(xs, xe, (int *)z.get(), 2);
}
}
