//===- ParallelRegionInfo.cpp - Parallel region detection analysis --------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Implementation of the ParallelRegionInfo analysis as well as the ParallelTask
// and ParallelRegion class.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/ParallelRegionInfo.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/LoopInfo.h"

using namespace llvm;

#define DEBUG_TYPE "parallel-region-info"

STATISTIC(NumParallelRegions, "The # of parallel regions");
STATISTIC(NumJoinInstructions, "The # of join instructions");
STATISTIC(NumHaltInstructions, "The # of halt instructions");

//===----------------------------------------------------------------------===//
// ParallelTask implementation
//

ParallelTask::ParallelTask(ParallelRegion &ParentRegion, BasicBlock &EntryBB)
    : ParentRegion(ParentRegion), EntryBB(EntryBB) {}

void ParallelTask::addHaltOrJoin(TerminatorInst &TI) {
  assert((!isForkedTask() || isa<HaltInst>(TI)) &&
         "Expected a halt instruction for a forked task!");
  assert((!isContinuationTask() || isa<JoinInst>(TI)) &&
         "Expected a join instruction for a continuation task!");
  HaltsOrJoints.insert(&TI);
}

bool ParallelTask::isForkedTask() const {
  return &ParentRegion.getForkedTask() == this;
}

bool ParallelTask::isContinuationTask() const { return !isForkedTask(); }

ParallelTask &ParallelTask::getSiblingTask() const {
  return isForkedTask() ? ParentRegion.getContinuationTask()
                        : ParentRegion.getForkedTask();
}

bool ParallelTask::contains(const BasicBlock *BB,
                            const DominatorTree &DT) const {
  if (!mayContain(BB, DT))
    return false;
  if (hasSingleExit())
    return true;

  // Fallback to a search of all blocks in this task.
  VisitorTy IsNotBB = [BB](BasicBlock &CurrentBB, const ParallelTask &) {
    return BB != &CurrentBB;
  };
  return !visit(IsNotBB);
}

bool ParallelTask::contains(const Instruction *I,
                            const DominatorTree &DT) const {
  return contains(I->getParent(), DT);
}

bool ParallelTask::contains(const Loop *L, const DominatorTree &DT) const {
  // A loop can only be contained in a parallel region or task if the header is.
  // At the same time it has to be contained if the header is as otherwise the
  // parallel region entry (the fork block) would be in the loop and it would
  // dominate the supposed header.
  return contains(L->getHeader(), DT);
}

bool ParallelTask::mayContain(const BasicBlock *BB,
                              const DominatorTree &DT) const {
  // All contained blocks are dominated by the entry block.
  if (!DT.dominates(&EntryBB, BB))
    return false;

  // If a join or halt block dominates the block it cannot be contained.
  for (TerminatorInst *TI : getHaltsOrJoints())
    if (DT.properlyDominates(TI->getParent(), BB))
      return false;

  return true;
}

bool ParallelTask::mayContain(const Instruction *I,
                              const DominatorTree &DT) const {
  return contains(I->getParent(), DT);
}

bool ParallelTask::mayContain(const Loop *L, const DominatorTree &DT) const {
  // See contains(const Loop *, DominatorTree &) for reasoning.
  return contains(L->getHeader(), DT);
}

bool ParallelTask::visit(ParallelTask::VisitorTy &Visitor,
                         bool Recursive) const {
  auto &SubRegionMap = ParentRegion.getSubRegions();
  DenseSet<BasicBlock *> SeenBlocks;
  SmallVector<BasicBlock *, 8> Worklist;
  Worklist.push_back(&EntryBB);

  while (!Worklist.empty()) {
    BasicBlock *CurrentBB = Worklist.pop_back_val();
    if (!SeenBlocks.insert(CurrentBB).second)
      continue;

    if (!Visitor(*CurrentBB, *this))
      return false;

    if (Recursive) {
      if (ForkInst *FI = dyn_cast<ForkInst>(CurrentBB->getTerminator())) {
        ParallelRegion *PR = SubRegionMap.lookup(FI);
        assert(PR && "Found fork instruction but no parallel sub-region!");

        if (!PR->visit(Visitor, Recursive))
          return false;

        ParallelTask &ContinuationTask = PR->getContinuationTask();
        for (TerminatorInst *TI : ContinuationTask.getHaltsOrJoints()) {
          assert(isa<JoinInst>(TI) &&
                 "Expected join instructions to terminate continuation task!");
          BasicBlock *JoinBB = TI->getParent();
          Worklist.append(succ_begin(JoinBB), succ_end(JoinBB));
        }

        // Do not traverse the parallel sub-region again.
        continue;
      }
    }

    if (HaltsOrJoints.count(CurrentBB->getTerminator()))
      continue;

    Worklist.append(succ_begin(CurrentBB), succ_end(CurrentBB));
  }

  return true;
}

void ParallelTask::print(raw_ostream &OS, unsigned indent) const {
  if (isForkedTask())
    OS.indent(indent) << "Forked Task:\n";
  else
    OS.indent(indent) << "Continuation Task:\n";
  OS.indent(indent) << "- Begin: " << EntryBB.getName() << "\n";
  for (TerminatorInst *TI : HaltsOrJoints)
    OS.indent(indent) << "- End:" << *TI << "\n";
}

void ParallelTask::dump() const { print(dbgs()); }

//===----------------------------------------------------------------------===//
// ParallelRegion implementation
//

ParallelRegion::ParallelRegion(ParallelRegionInfo &PRI, ForkInst &Fork,
                               ParallelTask *ParentTask)
    : PRI(PRI), Fork(Fork), ParentTask(ParentTask),
      ForkedTask(*this, *Fork.getForkedBB()),
      ContinuationTask(*this, *Fork.getContinuationBB()) {
  if (ParentTask)
    ParentTask->getParentRegion().addParallelSubRegion(*this);
}

ParallelRegion::~ParallelRegion() {
  DeleteContainerSeconds(ParallelSubRegions);
}

void ParallelRegion::addParallelSubRegion(ParallelRegion &ParallelSubRegion) {
  ParallelSubRegions[&ParallelSubRegion.getFork()] = &ParallelSubRegion;
}

bool ParallelRegion::isParallelLoopRegion(const Loop &L,
                                          const DominatorTree &DT) const {
  // Bail if the fork is not part of the loop.
  if (!L.contains(getFork().getParent()))
    return false;

  // Bail if a join is part of the loop.
  const auto &JoinsVector = ContinuationTask.getHaltsOrJoints();
  if (std::any_of(JoinsVector.begin(), JoinsVector.end(),
                  [&L](TerminatorInst *TI) { return L.contains(TI); }))
    return false;

  // Now check for possible side effects outside the forked task. First the
  // header to the fork and then the part of the continuation which is inside
  // the loop.
  SmallVector<BasicBlock *, 8> Worklist;
  BasicBlock *HeaderBB = L.getHeader();
  Worklist.push_back(HeaderBB);

  while (!Worklist.empty()) {
    BasicBlock *BB = Worklist.pop_back_val();
    assert(L.contains(BB));

    for (Instruction &I : *BB)
      if (I.mayHaveSideEffects())
        return false;

    if (&getFork() == BB->getTerminator())
      continue;

    for (BasicBlock *SuccessorBB : successors(BB))
      if (L.contains(SuccessorBB))
        Worklist.push_back(SuccessorBB);
  }

  BasicBlock &ContinuationEntryBB = ContinuationTask.getEntry();
  assert(L.contains(&ContinuationEntryBB) &&
         "Fork instructions should not be used to exit a loop!");

  // We do not support nested loops in the continuation part.
  if (std::any_of(L.begin(), L.end(), [&](Loop *SubLoop) {
        return ContinuationTask.contains(SubLoop, DT);
      }))
    return false;

  // Traverse all blocks that are in the continuation and in the loop and check
  // for side-effects.
  assert(Worklist.empty());
  Worklist.push_back(&ContinuationEntryBB);

  while (!Worklist.empty()) {
    BasicBlock *BB = Worklist.pop_back_val();
    assert(L.contains(BB));

    for (Instruction &I : *BB)
      if (I.mayHaveSideEffects())
        return false;

    for (BasicBlock *SuccessorBB : successors(BB))
      if (L.contains(SuccessorBB) && SuccessorBB != HeaderBB)
        Worklist.push_back(SuccessorBB);
  }

  return true;
}

bool ParallelRegion::contains(const BasicBlock *BB,
                              const DominatorTree &DT) const {
  // All contained blocks are dominated by the fork block.
  if (!DT.properlyDominates(Fork.getParent(), BB))
    return false;

  // If the above condition is not met we let the sub-tasks handle it.
  return ForkedTask.contains(BB, DT) || ContinuationTask.contains(BB, DT);
}

bool ParallelRegion::contains(const Instruction *I,
                              const DominatorTree &DT) const {
  return contains(I->getParent(), DT);
}

bool ParallelRegion::contains(const Loop *L, const DominatorTree &DT) const {
  // See ParallelTask::contains(const Loop *, DominatorTree &) for reasoning.
  return contains(L->getHeader(), DT);
}

bool ParallelRegion::mayContain(const BasicBlock *BB,
                                const DominatorTree &DT) const {
  // All contained blocks are dominated by the fork block.
  if (!DT.properlyDominates(Fork.getParent(), BB))
    return false;

  // If the above condition is not met we let the sub-tasks handle it.
  return ForkedTask.mayContain(BB, DT) || ContinuationTask.mayContain(BB, DT);
}

bool ParallelRegion::mayContain(const Instruction *I,
                                const DominatorTree &DT) const {
  return mayContain(I->getParent(), DT);
}

bool ParallelRegion::mayContain(const Loop *L, const DominatorTree &DT) const {
  // See ParallelTask::contains(const Loop *, DominatorTree &) for reasoning.
  return mayContain(L->getHeader(), DT);
}

void ParallelRegion::print(raw_ostream &OS, unsigned indent) const {
  OS.indent(indent) << "Parallel region:\n";
  OS.indent(indent) << "-" << Fork << "\n";

  OS.indent(indent) << "Forked Task:\n";
  OS.indent(indent + 2) << "- Begin: " << ForkedTask.getEntry().getName()
                        << "\n";
  for (auto It : ParallelSubRegions) {
    ParallelRegion *ParallelSubRegion = It.second;
    if (ParallelSubRegion->getParentTask() == &ForkedTask)
      ParallelSubRegion->print(OS, indent + 4);
  }
  for (TerminatorInst *TI : ForkedTask.getHaltsOrJoints())
    OS.indent(indent + 2) << "- End:" << *TI << "\n";

  OS.indent(indent) << "Continuation Task:\n";
  OS.indent(indent + 2) << "- Begin: " << ContinuationTask.getEntry().getName()
                        << "\n";
  for (auto It : ParallelSubRegions) {
    ParallelRegion *ParallelSubRegion = It.second;
    if (ParallelSubRegion->getParentTask() == &ContinuationTask)
      ParallelSubRegion->print(OS, indent + 4);
  }
  for (TerminatorInst *TI : ContinuationTask.getHaltsOrJoints())
    OS.indent(indent + 2) << "- End:" << *TI << "\n";
}

void ParallelRegion::dump() const { print(dbgs()); }

//===----------------------------------------------------------------------===//
// ParallelRegionInfo implementation
//

void ParallelRegionInfo::print(raw_ostream &OS) const {
  for (auto *PR : TopLevelParallelRegions)
    PR->print(OS);
}

void ParallelRegionInfo::dump() const {
  ParallelTaskMappingTy Mapping = createMapping();
  DenseMap<const ParallelTask *, SmallVector<BasicBlock *, 8>> ReverseMap;
  for (auto It : Mapping) {
    auto &Blocks = ReverseMap[It.second];
    Blocks.push_back(It.first);
  }
  for (auto It : ReverseMap) {
    errs() << It.getFirst()->getParentRegion().getFork() << "\n";
    errs() << *It.getFirst() << "\n";
    for (auto *BB : It.second)
      errs() << "\t - " << BB->getName() << "\n";
  }
}

void ParallelRegionInfo::releaseMemory() {
  DeleteContainerPointers(TopLevelParallelRegions);
  TopLevelParallelRegions.clear();
}

ParallelRegionInfo::ParallelTaskMappingTy
ParallelRegionInfo::recalculate(Function &F, const DominatorTree &DT) {
  releaseMemory();

  // A mapping from blocks to the parallel tasks they are contained in.
  ParallelTaskMappingTy BB2PTMap;

  // Scan the function first to check for fork, halt or join instructions.
  // If none are found bail early. This should be removed once most
  // transformation that preserve this pass do communicate that to the PM.
  bool ContainsParallelTI = false;
  for (BasicBlock &BB : F) {
    ContainsParallelTI = BB.getTerminator()->isForkJoinHalt();
    if (ContainsParallelTI)
      break;
  }
  if (!ContainsParallelTI)
    return BB2PTMap;

  // We use reverse post order (RPO) here only for verification purposes. A
  // simple CFG traversal would do just fine if the parallel IR is well-formed
  // but it cannot always detect if it is not. A possible alternative to RPO
  // would be to use not only dependence but also post-dependence information,
  // though that might be more complicated.
  ReversePostOrderTraversal<Function *> RPOT(&F);

  // Traverse all blocks in the function and record + propagate information
  // about parallel regions and tasks.
  for (BasicBlock *BB : RPOT) {
    ParallelTask *PT = const_cast<ParallelTask *>(BB2PTMap.lookup(BB));
    TerminatorInst *TI = BB->getTerminator();

    ForkInst *FI = dyn_cast<ForkInst>(TI);
    HaltInst *Halt = dyn_cast<HaltInst>(TI);
    JoinInst *Join = dyn_cast<JoinInst>(TI);

    // If this block is not in a parallel region and not starting one, just
    // continue with its successors.
    if (!PT && !FI) {
      assert(!Halt && "Found halt instruction outside of a parallel region!");
      assert(!Join && "Found join instruction outside of a parallel region!");
      continue;
    }

    if (FI) {
      // Fork instructions start a parallel region. We create it and map the
      // successors to the new one.
      ParallelRegion *NewPR = new ParallelRegion(*this, *FI, PT);

      // If it is a top level parallel region we additionally store it in the
      // TopLevelParallelRegions container.
      if (!PT)
        TopLevelParallelRegions.push_back(NewPR);

      // Bookkeeping
      NumParallelRegions++;

      // We add the successors of forks explicitly to the BB2PT map to be able
      // to exit early here.
      BB2PTMap[FI->getForkedBB()] = &NewPR->getForkedTask();
      BB2PTMap[FI->getContinuationBB()] = &NewPR->getContinuationTask();
      continue;
    }

    if (Halt) {
      // Halt instructions terminate the forked task but do not change the
      // current parallel region.
      assert(PT && "Found halt instruction outside of a parallel region!");
      assert(PT->isForkedTask() &&
             "Found halt instruction in continuation task!");
      ParallelRegion &PR = PT->getParentRegion();
      assert(DT.dominates(PR.getFork().getParent(), BB) &&
             "Parallel region fork does not dominate halt instruction!");
      assert(DT.dominates(&PT->getEntry(), BB) &&
             "Forked task entry does not dominate halt instruction!");
      assert(PR.getFork().getContinuationBB() == Halt->getContinuationBB() &&
             "Halt successor was not the continuation block!");
      PT->addHaltOrJoin(*Halt);

      // Bookkeeping
      NumHaltInstructions++;

      // The forked tasks ends with the halt instruction.
      continue;
    }

    if (Join) {
      // Join instructions terminate the continuation task and the current
      // parallel region.
      assert(PT && "Found join instruction outside of a parallel region!");
      assert(PT->isContinuationTask() &&
             "Found join instruction in forked task!");
      ParallelRegion &PR = PT->getParentRegion();
      assert(DT.dominates(PR.getFork().getParent(), BB) &&
             "Parallel region fork does not dominate join instruction!");
      assert(DT.dominates(&PT->getEntry(), BB) &&
             "Continuation task entry does not dominate join instruction!");
      PT->addHaltOrJoin(*Join);
      PT = PR.getParentTask();

      // Bookkeeping
      NumJoinInstructions++;
    }

    // If we left all parallel regions we do not need the BB2PTMap.
    if (!PT)
      continue;

    // For now we do not allow exception handling in parallel regions.
    assert(!TI->isExceptional() &&
           "Exception handling in parallel regions is not supported yet!");

    // Verify we do not leave a parallel region "open", thus reach a function
    // exit terminator before a join.
    assert(TI->getNumSuccessors() > 0 && "A parallel region was not terminated "
                                         "before a function exit was reached");

    // Propagate the parallel task information to the successors.
    for (BasicBlock *SuccBB : successors(BB)) {
      const ParallelTask *&OldTask = BB2PTMap[SuccBB];
      assert((!OldTask || OldTask == PT) &&
             "Basic block cannot belong to two different parallel tasks!");
      OldTask = PT;
    }
  }

  return BB2PTMap;
}

ParallelRegionInfo::ParallelTaskMappingTy
ParallelRegionInfo::createMapping() const {
  // While we could use the recompute algorithm here it is easier to just walk
  // the parallel regions we know recursively.
  ParallelTaskMappingTy BB2PTMap;

  ParallelTask::VisitorTy Visitor = [&BB2PTMap](BasicBlock &BB,
                                                const ParallelTask &PT) {
    BB2PTMap[&BB] = &PT;
    return true;
  };

  for (ParallelRegion *PR : TopLevelParallelRegions) {
    bool Success = PR->visit(Visitor, true);
    (void)Success;
    assert(Success && "Expeded walk over parallel region to visit all blocks!");
  }

  return BB2PTMap;
}

ParallelRegion *ParallelRegionInfo::getParallelLoopRegion(const Loop &L,
                                        const DominatorTree &DT) const {
  if (empty())
    return nullptr;

  SmallVector<ParallelRegion *, 8> ParallelRegions;
  ParallelRegions.append(begin(), end());

  // The parallel region we are looking for.
  ParallelRegion *ParallelLoopRegion = nullptr;

  while (!ParallelRegions.empty()) {
    ParallelRegion *PR = ParallelRegions.pop_back_val();

    for (const auto &It : PR->getSubRegions())
      ParallelRegions.push_back(It.getSecond());

    if (!PR->isParallelLoopRegion(L, DT))
      continue;

    assert(!ParallelLoopRegion &&
           "Broken nesting resulted in multiple parallel loop regions");

    ParallelLoopRegion = PR;

#ifdef NDEBUG
    // For debug builds we verify that at most one parallel loop region exists,
    // otherwise we just assume the nesting was intact.
    break;
#endif
  }

  return ParallelLoopRegion;
}

bool ParallelRegionInfo::isParallelLoop(const Loop &L,
                                        const DominatorTree &DT) const {
  // See ParallelRegionInfo::getParallelLoopRegion(...) for more information.
  return getParallelLoopRegion(L, DT) != nullptr;
}

bool ParallelRegionInfo::containedInAny(const BasicBlock *BB,
                                        const DominatorTree &DT) const {
  return std::any_of(
      TopLevelParallelRegions.begin(), TopLevelParallelRegions.end(),
      [BB, &DT](ParallelRegion *PR) { return PR->contains(BB, DT); });
}

bool ParallelRegionInfo::containedInAny(const Instruction *I,
                                        const DominatorTree &DT) const {
  return containedInAny(I->getParent(), DT);
}

bool ParallelRegionInfo::containedInAny(const Loop *L,
                                        const DominatorTree &DT) const {
  // See ParallelTask::contains(const Loop *, DominatorTree &) for reasoning.
  return containedInAny(L->getHeader(), DT);
}

bool ParallelRegionInfo::maybeContainedInAny(const BasicBlock *BB,
                                             const DominatorTree &DT) const {
  return std::any_of(
      TopLevelParallelRegions.begin(), TopLevelParallelRegions.end(),
      [BB, &DT](ParallelRegion *PR) { return PR->mayContain(BB, DT); });
}

bool ParallelRegionInfo::maybeContainedInAny(const Instruction *I,
                                             const DominatorTree &DT) const {
  return maybeContainedInAny(I->getParent(), DT);
}

bool ParallelRegionInfo::maybeContainedInAny(const Loop *L,
                                             const DominatorTree &DT) const {
  // See ParallelTask::contains(const Loop *, DominatorTree &) for reasoning.
  return maybeContainedInAny(L->getHeader(), DT);
}

bool ParallelRegionInfo::isSafeToPromote(const AllocaInst &AI,
                                         const DominatorTree &DT) const {
  if (empty())
    return true;

  // First check if we know that AI is contained in a parallel region.
  ParallelRegion *AIPR = nullptr;
  if (AI.getParent() != &AI.getFunction()->getEntryBlock()) {
    for (ParallelRegion *PR : TopLevelParallelRegions) {
      // In order to not perform a potentially linear contains check we will
      // skip parallel regions that have multi-exit tasks. This is conservative
      // but sound.
      if (!PR->hasTwoSingleExits())
        continue;
      if (!PR->contains(&AI, DT))
        continue;

      AIPR = PR;
      break;
    }
  }

  // If we know a parallel region that contains AI we determine the smallest one
  // that does. If the smallest one does not have parallel sub-regions we can
  // promote the alloca as it will only be alive inside one parallel thread of
  // that region. If the smallest one does contain parallel sub-regions we bail
  // for now as the alloca could be used in a parallel context.
  while (AIPR) {
    ParallelRegion::SubRegionMapTy SubRegions = AIPR->getSubRegions();
    if (SubRegions.empty())
      return true;

    ParallelRegion *SubAIPR = nullptr;
    for (auto It : SubRegions) {
      ParallelRegion *SR = It.second;
      // In order to not perform a potentially linear contains check we will
      // bail for parallel sub-regions that have multi-exit tasks.
      if (!SR->hasTwoSingleExits())
        return false;

      if (!SR->contains(&AI, DT))
        continue;

      SubAIPR = SR;
      break;
    }

    if (!SubAIPR)
      return false;
    AIPR = SubAIPR;
  }

  assert(!AIPR);

  // If we do not know that the alloca is inside a parallel task we will not
  // allow promotion if any user might be in a parallel region.
  // TODO Check only for spawned tasks not parallel regions.
  for (const User *U : AI.users()) {
    const Instruction *I = dyn_cast<Instruction>(U);
    if (!I || !I->mayWriteToMemory())
      continue;

    if (maybeContainedInAny(I, DT))
      return false;
  }

  return true;
}

//===----------------------------------------------------------------------===//
// ParallelRegionAnalysis implementation
//

AnalysisKey ParallelRegionAnalysis::Key;

ParallelRegionInfo ParallelRegionAnalysis::run(Function &F,
                                               FunctionAnalysisManager &AM) {
  DominatorTree &DT = AM.getResult<DominatorTreeAnalysis>(F);
  return ParallelRegionInfo(F, DT);
}

//===----------------------------------------------------------------------===//
// ParallelRegionInfoPass implementation
//

bool ParallelRegionInfoPass::runOnFunction(Function &F) {
  auto &DT = getAnalysis<DominatorTreeWrapperPass>().getDomTree();
  PRI.recalculate(F, DT);
  return false;
}

void ParallelRegionInfoPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<DominatorTreeWrapperPass>();
}

void ParallelRegionInfoPass::print(raw_ostream &OS, const Module *) const {
  PRI.print(OS);
}

void ParallelRegionInfoPass::verifyAnalysis() const {
  // TODO Not implemented but merely a stub.
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void ParallelRegionInfoPass::dump() const { PRI.dump(); }
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
