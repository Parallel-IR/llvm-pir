//===- PIR/Backends/OpenMP.cpp --- PIR backend for the OpenMP runtime -----===//
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

#include "llvm/Transforms/Utils/ParallelUtils.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Constants.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

#include <deque>

using namespace llvm;

#define DEBUG_TYPE "parallel-utils"

OpenMPParallelTasks::OpenMPParallelTasks() :
  FunctionPass(ID) {
  initializeOpenMPParallelTasksPass(*PassRegistry::getPassRegistry());
}

OpenMPParallelTasks::~OpenMPParallelTasks() {}

// Construct the header for a parallel region (that is, the call to
// GOMP_sections_start). The value returned is the result of the sections_start
// call.
Instruction *OpenMPParallelTasks::CreateHeader(BasicBlock *regionStart) {
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
  Value *NumThreads =
    ConstantInt::get(IntType, regionStart->getTerminator()->getNumSuccessors());
  Value *Args[] = {NumThreads};

  BasicBlock* Header = BasicBlock::Create(regionStart->getContext(),
                                          "GOMP_header",
                                          regionStart->getParent(),
                                          regionStart->getTerminator()->getSuccessor(0));
  CallInst* StartCall = CallInst::Create(StartFunc, Args);
  Header->getInstList().push_back(StartCall);
  return StartCall;
}

// Construct the block that makes the call to GOMP_sections_next.
// The value returned is the number of the parallel region that should be
// executed next.
Instruction *OpenMPParallelTasks::CreateNextRegion(BasicBlock *regionStart) {
  const std::string Name = "GOMP_sections_next";
  Function *NextFunc = regionStart->getModule()->getFunction(Name);

  if (!NextFunc) {
    GlobalValue::LinkageTypes Linkage = Function::ExternalLinkage;
    FunctionType *Ty =
      FunctionType::get(Type::getInt32Ty(regionStart->getContext()), false);
    NextFunc = Function::Create(Ty, Linkage, Name, regionStart->getModule());
  }

  BasicBlock *EndRegion = BasicBlock::Create(regionStart->getContext(),
                                             "GOMP_footer",
                                             regionStart->getParent());
  regionStart->getParent()->getBasicBlockList().push_back(EndRegion);
  CallInst *NextCall = CallInst::Create(NextFunc);
  EndRegion->getInstList().push_back(NextCall);
  return NextCall;
}

// Convert a fork instruction into a series of OpenMP calls.
BasicBlock *OpenMPParallelTasks::CreateTasks(BasicBlock *regionStart) {
  SmallVector<BasicBlock *, 4> EntryBlocks;
  std::deque<BasicBlock *> OpenBlocks;
  DenseSet<BasicBlock *> VisitedBlocks;

  TerminatorInst *Term = regionStart->getTerminator();

  // Construct the exit from the parallel region if nne exists.
  BasicBlock* Exit = BasicBlock::Create(regionStart->getContext(),
                                        "AllHalt", regionStart->getParent());
  new UnreachableInst(regionStart->getContext(), Exit);
  regionStart->getParent()->getBasicBlockList().push_back(Exit);

  // Create the initial OpenMP calls.
  Instruction *StartRegion = CreateHeader(regionStart);
  Instruction *NextRegion = CreateNextRegion(regionStart);

  BasicBlock *EndRegion = BasicBlock::Create(regionStart->getContext(),
                                             "EndRegion",
                                             regionStart->getParent());
  regionStart->getParent()->getBasicBlockList().push_back(EndRegion);
  PHINode *Cond = PHINode::Create(Type::getInt32Ty(regionStart->getContext()),
                                  2, "", EndRegion);
  SwitchInst *Switch = SwitchInst::Create(Cond, NextRegion->getParent(),
                                          Term->getNumSuccessors() + 1,
                                          NextRegion->getParent());

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
}

bool OpenMPParallelTasks::runOnFunction(Function &F) {
  bool modified = false;
  std::deque<BasicBlock *> OpenBlocks;
  DenseSet<BasicBlock *> SeenBlocks;

  // Step 1: Iterate over blocks until we find start of parallel region.
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
      BB = CreateTasks(BB);
      OpenBlocks.push_back(BB);
    } else {
      for (BasicBlock *successor : successors(BB)) {
        OpenBlocks.push_back(successor);
      }
    }
  }

  return modified;
}

void OpenMPParallelTasks::releaseMemory() {}
void OpenMPParallelTasks::print(raw_ostream &, const Module *) const {}
void OpenMPParallelTasks::getAnalysisUsage(AnalysisUsage &AU) const {}
char OpenMPParallelTasks::ID = 1;

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void OpenMPParallelTasks::dump() const {}
#endif

INITIALIZE_PASS_BEGIN(OpenMPParallelTasks, "openmp-task-parallelize",
                       "Convert fork and join to Open MP tasks", true, false)
INITIALIZE_PASS_END(OpenMPParallelTasks, "openmp-task-parallelize",
                    "Convert fork and join to Open MP tasks", true, false)

namespace llvm {
FunctionPass *createOpenMPParallelTasksPass() {
  return new OpenMPParallelTasks();
}
}
