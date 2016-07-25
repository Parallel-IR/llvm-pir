//===- llvm/Transforms/Utils/ParallelUtils.h - Parallel utilities -*- C++ -*-=========//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines some parallel transformation utilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_PARALLELUTILS_H
#define LLVM_TRANSFORMS_UTILS_PARALLELUTILS_H

#include "llvm/Pass.h"

namespace llvm {

//===----------------------------------------------------------------------===//
//
// SequentializeParallelRegions - This pass removes all fork and join
// instructions, creating a sequential program.
//
FunctionPass *createSequentializeParallelRegionsPass();

class SequentializeParallelRegions : public FunctionPass {
public:
  static char ID;
  explicit SequentializeParallelRegions();

  ~SequentializeParallelRegions() override;

  /// @name FunctionPass interface
  //@{
  bool runOnFunction(Function&) override;
  void releaseMemory() override;
  void getAnalysisUsage(AnalysisUsage&) const override;
  void print(raw_ostream &, const Module *) const override;
  void dump() const;
  //@}
};

}
#endif
