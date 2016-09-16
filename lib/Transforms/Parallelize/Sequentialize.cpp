//===------------------- Sequentializing PIR backend ----------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Parallelize/Sequentialize.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ParallelRegionInfo.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/Transforms/Utils/ParallelUtils.h"

#include <deque>

using namespace llvm;

#define DEBUG_TYPE "flatten-pir"

int SequentializingBackend::getScore(ParallelRegion *PR, ForkInst *FI) const {
  int Score = 10;

  if (FI)
    return FI->canBeSequentialized() ? Score : -1;

  if (!PR)
    return Score;

  // TODO: Check all fork inst in PR

  return Score;
}

static void removeFalsePredecessor(BasicBlock *removeFrom, BasicBlock *remove) {
  for (BasicBlock::iterator I = removeFrom->begin(); isa<PHINode>(I);) {
    PHINode *PN = cast<PHINode>(I);
    ++I;
    assert(PN->getBasicBlockIndex(remove) >= 0 &&
           "Even though not a predecessor, still in PHI.");
    PN->removeIncomingValue(remove);
  }
}

static std::pair<BasicBlock *, BasicBlock *>
createTaskLoop(ParallelTask *Task, unsigned NumExecutions,
               SmallVector<BasicBlock *, 8> &EndBlocks, DominatorTree &DT,
               LoopInfo &LI) {
  Task->dump();
  auto *TaskBB = *Task->begin();
  auto &Ctx = TaskBB->getContext();
  auto *HeaderBB = BasicBlock::Create(Ctx, "task_header", TaskBB->getParent(), TaskBB);
  auto *ExitBB = BasicBlock::Create(Ctx, "task_exit", TaskBB->getParent(), TaskBB);
  BranchInst::Create(ExitBB, ExitBB);

  const auto &TaskEndBlocks = Task->getEndBlocks();

  auto *IntTy = Type::getInt32Ty(Ctx);
  auto *PHI = PHINode::Create(IntTy, EndBlocks.size() + TaskEndBlocks.size(), "",
                              HeaderBB);

  HeaderBB->getParent()->dump();
  errs() << HeaderBB->getName() << "\n";
  errs() << ExitBB->getName() << "\n";
  for (auto *EndBlock : EndBlocks) {
    errs() << "endBB: " << EndBlock->getName() << "\n";
    PHI->addIncoming(ConstantInt::getNullValue(IntTy), EndBlock);
  }

  auto *Inc = BinaryOperator::CreateAdd(PHI, ConstantInt::get(IntTy, 1));
  Inc->insertAfter(PHI);

  for (auto *TaskEndBlock : TaskEndBlocks) {
  errs() << "TaskendBB: " << TaskEndBlock->getName() << "\n";
    PHI->addIncoming(Inc, TaskEndBlock);
    EndBlocks.push_back(TaskEndBlock);
  }


  auto *Cmp =
      ICmpInst::Create(Instruction::ICmp, ICmpInst::ICMP_NE, PHI,
                       ConstantInt::get(IntTy, NumExecutions), "", HeaderBB);
  BranchInst::Create(TaskBB, ExitBB, Cmp, HeaderBB);

  HeaderBB->getParent()->dump();

  return std::make_pair(HeaderBB, ExitBB);
}

bool SequentializingBackend::runOnParallelRegion(ParallelRegion &PR,
                                                 ForkInst &FI,
                                                 DominatorTree &DT,
                                                 LoopInfo &LI) {
  if (PR.empty())
    return false;

  bool IsInterior = FI.isInterior();
  SmallVector<ParallelTask *, 8> Tasks;
  for (auto *Task : PR.tasks()) {
    if (Task->getFork() != &FI)
      continue;

    Tasks.push_back(Task);
  }
  assert(!Tasks.empty());
  assert(Tasks.size() == FI.getNumSuccessors());

  auto *M = FI.getModule();
  M->dump();

  errs() << "\n\nFI:" << FI << "\n";
  errs() << "#Tasks: " << Tasks.size() << "\n";
  SmallVector<ParallelTask *, 8> Tasks2(PR.tasks());
  separateTasks(Tasks2, &DT, &LI);

  assert(getScore(&PR, &FI) >= 0);
  assert(PR.size() > 0);

  DenseMap<BasicBlock *, unsigned> NumTasksPerBlock;
  for (auto *Task : Tasks)
    NumTasksPerBlock[*Task->begin()]++;

  TerminatorInst *ContinuationTI = nullptr;
  SmallVector<BasicBlock *, 8> EndBlocks;
  EndBlocks.push_back(FI.getParent());

  for (auto *Task : Tasks) {
    auto *TaskEntryBB = *Task->begin();
    errs() << TaskEntryBB->getName() << " ##: " << NumTasksPerBlock.lookup(TaskEntryBB) << "\n";
    if (NumTasksPerBlock.lookup(TaskEntryBB) == 0)
      continue;

    BasicBlock *ExitBB = nullptr;
    unsigned &NumExecutions = NumTasksPerBlock[TaskEntryBB];
    errs() << "#E: " << NumExecutions << "\n";
    if (NumExecutions > 1) {
      auto HeaderExitPair = createTaskLoop(Task, NumExecutions, EndBlocks, DT, LI);
      TaskEntryBB = HeaderExitPair.first;
      ExitBB = HeaderExitPair.second;
      NumExecutions = 0;
    }

    for (auto *EndBlock : EndBlocks) {
      errs() << "END BLOCK : "<< EndBlock->getName() << "\n";
      EndBlock->dump();
      auto *TI = EndBlock->getTerminator();
      BranchInst::Create(TaskEntryBB, TI);

      if (TI == &FI)
        PR.eraseFork(&FI);
      else if (!ContinuationTI && (isa<ReturnInst>(TI) || isa<JoinInst>(TI)))
        ContinuationTI = TI;
      else
        TI->eraseFromParent();
    }

    EndBlocks.clear();
    if (ExitBB) {
      PR.addBlock(TaskEntryBB);
      PR.addBlock(ExitBB);
      for (auto *T : PR.getTasksMap().lookup(ExitBB))
        T->setSingleExit(ExitBB);
      EndBlocks.push_back(ExitBB);
    }else
      EndBlocks.append(Task->getEndBlocks().begin(), Task->getEndBlocks().end());
  }

  for (auto *EndBlock : EndBlocks) {
    errs() << "END BLOCK : "<< EndBlock->getName() << "\n";
    auto *TI = EndBlock->getTerminator();

    if (TI == &FI)
      PR.eraseFork(&FI);
    else if (!ContinuationTI && (isa<ReturnInst>(TI) || isa<JoinInst>(TI)))
      ContinuationTI = TI;
    else
      TI->eraseFromParent();
  }

  if (EndBlocks.empty())
    return true;

  errs() << "ContinuationTI: " << ContinuationTI << "\n";
  if (ContinuationTI)
    errs() << "ContinuationTI: " << *ContinuationTI << "\n";
  if (!ContinuationTI)
    ContinuationTI =
        new UnreachableInst(EndBlocks.front()->getContext(), EndBlocks.front());
  else if (isa<JoinInst>(ContinuationTI)) {
    if (!IsInterior) {
      auto *NewContTI = BranchInst::Create(ContinuationTI->getSuccessor(0), ContinuationTI);
      ContinuationTI->eraseFromParent();
      ContinuationTI = NewContTI;
    }
  } else {
    assert(isa<ReturnInst>(ContinuationTI));
  }

  ContinuationTI->getFunction()->dump();
  errs() << "CTI: " << *ContinuationTI << "\n";

  for (auto *EndBlock : EndBlocks) {
    errs() << "END BLOCK : "<< EndBlock->getName() << "\n";
    EndBlock->dump();
    auto *NewTI = ContinuationTI->clone();
    BranchInst *B = nullptr;
    if (EndBlock->empty())
      B = BranchInst::Create(EndBlock, EndBlock);
    NewTI->insertAfter(&EndBlock->back());
    if (B)
      B->eraseFromParent();
  }

  ContinuationTI->eraseFromParent();

  M->dump();
  PR.dump();
  return true;
}

static inline unsigned minDepth(BasicBlock *BB,
                                DenseMap<BasicBlock *, unsigned> &Block2Depth) {
  unsigned minDepth = UINT_MAX;
  for (pred_iterator PI = pred_begin(BB), E = pred_end(BB); PI != E; ++PI) {
    if (Block2Depth.count(*PI) && Block2Depth[*PI] < minDepth) {
      minDepth = Block2Depth[*PI];
    }
  }

  if (minDepth == UINT_MAX) {
    return 0;
  }

  return minDepth + 1;
}

static inline void remapInstruction(Instruction *I,
                                    DenseSet<BasicBlock *> &PrevBlocks,
                                    DenseSet<BasicBlock *> &NewBlocks,
                                    ValueToValueMapTy &VMap,
                                    DenseMap<BasicBlock *, unsigned> &Blck2D) {
  PHINode *PN = dyn_cast<PHINode>(I);
  if (PN) {
    for (unsigned idx = 0; idx < PN->getNumIncomingValues(); ++idx) {
      BasicBlock *Incoming = PN->getIncomingBlock(idx);
      ValueToValueMapTy::iterator InIt = VMap.find(Incoming);
      if (InIt != VMap.end()) {
        Incoming = cast<BasicBlock>(InIt->second);
        if (PN->getBasicBlockIndex(Incoming) >= 0) {
          PN->removeIncomingValue(idx);
          --idx;
          continue;
        }
        PN->setIncomingBlock(idx, Incoming);
      } else if (!PrevBlocks.count(Incoming) && !NewBlocks.count(Incoming)) {
        PN->removeIncomingValue(idx);
        --idx;
        continue;
      }

      ValueToValueMapTy::iterator ValIt = VMap.find(PN->getIncomingValue(idx));
      if (ValIt != VMap.end()) {
        Instruction *Val = dyn_cast<Instruction>(ValIt->second);
        if (!Val || (!Blck2D.count(Val->getParent()) ||
                     (Blck2D[Incoming] >= Blck2D[Val->getParent()]))) {
          PN->setIncomingValue(idx, ValIt->second);
        }
      }
    }
  } else {
    for (unsigned op = 0, E = I->getNumOperands(); op != E; ++op) {
      Value *Op = I->getOperand(op);
      ValueToValueMapTy::iterator It = VMap.find(Op);
      if (It != VMap.end()) {
        I->setOperand(op, It->second);
      }
    }
  }
}

// All basic blocks must be copied!
// Returns the Basic Block executed after the parallel region.
BasicBlock *SequentializeRegion(BasicBlock *regionStart) {
  SmallVector<BasicBlock *, 4> EntryBlocks;
  SmallVector<BasicBlock *, 4> OrigEntrys;

  // Keep track of clone values for remapping.
  // Each entry point has its own value map.
  SmallVector<ValueToValueMapTy *, 4> ValueMaps;

  // Blocks remaining to be processed.
  std::deque<BasicBlock *> OpenBlocks;

  // Prevent infinite loops by keeping track of where we've been.
  DenseSet<BasicBlock *> VisitedBlocks;

  // Keep track of clone blocks whose instructions need to be remapped.
  DenseSet<BasicBlock *> NewBlocks;

  // These are the only basic blocks that know about copies from a different
  // entry point.
  DenseSet<BasicBlock *> Prev;
  Prev.insert(regionStart);

  // Keep track of what depth in the tree copies are.
  // TODO: This is a hack to prevent overwriting a PHI node to use something
  // that does not dominate its use. Either prove this is a valid way of
  // precisely avoiding such situations or, better, update dominator information
  // and use that instead.
  DenseMap<BasicBlock *, unsigned> Block2Depth;

  // Used as exit if every path ends in a halt.
  BasicBlock *exit = BasicBlock::Create(regionStart->getContext(), "AllHalt",
                                        regionStart->getParent());
  new UnreachableInst(regionStart->getContext(), exit);

  ForkInst *regTerm = dyn_cast<ForkInst>(regionStart->getTerminator());
  assert(regTerm && "Should only be called on a block that ends with a fork.");

  // First, copy all of the entry blocks.
  for (unsigned i = 0; i < regTerm->getNumSuccessors(); ++i) {
    BasicBlock *successor = regTerm->getSuccessor(i);
    if (successor == regionStart ||
        (isa<JoinInst>(successor->getTerminator()) &&
         isa<UnreachableInst>(exit->getTerminator()))) {
      exit = successor;
    } else if (!isa<JoinInst>(successor->getTerminator())) {
      if (isa<ForkInst>(successor->getTerminator())) {
        SequentializeRegion(successor);
      }
      ValueToValueMapTy *vmap = new ValueToValueMapTy();
      BasicBlock *newEntry = CloneBasicBlock(successor, *vmap, "forkChild");
      regionStart->getParent()->getBasicBlockList().push_back(newEntry);
      EntryBlocks.push_back(newEntry);
      OrigEntrys.push_back(successor);
      ValueMaps.push_back(vmap);
    }
  }

  for (BasicBlock *OrigEntry : OrigEntrys) {
    OrigEntry->removePredecessor(regionStart, true);
  }

  // Then, handle all of the children.
  for (unsigned i = 0; i < EntryBlocks.size(); ++i) {
    BasicBlock *BB = EntryBlocks[i];
    ValueToValueMapTy *vmap = ValueMaps[i];
    BasicBlock *end = exit;
    if (i < EntryBlocks.size() - 1) {
      end = EntryBlocks[i + 1];
    }

    OpenBlocks.clear();
    NewBlocks.clear();
    Block2Depth.clear();
    VisitedBlocks.clear();

    VisitedBlocks.insert(regionStart);
    VisitedBlocks.insert(OrigEntrys[i]);
    VisitedBlocks.insert(BB);
    VisitedBlocks.insert(end);

    (*vmap)[OrigEntrys[i]] = BB;
    Block2Depth[BB] = 1;
    Block2Depth[OrigEntrys[i]] = 1;
    Block2Depth[regionStart] = 0;

    NewBlocks.insert(BB);
    OpenBlocks.push_back(BB);
    while (!OpenBlocks.empty()) {
      BB = OpenBlocks.front();
      OpenBlocks.pop_front();

      if (isa<HaltInst>(BB->getTerminator())) {
        if (!isa<UnreachableInst>(end->getTerminator()) ||
            !regTerm->isInterior()) {
          ReplaceInstWithInst(BB->getTerminator(), BranchInst::Create(end));
          if (i < EntryBlocks.size()) {
            Prev.insert(BB);
            PHINode *PN;
            for (BasicBlock::iterator I = end->begin();
                 (PN = dyn_cast<PHINode>(I)); ++I) {
              PN->addIncoming(PN->getIncomingValueForBlock(regionStart), BB);
            }
          }
        }
        continue;
      }

      TerminatorInst *Term = BB->getTerminator();
      for (unsigned idx = 0; idx < Term->getNumSuccessors(); ++idx) {
        BasicBlock *successor = Term->getSuccessor(idx);
        TerminatorInst *succTerm = successor->getTerminator();

        if (successor == regionStart ||
            (isa<JoinInst>(succTerm) &&
             isa<UnreachableInst>(exit->getTerminator()))) {
          // For fork interior, delay the join so the next call up on the
          // stack can deal with it correctly.
          if (successor != regionStart && regTerm->isInterior()) {
            exit = BasicBlock::Create(successor->getContext(), "delayJoin",
                                      successor->getParent(), successor);
            BranchInst::Create(successor, exit);
            Term->setSuccessor(idx, exit);
          } else {
            exit = successor;
          }
        } else if (!isa<JoinInst>(succTerm) &&
                   VisitedBlocks.insert(successor).second) {
          ForkInst *SubFork = dyn_cast<ForkInst>(successor->getTerminator());
          if (SubFork) {
            // Any already made copies of successors children will not know
            // the block has rewired their uses.
            for (BasicBlock *SubSucc : successors(successor)) {
              if (vmap->count(SubSucc)) {
                BasicBlock *copy = dyn_cast<BasicBlock>((*vmap)[SubSucc]);
                if (copy) {
                  removeFalsePredecessor(copy, successor);
                }
              }
            }
            SequentializeRegion(successor);
          }

          BasicBlock *newSuccessor = CloneBasicBlock(successor, *vmap);
          regionStart->getParent()->getBasicBlockList().push_back(newSuccessor);
          (*vmap)[successor] = newSuccessor;
          NewBlocks.insert(newSuccessor);
          OpenBlocks.push_back(newSuccessor);

          unsigned depth = minDepth(successor, Block2Depth);
          Block2Depth[successor] = depth;
          Block2Depth[newSuccessor] = depth;
        }

        if (i < EntryBlocks.size() - 1 &&
            (isa<JoinInst>(successor->getTerminator()) ||
             successor == regionStart)) {
          Prev.insert(BB);
          Term->setSuccessor(idx, end);

          PHINode *PN;
          for (BasicBlock::iterator I = end->begin();
               (PN = dyn_cast<PHINode>(I)); ++I) {
            PN->addIncoming(PN->getIncomingValueForBlock(regionStart), BB);
          }
        }
      }
    }

    for (BasicBlock *New : NewBlocks)
      for (Instruction &I : *(New))
        ::remapInstruction(&I, Prev, NewBlocks, *vmap, Block2Depth);
  }

  // Remove the join instruction that exits the parallel region.
  // On a fork interior instruction the fork that starts its parallel region
  // will handle this.
  TerminatorInst *EndTerm = exit->getTerminator();
  if (!(regTerm->isInterior()) && isa<JoinInst>(EndTerm)) {
    BranchInst::Create(EndTerm->getSuccessor(0), EndTerm);
    EndTerm->eraseFromParent();
  }

  // Only the first EntryBlock is visited by regionStart, so clear up PHIs.
  // Don't do this until all of the new predecessors have added their PHIs
  for (unsigned idx = 1; idx < EntryBlocks.size(); idx++) {
    removeFalsePredecessor(EntryBlocks[idx], regionStart);
  }

  // Rewire regionStart to point to the first entry.
  if (EntryBlocks.size() > 0) {
    BranchInst::Create(EntryBlocks[0], regTerm);
  } else {
    BranchInst::Create(exit, regTerm);
  }
  regTerm->eraseFromParent();

  for (ValueToValueMapTy *vmap : ValueMaps) {
    delete vmap;
  }

  return exit;
}

// This makes the assumption that the passed in function is well-formed.
// It seems reasonable that the compilation routine would check this, just
// like it currently checks for well-formed PHIs, etc., but there is not
// currently such a check.
// Adding a dependency on the ParallelRegionInfoPass and reporting an error
// if it finds an issue could also avoid this assumption
#if 0
  bool modified = false;

  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  auto &PRI = getAnalysis<ParallelRegionInfoPass>().getParallelRegionInfo();
  for (auto *PR : PRI)
    modified |= runOnParallelRegion(*PR, DT);

  F.getParent()->dump();
  return modified;

  bool modified = false;

  std::deque<BasicBlock *> OpenBlocks;
  DenseSet<BasicBlock *> SeenBlocks;

  OpenBlocks.push_back(&F.getEntryBlock());
  while (!OpenBlocks.empty()) {
    BasicBlock *BB = OpenBlocks.front();
    OpenBlocks.pop_front();

    if (!SeenBlocks.insert(BB).second) {
      continue;
    }

    ForkInst *fork = dyn_cast<ForkInst>(BB->getTerminator());
    if (fork) {
      modified = true;
      BB = SequentializeRegion(BB);
      OpenBlocks.push_back(BB);
    } else {
      for (BasicBlock *successor : successors(BB)) {
        OpenBlocks.push_back(successor);
      }
    }
  }

  if (modified) {
    // There are some cases where different calls to SequentializeRegion can
    // take a block down to 0 predecessors without realizing it.
    // Clear up any resulting empty PHIs.
    PHINode *PN;
    for (BasicBlock &BB : F) {
      BasicBlock::iterator it = BB.begin();
      while ((PN = dyn_cast<PHINode>(it))) {
        ++it;
        if (PN->getNumIncomingValues() == 0) {
          PN->replaceAllUsesWith(UndefValue::get(PN->getType()));
          PN->eraseFromParent();
        }
      }
    }
  }

  return modified;
#endif
