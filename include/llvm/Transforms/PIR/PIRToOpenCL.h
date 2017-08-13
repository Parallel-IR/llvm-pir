//===-- llvm/PIRToOpenCL.h - Instruction class definition -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_PIR_PIRTOOPENCL_H
#define LLVM_TRANSFORMS_PIR_PIRTOOPENCL_H

#include "llvm/Analysis/ParallelRegionInfo.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/Analysis/ScalarEvolution.h"

namespace llvm {
class PIRToOpenCLPass : public ModulePass {
public:
  static char ID;

  PIRToOpenCLPass() : ModulePass(ID) {}

  bool runOnModule(Module &M) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  void print(raw_ostream &OS, const Module *) const override;
  void dump() const;

private:
  bool verifyExtractedFn(Function *Fn) const;

  void startRegionEmission(const ParallelRegion &PR, LoopInfo &LI,
                           DominatorTree &DT, ScalarEvolution &SE);

  void removePIRInstructions(ForkInst &ForkInst, const ParallelTask &ForkedTask,
                             const ParallelTask &ContTask);

  Function *declareOCLRegionFn(Function *RegionFn, ValueToValueMapTy &VMap);

  int emitKernelFile(Module *M, Function *OCLKernel, std::ostream &Out);

private:
  ParallelRegionInfo *PRI = nullptr;
};
}

#endif
