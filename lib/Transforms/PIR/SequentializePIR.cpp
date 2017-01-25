//===-- PIR/SequentializePIR.cpp - Sequentialize parallel regions ---------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass will lower all parallel regions to sequential IR.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {

/// This pass will lower all parallel regions to sequential IR. It replaces
/// PIR instruction by unconditional branch instructions as follows:
///   - fork is replaced with a branch to the forked block.
///   - halt is replaced with a branch to the sibling continuation block.
///   - join is replaced with a branch to its destination block.
struct SequentializePIR : public FunctionPass {
  static char ID;

  SequentializePIR() : FunctionPass(ID) {
    initializeSequentializePIRPass(*PassRegistry::getPassRegistry());
  }

  bool runOnFunction(Function &F) override {
    bool Changed = false;

    for (auto BB = F.begin(); BB != F.end(); ++BB) {
      auto I = BB->getTerminator();

      if (ForkInst *FI = dyn_cast<ForkInst>(I)) {
        BranchInst::Create(FI->getForkedBB(), I);
        I->eraseFromParent();
        Changed = true;
      } else if (HaltInst *HI = dyn_cast<HaltInst>(I)) {
        BranchInst::Create(HI->getContinuationBB(), I);
        I->eraseFromParent();
        Changed = true;
      } else if (JoinInst *JI = dyn_cast<JoinInst>(I)) {
        BranchInst::Create(JI->getSuccessor(0), I);
        I->eraseFromParent();
        Changed = true;
      }
    }

    return Changed;
  }
};
}

char SequentializePIR::ID = 0;
INITIALIZE_PASS(SequentializePIR, "sequentialize-pir",
                "Lower parallel regions to sequential IR", false, false)
