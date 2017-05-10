//===-- llvm/PIRToOpenMP.h - Instruction class definition -------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the declaration of PIRToOpenMPPass class, which
/// implements OpenMP backend code generation for Parallel IR (PIR).
///
/// This pass extracts parallel regions and child parallel tasks into outlined
/// functions. Inside these outlined functions, the required OpenMP structs
/// are created and called as required by the OpenMP runtime to implement
/// task-based parallelism.
///
/// Example:
/// ========
/// For the following parallel region:
///   ...
///   fork label %forked, %cont
///
/// forked:
///   call void @foo()
///   halt label %cont
///
/// cont:
///   call void @bar()
///   join label %join
///
/// join:
/// ...
///
/// , this pass generates the OpenMP runtime code equivalent to the following
/// C code:
/// ...
/// if (omp_get_num_threads() == 1) {
///   #pragma omp parallel
///   {
///     #pragma omp master
///     {
///       #pragma omp task
///       { foo(); }
///       bar();
///       #pragma omp taskwait
///     }
///   }
/// } else {
///       #pragma omp task
///       { foo(); }
///       bar();
///       #pragma omp taskwait
/// }
/// ...
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_PIR_PIRTOOPENMP_H
#define LLVM_TRANSFORMS_PIR_PIRTOOPENMP_H

#include "llvm/Analysis/ParallelRegionInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

namespace llvm {
enum OpenMPRuntimeFunction {
  OMPRTL__kmpc_fork_call,
  OMPRTL__kmpc_for_static_fini,
  OMPRTL__kmpc_master,
  OMPRTL__kmpc_end_master,
  OMPRTL__kmpc_omp_task_alloc,
  OMPRTL__kmpc_omp_task,
  OMPRTL__kmpc_omp_taskwait,
  OMPRTL__kmpc_global_thread_num,
};

enum OpenMPSchedType {
  OMP_sch_static = 34,
};

class PIRToOpenMPPass : public FunctionPass {
public:
  static char ID;

  PIRToOpenMPPass() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  void print(raw_ostream &OS, const Module *) const override;
  void dump() const;

private:
  void startRegionEmission(const ParallelRegion &PR);

  Function *declareOMPRegionFn(Function *RegionFn, bool Nested,
                               ValueToValueMapTy &VMap);

  void replaceExtractedRegionFnCall(CallInst *CI, Function *OMPRegionFn,
                                    Function *OMPNestedRegionFn);

  void emitOMPRegionFn(Function *OMPRegionFn, Function *ForkedFn,
                       Function *ContFn,
                       ArrayRef<Argument *> ForkedFnArgs,
                       ArrayRef<Argument *> ContFnArgs, bool Nested);

  /// Emits declaration of some OMP runtime functions.
  Constant *createRuntimeFunction(OpenMPRuntimeFunction Function, Module *M);
  /// Emits calls to some OMP runtime functions.
  CallInst *emitRuntimeCall(Value *Callee, ArrayRef<Value *> Args,
                            const Twine &Name, BasicBlock *Parent) const;
  /// Emits calls to some OMP runtime functions.
  CallInst *emitRuntimeCall(Value *Callee, ArrayRef<Value *> Args,
                            const Twine &Name, IRBuilder<> &IRBuilder) const;

  /// Emits the implicit args needed for an outlined OMP region function.
  DenseMap<Argument *, Value *> emitImplicitArgs(Function *OMPRegionFn,
                                                 IRBuilder<> &AllocaIRBuilder,
                                                 IRBuilder<> &StoreIRBuilder,
                                                 bool Nested);
  /* void emitImplicitArgs(BasicBlock *PRFuncEntryBB); */

  void emitOMPRegionLogic(Function *OMPRegionFn, IRBuilder<> &IRBuilder,
                          llvm::IRBuilder<> &AllocaIRBuilder,
                          Function *ForkedFn, Function *ContFn,
                          DenseMap<Argument *, Value *> ArgToAllocaMap,
                          ArrayRef<Argument *> ForkedFnArgs,
                          ArrayRef<Argument *> ContFnArgs, bool Nested);

  Value *emitTaskInit(Function *Caller, IRBuilder<> &CallerIRBuilder,
                      IRBuilder<> &CallerAllocaIRBuilder, Function *ForkedFn,
                      ArrayRef<Value *> LoadedCapturedArgs);

  StructType *createSharedsTy(Function *F);
  Function *emitProxyTaskFunction(Type *KmpTaskTWithPrivatesPtrTy,
                                  Type *SharedsPtrTy, Function *TaskFunction,
                                  Value *TaskPrivatesMap);
  Function *emitTaskOutlinedFunction(Module *M, Type *SharedsPtrTy,
                                     Function *ForkedFn);
  void emitTaskwaitCall(Function *Caller, IRBuilder<> &CallerIRBuilder,
                        const DataLayout &DL);

   void emitBlock(Function *F, IRBuilder<> &IRBuilder, BasicBlock *BB,
                 bool IsFinished = false);

  void emitBranch(BasicBlock *Target, IRBuilder<> &IRBuilder);

  Value *getThreadID(Function *F);
  Value *getThreadID(Function *F, IRBuilder<> &IRBuilder);

  Type *getOrCreateIdentTy(Module *M);
  PointerType *getIdentTyPointerTy() const;
  Value *getOrCreateDefaultLocation(Module *M);

  FunctionType *getOrCreateKmpc_MicroTy(LLVMContext &Context);
  PointerType *getKmpc_MicroPointerTy(LLVMContext &Context);

  Type *createKmpTaskTTy(Module *M, PointerType *KmpRoutineEntryPointerQTy);
  Type *createKmpTaskTWithPrivatesTy(Type *KmpTaskTTy);
  void emitKmpRoutineEntryT(Module *M);

  DenseMap<Argument *, Value *> startFunction(Function *Fn);

  Function *emitTaskFunction(const ParallelRegion &PR, bool IsForked) const;

private:
  ParallelRegionInfo *PRI = nullptr;

  StructType *IdentTy = nullptr;
  FunctionType *Kmpc_MicroTy = nullptr;
  Constant *DefaultOpenMPPSource = nullptr;
  Constant *DefaultOpenMPLocation = nullptr;
  PointerType *KmpRoutineEntryPtrTy = nullptr;

  // Maps a funtion representing an outlined top-level region to the alloca
  // instruction of its thread id.
  typedef DenseMap<Function *, Value *> OpenMPThreadIDAllocaMapTy;
  OpenMPThreadIDAllocaMapTy OpenMPThreadIDAllocaMap;
  // Maps a funtion to the instruction where we loaded the thread id addrs
  typedef DenseMap<Function *, Value *> OpenMPThreadIDLoadMapTy;
  OpenMPThreadIDLoadMapTy OpenMPThreadIDLoadMap;

  // Maps an extracted forked function (Using CodeExtractor) to its
  // corresponding task outlined function as required by OMP runtime.
  typedef DenseMap<Function *, Function *> ExtractedToOutlinedMapTy;
  ExtractedToOutlinedMapTy ExtractedToOutlinedMap;

  // Maps an outlined task function to its corresponding task entry function.
  typedef DenseMap<Function *, Function *> OutlinedToEntryMapTy;
  OutlinedToEntryMapTy OutlinedToEntryMap;
};

class PIRToOMPPass : public PassInfoMixin<PIRToOMPPass> {
  static StringRef name() { return "PIRToOMPPass"; }

  PIRToOMPPass() {}

  void run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm

#endif
