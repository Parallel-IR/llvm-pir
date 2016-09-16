//===- PIR/Utils/ParallelUtils.cpp - Parallel IR Utility functions --------===//
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

#include "llvm/Transforms/Utils/ParallelUtils.h"

#include "llvm/IR/Instructions.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ParallelRegionInfo.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"

#define DEBUG_TYPE "parallel-utils"

namespace llvm {

bool isTaskTerminator(TerminatorInst *TI) {
  return isa<ReturnInst>(TI) || isa<HaltInst>(TI) || isa<JoinInst>(TI);
}

Function *outlineTask(ParallelTask &PT, DominatorTree *DT, LoopInfo *LI) {
  SmallVector<BasicBlock *, 32> Blocks;
  Blocks.reserve(PT.size());

  auto IsContained = [PT](BasicBlock *BB) { return PT.contains(BB); };

  ValueToValueMapTy VMap;
  for (auto *BB : PT.blocks()) {
    if (PT.isStartBlock(BB) ||
        std::all_of(pred_begin(BB), pred_end(BB), IsContained)) {
      Blocks.push_back(BB);
      continue;
    }

    SmallVector<BasicBlock *, 8> PredBlocks;
    for (auto *PredBB : predecessors(BB))
      if (PT.contains(PredBB))
        PredBlocks.push_back(PredBB);

    auto *SplitBB = SplitBlockPredecessors(BB, PredBlocks, "_pt", DT, LI);
    assert(SplitBB->size() == 1);
    Blocks.push_back(SplitBB);

    for (auto &Inst : *BB) {
      auto *InstClone = Inst.clone();
      VMap[&Inst] = InstClone;
      InstClone->insertBefore(&SplitBB->back());
    }
    SplitBB->back().eraseFromParent();

    if (!PT.isEndBlock(BB))
      continue;

    for (auto *SuccBB : successors(BB)) {
      if (PT.contains(SuccBB))
        continue;

      auto It = SuccBB->begin();
      while (auto *PHI = dyn_cast<PHINode>(It++))
        PHI->addIncoming(PHI->getIncomingValueForBlock(BB), SplitBB);
    }
  }

  remapInstructionsInBlocks(Blocks, VMap);
  Blocks.front()->getParent()->dump();
  for (auto *BB : Blocks)
    errs() << BB->getName() << "\n";

  CodeExtractor CE(Blocks, DT, LI);
  assert(CE.isEligible() && "Cannot extract task!");
  return CE.extractCodeRegion();
}

Function *getOrCreateFunction(Module &M, StringRef Name, Type *RetTy,
                              ArrayRef<Type *> ArgTypes) {

  Function *F = M.getFunction(Name);

  // If F is not available, declare it.
  if (!F) {
    RetTy = RetTy ? RetTy : Type::getVoidTy(M.getContext());
    for (auto *T : ArgTypes)
      errs() << T << "\n";
    for (auto *T : ArgTypes)
      errs() << *T << " : " << FunctionType::isValidArgumentType(T) << "\n";
    auto *FnTy = FunctionType::get(RetTy, ArgTypes, false);
    F = Function::Create(FnTy, Function::ExternalLinkage, Name, &M);
  }

  return F;
}

void removeTrivialForks(ParallelRegion &PR) {
  SmallVector<ForkInst *, 4> EliminatedForks;

  for (auto *FI : PR.forks()) {
    if (FI->isInterior()) {
      if (FI->getNumSuccessors() != 1)
        continue;
      BranchInst::Create(FI->getSuccessor(0), FI);
      EliminatedForks.push_back(FI);
    }
  }

  for (auto *FI : EliminatedForks)
    PR.eraseFork(FI);
}

void simplifyForks(ParallelRegion &PR) {}

void makeOutgoingCommunicationExplicit(SetVector<BasicBlock *> &Blocks) {
  if (Blocks.empty())
    return;

  auto IP = Blocks[0]->getParent()->getEntryBlock().getFirstInsertionPt();

  for (auto *BB : Blocks)
    for (auto &I : *BB) {
      AllocaInst *AI = nullptr;
      for (const auto &U : I.users()) {
        auto *UI = cast<Instruction>(U);
        if (UI->getParent() == BB || Blocks.count(UI->getParent()))
          continue;

        if (!AI) {
          AI = new AllocaInst(I.getType(), I.getName() + ".storage", &*IP);
          auto *SI = new StoreInst(&I, AI);
          SI->insertAfter(&I);
        }

        auto *LI = new LoadInst(AI, I.getName() + ".load", UI);
        UI->replaceUsesOfWith(&I, LI);
      }
    }
}

void separateTasks(ArrayRef<ParallelTask *> Tasks, DominatorTree *DT,
                   LoopInfo *LI) {
  if (Tasks.empty())
    return;

  auto &PR = Tasks.front()->getParent();
  auto &TasksMap = PR.getTasksMap();

  SetVector<BasicBlock *> Blocks;
  for (auto *Task : Tasks)
    Blocks.insert(Task->begin(), Task->end());
  Blocks[0]->getParent()->dump();

  makeOutgoingCommunicationExplicit(Blocks);
  PR.dump();

  SmallVector<BasicBlock *, 4> RefBBs;
  while (!Blocks.empty()) {
    bool Split = false;
    RefBBs.clear();

    auto *BB = Blocks.pop_back_val();
    errs() << "Check: " << BB->getName() << "\n";

    auto PredIt = pred_begin(BB), PredEnd = pred_end(BB);
    auto TasksMapIt = TasksMap.find(*PredIt++);
    auto TasksMapEnd = TasksMap.end();

    while (TasksMapIt == TasksMapEnd && PredIt != PredEnd) {
      TasksMapIt = TasksMap.find(*PredIt++);
      Split = true;
      errs() << "Split 0\n";
    }

    if (TasksMapIt == TasksMapEnd) {
      errs() << "Skip 0\n";
      continue;
    }

    auto *PredBB = TasksMapIt->getFirst();
    RefBBs.push_back(PredBB);
    errs() << "REF: " << *PredBB << "\n";
    const auto &RefTasks = TasksMap.lookup(PredBB);
    auto InRefTasks = [&RefTasks](ParallelTask *PT) {
      return RefTasks.count(PT);
    };

    while (!Split && PredIt != PredEnd) {
      PredBB = *PredIt++;
      auto PredTasksIt = TasksMap.find(PredBB);
      // This assumes one non-interior fork!
      if (PredTasksIt == TasksMapEnd) {
        errs() << "Split 1\n";
        Split = true;
        break;
      }

      auto &PredTasks = PredTasksIt->getSecond();
      if (RefTasks.size() != PredTasks.size()) {
        errs() << "Split 2 : " << PredBB->getName() << "\n";
        for (auto *RT: RefTasks) {
          errs() << "\tRT: ";RT->print(errs());
        }
        for (auto *RT: PredTasks) {
          errs() << "\tPT: ";RT->print(errs());
        }
        Split = true;
        break;
      }

      if (!std::all_of(PredTasks.begin(), PredTasks.end(), InRefTasks)) {
        errs() << "Split 3\n";
        Split = true;
        break;
      }

      RefBBs.push_back(PredBB);
    }

    if (!Split) {
      errs() << "Skip 1\n";
      continue;
    }

    auto *NewBB = PR.splitBlockPredecessors(BB, RefBBs, DT, LI);
    Blocks.insert(NewBB);
    Blocks.insert(BB);
  }

  PR.dump();
#if 0
  auto *PID = PR.getId();
  ParallelTask *SingleContTask = nullptr;
  TerminatorInst *SingleContTI = nullptr;
  for (auto *Task : Tasks)
    for (auto *ExitBB : Task->getEndBlocks()) {
      auto *TI = ExitBB->getTerminator();
      bool IsContTI = isa<JoinInst>(TI) || isa<ReturnInst>(TI);
      if (IsContTI && (!SingleContTask || SingleContTask == Task) &&
          (!SingleContTI || SingleContTI == TI)) {
        SingleContTI = TI;
        SingleContTask = Task;
      } else {
        HaltInst::Create(PID, TI);
        TI->eraseFromParent();
      }
    }
#endif

}
}
