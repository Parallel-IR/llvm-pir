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

void removeFalsePredecessor(BasicBlock *removeFrom, BasicBlock *remove) {
  for (BasicBlock::iterator I = removeFrom->begin(); isa<PHINode>(I);) {
    PHINode *PN = cast<PHINode>(I);
    ++I;
    assert(PN->getBasicBlockIndex(remove) >= 0 &&
           "Even though not a predecessor, still in PHI.");
    PN->removeIncomingValue(remove);
  }
}

SequentializeParallelRegions::SequentializeParallelRegions() :
  FunctionPass(ID) {
  initializeSequentializeParallelRegionsPass(*PassRegistry::getPassRegistry());
}

SequentializeParallelRegions::~SequentializeParallelRegions() {}

static inline unsigned minDepth(BasicBlock *BB,
                                DenseMap<BasicBlock*, unsigned> &Block2Depth) {
  unsigned minDepth = UINT_MAX;
  for (pred_iterator PI = pred_begin(BB), E = pred_end(BB);
       PI != E; ++PI) {
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
                                    DenseSet<BasicBlock*> &NewBlocks,
                                    ValueToValueMapTy &VMap,
                                    DenseMap<BasicBlock*, unsigned> &Blck2D){
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
        if (!Val ||
            (!Blck2D.count(Val->getParent()) || (
             Blck2D[Incoming] >= Blck2D[Val->getParent()]))) {
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
      BasicBlock *newEntry = CloneBasicBlock(successor, *vmap,
                                            "forkChild");
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

        if (successor == regionStart || (isa<JoinInst>(succTerm) && 
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
bool SequentializeParallelRegions::runOnFunction(Function &F) {
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
}

void SequentializeParallelRegions::releaseMemory() {
}

void SequentializeParallelRegions::print(raw_ostream &, const Module *) const {
}

void SequentializeParallelRegions::getAnalysisUsage(AnalysisUsage &AU)
  const {}

char SequentializeParallelRegions::ID = 0;

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
void SequentializeParallelRegions::dump() const {}
void OpenMPParallelTasks::dump() const {}
#endif

INITIALIZE_PASS_BEGIN(SequentializeParallelRegions, "sequentialize-regions",
                      "Remove all fork and join instructions", true, false)
INITIALIZE_PASS_END(SequentializeParallelRegions, "sequentialize-regions",
                    "Remove all fork and join instructions", true, false)
INITIALIZE_PASS_BEGIN(OpenMPParallelTasks, "openmp-task-parallelize",
                       "Convert fork and join to Open MP tasks", true, false)
INITIALIZE_PASS_END(OpenMPParallelTasks, "openmp-task-parallelize",
                    "Convert fork and join to Open MP tasks", true, false)

namespace llvm {
FunctionPass *createSequentializeParallelRegionsPass() {
  return new SequentializeParallelRegions();
}

FunctionPass *createOpenMPParallelTasksPass() {
  return new OpenMPParallelTasks();
}
}
