#ifndef LLVM_TRANSFORMS_PIR_PIRTOOPENMP_H
#define LLVM_TRANSFORMS_PIR_PIRTOOPENMP_H

#include "llvm/Analysis/ParallelRegionInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
enum OpenMPRuntimeFunction {
  OMPRTL__kmpc_fork_call,
  OMPRTL__kmpc_for_static_fini,
  OMPRTL__kmpc_master,
  OMPRTL__kmpc_end_master,
  OMPRTL__kmpc_omp_task_alloc,
  OMPRTL__kmpc_omp_task,
  OMPRTL__kmpc_omp_taskwait,
};

enum OpenMPSchedType {
  OMP_sch_static = 34,
};

class PIRToOpenMPPass : public FunctionPass {
public:
  static char ID;

  PIRToOpenMPPass()
      : FunctionPass(ID), PRI(nullptr), IdentTy(nullptr), Kmpc_MicroTy(nullptr),
        DefaultOpenMPPSource(nullptr), DefaultOpenMPLocation(nullptr) {}

  bool runOnFunction(Function &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  void print(raw_ostream &OS, const Module *) const override;
  void dump() const;

private:
  /// Emits the outlined function corresponding to the parallel region.
  void emitRegionFunction(const ParallelRegion &PR);
  Function *createOMPRegionFn(Function *RegionFn, Module *Module,
                              LLVMContext &Context);
  /// Emits the outlined function corresponding to the parallel task (whehter
  /// forked or continuation).
  Function *emitTaskFunction(const ParallelRegion &PR, bool IsForked) const;

  /// Emits declaration of some OMP runtime functions.
  Constant *createRuntimeFunction(OpenMPRuntimeFunction Function, Module *M);
  /// Emits calls to some OMP runtime functions.
  CallInst *emitRuntimeCall(Value *Callee, ArrayRef<Value *> Args,
                            const Twine &Name, BasicBlock *Parent) const;
  /// Emits calls to some OMP runtime functions.
  CallInst *emitRuntimeCall(Value *Callee, ArrayRef<Value *> Args,
                            const Twine &Name, IRBuilder<> &IRBuilder) const;

  /// Emits the implicit args needed for an outlined OMP region function.
  void emitImplicitArgs(Function *OMPRegionFn, IRBuilder<> &AllocaIRBuilder,
                        IRBuilder<> &StoreIRBuilder);
  /* void emitImplicitArgs(BasicBlock *PRFuncEntryBB); */

  void emitMasterRegion(Function *OMPRegionFn, IRBuilder<> &IRBuilder);
  /* void emitMasterRegion(Function *F, const DataLayout &DL, Function
   * *ForkedFn, */
  /*                       Function *ContFn); */
  Value *emitTaskInit(Module *M, Function *Caller, IRBuilder<> &CallerIRBuilder,
                      const DataLayout &DL, Function *ForkedFn);
  Function *emitProxyTaskFunction(Module *M, Type *KmpTaskTWithPrivatesPtrTy,
                                  Type *SharedsPtrTy, Value *TaskFunction,
                                  Value *TaskPrivatesMap);
  Function *emitTaskOutlinedFunction(Module *M, Type *SharedsPtrTy,
                                     Function *ForkedFn);
  void emitTaskwaitCall(Function *Caller, IRBuilder<> &CallerIRBuilder,
                        const DataLayout &DL);
  /// Emits code needed to express the semantics of a sections construct
  void emitSections(Function *F, LLVMContext &C, const DataLayout &DL,
                    Function *ForkedFn, Function *ContFn);
  /// Emits code for variables needed by the sections loop.
  AllocaInst *createSectionVal(Type *Ty, const Twine &Name,
                               const DataLayout &DL, Value *Init = nullptr);

  /// Emits declaration code for OMP __kmpc_for_static_init.
  Constant *createForStaticInitFunction(Module *M, unsigned IVSize,
                                        bool IVSigned);
  /// Emits the cond code for the sections construct loop.
  void emitForLoopCond(const DataLayout &DL, Value *IV, Value *UB,
                       BasicBlock *Body, BasicBlock *Exit);
  /// Manages the code emition for all parts of the sections loop.
  void emitOMPInnerLoop(Function *F, LLVMContext &C, const DataLayout &DL,
                        Value *IV, Value *UB,
                        const function_ref<void()> &BodyGen);
  /// Emits code for loop increment logic.
  void emitForLoopInc(Value *IV, const DataLayout &DL);
  /// Calls the __kmpc_for_static_fini runtime function to tell it that
  /// the parallel loop is done.
  void emitForStaticFinish(Function *F, const DataLayout &DL);

  /// A helper to emit a basic block and transform the builder insertion
  /// point to its start.
  void emitBlock(Function *F, IRBuilder<> &IRBuilder, BasicBlock *BB,
                 bool IsFinished = false);
  /// A helper to emit a branch to a BB.
  void emitBranch(BasicBlock *Target, IRBuilder<> &IRBuilder);
  /// Creates an aligned load.
  Value *emitAlignedLoad(Value *Addr, const DataLayout &DL);
  /// Creates an aligned store.
  void emitAlignedStore(Value *Val, Value *Addr, const DataLayout &DL);

  /// Emits code to load the value of the thread ID variable of a parallel
  /// thread.
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

private:
  ParallelRegionInfo *PRI;

  StructType *IdentTy;
  FunctionType *Kmpc_MicroTy;
  Constant *DefaultOpenMPPSource;
  Constant *DefaultOpenMPLocation;
  PointerType *KmpRoutineEntryPtrTy;
  // Maps a funtion representing an outlined top-level region to the alloca
  // instruction of its thread id.
  typedef DenseMap<Function *, Value *> OpenMPThreadIDAllocaMapTy;
  OpenMPThreadIDAllocaMapTy OpenMPThreadIDAllocaMap;
  // Maps a funtion to the instruction where we loaded the thread id addrs
  typedef DenseMap<Function *, Value *> OpenMPThreadIDLoadMapTy;
  OpenMPThreadIDLoadMapTy OpenMPThreadIDLoadMap;

  BitCastInst *AllocaInsertPt;
  IRBuilder<> *AllocaIRBuilder;
  IRBuilder<> *StoreIRBuilder;
};

class PIRToOMPPass : public PassInfoMixin<PIRToOMPPass> {
  static StringRef name() { return "PIRToOMPPass"; }

  PIRToOMPPass() {}

  void run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm

#endif
