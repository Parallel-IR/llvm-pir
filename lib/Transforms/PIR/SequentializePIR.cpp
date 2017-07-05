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

#include "llvm/Analysis/ParallelRegionInfo.h"
#include "llvm/Analysis/LoopInfo.h"
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
///
/// If a parallel loop is encountered we annotate it as parallel using metadata.
struct SequentializePIR : public FunctionPass {
  static char ID;

  SequentializePIR() : FunctionPass(ID) {
    initializeSequentializePIRPass(*PassRegistry::getPassRegistry());
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<DominatorTreeWrapperPass>();
    AU.addRequired<ParallelRegionInfoPass>();
    AU.addRequired<LoopInfoWrapperPass>();

    AU.addPreserved<DominatorTreeWrapperPass>();
    AU.addPreserved<ParallelRegionInfoPass>();
    AU.addPreserved<LoopInfoWrapperPass>();
    AU.setPreservesCFG();
  }

  ParallelTask::VisitorTy createParallelTaskAnnotator(MDNode *LoopID) {
    assert(LoopID);

    // Helper function that annotates all read/write instructions in a basic
    // block with parallel memory metadata referring to LoopID.
    ParallelTask::VisitorTy Annotator = [LoopID](BasicBlock &BB,
                                                 const ParallelTask &) {
      for (Instruction &I : BB) {
        if (!I.mayReadOrWriteMemory())
          continue;

        MDNode *LoopIdMD =
            I.getMetadata(LLVMContext::MD_mem_parallel_loop_access);
        if (!LoopIdMD)
          LoopIdMD = MDNode::get(I.getContext(), {LoopID});
        else
          LoopIdMD = MDNode::concatenate(LoopIdMD, LoopID);
        I.setMetadata(LLVMContext::MD_mem_parallel_loop_access, LoopIdMD);
      }

      return true;
    };

    return Annotator;
  }

  bool runOnFunction(Function &F) override {
    ParallelRegionInfo &PRI =
        getAnalysis<ParallelRegionInfoPass>().getParallelRegionInfo();

    // Exit early if there is nothing to do.
    if (PRI.empty())
      return false;

    LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>().getLoopInfo();
    const DominatorTree &DT =
        getAnalysis<DominatorTreeWrapperPass>().getDomTree();

    ParallelRegionInfo::ParallelRegionVectorTy ParallelRegions;
    PRI.getAllParallelRegions(ParallelRegions);

    for (ParallelRegion *PR : ParallelRegions) {
      Loop *L = LI.getLoopFor(PR->getFork().getParent());
      bool AnnotateAsParallel =
          (L && !L->isAnnotatedParallel() && PR->isParallelLoopRegion(*L, DT));

      // Replace the fork, join and halt instructions by branches. Note that
      // this does not change the dominator tree or loop info.
      ForkInst &Fork = PR->getFork();
      BranchInst::Create(Fork.getForkedBB(), &Fork);
      Fork.eraseFromParent();

      ParallelTask &ForkedTask = PR->getForkedTask();
      for (TerminatorInst *TI : ForkedTask.getHaltsOrJoints()) {
        assert(isa<HaltInst>(TI) && "Forked task was not terminated by a halt!");
        BranchInst::Create(TI->getSuccessor(0), TI);
        TI->eraseFromParent();
      }

      ParallelTask &ContinuationTask = PR->getContinuationTask();
      for (TerminatorInst *TI : ContinuationTask.getHaltsOrJoints()) {
        assert(isa<JoinInst>(TI) &&
               "Continuation task was not terminated by a join!");
        BranchInst::Create(TI->getSuccessor(0), TI);
        TI->eraseFromParent();
      }

      if (!AnnotateAsParallel)
        continue;

      // Get or create a loop id for the current loop.
      MDNode *LoopID = L->getLoopID();
      if (!LoopID) {
        LoopID = MDNode::get(F.getContext(), {nullptr});
        LoopID->replaceOperandWith(0, LoopID);
        L->setLoopID(LoopID);
      }

      // Annotate all blocks in the forked task with
      // llvm.mem.parallel_loop_access metadata. Note that no other side-effects
      // are allowed in the loop outside the forked task.
      ParallelTask::VisitorTy Annotator = createParallelTaskAnnotator(LoopID);
      ForkedTask.visit(Annotator);

      // Verify that (for simplified loops) the annotation is recognized.
      assert(!L->isLoopSimplifyForm() || L->isAnnotatedParallel());
    }

    // Update the parallel region info.
    PRI.clear();
    assert(PRI.empty());

    return true;
  }
};
}

char SequentializePIR::ID = 0;

INITIALIZE_PASS_BEGIN(SequentializePIR, "sequentialize-pir",
                      "Lower parallel regions to sequential IR", false, false)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass)
INITIALIZE_PASS_DEPENDENCY(ParallelRegionInfoPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(SequentializePIR, "sequentialize-pir",
                    "Lower parallel regions to sequential IR", false, false)
