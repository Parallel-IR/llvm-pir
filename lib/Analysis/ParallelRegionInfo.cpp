//===- ParallelRegionInfo.cpp - Parallel region detection analysis --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ParallelRegionInfo.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"

#include <deque>

using namespace llvm;

#define DEBUG_TYPE "parallel-region-info"

STATISTIC(NumParallelRegions, "The # of parallel regions");

//===----------------------------------------------------------------------===//
// ParallelRegion implementation
//

ParallelRegion::ParallelRegion(ParallelRegionInfo *RI, DominatorTree *DT,
                               unsigned id, ForkInst* fork, ParallelRegion *parent)
    : Fork(fork), Parent(parent), ParallelRegionID(id) {}

ParallelRegion::~ParallelRegion() {
  /* NOTE: This assumes subegions are only subegions of depth 1 less
     To fix this, check depth first. */
  DeleteContainerPointers(SubRegions);
}

//===----------------------------------------------------------------------===//
// ParallelRegionInfo implementation
//

ParallelRegionInfo::ParallelRegionInfo() : NextParallelRegionId(0) {}

ParallelRegionInfo::~ParallelRegionInfo() {}

void ParallelRegionInfo::print(raw_ostream &OS) const { OS << "PIR\n"; }

void ParallelRegionInfo::dump() const { return print(errs()); }

void ParallelRegionInfo::releaseMemory() {
  DeleteContainerPointers(TopLevelRegions);
}

void ParallelRegionInfo::recalculate(Function &F, DominatorTree *DT) {
  unsigned regionID = 1;
  std::deque<BasicBlock *> OpenBlocks;
  DenseSet<BasicBlock *> SeenBlocks;
  BB2PRMap.clear();
  Fork2PRMap.clear();

  BasicBlock *EntryBB = &F.getEntryBlock();
  OpenBlocks.push_back(EntryBB);
  BB2PRMap[EntryBB];
  while (!OpenBlocks.empty()) {
    BasicBlock *BB = OpenBlocks.front();
    OpenBlocks.pop_front();

    if (!SeenBlocks.insert(BB).second)
      continue;

    ParallelRegion *curPR = BB2PRMap[BB];

    if (curPR && (isa<ReturnInst>(BB->getTerminator()) ||
                  isa<HaltInst>(BB->getTerminator()))) {
      curPR->addExitBlockToRegion(BB);
    }

    ForkInst *FI = dyn_cast<ForkInst>(BB->getTerminator());
    if (FI && !(FI->isInterior())) {
      ParallelRegion *subRegion = new ParallelRegion(this, DT, regionID,
                                                     FI, curPR);
      ++regionID;

      if (!curPR) {
        TopLevelRegions.push_back(subRegion);
      } else {
        curPR->addSubRegion(subRegion);
      }
      curPR = subRegion;
      Fork2PRMap[FI] = subRegion;
    }

    for (BasicBlock *SuccBB : successors(BB)) {
      if (isa<JoinInst>(SuccBB->front())) {
        assert(curPR && "Join instruction outside of a parallel region.");
        assert(!BB2PRMap.count(SuccBB) || (BB2PRMap[SuccBB] == curPR->Parent &&
                                           "Inconsistent CFG, paths to the "
                                           "same join with different depths."));
        curPR->addExitBlockToRegion(BB);
        BB2PRMap[SuccBB] = curPR->Parent;
      } else {
        BB2PRMap[SuccBB] = curPR;
      }

      if (FI) {
        curPR->addEntryBlockToRegion(SuccBB);
      }
      OpenBlocks.push_back(SuccBB);
    }
  }

  for (auto &It : BB2PRMap) {
    ParallelRegion *region = It.second;
    while (region != nullptr) {
      region->Blocks.push_back(It.first);
      region = region->Parent;
    }
  }

  std::deque<ParallelRegion *> PrintRegions;
  for (ParallelRegion *region : TopLevelRegions)
    PrintRegions.push_back(region);

  while (!PrintRegions.empty()) {
    ParallelRegion *region = PrintRegions.front();
    PrintRegions.pop_front();

    errs() << "PR: ";
    if (region->ParallelRegionID) {
      errs() << (region->ParallelRegionID);
    }
    errs() << "\n";

    for (const auto &BB : region->Blocks) {
      errs().indent(4) << BB->getName() << "\n";
    }

    for (const auto &It : region->getSubRegions()) {
      errs() << (It->ParallelRegionID) << "\t";
      PrintRegions.push_back(It);
    }
    errs() << "\n";
  }
}

ParallelRegion *ParallelRegionInfo::getForkedRegion(Instruction *I) {
  return Fork2PRMap[I];
}

//===----------------------------------------------------------------------===//
// ParallelRegionInfoPass implementation
//

ParallelRegionInfoPass::ParallelRegionInfoPass() : FunctionPass(ID) {
  initializeParallelRegionInfoPassPass(*PassRegistry::getPassRegistry());
}

ParallelRegionInfoPass::~ParallelRegionInfoPass() {}

bool ParallelRegionInfoPass::runOnFunction(Function &F) {
  releaseMemory();

  auto DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();

  RI.recalculate(F, DT);
  return false;
}

void ParallelRegionInfoPass::releaseMemory() { RI.releaseMemory(); }

void ParallelRegionInfoPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequiredTransitive<DominatorTreeWrapperPass>();
}

void ParallelRegionInfoPass::print(raw_ostream &OS, const Module *) const {
  RI.print(OS);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void ParallelRegionInfoPass::dump() const { RI.dump(); }
#endif

char ParallelRegionInfoPass::ID = 0;

INITIALIZE_PASS_BEGIN(ParallelRegionInfoPass, "parallel-regions",
                      "Detect parallel regions", true, true)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(ParallelRegionInfoPass, "parallel-regions",
                    "Detect parallel regions", true, true)

// Create methods available outside of this file, to use them
// "include/llvm/LinkAllPasses.h". Otherwise the pass would be deleted by
// the link time optimization.

namespace llvm {
FunctionPass *createParallelRegionInfoPass() {
  return new ParallelRegionInfoPass();
}
}
