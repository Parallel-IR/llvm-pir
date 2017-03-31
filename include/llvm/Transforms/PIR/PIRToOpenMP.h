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
  void emitRegionFunction(const ParallelRegion &PR);
  Function *emitTaskFunction(const ParallelRegion &PR, bool IsForked) const;

  Constant *createRuntimeFunction(OpenMPRuntimeFunction Function, Module *M);
  CallInst *emitRuntimeCall(Value *Callee, ArrayRef<Value *> Args,
                            const Twine &Name, BasicBlock *Parent) const;
  CallInst *emitRuntimeCall(Value *Callee, ArrayRef<Value *> Args,
                            const Twine &Name) const;

  void emitImplicitArgs(BasicBlock *PRFuncEntryBB);

  void emitSections(Function *F, LLVMContext &C, const DataLayout &DL,
                    Function *ForkedFn, Function *ContFn);
  AllocaInst *createSectionVal(Type *Ty, const Twine &Name,
                               const DataLayout &DL, Value *Init = nullptr);

  Constant *createForStaticInitFunction(Module *M, unsigned IVSize,
                                        bool IVSigned);
  void emitForLoopCond(const DataLayout &DL, Value *IV, Value *UB,
                       BasicBlock *Body, BasicBlock *Exit);
  void emitOMPInnerLoop(Function *F, LLVMContext &C, const DataLayout &DL,
                        Value *IV, Value *UB,
                        const function_ref<void()> &BodyGen);
  void emitForLoopInc(Value *IV, const DataLayout &DL);
  void emitForStaticFinish(Function *F, const DataLayout &DL);

  void emitBlock(Function *F, BasicBlock *BB, bool IsFinished = false);
  void emitBranch(BasicBlock *Target);
  Value *emitAlignedLoad(Value *Addr, const DataLayout &DL);
  void emitAlignedStore(Value *Val, Value *Addr, const DataLayout &DL);

  Value *getThreadID(Function *F, const DataLayout &DL);

  Type *getOrCreateIdentTy(Module *M);
  PointerType *getIdentTyPointerTy() const;
  Value *getOrCreateDefaultLocation(Module *M);

  FunctionType *getOrCreateKmpc_MicroTy(LLVMContext &Context);
  PointerType *getKmpc_MicroPointerTy(LLVMContext &Context);

private:
  ParallelRegionInfo *PRI;

  StructType *IdentTy;
  FunctionType *Kmpc_MicroTy;
  Constant *DefaultOpenMPPSource;
  Constant *DefaultOpenMPLocation;
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
}

#endif
