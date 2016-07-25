//===-- ParallelUtils.cpp - Parallel Utility functions -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines common parallel utility functions.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ParallelRegionInfo.h"
#include "llvm/Transforms/Utils/ParallelUtils.h"

#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

#include <deque>

using namespace llvm;

#define DEBUG_TYPE "parallel-utils"

SequentializeParallelRegions::SequentializeParallelRegions() :
  FunctionPass(ID) {
  initializeSequentializeParallelRegionsPass(*PassRegistry::getPassRegistry());
}

SequentializeParallelRegions::~SequentializeParallelRegions() {}

bool SequentializeParallelRegions::runOnFunction(Function &F) {
  ParallelRegionInfo *PRI =
      &(getAnalysis<ParallelRegionInfoPass>().getParallelRegionInfo());
  if(PRI->TopLevelRegions.empty()) {
    return false;
  }

  auto DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();

  std::deque<BasicBlock *> OpenBlocks;
  std::deque<ParallelRegion *> OpenRegions;
  DenseMap<BasicBlock *, unsigned> Exit2Entry;
  DenseMap<ParallelRegion *, unsigned> Region2EntryJoin;
  DenseSet<BasicBlock *> SeenBlocks;

  BasicBlock* BB;
  ParallelRegion* PR;

  // Figure out which exit block ends which entry block.
  for (ParallelRegion *SubRegion : PRI->TopLevelRegions) {
    OpenRegions.push_back(SubRegion);
  }

  // TODO: Handle fork interior
  while(!OpenRegions.empty()) {
    PR = OpenRegions.back();
    OpenRegions.pop_back();

    for(ParallelRegion *SubRegion : PR->getSubRegions()) {
      OpenRegions.push_back(SubRegion);
    }

    SmallVectorImpl<BasicBlock *> &Entrys = PR->getEntryBlocks();
    for (BasicBlock *Exit : PR->getExitBlocks()) {
      for (unsigned i = 0; i < Entrys.size(); ++i) {
        if (DT->dominates(Entrys[i], Exit)) {
          Exit2Entry[Exit] = i;
        }
      }
      assert(Exit2Entry.count(Exit) || isa<ForkInst>(Exit->getTerminator()));

      // TODO: Replace second condition with checking terminator for halt
      if (!Region2EntryJoin.count(PR) && true) {
        Region2EntryJoin[PR] = Exit2Entry[Exit];
      }
    }

    // If fork jumps directly to a join, that should be the last branch taken.
    // TODO: Figure out how to make this not a speccial case.
    for (unsigned i = 0; i < Entrys.size(); ++i) {
      if (isa<JoinInst>(Entrys[i]->begin())) {
        Region2EntryJoin[PR] = i;
      }
    }
  }


  OpenBlocks.push_back(&F.getEntryBlock());
  while (!OpenBlocks.empty()) {
    BB = OpenBlocks.front();
    PR = PRI->BB2PRMap[BB];
    OpenBlocks.pop_front();

    if (!SeenBlocks.insert(BB).second) {
      continue;
    }

    // First, check to see if the last instruction of the block is a fork.
    // If it is, change it to a direct branch to the enty block AFTER
    // the one that will exit.
    // TODO: Handle fork interior.
    ForkInst *fork = dyn_cast<ForkInst>(BB->getTerminator());
    if (fork) {
      PR = PRI->getForkedRegion(fork);
      assert(PR);
      SmallVectorImpl<BasicBlock *> &Entrys = PR->getEntryBlocks();
      unsigned NewEntry = (Region2EntryJoin[PR] + 1)
        % Entrys.size();
      BranchInst::Create(Entrys[NewEntry], BB->getTerminator());
      OpenBlocks.push_back(Entrys[NewEntry]);
      // Fork instructions deleted later
    }

    // TODO: Check for halt.
    // Then, we check if the last instruction of the block is a halt.
    // If it is, we change it to a jump to the next entry block.
    // To prevent infinite loops, will need to change one to unreachable inst
    // if all branches lead to halt.

    // Otherwise, we check if the first instruction of each next block is join.
    // If it is, we change to a jump to the next remaining entry block
    // if this is not the instruction that will leave the entry block.
    BranchInst *Branch = dyn_cast<BranchInst>(BB->getTerminator());
    if (Branch) {
      SmallVectorImpl<BasicBlock *> &Entrys = PR->getEntryBlocks();
      for (unsigned idx = 0; idx < Branch->getNumSuccessors(); idx++) {
        if (dyn_cast<JoinInst>(&Branch->getSuccessor(idx)->front())) {
          assert(PR && "Join instruction must be inside a basic block");
          unsigned EntryNum = Exit2Entry[BB];
          if (EntryNum != Region2EntryJoin[PR]) {
            unsigned nextBlock = (EntryNum + 1) % Entrys.size();
            Branch->setSuccessor(idx, Entrys[nextBlock]);

            // TODO: Handle PHI nodes
            for (BasicBlock::iterator I = Entrys[nextBlock]->begin();
                 isa<PHINode>(I); ++I) {
              PHINode *PN = cast<PHINode>(I);
              PN->addIncoming(PN->getIncomingValueForBlock(
                              PR->getEntryFork()->getParent()), BB);
            }

          }
        }
        OpenBlocks.push_back(Branch->getSuccessor(idx));
      }
    }
  }


  // Have to wait to delete join and fork instructions
  // because they are used to determine when a parallel region ends.
  ForkInst *FI;
  for (auto iter = F.begin(); iter != F.end(); ++iter) {
    if (dyn_cast<JoinInst>(iter->begin())) {
      iter->begin()->eraseFromParent();
    }

    if ((FI = dyn_cast<ForkInst>(iter->getTerminator()))) {
      ParallelRegion *PR = PRI->getForkedRegion(FI);
      // Clear up PHI Nodes
      unsigned keepIndex = (Region2EntryJoin[PR] + 1) %
                           PR->getEntryBlocks().size();
      for (unsigned i = 0; i < FI->getNumSuccessors(); ++i) {
        if (i != keepIndex) {
          FI->getSuccessor(i)->removePredecessor(&(*iter));
        }
      }
      FI->eraseFromParent();
    }
  }

  return true;
}

void SequentializeParallelRegions::releaseMemory() {
}

void SequentializeParallelRegions::print(raw_ostream &, const Module *) const {
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void SequentializeParallelRegions::dump() const {}
#endif

void SequentializeParallelRegions::getAnalysisUsage(AnalysisUsage &AU)
  const {
  AU.addRequiredTransitive<ParallelRegionInfoPass>();
  AU.addRequiredTransitive<DominatorTreeWrapperPass>();
}

char SequentializeParallelRegions::ID = 0;

INITIALIZE_PASS_BEGIN(SequentializeParallelRegions, "sequentialize-regions",
                      "Remove all fork and join instructions", true, false)
INITIALIZE_PASS_DEPENDENCY(ParallelRegionInfoPass)
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(SequentializeParallelRegions, "sequentialize-regions",
                    "Remove all fork and join instructions", true, false)

namespace llvm {
FunctionPass *createSequentializeParallelRegionsPass() {
  return new SequentializeParallelRegions();
}
}
