#ifndef LLVM_TRANSFORMS_PIR_PIRTOOPENMP_H
#define LLVM_TRANSFORMS_PIR_PIRTOOPENMP_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/ParallelRegionInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"

namespace llvm {
  enum OpenMPRuntimeFunction {
    OMPRTL__kmpc_fork_call,
    OMPRTL__kmpc_for_static_fini,
  };

  enum OpenMPSchedType {
    OMP_sch_static = 34,
  };

  class PIRToOMPPass : public PassInfoMixin<PIRToOMPPass> {
    static StringRef name() { return "PIRToOMPPass"; }

    PIRToOMPPass() {}

    void run(Function &F, FunctionAnalysisManager &AM);
  };

  class PIRToOpenMPPass : public FunctionPass {
    ParallelRegionInfo *PRI;
    StructType *IdentTy;
    FunctionType *Kmpc_MicroTy;
    Constant *DefaultOpenMPPSource;
    Constant *DefaultOpenMPLocation;

    BitCastInst *AllocaInsertPt;
    IRBuilder<> *AllocaIRBuilder;
    IRBuilder<> *StoreIRBuilder;

    // Maps a funtion representing an outlined top-level region to the alloca
    // instruction of its thread id.
    typedef DenseMap<Function *, Value *> OpenMPThreadIDAllocaMapTy;
    OpenMPThreadIDAllocaMapTy OpenMPThreadIDAllocaMap;

    // Maps a funtion to the instruction where we loaded the thread id addrs
    typedef DenseMap<Function *, Value *> OpenMPThreadIDLoadMapTy;
    OpenMPThreadIDLoadMapTy OpenMPThreadIDLoadMap;

    Type *getOrCreateIdentTy(Module *M);
    PointerType *getIdentTyPointerTy() const;
    FunctionType *getOrCreateKmpc_MicroTy(LLVMContext& Context);
    PointerType *getKmpc_MicroPointerTy(LLVMContext& Context);
    Value *getOrCreateDefaultLocation(Module *M);
    Constant *createRuntimeFunction(OpenMPRuntimeFunction Function,
                                    Module *M);
    CallInst *emitRuntimeCall(Value *Callee,
                              ArrayRef<Value*> Args,
                              const Twine &Name,
                              BasicBlock *Parent) const;
    CallInst *emitRuntimeCall(Value *Callee,
                                               ArrayRef<Value*> Args,
                                               const Twine &Name) const;
    void emitForStaticFinish(Function *F, const DataLayout &DL);
    Function* emitTaskFunction(const ParallelRegion &PR, bool IsForked) const;
    void emitRegionFunction(const ParallelRegion &PR);
    void emitImplicitArgs(BasicBlock* PRFuncEntryBB);
    void emitSections(Function *F, LLVMContext &C, const DataLayout &DL,
                      Function *ForkedFn, Function *ContFn);
    AllocaInst *createSectionVal(Type *Ty, const Twine &Name, const DataLayout &DL,
                                 Value *Init = nullptr);

    Value *getThreadID(Function *F, const DataLayout &DL);
    Constant *createForStaticInitFunction(Module *M, unsigned IVSize,
                                                           bool IVSigned);
    void emitOMPInnerLoop(Function *F, LLVMContext &C, const DataLayout& DL,
                          Value *IV, Value *UB, const function_ref<void()> &BodyGen);
    void emitBlock(Function *F, BasicBlock *BB, bool IsFinished=false);
    void emitBranch(BasicBlock *Target);
    void emitForLoopCond(const DataLayout& DL, Value *IV, Value *UB, BasicBlock *Body,
                                          BasicBlock *Exit);
    Value *emitAlignedLoad(Value *Addr, const DataLayout& DL);
    void emitForLoopInc(Value *IV, const DataLayout& DL);
    void emitAlignedStore(Value *Val, Value *Addr, const DataLayout& DL);
  public:
    static char ID;
  PIRToOpenMPPass() : FunctionPass(ID), PRI(nullptr), IdentTy(nullptr),
      Kmpc_MicroTy(nullptr), DefaultOpenMPPSource(nullptr),
      DefaultOpenMPLocation(nullptr) {
    }

    bool runOnFunction(Function &F) override;
    void getAnalysisUsage(AnalysisUsage &AU) const override;

    void print(raw_ostream &OS, const Module *) const override;
    void dump() const;
  };
}

#endif
