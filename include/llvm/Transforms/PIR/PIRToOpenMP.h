#ifndef LLVM_TRANSFORMS_PIR_PIRTOOPENMP_H
#define LLVM_TRANSFORMS_PIR_PIRTOOPENMP_H

#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/ParallelRegionInfo.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/IRBuilder.h"

namespace llvm {
  enum OpenMPRuntimeFunction {
    OMPRTL__kmpc_fork_call
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

    Type *getOrCreateIdentTy(Module *M);
    PointerType *getIdentTyPointerTy() const;
    FunctionType *getOrCreateKmpc_MicroTy(LLVMContext& Context);
    PointerType *getKmpc_MicroPointerTy(LLVMContext& Context);
    void getOrCreateDefaultLocation(Module *M);
    Constant *createRuntimeFunction(OpenMPRuntimeFunction Function,
                                    Module *M);
    CallInst *emitRuntimeCall(Value *Callee,
                              ArrayRef<Value*> Args,
                              const Twine &Name,
                              BasicBlock *Parent) const;
    Function* emitTaskFunction(const ParallelRegion &PR, bool IsForked) const;
    void emitRegionFunction(const ParallelRegion &PR);
    void emitImplicitArgs(BasicBlock* PRFuncEntryBB);
    void emitSections(LLVMContext &C, const DataLayout &DL);
    AllocaInst *createSectionVal(Type *Ty, const Twine &Name, const DataLayout &DL,
                                 Value *Init = nullptr);
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
