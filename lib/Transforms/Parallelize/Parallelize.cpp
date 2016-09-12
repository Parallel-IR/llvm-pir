//===-- Parallelize.cpp ---------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Parallelize.h"

using namespace llvm;

void initializeParallelization(PassRegistry &Registry) {
  initializeParallelRegionInfoPassPass(Registry);
  initializeSequentializeParallelRegionsPass(Registry);
  initializeOpenMPParallelTasksPass(Registry);
}
