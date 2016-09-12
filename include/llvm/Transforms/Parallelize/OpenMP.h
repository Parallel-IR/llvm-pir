//===- PIR/Backend/OpenMP.h --- PIR backend for OpenMP --------*- C++ -*---===//
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

#ifndef LLVM_PIR_BACKENDS_OPENMP_H
#define LLVM_PIR_BACKENDS_OPENMP_H

#include "llvm/Pass.h"
#include "llvm/IR/Instruction.h"

namespace llvm {


class OpenMPParallelTasks : public FunctionPass {
public:
  static char ID;
  explicit OpenMPParallelTasks();

  ~OpenMPParallelTasks() override;

  /// @name FunctionPass interface
  //@{
  bool runOnFunction(Function&) override;
  void releaseMemory() override;
  void getAnalysisUsage(AnalysisUsage&) const override;
  void print(raw_ostream &, const Module *) const override;
  void dump() const;
  //@}

private:
  Instruction *CreateHeader(BasicBlock *);
  Instruction *CreateNextRegion(BasicBlock *);
  BasicBlock *CreateTasks(BasicBlock *);
};

}
#endif
