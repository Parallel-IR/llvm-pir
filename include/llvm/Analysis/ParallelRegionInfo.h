//===- PIR/ParallelRegionInfo.h --- Parallel region analysis --*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PIR_PARALLELREGIONINFO_H
#define LLVM_PIR_PARALLELREGIONINFO_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"

namespace llvm {

class LoopInfo;
class DominatorTree;
class ParallelTask;
class ParallelRegion;
class ParallelRegionInfo;

class ParallelTask {
  ParallelRegion &Parent;
  ForkInst *FI;
  BasicBlock *EntryBB;
  SmallSet<BasicBlock *, 8> EndBlocks;
  SetVector<BasicBlock *> Blocks;

  friend class ParallelRegion;

public:
  ParallelTask(ParallelRegion &Parent, ForkInst *FI, BasicBlock *EntryBB);

  //const SmallVectorImpl<BasicBlock *> &getBlocks() const { return Blocks; }
  const SmallSet<BasicBlock *, 8> &getEndBlocks() const { return EndBlocks; }

  ForkInst *getFork() const { return FI; }
  ParallelRegion &getParent() const { return Parent; }

  void addBlock(BasicBlock *BB);
  void eraseBlock(BasicBlock *BB);
  void setSingleExit(BasicBlock *BB);

  bool isStartBlock(BasicBlock *BB) const { return BB == EntryBB; }
  bool isEndBlock(BasicBlock *BB) const;
  size_t size() const { return Blocks.size(); }
  bool contains(BasicBlock *BB) const { return Blocks.count(BB); }

  using iterator = decltype(Blocks)::iterator;
  iterator begin() { return Blocks.begin(); }
  iterator end() { return Blocks.end(); }
  iterator_range<iterator> blocks() { return Blocks; }

  void print(raw_ostream &OS) const;
  void dump() const { return print(errs()); }
};

class ParallelRegion {
  ParallelRegionInfo &PRI;

  SmallVector<ForkInst *, 4> Forks;
  ParallelRegion *Parent;
  using TasksTy = SetVector<ParallelTask *>;
  TasksTy Tasks;
  DenseMap<BasicBlock *, TasksTy> TasksMap;
  SmallVector<ParallelRegion *, 4> SubRegions;

  void addSubRegion(ParallelRegion *SubPR);
  void removeSubRegion(ParallelRegion *SubPR);

  const SmallVectorImpl<ParallelRegion *> &getSubRegions() const {
    return SubRegions;
  }

  void addTasks(ForkInst *FI);

  ParallelRegion(ParallelRegionInfo &PRI, ForkInst *FI,
                 ParallelRegion *Parent = nullptr);
  ParallelRegion(ParallelRegion &&OTher);

  void setParent(ParallelRegion *ParentPR) { Parent = ParentPR; }

  friend class ParallelRegionInfo;

public:
  ~ParallelRegion();

  unsigned getParallelRegionDepth() const {
    unsigned D = 1;
    for (const ParallelRegion *CurRegion = Parent; CurRegion;
         CurRegion = CurRegion->Parent)
      ++D;
    return D;
  }

  ConstantInt *getId() const { return Forks.front()->getParallelRegionId(); }

  ParallelRegionInfo &getParallelRegionInfo() {return PRI; }
  const ParallelRegionInfo &getParallelRegionInfo() const {return PRI; }

  ParallelRegion *getParentRegion() const { return Parent; }

  ForkInst *getEntryFork() const { return Forks.front(); }
  size_t getNumForks() const { return Forks.size(); }

  std::string getName() const {
    return Forks.empty() ? "<NONE>"
                         : "PR" + std::to_string(getId()->getSExtValue());
  }

  BasicBlock *splitBlockPredecessors(BasicBlock *BB,
                                     ArrayRef<BasicBlock *> PredBBs,
                                     DominatorTree *DT = nullptr,
                                     LoopInfo *LI = nullptr);
  void addBlock(BasicBlock *BB);

  const DenseMap<BasicBlock *, TasksTy> &getTasksMap() const {
    return TasksMap;
  }

  bool hasSubRegions() const { return !SubRegions.empty(); }
  unsigned getNumSubRegions() const { return SubRegions.size(); }

  using region_iterator = SmallVectorImpl<ParallelRegion *>::const_iterator;
  region_iterator sub_regions_begin() const { return SubRegions.begin(); }
  region_iterator sub_regions_end() const { return SubRegions.end(); }
  iterator_range<region_iterator> sub_regions() const { return SubRegions; }

  size_t size() const { return Tasks.size(); }
  bool empty() const { return Tasks.empty(); }

  using iterator = decltype(Tasks)::const_iterator;
  iterator begin() const { return Tasks.begin(); }
  iterator end() const { return Tasks.end(); }
  iterator_range<iterator> tasks() const { return Tasks; }

  using fork_iterator = decltype(Forks)::const_iterator;
  fork_iterator fork_begin() const { return Forks.begin(); }
  fork_iterator fork_end() const { return Forks.end(); }
  iterator_range<fork_iterator> forks() const { return Forks; }
  iterator_range<fork_iterator> interior_forks() const {
    auto It = fork_begin();
    return make_range(++It, fork_end());
  }

  void eraseFork(ForkInst *FI);
  void eraseForkSuccessor(ForkInst *FI, BasicBlock *SuccBB);

  void addBlocks(SetVector<BasicBlock *> &Blocks) const;

  void print(raw_ostream &OS) const;
  void dump() const { return print(errs()); }
};

inline raw_ostream &operator<<(raw_ostream &OS, const ParallelRegion &R) {
  OS << R.getName();
  return OS;
}

inline raw_ostream &operator<<(raw_ostream &OS, const ParallelRegion *R) {
  if (R)
    OS << R->getName();
  return OS;
}

class ParallelRegionInfo {
  unsigned NextParallelRegionId;

public:
  SmallVector<ParallelRegion *, 32> TopLevelRegions;
  DenseMap<BasicBlock *, ParallelRegion *> BB2PRMap;

  explicit ParallelRegionInfo();

  ~ParallelRegionInfo();

  unsigned getNewParallelRegionId() { return NextParallelRegionId++; }

  void eraseParallelRegion(ParallelRegion &PR);

  using iterator = decltype(TopLevelRegions)::iterator;
  iterator begin() { return TopLevelRegions.begin(); }
  iterator end() { return TopLevelRegions.end(); }

  bool empty() const { return TopLevelRegions.empty(); }

  void print(raw_ostream &OS) const;
  void dump() const;
  void releaseMemory();

  void recalculate(Function &F, DominatorTree *DT);
};

class ParallelRegionInfoPass : public FunctionPass {
  ParallelRegionInfo RI;

public:
  static char ID;
  explicit ParallelRegionInfoPass();

  ~ParallelRegionInfoPass() override;

  ParallelRegionInfo &getParallelRegionInfo() { return RI; }

  const ParallelRegionInfo &getParallelRegionInfo() const { return RI; }

  /// @name FunctionPass interface
  //@{
  bool runOnFunction(Function &F) override;
  void releaseMemory() override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  void print(raw_ostream &OS, const Module *) const override;
  void dump() const;
  //@}
};

} // End llvm namespace
#endif
