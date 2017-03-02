#ifndef LLVM_TRANSFORMS_PIR_PIRTOOPENMP_H
#define LLVM_TRANSFORMS_PIR_PIRTOOPENMP_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/ParallelRegionInfo.h"

namespace llvm {

  class PIRToOMPPass : public PassInfoMixin<PIRToOMPPass> {
    static StringRef name() { return "PIRToOMPPass"; }

    PIRToOMPPass() {}

    void run(Function &F, FunctionAnalysisManager &AM);
  };

  class PIRToOpenMPPass : public FunctionPass {
    ParallelRegionInfo *PRI;

  public:
    static char ID;
    PIRToOpenMPPass() : FunctionPass(ID) {
    }

    bool runOnFunction(Function &F) override;
    void getAnalysisUsage(AnalysisUsage &AU) const override;

    void print(raw_ostream &OS, const Module *) const override;
    void dump() const;
  };
}

#endif
