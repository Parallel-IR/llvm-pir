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

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"

namespace llvm {

class Loop;
class ParallelTask;
class ParallelRegion;

/// The parallel region info (PRI) identifies and creates parallel regions.
///
/// Currently the parallel region info is "lazy" in the sense that it does only
/// need to be updated if new parallel regions are created (or deleted). As this
/// should not happen very often (and only in very few places) it allows
/// transformation passes to preserve the parallel region info without
/// modifications. Additionally, it makes the analysis very lightweight in the
/// absence of parallel regions (which should be the majority of functions).
///
/// The drawback for passes that need to deal with parallel regions explicitly
/// is the absence of a mapping from basic blocks to parallel regions. For now
/// these passes can use the createMapping() function to generate such a mapping
/// on-demand. After integration of parallel regions a separate function pass
/// could be introduced to maintain this mapping and recompute it if it was not
/// preserved by a transformation. However, at the moment there are only a small
/// number of places that require it but a lot of transformations that would
/// need to be modified to preserve it.
///
/// @TODO The update interface to add/remove parallel regions is missing.
///
class ParallelRegionInfo {
public:
  /// The container type used to store top level parallel regions.
  using ParallelRegionVectorTy = SmallVector<ParallelRegion *, 4>;

  /// Iterator types for top level parallel regions.
  ///
  ///{
  using iterator = ParallelRegionVectorTy::iterator;
  using const_iterator = ParallelRegionVectorTy::const_iterator;
  ///}

private:
  /// The parallel regions that are not nested in other parallel regions.
  ParallelRegionVectorTy TopLevelParallelRegions;

public:
  ParallelRegionInfo() {}
  ParallelRegionInfo(Function &F, const DominatorTree &DT) { recalculate(F, DT); }
  ~ParallelRegionInfo() { releaseMemory(); }

  /// Type for the mapping of basic blocks to parallel tasks.
  using ParallelTaskMappingTy = DenseMap<BasicBlock *, const ParallelTask *>;

  /// Identify the parallel regions in @p F from scratch.
  ///
  /// Note that only the extend of the parallel regions and there nesting is
  /// stored but not the mapping from basic blocks to the surrounding parallel
  /// task that is returned. Only the former information can be preserved easily
  /// by passes but the latter is generally only valid at creation time. For an
  /// on-demand computation of this mapping use the createMapping() function.
  ///
  /// @returns A mapping from basic blocks to the surrounding parallel task.
  ParallelTaskMappingTy recalculate(Function &F, const DominatorTree &DT);

  /// Return the top-level parallel regions in this function.
  ///
  ///{
  ParallelRegionVectorTy &getTopLevelParallelRegions() {
    return TopLevelParallelRegions;
  }
  const ParallelRegionVectorTy &getTopLevelParallelRegions() const {
    return TopLevelParallelRegions;
  }
  ///}

  /// Return true if there is no parallel region in this function.
  bool empty() const { return TopLevelParallelRegions.empty(); }

  /// Remove all cached parallel regions of this function.
  void clear() { TopLevelParallelRegions.clear(); }

  /// Return the number of top level parallel regions in this function.
  ParallelRegionVectorTy::size_type size() const {
    return TopLevelParallelRegions.size();
  }

  /// Iterators for parallel regions.
  ///
  ///{
  iterator begin() { return TopLevelParallelRegions.begin(); }
  iterator end() { return TopLevelParallelRegions.end(); }
  const_iterator begin() const { return TopLevelParallelRegions.begin(); }
  const_iterator end() const { return TopLevelParallelRegions.end(); }
  ///}

  /// Return the parallel region that makes @p L a parallel loop, if any.
  ///
  /// A parallel loop is a loop that does fork (parts of) its body to a new
  /// task which are joined only after the loop. It is also ensured that
  /// everything that is not forked but part of the loop does not cause
  /// side-effects. These instructions usually compute the exit condition
  /// and the new values for the induction variable(s).
  ///
  /// Note: We allow multiple induction variables and complex exit conditions
  /// here. However, for some parallel runtimes these have to be simplified,
  /// e.g. expressed with regards to one canonical induction variable.
  /// TODO: Provide an interface to perform the simplification.
  ///
  /// Note: We allow arbitrary code after the loop but before a join
  /// instruction.
  ParallelRegion *getParallelLoopRegion(const Loop &L,
                                        const DominatorTree &DT) const;

  /// Return true if @p L is a parallel loop.
  ///
  /// See ParallelRegionInfo::getParallelLoopRegion for more information.
  bool isParallelLoop(const Loop &L, const DominatorTree &DT) const;

  /// Check for containment in any parallel region.
  ///{
  bool containedInAny(const BasicBlock *BB, const DominatorTree &DT) const;
  bool containedInAny(const Instruction *I, const DominatorTree &DT) const;
  bool containedInAny(const Loop *L, const DominatorTree &DT) const;
  ///}

  /// Check for possible containment in any parallel region.
  ///{
  bool maybeContainedInAny(const BasicBlock *BB, const DominatorTree &DT) const;
  bool maybeContainedInAny(const Instruction *I, const DominatorTree &DT) const;
  bool maybeContainedInAny(const Loop *L, const DominatorTree &DT) const;
  ///}

  /// Return true if promoting @p AI will not interfere with parallel regions.
  bool isSafeToPromote(const AllocaInst &AI, const DominatorTree &DT) const;

  /// Compute a _new_ mapping from basic block to parallel task (and region).
  ///
  /// This function is linear in the number of blocks contained in parallel
  /// regions.
  ///
  /// @returns A mapping from basic blocks to the surrounding parallel task.
  ParallelTaskMappingTy createMapping() const;

  /// Delete all memory allocated for parallel regions.
  void releaseMemory();

  /// Pretty print the parallel regions of the function.
  ///{
  void print(raw_ostream &OS) const;
  void dump() const;
  ///}
};

/// A parallel task is a single entry, multiple exit CFG region that represents
/// code that can be executed in parallel to its sibling task.
class ParallelTask {

  /// The surrounding parallel region.
  ParallelRegion &ParentRegion;

  /// The single entry block of this task.
  BasicBlock &EntryBB;

  /// The set of halts (for forked tasks) or joins (for continuation tasks) that
  /// terminate this task.
  SetVector<TerminatorInst *> HaltsOrJoints;

  /// Creator functions for parallel tasks.
  ///{
  ParallelTask(ParallelRegion &ParentRegion, BasicBlock &EntryBB);

  /// Add a halt or join to this parallel task.
  void addHaltOrJoin(TerminatorInst &TI);

  ///}

public:
  /// Return the parallel region surrounding this task.
  ParallelRegion &getParentRegion() const { return ParentRegion; }

  /// Return the start block of this parallel task, thus the successor of the
  /// fork that started the parallel region.
  BasicBlock &getEntry() const { return EntryBB; }

  /// Return true if this is a forked task.
  bool isForkedTask() const;

  /// Return true if this is a continuation task.
  bool isContinuationTask() const;

  /// Return the sibling task of this one.
  ParallelTask &getSiblingTask() const;

  /// Return all halts (for forked tasks) or joins (for continuation tasks).
  const SetVector<TerminatorInst *> &getHaltsOrJoints() const {
    return HaltsOrJoints;
  }

  /// The contain interface is designed deliberately different from similar
  /// functions like Loop::contains(*) as it takes a dominator tree as a second
  /// argument. This allows both ParallelTask and ParallelRegion to remain valid
  /// even if transformations change the CFG structure inside them. As a
  /// consequence there are less modifications needed in the existing code base.
  ///
  /// The cost for all three calls is the same as they can all be reduced to the
  /// lookup of a single basic block.
  ///{
  bool contains(const BasicBlock *BB, const DominatorTree &DT) const;
  bool contains(const Instruction *I, const DominatorTree &DT) const;
  bool contains(const Loop *L, const DominatorTree &DT) const;
  ///}

  /// Potentially approximated but fast contains check.
  ///
  /// If mayContain returns false contains will also return false. If
  /// mayContains returns true, contains can either return true or false.
  ///
  /// In case hasSingleExit(*) returns true, these functions are  equal to
  /// a call to contains.
  ///
  /// Note that this function is already used by the contains interface. Use it
  /// only if approximate information is sufficient, otherwise just call
  /// contains.
  ///{
  bool mayContain(const BasicBlock *BB, const DominatorTree &DT) const;
  bool mayContain(const Instruction *I, const DominatorTree &DT) const;
  bool mayContain(const Loop *L, const DominatorTree &DT) const;

  /// Return true if this region has a single exit (halt or join).
  bool hasSingleExit() const { return HaltsOrJoints.size() == 1;}
  ///}

  /// A generic visitor interface as an alternative to an iterator. See the
  /// comments if ParallelTask::contains(*) and the ParallelRegionInfo class
  /// description for the rational.
  ///
  ///{

  /// Type of the visitor function.
  ///
  /// Note that the return value indicates if the traversal should continue.
  using VisitorTy = std::function<bool(BasicBlock &, const ParallelTask &)>;

  /// Invokes the @p Visitor on each block of the task or until @p Visitor
  /// returns false.
  ///
  /// @param Visitor   The user provided visitor function.
  /// @param Recursive If not set this task will visit all contained blocks,
  ///                  thus the second argument for the @p Visitor function will
  ///                  always be this task. If set, parallel tasks contained in
  ///                  this one will visit their blocks.
  ///
  /// @returns True, if all blocks have been visited, false otherwise.
  bool visit(VisitorTy &Visitor, bool Recursive = false) const;
  ///}

  /// Pretty print this parallel task.
  ///{
  void print(raw_ostream &OS, unsigned indent = 0) const;
  void dump() const;
  ///}

  friend class ParallelRegion;
  friend class ParallelRegionInfo;
};

/// Pretty print the parallel task @p PT to @p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const ParallelTask &PT) {
  PT.print(OS);
  return OS;
}

/// A parallel region is a single entry, multiple exit CFG region that
/// represents code that can be executed in parallel. It is divided in two
/// parallel tasks, the forked task and the continuation task.
class ParallelRegion {
public:
  using SubRegionMapTy = DenseMap<ForkInst *, ParallelRegion *>;

private:
  /// The parallel regions that are surrounded (and owned) by this one.
  SubRegionMapTy ParallelSubRegions;

  /// The parallel region info analysis.
  ParallelRegionInfo &PRI;

  /// The fork that starts this parallel region.
  ForkInst &Fork;

  /// The parent parallel task or nullptr if this is a top-level region.
  ParallelTask *ParentTask;

  /// The forked parallel task.
  ParallelTask ForkedTask;

  /// The continuation parallel task.
  ParallelTask ContinuationTask;

  /// Creator functions for parallel regions.
  ///
  ///{
  ParallelRegion(ParallelRegionInfo &PRI, ForkInst &Fork,
                 ParallelTask *ParentTask);

  /// Add a sub-region to this one and thereby transfer ownership.
  void addParallelSubRegion(ParallelRegion &ParallelSubRegion);
  ///}

public:
  ~ParallelRegion();

  /// Return the fork that starts this parallel region.
  ForkInst &getFork() const { return Fork; }

  /// Return the parallel task that contains this parallel region.
  ParallelTask *getParentTask() const { return ParentTask; }

  /// Return the set of parallel sub-regions.
  ///{
  SubRegionMapTy &getSubRegions() { return ParallelSubRegions; }
  const SubRegionMapTy &getSubRegions() const {
    return ParallelSubRegions;
  }
  ///}

  /// Return the forked task.
  ///{
  ParallelTask &getForkedTask() { return ForkedTask; }
  const ParallelTask &getForkedTask() const { return ForkedTask; }
  ///}

  /// Return the continuation task.
  ///{
  ParallelTask &getContinuationTask() { return ContinuationTask; }
  const ParallelTask &getContinuationTask() const { return ContinuationTask; }
  ///}

  /// A generic visitor interface as an alternative to an iterator.
  /// See ParallelTask::visit for more information
  ///{
  /// @see ParallelTask::visit(VisitorTy &Visitor, bool Recursive)
  bool visit(ParallelTask::VisitorTy &Visitor, bool Recursive = false) const {
    return ForkedTask.visit(Visitor, Recursive) &&
           ContinuationTask.visit(Visitor, Recursive);
  }
  ///}

  /// Return true if this is a parallel loop region for @p L.
  ///
  /// See ParallelRegionInfo::getParallelLoopRegion for more information.
  bool isParallelLoopRegion(const Loop &L, const DominatorTree &DT) const;

  /// @see ParallelTask::contains(const BasicBlock *BB, DominatorTree &DT)
  bool contains(const BasicBlock *BB, const DominatorTree &DT) const;

  /// @see ParallelTask::contains(const Instruction *I, DominatorTree &DT)
  bool contains(const Instruction *I, const DominatorTree &DT) const;

  /// @see ParallelTask::contains(const Loop *L, DominatorTree &DT)
  bool contains(const Loop *L, const DominatorTree &DT) const;

  /// Return true if both tasks have a single exit (halt or join).
  bool hasTwoSingleExits() const {
    return ForkedTask.hasSingleExit() && ContinuationTask.hasSingleExit();
  }

  /// @see ParallelTask::mayContain(const BasicBlock *BB, DominatorTree &DT)
  bool mayContain(const BasicBlock *BB, const DominatorTree &DT) const;

  /// @see ParallelTask::mayContain(const Instruction *I, DominatorTree &DT)
  bool mayContain(const Instruction *I, const DominatorTree &DT) const;

  /// @see ParallelTask::mayContain(const Loop *L, DominatorTree &DT)
  bool mayContain(const Loop *L, const DominatorTree &DT) const;

  /// Pretty print this parallel region and all sub-regions.
  ///{
  void print(raw_ostream &OS, unsigned indent = 0) const;
  void dump() const;
  ///}

  friend class ParallelRegionInfo;
};

/// Pretty print the parallel region @p PR to @p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const ParallelRegion &PR) {
  PR.print(OS);
  return OS;
}

/// New pass manager wrapper pass around the parallel region info.
class ParallelRegionAnalysis : public AnalysisInfoMixin<ParallelRegionAnalysis> {
  friend AnalysisInfoMixin<ParallelRegionAnalysis>;
  static AnalysisKey Key;

public:
  typedef ParallelRegionInfo Result;

  /// Run the analysis pass over a function and identify the parallel regions.
  ParallelRegionInfo run(Function &F, FunctionAnalysisManager &AM);
};


/// Function pass wrapper around the parallel region info.
class ParallelRegionInfoPass : public FunctionPass {
  ParallelRegionInfo PRI;

public:
  static char ID;
  ParallelRegionInfoPass() : FunctionPass(ID) {}

  /// Return the parallel region info analysis.
  ///{
  ParallelRegionInfo &getParallelRegionInfo() { return PRI; }
  const ParallelRegionInfo &getParallelRegionInfo() const { return PRI; }
  ///}

  /// Initialize the parallel region info for this function.
  bool runOnFunction(Function &F) override;

  /// Verify the analysis as well as some of the functions provided.
  void verifyAnalysis() const override;

  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Pretty print the parallel regions of the function.
  ///{
  void print(raw_ostream &OS, const Module *) const override;
  void dump() const;
  ///}
};

} // End llvm namespace
#endif
