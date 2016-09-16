//===- Transforms/Parallelize/OpenMP.h --- PIR OpenMP backend -*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OPENMP_RUNTIME_BACKEND_H
#define LLVM_OPENMP_RUNTIME_BACKEND_H

// TODO: Create preprocessor guards that will disable this code if no OpenMP
//       runtime (e.g., libgomp) is available on the target.

#include "llvm/Transforms/Parallelize/PIRBackend.h"

namespace llvm {

class OpenMPRuntimeBackend : public PIRBackend {
  Instruction *CreateHeader(BasicBlock *);
  Instruction *CreateNextRegion(BasicBlock *);
  BasicBlock *CreateTasks(BasicBlock *);

public:
  virtual bool runOnParallelRegion(ParallelRegion &PR, ForkInst &FI,
                                   DominatorTree &DT, LoopInfo &LI) override;

  virtual int getScore(ParallelRegion *PR = nullptr,
                       ForkInst *FI = nullptr) const override;

  virtual const StringRef getName() const override { return "OpenMP"; }
};

}
#endif
