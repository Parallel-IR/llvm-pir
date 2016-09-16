//===- PIR/Backend/Sequentialize.h - Sequentializing PIR pass -*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SEQUENTIALIZING_BACKEND_H
#define LLVM_SEQUENTIALIZING_BACKEND_H

#include "llvm/Transforms/Parallelize/PIRBackend.h"

namespace llvm {

class SequentializingBackend : public PIRBackend {

public:
  virtual bool runOnParallelRegion(ParallelRegion &PR, ForkInst &FI,
                                   DominatorTree &DT, LoopInfo &LI) override;

  virtual int getScore(ParallelRegion *PR = nullptr,
                       ForkInst *FI = nullptr) const override;

  virtual const StringRef getName() const override { return "Sequentialization"; }
};

}
#endif
