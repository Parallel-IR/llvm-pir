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

#include "llvm/Analysis/LoopInfo.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ParallelUtils.h"

#include <deque>

using namespace llvm;

#define DEBUG_TYPE "parallel-region-info"

STATISTIC(NumParallelRegions, "The # of parallel regions");

//===----------------------------------------------------------------------===//
// ParallelTask implementation
//
ParallelTask::ParallelTask(ParallelRegion &Parent, ForkInst *FI, BasicBlock *EntryBB)
    : Parent(Parent), FI(FI), EntryBB(EntryBB) {}

void ParallelTask::setSingleExit(BasicBlock *BB) {
  EndBlocks.clear();
  EndBlocks.insert(BB);
}

void ParallelTask::addBlock(BasicBlock *BB) {
  Blocks.insert(BB);
  if (isTaskTerminator(BB->getTerminator()))
    EndBlocks.insert(BB);
}

void ParallelTask::eraseBlock(BasicBlock *BB) {
  Blocks.remove(BB);
  if (isTaskTerminator(BB->getTerminator()))
    EndBlocks.erase(BB);
}

bool ParallelTask::isEndBlock(BasicBlock *BB) const {
  return EndBlocks.count(BB);
}

void ParallelTask::print(raw_ostream &OS) const {
  OS << "PT[#B " << Blocks.size() << "][" << *FI << " ]\n";
  for (auto *BB : Blocks)
    OS.indent(8) << "- " << BB->getName()
                 << (isa<ForkInst>(BB->getTerminator())  ? " [fork]" : "")
                 << (BB == EntryBB  ? " [start]" : "")
                 << (isEndBlock(BB) ? " [end]\n" : "\n");
}

//===----------------------------------------------------------------------===//
// ParallelRegion implementation
//

ParallelRegion::ParallelRegion(ParallelRegionInfo &PRI, ForkInst *FI,
                               ParallelRegion *Parent)
    : PRI(PRI), Parent(Parent) {
  NumParallelRegions++;

  addTasks(FI);
  if (Parent)
    Parent->addSubRegion(this);
}

ParallelRegion::ParallelRegion(ParallelRegion &&Other)
    : PRI(Other.PRI), Forks(std::move(Other.Forks)),
      Parent(Other.Parent), TasksMap(std::move(Other.TasksMap)),
      SubRegions(std::move(Other.SubRegions)) {
  if (Parent)
    Parent->addSubRegion(this);
}

ParallelRegion::~ParallelRegion() {}

void ParallelRegion::addSubRegion(ParallelRegion *SubPR) {
  SubRegions.push_back(SubPR);
}

void ParallelRegion::removeSubRegion(ParallelRegion *SubPR) {
  SubRegions.erase(&SubPR);
}

void ParallelRegion::addTasks(ForkInst *FI) {
  assert(FI->isInterior() || !TasksMap.count(FI->getParent()));
  Forks.push_back(FI);
  bool Skip = false; //FI->isInterior();
  for (auto *TaskStartBB : FI->successors()) {
    if (Skip) {
      auto &SuccTasks = TasksMap[TaskStartBB];
      assert(TasksMap.count(FI->getParent()));
      const auto &FITasks = TasksMap.lookup(FI->getParent());
      SuccTasks.insert(FITasks.begin(), FITasks.end());
      Skip = false;
    } else {
      auto *Task = new ParallelTask(*this, FI, TaskStartBB);
      TasksMap[TaskStartBB].insert(Task);
      Tasks.insert(Task);
    }
  }
}

void ParallelRegion::eraseForkSuccessor(ForkInst *FI, BasicBlock *SuccBB) {
  for (auto It = begin(), End = end(); It != End;) {
    if ((*It)->getFork() == FI && (*It)->isStartBlock(SuccBB))
      It = Tasks.erase(It);
    else
      It++;
  }
}

void ParallelRegion::eraseFork(ForkInst *FI) {
  for (auto It = Tasks.begin(); It != Tasks.end();) {
    if ((*It)->getFork() != FI) {
      It++;
      continue;
    }

    for (auto *BB : (*It)->blocks())
      TasksMap[BB].remove(*It);

    It = Tasks.erase(It);
  }

  for (auto It = fork_begin(), End = fork_end(); It != End; It++)
    if (*It == FI) {
      Forks.erase(It);
      break;
    }

  FI->eraseFromParent();
}

void ParallelRegion::addBlock(BasicBlock *BB) {
  auto &BBTasks = TasksMap[BB];
    errs() << "AB " << BB->getName() << "\n";
  for (auto *PredBB : predecessors(BB)) {
    errs() << "-- " << PredBB->getName() << "\n";
    if (isa<ForkInst>(PredBB->getTerminator()))
      continue;
    const auto &PredBBTasks = TasksMap.lookup(PredBB);
    BBTasks.insert(PredBBTasks.begin(), PredBBTasks.end());
  }

  for (auto *Task : BBTasks)
    Task->addBlock(BB);
}

void ParallelRegion::addBlocks(SetVector<BasicBlock *> &Blocks) const {
  for (auto *Task : Tasks)
    Blocks.insert(Task->begin(), Task->end());
  for (auto *SR : SubRegions)
    SR->addBlocks(Blocks);
}

BasicBlock *ParallelRegion::splitBlockPredecessors(BasicBlock *BB,
                                     ArrayRef<BasicBlock *> PredBBs,
                                     DominatorTree *DT,
                                     LoopInfo *LI) {

  dump();
  errs() << "Split: " << *BB << "\n";
  for (auto *PBB : PredBBs)
    errs() << "\t- " << PBB->getName() << "\n";
  assert(TasksMap.count(BB));
  auto *NewBB = SplitBlockPredecessors(BB, PredBBs, "", DT, LI);
  auto *NewBBTI = NewBB->getTerminator();
  assert(isa<BranchInst>(NewBBTI) && NewBBTI->getNumSuccessors() == 1 &&
          NewBBTI->getSuccessor(0) == BB);

  errs() << "NEwBB: " << *NewBB << "\n";
  errs() << "BB: " << *BB << "\n";

  ValueToValueMapTy VMap;
  auto *CloneBB = CloneBasicBlock(BB, VMap, "", BB->getParent());

  for (auto &I : *BB) {
    auto *PHI = dyn_cast<PHINode>(&I);
    if (!PHI)
      break;
    assert(PHI->getBasicBlockIndex(NewBB) >= 0);
    VMap[PHI] = PHI->getIncomingValueForBlock(NewBB);
  }

  SmallVector<BasicBlock *, 1> Blocks({CloneBB});
  remapInstructionsInBlocks(Blocks, VMap);

  while (auto *PHI = dyn_cast<PHINode>(CloneBB->begin()))
    PHI->eraseFromParent();

  for (auto &I : *BB) {
    auto *PHI = dyn_cast<PHINode>(&I);
    if (!PHI)
      break;
    PHI->removeIncomingValue(NewBB, true);
  }

  if (DT)
    DT->addNewBlock(CloneBB, NewBB);
  if (LI)
    LI->changeLoopFor(CloneBB, LI->getLoopFor(BB));
  NewBBTI->setSuccessor(0, CloneBB);

  bool Success = MergeBlockIntoPredecessor(CloneBB, DT, LI);
  assert(Success);

  errs() << "NewBB: " << NewBB->getName() << "\n";
  auto &NewBBTasks = TasksMap[NewBB];
  for (auto *PredBB : PredBBs) {
    errs() << "\tPredBB: " << PredBB->getName() << "\n";
    for (auto *Task : TasksMap.lookup(PredBB)) {
      Task->dump();
      Task->addBlock(NewBB);
      Task->eraseBlock(BB);
      NewBBTasks.insert(Task);
      Task->dump();
    }

    if (auto *FI = dyn_cast<ForkInst>(PredBB->getTerminator())) {
      for (auto *Task : TasksMap.lookup(BB)) {
        if (Task->getFork() != FI)
          continue;
        Task->dump();
        Task->addBlock(NewBB);
        Task->eraseBlock(BB);
        NewBBTasks.insert(Task);
        Task->dump();
      }
    }
  }

  errs() << "BB: " << BB->getName() << "\n";
  auto &BBTasks = TasksMap[BB];
  errs() << "BBTasls:\n";
  for (auto *T : BBTasks)
    T->dump();
  BBTasks.clear();
  for (auto *PredBB : predecessors(BB)) {
    errs() << "\tPredBB: " << PredBB->getName() << "\n";
    for (auto *Task : TasksMap.lookup(PredBB)) {
      Task->dump();
      Task->addBlock(BB);
      BBTasks.insert(Task);
      Task->dump();
    }
  }

  return NewBB;
}

void ParallelRegion::print(raw_ostream &OS) const {
  OS << getName() << "[#SR " << getNumSubRegions() << "]\n";

  for (const auto *Task : *this)
    Task->print(OS.indent(8));

  for (const auto *SR : SubRegions)
    SR->print(OS.indent(8));
}

//===----------------------------------------------------------------------===//
// ParallelRegionInfo implementation
//

ParallelRegionInfo::ParallelRegionInfo() : NextParallelRegionId(0) {}

ParallelRegionInfo::~ParallelRegionInfo() {}

void ParallelRegionInfo::print(raw_ostream &OS) const {
  OS << "Top level parallel regions: " << TopLevelRegions.size() << "\n";
  for (auto *TLR : TopLevelRegions)
    TLR->print(OS.indent(4));
}

void ParallelRegionInfo::eraseParallelRegion(ParallelRegion &PR) {
  auto *ParentPR = PR.getParentRegion();

  SetVector<BasicBlock *> Blocks;
  PR.addBlocks(Blocks);
  for (auto *BB : Blocks) {
    if (ParentPR)
      BB2PRMap[BB] = ParentPR;
    else
      BB2PRMap.erase(BB);
  }

  if (ParentPR)
    ParentPR->removeSubRegion(&PR);
  else {
    for (unsigned u = 0, e = TopLevelRegions.size(); u < e; u++)
      if (TopLevelRegions[u] == &PR) {
        TopLevelRegions.erase(&TopLevelRegions[u]);
        break;
      }
  }

  for (auto *SubPR : PR.sub_regions()) {
    SubPR->setParent(ParentPR);
    if (ParentPR)
      ParentPR->addSubRegion(SubPR);
    else
      TopLevelRegions.push_back(SubPR);
  }

  delete &PR;
}

void ParallelRegionInfo::dump() const { return print(errs()); }

void ParallelRegionInfo::releaseMemory() {
  DeleteContainerPointers(TopLevelRegions);
  TopLevelRegions.clear();
}

void ParallelRegionInfo::recalculate(Function &F, DominatorTree *DT) {
  std::deque<BasicBlock *> OpenBlocks;
  BB2PRMap.clear();

  ReversePostOrderTraversal<Function *> RPOT(&F);
  for (auto It = RPOT.begin(), End = RPOT.end(); It != End; ++It) {
    auto *BB = *It;
    auto *CurPR = BB2PRMap[BB];

    if (CurPR)
      CurPR->addBlock(BB);

    auto *FI = dyn_cast<ForkInst>(BB->getTerminator());
    if (FI) {
      if (FI->isInterior()) {
        CurPR->addTasks(FI);
      } else {
        auto *SubPR = new ParallelRegion(*this, FI, CurPR);
        if (CurPR)
          CurPR->addSubRegion(SubPR);
        else
          TopLevelRegions.push_back(SubPR);
        CurPR = SubPR;
      }
    }

    ParallelRegion *SuccPR = CurPR;
    if (isa<JoinInst>(BB->getTerminator())) {
      assert(CurPR && "Join instruction outside of a parallel region.");
      SuccPR = CurPR->Parent;
    }

    for (auto *SuccBB : successors(BB)) {
      auto *&CurSuccPR = BB2PRMap[SuccBB];
      assert((!CurSuccPR || CurSuccPR == SuccPR) &&
             "Inconsistent path! Different parallel regions for one block!");
      CurSuccPR = SuccPR;
    }
  }
}

//===----------------------------------------------------------------------===//
// ParallelRegionInfoPass implementation
//

ParallelRegionInfoPass::ParallelRegionInfoPass() : FunctionPass(ID) {}

ParallelRegionInfoPass::~ParallelRegionInfoPass() {}

bool ParallelRegionInfoPass::runOnFunction(Function &F) {
  releaseMemory();

  auto DT = &getAnalysis<DominatorTreeWrapperPass>().getDomTree();

  RI.recalculate(F, DT);
  return false;
}

void ParallelRegionInfoPass::releaseMemory() { RI.releaseMemory(); }

void ParallelRegionInfoPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired<DominatorTreeWrapperPass>();

  AU.setPreservesAll();
}

void ParallelRegionInfoPass::print(raw_ostream &OS, const Module *) const {
  RI.print(OS);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void ParallelRegionInfoPass::dump() const { RI.dump(); }
#endif

char ParallelRegionInfoPass::ID = 0;

INITIALIZE_PASS_BEGIN(ParallelRegionInfoPass, "parallel-regions",
                      "Detect parallel regions", false, true)
INITIALIZE_PASS_DEPENDENCY(LoopInfoWrapperPass);
INITIALIZE_PASS_DEPENDENCY(DominatorTreeWrapperPass)
INITIALIZE_PASS_END(ParallelRegionInfoPass, "parallel-regions",
                    "Detect parallel regions", false, true)

// Create methods available outside of this file, to use them
// "include/llvm/LinkAllPasses.h". Otherwise the pass would be deleted by
// the link time optimization.

namespace llvm {
FunctionPass *createParallelRegionInfoPass() {
  return new ParallelRegionInfoPass();
}
}
