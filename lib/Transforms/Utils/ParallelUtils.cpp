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
    for (unsigned i = 0; i < Entrys.size(); ++i) {
      if (isa<JoinInst>(Entrys[i]->begin())) {
        assert(!Region2EntryJoin.count(PR) && "Do not handle duplicate fork"
                                              "destinations yet");
        // TODO: Remove additional branches that go directly to join.
        // Easier if removeTask interface for ForkInst is modified.
        Region2EntryJoin[PR] = i;
      }
    }

    if (!Region2EntryJoin.count(PR)) {
      Region2EntryJoin[PR] = 0;
    }
  
    for (BasicBlock *Exit : PR->getExitBlocks()) {
      for (unsigned i = 0; i < Entrys.size(); ++i) {
        if (DT->dominates(Entrys[i], Exit)) {
          Exit2Entry[Exit] = i;
        }
      }
      assert(Exit2Entry.count(Exit) || isa<ForkInst>(Exit->getTerminator()));

      if (isa<HaltInst>(Exit->getTerminator()) && 
          Region2EntryJoin[PR] == Exit2Entry[Exit]) {
        Region2EntryJoin[PR] = (Region2EntryJoin[PR] + 1) % Entrys.size();
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
      SmallVectorImpl<BasicBlock *> &SubEntrys = PR->getEntryBlocks();
      unsigned NewEntry = (Region2EntryJoin[PR] + 1)
        % SubEntrys.size();
      BranchInst::Create(SubEntrys[NewEntry], BB->getTerminator());
      // Fork instructions deleted later
    }

    SmallVectorImpl<BasicBlock *> &Entrys = PR->getEntryBlocks();

    // Check if the last instruction of the block is a halt.
    // If it is, we change it to a jump to the next entry block.
    // TODO: To prevent infinite loops, will need to change one to unreachable
    // if all branches lead to halt.
    if (isa<HaltInst>(BB->getTerminator())) {
      assert(Exit2Entry.count(BB) && "Halt in non-exit block");
      unsigned EntryNum = Exit2Entry[BB];
      assert(EntryNum != Region2EntryJoin[PR]);

      EntryNum = (EntryNum + 1) % (Entrys.size());

      BranchInst::Create(Entrys[EntryNum], BB->getTerminator());
      BB->getTerminator()->eraseFromParent();
      continue;
    }

    // Otherwise, we check if the first instruction of each next block is join.
    // If it is, we change to a jump to the next remaining entry block
    // if this is not the instruction that will leave the entry block.
    TerminatorInst *Branch = BB->getTerminator();
    for (unsigned idx = 0; idx < Branch->getNumSuccessors(); idx++) {
      JoinInst *join = dyn_cast<JoinInst>(&Branch->getSuccessor(idx)->front());
      if (join) {
        assert(PR && "Join instruction must be inside a basic block");
        unsigned EntryNum = Exit2Entry[BB];
        if (EntryNum != Region2EntryJoin[PR]) {
          EntryNum = (EntryNum + 1) % Entrys.size();
          Branch->setSuccessor(idx, Entrys[EntryNum]);

          for (BasicBlock::iterator I = Entrys[EntryNum]->begin();
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

  // Have to wait to delete join and fork instructions
  // because they are used to determine when a parallel region ends.
  ForkInst *FI;
  JoinInst *JI;
  for (auto iter = F.begin(); iter != F.end(); ++iter) {
    if ((JI = dyn_cast<JoinInst>(iter->begin()))) {
      BranchInst::Create(JI->getSuccessor(0), &*iter);
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
