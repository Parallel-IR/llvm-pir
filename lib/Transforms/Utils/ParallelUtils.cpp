//===-- ParallelUtils.cpp - Parallel Utility functions -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines common parallel utility functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ParallelRegionInfo.h"
#include "llvm/Transforms/Utils/ParallelUtils.h"

using namespace llvm;

#define DEBUG_TYPE "parallel-utils"

