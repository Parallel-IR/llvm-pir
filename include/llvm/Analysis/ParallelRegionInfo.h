//===- ParallelParallelRegionInfo.h - Parallel region analysis --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_PARALLELREGIONINFO_H
#define LLVM_ANALYSIS_PARALLELREGIONINFO_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Pass.h"

namespace llvm {

class ParallelRegionInfo;

class ParallelRegion {
public:
  ParallelRegion(ParallelRegionInfo *RI, DominatorTree *DT, unsigned id,
                 ParallelRegion *Parent = nullptr);
  ~ParallelRegion();

  friend class ParallelRegionInfo;

  unsigned getParallelRegionDepth() const {
    unsigned D = 1;
    for (const ParallelRegion *CurRegion = Parent; CurRegion;
         CurRegion = CurRegion->Parent) {
      ++D;
    }
    return D;
  }

  unsigned getId() const { return ParallelRegionID; }
  ParallelRegion *getParentRegion() const { return Parent; }
  void setParentRegion(ParallelRegion *P) { Parent = P; }

  void addEntryBlockToRegion(BasicBlock *NewBB) {
    EntryBlocks.emplace_back(NewBB);
  }

  void addExitBlockToRegion(BasicBlock *NewBB) {
    ExitBlocks.emplace_back(NewBB);
  }

  const SmallVectorImpl<ParallelRegion *> &getSubRegions() const {
    return SubRegions;
  }

  SmallVectorImpl<ParallelRegion *> &getSubRegionsVector() {
    return SubRegions;
  }

  const SmallVectorImpl<BasicBlock *> &getBlocks() const { return Blocks; }
  SmallVectorImpl<BasicBlock *> &getBlocksVector() { return Blocks; }

  void addSubRegion(ParallelRegion *sub) { SubRegions.emplace_back(sub); }

  typedef typename SmallVectorImpl<ParallelRegion *>::const_iterator iterator;
  typedef typename SmallVectorImpl<ParallelRegion *>::const_reverse_iterator
      reverse_iterator;
  iterator begin() const { return SubRegions.begin(); }
  iterator end() const { return SubRegions.end(); }
  reverse_iterator rbegin() const { return SubRegions.rbegin(); }
  reverse_iterator rend() const { return SubRegions.rend(); }
  bool empty() const { return SubRegions.empty(); }

private:
  ParallelRegion *Parent;
  unsigned ParallelRegionID;
  SmallVector<ParallelRegion *, 32> SubRegions;
  SmallVector<BasicBlock *, 32> Blocks;
  SmallVector<BasicBlock *, 2> EntryBlocks;
  SmallVector<BasicBlock *, 2> ExitBlocks;
};

class ParallelRegionInfo {
  unsigned NextParallelRegionId;

public:
  SmallVector<ParallelRegion *, 32> TopLevelRegions;

  explicit ParallelRegionInfo();

  ~ParallelRegionInfo();

  unsigned getNewParallelRegionId() { return NextParallelRegionId++; }

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

inline raw_ostream &operator<<(raw_ostream &OS, const ParallelRegion &R) {
  OS << "Dump Parallel Region\n";
  return OS;
}

} // End llvm namespace
#endif
