//===---------------- PIR backend for the OpenMP runtime ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Parallelize/OpenMP.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/IR/Constants.h"
#include "llvm/Analysis/ParallelRegionInfo.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/Transforms/Utils/ParallelUtils.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

using namespace llvm;

#define DEBUG_TYPE "flatten-pir"

static const StringRef OpenMPParallelFnAttribute = "pir.fn.openmp";

static cl::opt<int>
    DefaultScore("openmp-score", cl::init(200),
                 cl::desc("Default score used by the OpenMP backend."));

int OpenMPRuntimeBackend::getScore(ParallelRegion *PR, ForkInst *FI) const {
  // TODO: Check if PR can be implemented by OpenMP:
  //       - nesting of parallel regions
  //       - user choice
  if (!FI)
    return DefaultScore;

  if (FI->getFunction()->hasFnAttribute(OpenMPParallelFnAttribute))
    return 0;

  if (!PR)
    return DefaultScore;

#if 0
  for (auto *Task : *PR) {
    for (auto *BB : *Task) {

    }
  }
#endif

  return DefaultScore;
}

#if 0
Value *ParallelLoopGenerator::createCallGetWorkItem(Value *LBPtr,
                                                    Value *UBPtr) {
  const std::string Name = "GOMP_loop_runtime_next";

  Function *F = M->getFunction(Name);

  // If F is not available, declare it.
  if (!F) {
    GlobalValue::LinkageTypes Linkage = Function::ExternalLinkage;
    Type *Params[] = {LongType->getPointerTo(), LongType->getPointerTo()};
    FunctionType *Ty = FunctionType::get(Builder.getInt8Ty(), Params, false);
    F = Function::Create(Ty, Linkage, Name, M);
  }

  Value *Args[] = {LBPtr, UBPtr};
  Value *Return = Builder.CreateCall(F, Args);
  Return = Builder.CreateICmpNE(
      Return, Builder.CreateZExt(Builder.getFalse(), Return->getType()));
  return Return;
}
#endif

// Construct the header for a parallel region (that is, the call to
// GOMP_sections_start). The value returned is the result of the sections_start
// call.
Instruction *OpenMPRuntimeBackend::CreateHeader(BasicBlock *regionStart) {
  assert(isa<ForkInst>(regionStart->getTerminator()) && "Should only be called"
                                                        "on fork instruction");

  // First, set up the function call for open MP GOMP_sections_start
  const std::string Name = "GOMP_sections_start";
  Type *IntType = Type::getInt32Ty(regionStart->getContext());
  Function *StartFunc = regionStart->getModule()->getFunction(Name);

  if (!StartFunc) {
    GlobalValue::LinkageTypes Linkage = Function::ExternalLinkage;
    Type *Params[] = {IntType};

    FunctionType *Ty = FunctionType::get(IntType, Params, false);
    StartFunc = Function::Create(Ty, Linkage, Name, regionStart->getModule());
  }

  // Then set up the actual call.
  Value *NumThreads = ConstantInt::get(
      IntType, regionStart->getTerminator()->getNumSuccessors());
  Value *Args[] = {NumThreads};

  BasicBlock *Header = BasicBlock::Create(
      regionStart->getContext(), "GOMP_header", regionStart->getParent(),
      regionStart->getTerminator()->getSuccessor(0));
  CallInst *StartCall = CallInst::Create(StartFunc, Args);
  Header->getInstList().push_back(StartCall);
  return StartCall;
}

// Construct the block that makes the call to GOMP_sections_next.
// The value returned is the number of the parallel region that should be
// executed next.
Instruction *OpenMPRuntimeBackend::CreateNextRegion(BasicBlock *regionStart) {
  const std::string Name = "GOMP_sections_next";
  Function *NextFunc = regionStart->getModule()->getFunction(Name);

  if (!NextFunc) {
    GlobalValue::LinkageTypes Linkage = Function::ExternalLinkage;
    FunctionType *Ty =
        FunctionType::get(Type::getInt32Ty(regionStart->getContext()), false);
    NextFunc = Function::Create(Ty, Linkage, Name, regionStart->getModule());
  }

  BasicBlock *EndRegion = BasicBlock::Create(
      regionStart->getContext(), "GOMP_footer", regionStart->getParent());
  regionStart->getParent()->getBasicBlockList().push_back(EndRegion);
  CallInst *NextCall = CallInst::Create(NextFunc);
  EndRegion->getInstList().push_back(NextCall);
  return NextCall;
}

// Convert a fork instruction into a series of OpenMP calls.
BasicBlock *OpenMPRuntimeBackend::CreateTasks(BasicBlock *regionStart) {
  #if 0
  SmallVector<BasicBlock *, 4> EntryBlocks;
  std::deque<BasicBlock *> OpenBlocks;
  DenseSet<BasicBlock *> VisitedBlocks;

  TerminatorInst *Term = regionStart->getTerminator();

  // Construct the exit from the parallel region if nne exists.
  BasicBlock *Exit = BasicBlock::Create(regionStart->getContext(), "AllHalt",
                                        regionStart->getParent());
  new UnreachableInst(regionStart->getContext(), Exit);
  regionStart->getParent()->getBasicBlockList().push_back(Exit);

  // Create the initial OpenMP calls.
  Instruction *StartRegion = CreateHeader(regionStart);
  Instruction *NextRegion = CreateNextRegion(regionStart);

  BasicBlock *EndRegion = BasicBlock::Create(
      regionStart->getContext(), "EndRegion", regionStart->getParent());
  regionStart->getParent()->getBasicBlockList().push_back(EndRegion);
  PHINode *Cond = PHINode::Create(Type::getInt32Ty(regionStart->getContext()),
                                  2, "", EndRegion);
  SwitchInst *Switch =
      SwitchInst::Create(Cond, NextRegion->getParent(),
                         Term->getNumSuccessors() + 1, NextRegion->getParent());

  // TODO: For each successor of regionStart.
  // 1. Copy the instruction and all of its children. Find the exit from the
  // parallel region, update Exit, and change any jumps to an exit from the
  // parallel region to point to NextRegion.
  // 2. Add each coppied successor of regionStart as a conditional to the
  // switch statement with a number corresponding to its position in the
  // succesor list (start w/ 1, not 0).

  // TODO: Add conditon to switch to jump to newly found Exit when Cond == 0
  // TODO: Remove join and fork instructions.

  return Exit;
#endif
}

bool OpenMPRuntimeBackend::runOnParallelRegion(ParallelRegion &PR, ForkInst &FI,
                                               DominatorTree &DT, LoopInfo &LI) {
  if (PR.empty())
    return false;

  SmallVector<ParallelTask *, 8> Tasks;
  for (auto *Task : PR.tasks())
    if (Task->getFork() == &FI)
      Tasks.push_back(Task);
  assert(!Tasks.empty());

  errs() << "#Tasks: " << Tasks.size() << "\n";
  separateTasks(Tasks, &DT, &LI);

  assert(getScore(&PR, &FI) >= 0);
  assert(PR.size() > 0);

  auto *FIParentBB = FI.getParent();
  auto *HeaderBB = SplitBlock(FIParentBB, &FI, &DT, &LI);
  HeaderBB->setName("par_section");
  SetVector<BasicBlock *> Blocks;
  Blocks.insert(HeaderBB);

  for (auto *Task : Tasks) {
    Blocks.insert(Task->begin(), Task->end());
  }

  auto &M = *HeaderBB->getModule();
  auto &Ctx = M.getContext();

  // TODO: Make types target independent.
  auto *VoidTy = Type::getVoidTy(Ctx);
  auto *VoidPtrTy = Type::getInt8PtrTy(Ctx);
  auto *UnsignedTy = Type::getInt32Ty(Ctx);

  CodeExtractor CE(Blocks.getArrayRef(), &DT, /* Aggregate Arguments */ true);
  assert(CE.isEligible());
  auto *ParFn = CE.extractCodeRegion();
  assert(ParFn->getArgumentList().size() <= 1);
  ParFn->addFnAttr(OpenMPParallelFnAttribute);

  auto *ParFnCI = cast<CallInst>(ParFn->user_back());
  auto *ParFnWrapper = getOrCreateFunction(
      M, ParFn->getName().str() + "_wrapper", VoidTy, {VoidPtrTy});

  auto *ParFnWrapperEntryBB = BasicBlock::Create(Ctx, "entry", ParFnWrapper);

  if (ParFn->getArgumentList().size() == 1) {
    auto *CIArg = cast<Instruction>(ParFnCI->getArgOperand(0));
    auto *CIArgCast = BitCastInst::CreatePointerCast(CIArg, VoidPtrTy, "");
    CIArgCast->insertBefore(ParFnCI);
    ParFnCI->setCalledFunction(ParFnWrapper);
    ParFnCI->setArgOperand(0, CIArgCast);
    auto *Arg = &*ParFnWrapper->arg_begin();
    auto *ArgCast = BitCastInst::CreatePointerCast(Arg, CIArg->getType(), "",
                                                   ParFnWrapperEntryBB);
    CallInst::Create(ParFn, {ArgCast}, "", ParFnWrapperEntryBB);
  } else {
    auto *ParFnWrapperCI = CallInst::Create(
        ParFnWrapper, {ConstantInt::getNullValue(VoidPtrTy)}, "", ParFnCI);
    ParFnCI->eraseFromParent();
    ParFnCI = ParFnWrapperCI;
    CallInst::Create(ParFn, "", ParFnWrapperEntryBB);
  }

  ReturnInst::Create(Ctx, ParFnWrapperEntryBB);

  auto *SubFnTy = FunctionType::get(VoidTy, {VoidPtrTy}, false);
  Type *ArgTypes[] = {SubFnTy->getPointerTo(), VoidPtrTy, UnsignedTy,
                      UnsignedTy};
  auto *GpssFn =
      getOrCreateFunction(M, "GOMP_parallel_sections_start", VoidTy, ArgTypes);

  assert(ParFnCI->getNumArgOperands() <= 1);
  auto *ParFnCIArg = ParFnCI->getNumArgOperands()
                         ? ParFnCI->getArgOperand(0)
                         : Constant::getNullValue(VoidPtrTy);
  // TODO: Add num threads attribute to ForkInst.
  auto *NumSections = ConstantInt::get(UnsignedTy, PR.size());
  auto *NumThreads =
      /* FI.getNumThreads() */ ConstantInt::getNullValue(UnsignedTy);
  Value *Args[] = {ParFnCI->getCalledFunction(), ParFnCIArg, NumThreads,
                   NumSections};
  CallInst::Create(GpssFn, Args, "", ParFnCI);

  auto *GpeFn = getOrCreateFunction(M, "GOMP_parallel_end");
  auto *GpeFnCI = CallInst::Create(GpeFn);
  GpeFnCI->insertAfter(ParFnCI);

  assert(HeaderBB->getParent() == ParFn);
  assert(isa<ForkInst>(HeaderBB->getTerminator()));
  HeaderBB->setName("par_section_header");

  auto *CallBB = ParFnCI->getParent();
  if (FI.isInterior()) {
    auto *BranchTI = CallBB->getTerminator();
    assert(isa<BranchInst>(BranchTI) && BranchTI->getNumSuccessors() == 1);
    JoinInst::Create(FI.getParallelRegionId(), BranchTI->getSuccessor(0),
                     BranchTI);
    BranchTI->eraseFromParent();
  }

  SmallSet<BasicBlock *, 4> ExitBBs;
  for (auto *Task : PR) {
    if (Task->contains(FIParentBB))
      Task->addBlock(CallBB);

    if (Task->getFork() != &FI)
      continue;

    for (auto *EndBB : Task->getEndBlocks()) {
      auto *EndBBTI = EndBB->getTerminator();

      // Happens if we have multiple tasks ending in the same join/halt and we
      // already replaced it.
      if (!isTaskTerminator(EndBBTI))
        continue;

      if (EndBBTI->getNumSuccessors())
        ExitBBs.insert(EndBBTI->getSuccessor(0));

      BranchInst::Create(HeaderBB, EndBBTI);
      EndBBTI->eraseFromParent();
     }
  }

  auto *GsnFn = getOrCreateFunction(M, "GOMP_sections_next", UnsignedTy);
  auto *NextSectionId = CallInst::Create(GsnFn, "", &FI);
  auto *SI = SwitchInst::Create(NextSectionId, *ExitBBs.begin(),
                                FI.getNumSuccessors(), &FI);

  uint64_t Id = 1;
  for (auto *SuccBB : FI.successors())
    SI->addCase(ConstantInt::get(UnsignedTy, Id++), SuccBB);
  PR.eraseFork(&FI);

  Function *GsewFn = getOrCreateFunction(M, "GOMP_sections_end_nowait");
  for (auto *ExitBB : ExitBBs) {
    assert(ExitBB->size() == 1);

    auto *TI = ExitBB->getTerminator();
    assert(isa<ReturnInst>(TI));
    CallInst::Create(GsewFn, "", TI);
  }

  return true;
}
