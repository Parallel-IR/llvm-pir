//===-- Parallelize.h - Parallelization Implementations ---------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_PARALELLIZE_H
#define LLVM_TRANSFORMS_PARALELLIZE_H

namespace llvm {
class FunctionPass;
class PassRegistry;

/// Initialize all passes linked into the Parallelize library.
void initializeParallelization(PassRegistry&);

/// @brief Identify all parallel regions and build the parallel region tree.
FunctionPass *createParallelRegionInfoPass();

/// @brief Create a sequential program by sequentializing parallel regions.
FunctionPass *createSequentializeParallelRegionsPass();

/// @brief Use OpenMP based parallelism to implement parallel region.
FunctionPass *createOpenMPParallelTasksPass();

} // End llvm namespace

#endif
