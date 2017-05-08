#include "llvm/Transforms/PIR/PIRToOpenMP.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"

// TODO the way I traverse the regions now results in every one being
// an top-level region. Modify that to traverse in reverse order instead.
// But first make it work for simple regions.

// TODO study the desing of clang's CodeGenFunction for a better generalized
// handling of function code generation logic. For now, things are written in an
// ad-hoc manner based on what is required for our specific case.

using namespace llvm;

bool PIRToOpenMPPass::runOnFunction(Function &F) {
  PRI = &getAnalysis<ParallelRegionInfoPass>().getParallelRegionInfo();

  if (PRI->getTopLevelParallelRegions().size() > 0) {
    auto M = (Module *)F.getParent();
    getOrCreateIdentTy(M);
    getOrCreateDefaultLocation(M);
  }

  for (auto Region : PRI->getTopLevelParallelRegions())
    emitRegionFunction(*Region);

  return false;
}

void PIRToOpenMPPass::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.setPreservesAll();
  AU.addRequired<ParallelRegionInfoPass>();
}

void PIRToOpenMPPass::print(raw_ostream &OS, const Module *) const {
  PRI->print(OS);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void PIRToOpenMPPass::dump() const { PRI->dump(); }
#endif

void PIRToOpenMPPass::emitRegionFunction(const ParallelRegion &PR) {
  // Split the fork instruction parant into 2 BBs to satisfy the assumption
  // of CodeExtractor (single-entry region and the head BB is the entry)
  auto &ForkInst = PR.getFork();
  auto *OldForkInstBB = ForkInst.getParent();
  auto *NewForkInstBB = SplitBlock(OldForkInstBB, &ForkInst);

  auto &ForkedTask = PR.getForkedTask();
  auto &ContTask = PR.getContinuationTask();

  Function *SpawningFn = ForkInst.getParent()->getParent();
  auto *Module = SpawningFn->getParent();
  auto &Context = Module->getContext();

  // Collect all the BBs of forked and continuation tasks for extraction
  std::vector<BasicBlock *> RegionBBs;
  // Keep track of which blocks belong to forked and cont tasks because we
  // are about to replace fork-join instructions by regular branches
  std::vector<BasicBlock *> ForkedBBs;
  std::vector<BasicBlock *> ContBBs;
  RegionBBs.push_back(NewForkInstBB);

  ParallelTask::VisitorTy ForkedVisitor =
      [&RegionBBs, &ForkedBBs](BasicBlock &BB, const ParallelTask &PT) -> bool {
    RegionBBs.push_back(&BB);
    ForkedBBs.push_back(&BB);

    return true;
  };

  ParallelTask::VisitorTy ContVisitor =
      [&RegionBBs, &ContBBs](BasicBlock &BB, const ParallelTask &PT) -> bool {
    RegionBBs.push_back(&BB);
    ContBBs.push_back(&BB);

    return true;
  };

  ForkedTask.visit(ForkedVisitor, true);
  ContTask.visit(ContVisitor, true);

  // Replace fork with branch
  BranchInst::Create(&ForkedTask.getEntry(), &ForkInst);
  ForkInst.eraseFromParent();

  // Replace halts with branches
  for (auto *I : ForkedTask.getHaltsOrJoints()) {
    if (HaltInst *HI = dyn_cast<HaltInst>(I)) {
      BranchInst::Create(HI->getContinuationBB(), HI);
      HI->eraseFromParent();
    } else {
      assert(false && "A forked task is terminated by a join instruction");
    }
  }

  // Replace joins with branches
  for (auto *I : ContTask.getHaltsOrJoints()) {
    if (JoinInst *JI = dyn_cast<JoinInst>(I)) {
      BranchInst::Create(JI->getSuccessor(0), JI);
      JI->eraseFromParent();
    } else {
      assert(false &&
             "A continuation task is terminated by a halt instruction");
    }
  }

  CodeExtractor RegionExtractor(RegionBBs);
  Function *RegionFn = RegionExtractor.extractCodeRegion();

  // Make sure that any value shared accross the parallel region bounds is a
  // pointer not a primitive value.
  for (auto *ParamTy : RegionFn->getFunctionType()->params()) {
    assert(ParamTy->isPointerTy() &&
           "Parallel regions must capture addresses not values!");
  }

  CodeExtractor ForkedExtractor(ForkedBBs);
  Function *ForkedFn = ForkedExtractor.extractCodeRegion();
  // The sub-set of RegionFn args that is passed to ForkedFn
  std::vector<Argument *> ForkedFnArgs;
  std::vector<Argument *> ForkedFnNestedArgs;
  CodeExtractor ContExtractor(ContBBs);
  Function *ContFn = ContExtractor.extractCodeRegion();
  // The sub-set of RegionFn args that is passed to ContFn
  std::vector<Argument *> ContFnArgs;
  std::vector<Argument *> ContFnNestedArgs;

  ValueToValueMapTy VMap;
  auto *OMPRegionFn = createOMPRegionFn(RegionFn, VMap, false);
  ValueToValueMapTy NestedVMap;
  auto *OMPNestedRegionFn = createOMPRegionFn(RegionFn, NestedVMap, true);

  CallInst *ExtractedFnCI = nullptr;
  for (auto &BB : *SpawningFn) {
    if (CallInst *CI = dyn_cast<CallInst>(BB.begin())) {
      // NOTE: I use pointer equality here, is that fine?
      if (RegionFn == CI->getCalledFunction()) {
        ExtractedFnCI = CI;
        break;
      }
    }
  }

  assert(ExtractedFnCI != nullptr &&
         "Couldn't find the call to the extracted region function!");

  replaceExtractedRegionFnCall(
      ExtractedFnCI, OMPRegionFn, VMap, OMPNestedRegionFn, NestedVMap, ForkedFn,
      ForkedFnArgs, ForkedFnNestedArgs, ContFn, ContFnArgs, ContFnNestedArgs);
  emitOMPRegionFn(OMPRegionFn, Context, ForkedFn, ContFn, ForkedFnArgs,
                  ContFnArgs, false);
  emitOMPRegionFn(OMPNestedRegionFn, Context, ForkedFn, ContFn,
                  ForkedFnNestedArgs, ContFnNestedArgs, true);
}

void PIRToOpenMPPass::emitOMPRegionFn(
    Function *OMPRegionFn, LLVMContext &Context, Function *ForkedFn,
    Function *ContFn, const std::vector<Argument *> &ForkedFnArgs,
    const std::vector<Argument *> &ContFnArgs, bool Nested) {
  auto *EntryBB = BasicBlock::Create(Context, "entry", OMPRegionFn, nullptr);
  auto Int32Ty = Type::getInt32Ty(Context);
  Value *Undef = UndefValue::get(Int32Ty);
  auto *AllocaInsertPt = new BitCastInst(Undef, Int32Ty, "allocapt", EntryBB);
  IRBuilder<> AllocaIRBuilder(EntryBB,
                              ((Instruction *)AllocaInsertPt)->getIterator());

  IRBuilder<> StoreIRBuilder(EntryBB);
  auto ParamToAllocaMap =
      emitImplicitArgs(OMPRegionFn, AllocaIRBuilder, StoreIRBuilder, Nested);

  emitMasterRegion(OMPRegionFn, StoreIRBuilder, AllocaIRBuilder, ForkedFn,
                   ContFn, ParamToAllocaMap, ForkedFnArgs, ContFnArgs, Nested);

  StoreIRBuilder.CreateRetVoid();

  AllocaInsertPt->eraseFromParent();
}

void PIRToOpenMPPass::replaceExtractedRegionFnCall(
    CallInst *CI, Function *OMPRegionFn, ValueToValueMapTy &VMap,
    Function *OMPNestedRegionFn, ValueToValueMapTy &NestedVMap,
    Function *ForkedFn, std::vector<Argument *> &ForkedFnArgs,
    std::vector<Argument *> &ForkedFnNestedArgs, Function *ContFn,
    std::vector<Argument *> &ContFnArgs,
    std::vector<Argument *> &ContFnNestedArgs) {
  auto *RegionFn = CI->getCalledFunction();
  auto *SpawningFn = CI->getParent()->getParent();
  auto *Module = SpawningFn->getParent();
  auto &Context = Module->getContext();

  IRBuilder<> IRBuilder(CI);
  std::vector<Value *> OMPRegionFnArgs = {
      DefaultOpenMPLocation,
      ConstantInt::getSigned(Type::getInt32Ty(Context), RegionFn->arg_size()),
      IRBuilder.CreateBitCast(OMPRegionFn, getKmpc_MicroPointerTy(Context))};

  std::vector<Value *> OMPNestedRegionFnArgs;
  auto ArgIt = CI->arg_begin();

  while (ArgIt != CI->arg_end()) {
    OMPRegionFnArgs.push_back(ArgIt->get());
    OMPNestedRegionFnArgs.push_back(ArgIt->get());
    ++ArgIt;
  }

  auto *BBRemainder = CI->getParent()->splitBasicBlock(
      std::next(CI->getIterator()), "remainder");

  auto *GetNumThreadsFn = Module->getOrInsertFunction(
      "omp_get_num_threads",
      FunctionType::get(Type::getInt32Ty(Context), false));
  auto *NonNestedBB =
      BasicBlock::Create(Context, "non.nested", SpawningFn, nullptr);
  auto *NestedBB = BasicBlock::Create(Context, "nested", SpawningFn, nullptr);
  CI->getParent()->getTerminator()->eraseFromParent();

  auto *IsNotNested = IRBuilder.CreateICmpEQ(
      IRBuilder.CreateCall(GetNumThreadsFn), IRBuilder.getInt32(1));
  IRBuilder.CreateCondBr(IsNotNested, NonNestedBB, NestedBB);

  IRBuilder.SetInsertPoint(NonNestedBB);
  auto ForkRTFn = createRuntimeFunction(
      OpenMPRuntimeFunction::OMPRTL__kmpc_fork_call, Module);
  // Replace the old call with __kmpc_fork_call
  emitRuntimeCall(ForkRTFn, OMPRegionFnArgs, "", IRBuilder);
  IRBuilder.CreateBr(BBRemainder);

  IRBuilder.SetInsertPoint(NestedBB);
  // Replace the old call with a call to the nested version of the parallel
  // region.
  emitRuntimeCall(OMPNestedRegionFn, OMPNestedRegionFnArgs, "", IRBuilder);
  IRBuilder.CreateBr(BBRemainder);

  ForkedFnArgs = findCalledFnArgs(CI, ForkedFn, VMap);
  ForkedFnNestedArgs = findCalledFnArgs(CI, ForkedFn, NestedVMap);
  ContFnArgs = findCalledFnArgs(CI, ContFn, VMap);
  ContFnNestedArgs = findCalledFnArgs(CI, ContFn, NestedVMap);

  CI->eraseFromParent();
}

// This function assumes the following:
//   1 - ParentCall->getCalledFunction() was extracted using CodeExtractor.
//   2 - ParentCall->getCalledFunction() had a list of BBs inside its extracted
//   body which were extracted using CodeExtractor which in turn replaced these
//   BBs with a call to the extracted function ChildFn.
//
// It returns a sub-list of ParentCall->getCalledFunction()'s arguments that
// ChildFn should be passed.
// TODO add example.
std::vector<Argument *>
PIRToOpenMPPass::findCalledFnArgs(CallInst *ParentCall, Function *ChildFn,
                                  ValueToValueMapTy &VMap) {
  auto *ParentFn = ParentCall->getCalledFunction();
  std::vector<Argument *> FilteredArgs;

  // Locate the CallInst to ChildFn
  for (auto &BB : *ParentFn) {
    if (CallInst *CI = dyn_cast<CallInst>(BB.begin())) {
      if (ChildFn == CI->getCalledFunction()) {
        auto ChildArgIt = CI->arg_begin();

        while (ChildArgIt != CI->arg_end()) {
          for (auto &Arg : ParentFn->args()) {
            if (ChildArgIt->get() == &Arg) {
              FilteredArgs.push_back(dyn_cast<Argument>(VMap[&Arg]));
              break;
            }
          }

          ++ChildArgIt;
        }

        break;
      }
    }
  }

  return FilteredArgs;
}

void PIRToOpenMPPass::addRegionFnArgs(Function *RegionFn, Module *Module,
                                      LLVMContext &Context,
                                      std::vector<Type *> &FnParams,
                                      std::vector<StringRef> &FnArgNames,
                                      std::vector<AttributeSet> &FnArgAttrs,
                                      int ArgOffset) {
  DataLayout DL(Module);
  auto &RegionFnArgList = RegionFn->getArgumentList();

  for (auto &Arg : RegionFnArgList) {
    assert(Arg.getType()->isPointerTy() &&
           "Parallel regions must capture addresses not values!");

    FnParams.push_back(Arg.getType());
    FnArgNames.push_back(Arg.getName());

    // Allow speculative loading from shared data.
    // NOTE: check https://www.cs.nmsu.edu/~rvinyard/itanium/speculation.htm
    // for more info on speculation
    AttrBuilder B;
    B.addDereferenceableAttr(
        DL.getTypeAllocSize(Arg.getType()->getPointerElementType()));
    FnArgAttrs.push_back(AttributeSet::get(Context, ++ArgOffset, B));
  }
}

Function *PIRToOpenMPPass::createRegionFn(Function *RegionFn, Module *Module,
                                          LLVMContext &Context,
                                          std::vector<Type *> &FnParams,
                                          std::vector<StringRef> &FnArgNames,
                                          std::vector<AttributeSet> &FnArgAttrs,
                                          const Twine &Name) {
  auto *OMPRegionFnTy =
      FunctionType::get(Type::getVoidTy(Context), FnParams, false);

  Function *OMPRegionFn = dyn_cast<Function>(
      Module->getOrInsertFunction(Name.str(), OMPRegionFnTy));
  auto &FnArgList = OMPRegionFn->getArgumentList();

  int i = 0;
  for (auto &Arg : FnArgList) {
    Arg.setName(FnArgNames[i]);
    ++i;
  }

  auto ArgIt = FnArgList.begin();

  for (auto &ArgAttr : FnArgAttrs) {
    (*ArgIt).addAttr(ArgAttr);
    ++ArgIt;
  }

  return OMPRegionFn;
}

Function *PIRToOpenMPPass::createOMPRegionFn(Function *RegionFn,
                                             ValueToValueMapTy &VMap,
                                             bool Nested) {
  auto *Module = RegionFn->getParent();
  auto &Context = Module->getContext();
  DataLayout DL(Module);

  std::vector<Type *> FnParams;
  std::vector<StringRef> FnArgNames;
  std::vector<AttributeSet> FnArgAttrs;

  if (!Nested) {
    FnParams.push_back(PointerType::getUnqual(Type::getInt32Ty(Context)));
    FnParams.push_back(PointerType::getUnqual(Type::getInt32Ty(Context)));

    FnArgNames.push_back(".global_tid.");
    FnArgNames.push_back(".bound_tid.");

    FnArgAttrs.push_back(AttributeSet::get(Context, 1, Attribute::NoAlias));
    FnArgAttrs.push_back(AttributeSet::get(Context, 2, Attribute::NoAlias));
  }

  int Offset = Nested ? 0 : 2;
  addRegionFnArgs(RegionFn, Module, Context, FnParams, FnArgNames, FnArgAttrs,
                  Offset);

  auto *OMPRegionFn = createRegionFn(
      RegionFn, Module, Context, FnParams, FnArgNames, FnArgAttrs,
      RegionFn->getName() + (Nested ? ".Nested.OMP" : ".OMP"));

  // If this is an outermost region, skip the first 2 arguments (global_tid and
  // bound_tid) ...
  auto OMPArgIt = OMPRegionFn->arg_begin();
  if (!Nested) {
    ++OMPArgIt;
    ++OMPArgIt;
  }
  // ... then map corresponding arguments in RegionFn and OMPRegionFn
  auto &RegionFnArgList = RegionFn->getArgumentList();

  for (auto &Arg : RegionFnArgList) {
    VMap[&Arg] = &*OMPArgIt;
    ++OMPArgIt;
  }

  return OMPRegionFn;
}

Function *PIRToOpenMPPass::emitTaskFunction(const ParallelRegion &PR,
                                            bool IsForked) const {
  auto &F = *PR.getFork().getParent()->getParent();
  auto &M = *(Module *)F.getParent();

  // Generate the name of the outlined function for the task
  auto FName = F.getName();
  auto PRName = PR.getFork().getParent()->getName();
  auto PT = IsForked ? PR.getForkedTask() : PR.getContinuationTask();
  auto PTName = PT.getEntry().getName();
  auto PTFName = FName + "." + PRName + "." + PTName;

  auto PTFunction = (Function *)M.getOrInsertFunction(
      PTFName.str(), Type::getVoidTy(M.getContext()), NULL);

  ParallelTask::VisitorTy Visitor =
      [PTFunction](BasicBlock &BB, const ParallelTask &PT) -> bool {
    BB.removeFromParent();
    BB.insertInto(PTFunction);
    return true;
  };

  PT.visit(Visitor, true);
  auto &LastBB = PTFunction->back();
  assert((dyn_cast<HaltInst>(LastBB.getTerminator()) ||
          dyn_cast<JoinInst>(LastBB.getTerminator())) &&
         "Should have been halt or join");
  LastBB.getTerminator()->eraseFromParent();

  BasicBlock *PTFuncExitBB =
      BasicBlock::Create(M.getContext(), "exit", PTFunction, nullptr);
  ReturnInst::Create(M.getContext(), PTFuncExitBB);

  BranchInst::Create(PTFuncExitBB, &LastBB);

  return PTFunction;
}

Constant *PIRToOpenMPPass::createRuntimeFunction(OpenMPRuntimeFunction Function,
                                                 Module *M) {
  Constant *RTLFn = nullptr;
  switch (Function) {
  case OMPRTL__kmpc_fork_call: {
    Type *TypeParams[] = {getIdentTyPointerTy(),
                          Type::getInt32Ty(M->getContext()),
                          getKmpc_MicroPointerTy(M->getContext())};
    FunctionType *FnTy =
        FunctionType::get(Type::getVoidTy(M->getContext()), TypeParams, true);
    RTLFn = M->getOrInsertFunction("__kmpc_fork_call", FnTy);
    break;
  }
  case OMPRTL__kmpc_for_static_fini: {
    Type *TypeParams[] = {getIdentTyPointerTy(),
                          Type::getInt32Ty(M->getContext())};
    FunctionType *FnTy = FunctionType::get(Type::getVoidTy(M->getContext()),
                                           TypeParams, /*isVarArg*/ false);
    RTLFn = M->getOrInsertFunction("__kmpc_for_static_fini", FnTy);
    break;
  }
  case OMPRTL__kmpc_master: {
    Type *TypeParams[] = {getIdentTyPointerTy(),
                          Type::getInt32Ty(M->getContext())};
    FunctionType *FnTy = FunctionType::get(Type::getInt32Ty(M->getContext()),
                                           TypeParams, /*isVarArg=*/false);
    RTLFn = M->getOrInsertFunction("__kmpc_master", FnTy);
    break;
  }
  case OMPRTL__kmpc_end_master: {
    Type *TypeParams[] = {getIdentTyPointerTy(),
                          Type::getInt32Ty(M->getContext())};
    FunctionType *FnTy = FunctionType::get(Type::getVoidTy(M->getContext()),
                                           TypeParams, /*isVarArg=*/false);
    RTLFn = M->getOrInsertFunction("__kmpc_end_master", FnTy);
    break;
  }
  case OMPRTL__kmpc_omp_task_alloc: {
    auto *Int32Ty = Type::getInt32Ty(M->getContext());
    auto *VoidPtrTy = Type::getInt8PtrTy(M->getContext());
    // TODO double check for how SizeTy get created. Eventually, it get emitted
    // as i64 on my machine.
    auto *SizeTy = Type::getInt64Ty(M->getContext());
    Type *TypeParams[] = {
        getIdentTyPointerTy(), Int32Ty, Int32Ty, SizeTy, SizeTy,
        KmpRoutineEntryPtrTy};
    FunctionType *FnTy =
        FunctionType::get(VoidPtrTy, TypeParams, /*isVarArg=*/false);
    RTLFn = M->getOrInsertFunction("__kmpc_omp_task_alloc", FnTy);
    break;
  }
  case OMPRTL__kmpc_omp_task: {
    auto *Int32Ty = Type::getInt32Ty(M->getContext());
    auto *VoidPtrTy = Type::getInt8PtrTy(M->getContext());
    Type *TypeParams[] = {getIdentTyPointerTy(), Int32Ty, VoidPtrTy};
    FunctionType *FnTy =
        FunctionType::get(Int32Ty, TypeParams, /*isVarArg=*/false);
    RTLFn = M->getOrInsertFunction("__kmpc_omp_task", FnTy);
    break;
  }
  case OMPRTL__kmpc_omp_taskwait: {
    auto *Int32Ty = Type::getInt32Ty(M->getContext());
    Type *TypeParams[] = {getIdentTyPointerTy(), Int32Ty};
    FunctionType *FnTy =
        FunctionType::get(Int32Ty, TypeParams, /*isVarArg=*/false);
    RTLFn = M->getOrInsertFunction("__kmpc_omp_taskwait", FnTy);
    break;
  }
  case OMPRTL__kmpc_global_thread_num: {
    auto *Int32Ty = Type::getInt32Ty(M->getContext());
    Type *TypeParams[] = {getIdentTyPointerTy()};
    FunctionType *FnTy =
        FunctionType::get(Int32Ty, TypeParams, /*isVarArg=*/false);
    RTLFn = M->getOrInsertFunction("__kmpc_global_thread_num", FnTy);
    break;
  }
  }
  return RTLFn;
}

CallInst *PIRToOpenMPPass::emitRuntimeCall(Value *Callee,
                                           ArrayRef<Value *> Args,
                                           const Twine &Name,
                                           BasicBlock *Parent) const {
  IRBuilder<> Builder(Parent);
  CallInst *call = Builder.CreateCall(Callee, Args, None, Name);
  return call;
}

CallInst *PIRToOpenMPPass::emitRuntimeCall(Value *Callee,
                                           ArrayRef<Value *> Args,
                                           const Twine &Name,
                                           IRBuilder<> &IRBuilder) const {
  CallInst *call = IRBuilder.CreateCall(Callee, Args, None, Name);
  return call;
}

DenseMap<Argument *, Value *>
PIRToOpenMPPass::emitImplicitArgs(Function *OMPRegionFn,
                                  IRBuilder<> &AllocaIRBuilder,
                                  IRBuilder<> &StoreIRBuilder, bool Nested) {
  // NOTE check clang's CodeGenFunction::EmitFunctionProlog for a general
  // handling of function prologue emition
  DataLayout DL(OMPRegionFn->getParent());
  DenseMap<Argument *, Value *> ParamToAllocaMap;

  // NOTE according to the docs in CodeGenFunction.h, it is preferable
  // to insert all alloca's at the start of the entry BB. But I am not
  // sure about the technical reason for this. To check later.
  //
  // It turns out this is to guarantee both performance and corrcetness,
  // check http://llvm.org/docs/Frontend/PerformanceTips.html#use-of-allocas

  auto emitArgProlog = [&](Argument &Arg) {
    auto Alloca = AllocaIRBuilder.CreateAlloca(Arg.getType(), nullptr,
                                               Arg.getName() + ".addr");
    Alloca->setAlignment(DL.getTypeAllocSize(Arg.getType()));
    StoreIRBuilder.CreateAlignedStore(&Arg, Alloca,
                                      DL.getTypeAllocSize(Arg.getType()));

    return (Value *)Alloca;
  };

  auto &ArgList = OMPRegionFn->getArgumentList();
  auto ArgI = ArgList.begin();

  if (!Nested) {
    auto GtidAlloca = emitArgProlog(*ArgI);
    // Add an entry for the current function (representing an outlined outer
    // region) and its associated global thread id address
    auto &Elem = OpenMPThreadIDAllocaMap.FindAndConstruct(OMPRegionFn);
    Elem.second = GtidAlloca;
    ++ArgI;

    emitArgProlog(*ArgI);
    ++ArgI;
  }

  while (ArgI != ArgList.end()) {
    auto *Alloca = emitArgProlog(*ArgI);
    auto &ArgToAllocaIt = ParamToAllocaMap.FindAndConstruct(&*ArgI);
    ArgToAllocaIt.second = Alloca;
    ++ArgI;
  }

  return ParamToAllocaMap;
}

void PIRToOpenMPPass::emitMasterRegion(
    Function *OMPRegionFn, IRBuilder<> &IRBuilder,
    ::IRBuilder<> &AllocaIRBuilder, Function *ForkedFn, Function *ContFn,
    DenseMap<Argument *, Value *> ParamToAllocaMap,
    std::vector<Argument *> ForkedFnArgs, std::vector<Argument *> ContFnArgs,
    bool Nested) {
  Module *M = OMPRegionFn->getParent();
  LLVMContext &C = OMPRegionFn->getContext();
  DataLayout DL(OMPRegionFn->getParent());

  auto CapturedArgIt = ParamToAllocaMap.begin();
  std::vector<Value *> ForkedCapArgsLoads;
  std::vector<Value *> ContCapArgsLoads;

  while (CapturedArgIt != ParamToAllocaMap.end()) {
    auto *CapturedArgLoad = IRBuilder.CreateAlignedLoad(
        CapturedArgIt->second,
        DL.getTypeAllocSize(
            CapturedArgIt->second->getType()->getPointerElementType()));

    auto PrepareLoadsVec = [&CapturedArgIt, &CapturedArgLoad](
                               const std::vector<Argument *> &FnArgs,
                               std::vector<Value *> &FnArgLoads) {
      auto ArgIt =
          std::find(FnArgs.begin(), FnArgs.end(), CapturedArgIt->first);
      if (ArgIt != FnArgs.end()) {
        auto Dist = std::distance(FnArgs.begin(), ArgIt);
        if (FnArgLoads.size() <= Dist) {
          FnArgLoads.resize(Dist + 1);
        }
        FnArgLoads[Dist] = CapturedArgLoad;
      }
    };

    PrepareLoadsVec(ForkedFnArgs, ForkedCapArgsLoads);
    PrepareLoadsVec(ContFnArgs, ContCapArgsLoads);

    ++CapturedArgIt;
  }

  BasicBlock *ExitBB =
      BasicBlock::Create(C, "omp_if.end", OMPRegionFn, nullptr);
  ArrayRef<Value *> MasterArgs = {DefaultOpenMPLocation,
                                  getThreadID(OMPRegionFn, IRBuilder)};

  if (!Nested) {
    auto MasterRTFn =
        createRuntimeFunction(OpenMPRuntimeFunction::OMPRTL__kmpc_master, M);
    auto IsMaster = emitRuntimeCall(MasterRTFn, MasterArgs, "", IRBuilder);

    BasicBlock *BodyBB =
        BasicBlock::Create(C, "omp_if.then", OMPRegionFn, nullptr);
    auto Cond = IRBuilder.CreateICmpNE(IsMaster, IRBuilder.getInt32(0));
    IRBuilder.CreateCondBr(Cond, BodyBB, ExitBB);
    IRBuilder.SetInsertPoint(BodyBB);
  }

  auto *NewTask = emitTaskInit(M, OMPRegionFn, IRBuilder, AllocaIRBuilder, DL,
                               ForkedFn, ForkedCapArgsLoads);
  ArrayRef<Value *> TaskArgs = {DefaultOpenMPLocation,
                                getThreadID(OMPRegionFn, IRBuilder), NewTask};
  emitRuntimeCall(
      createRuntimeFunction(OpenMPRuntimeFunction::OMPRTL__kmpc_omp_task, M),
      TaskArgs, "", IRBuilder);

  IRBuilder.CreateCall(ContFn, ContCapArgsLoads);

  emitTaskwaitCall(OMPRegionFn, IRBuilder, DL);

  if (!Nested) {
    auto EndMasterRTFn = createRuntimeFunction(
        OpenMPRuntimeFunction::OMPRTL__kmpc_end_master, M);
    emitRuntimeCall(EndMasterRTFn, MasterArgs, "", IRBuilder);
  }

  emitBranch(ExitBB, IRBuilder);
}

// Creates some data structures that are needed for the actual task work. It
// then calls into emitProxyTaskFunction which starts  code generation for the
// task
Value *PIRToOpenMPPass::emitTaskInit(Module *M, Function *Caller,
                                     IRBuilder<> &CallerIRBuilder,
                                     IRBuilder<> &CallerAllocaIRBuilder,
                                     const DataLayout &DL, Function *ForkedFn,
                                     std::vector<Value *> LoadedCapturedArgs) {
  LLVMContext &C = M->getContext();
  auto *SharedsTy = createSharedsTy(ForkedFn);
  auto *SharedsPtrTy = PointerType::getUnqual(SharedsTy);
  auto *SharedsTySize =
      CallerIRBuilder.getInt64(DL.getTypeAllocSize(SharedsTy));
  emitKmpRoutineEntryT(M);
  auto *KmpTaskTTy = createKmpTaskTTy(M, KmpRoutineEntryPtrTy);
  auto *KmpTaskTWithPrivatesTy = createKmpTaskTWithPrivatesTy(KmpTaskTTy);
  auto *KmpTaskTWithPrivatesPtrTy =
      PointerType::getUnqual(KmpTaskTWithPrivatesTy);
  auto *KmpTaskTWithPrivatesTySize =
      CallerIRBuilder.getInt64(DL.getTypeAllocSize(KmpTaskTWithPrivatesTy));
  auto OutlinedFn = emitTaskOutlinedFunction(M, SharedsPtrTy, ForkedFn);
  auto *TaskPrivatesMapTy = std::next(OutlinedFn->arg_begin(), 3)->getType();
  // NOTE for future use
  auto *TaskPrivatesMap =
      ConstantPointerNull::get(cast<PointerType>(TaskPrivatesMapTy));

  auto *TaskEntry = emitProxyTaskFunction(
      M, KmpTaskTWithPrivatesPtrTy, SharedsPtrTy, OutlinedFn, TaskPrivatesMap);

  // Allocate space to store the captured environment
  auto *AggCaptured =
      CallerAllocaIRBuilder.CreateAlloca(SharedsTy, nullptr, "agg.captured");

  // Store captured arguments into agg.captured
  for (unsigned i = 0; i < LoadedCapturedArgs.size(); ++i) {
    auto *AggCapturedElemPtr = CallerIRBuilder.CreateInBoundsGEP(
        SharedsTy, AggCaptured,
        {CallerIRBuilder.getInt32(0), CallerIRBuilder.getInt32(i)});
    CallerIRBuilder.CreateAlignedStore(
        LoadedCapturedArgs[i], AggCapturedElemPtr,
        DL.getTypeAllocSize(LoadedCapturedArgs[i]->getType()));
  }

  // NOTE check CGOpenMPRuntime::emitTaskInit for the complete set of task
  // flags. We only need tied tasks for now and that's what the 1 value is for.
  auto *TaskFlags = CallerIRBuilder.getInt32(1);
  ArrayRef<Value *> AllocArgs = {
      DefaultOpenMPLocation,
      getThreadID(Caller, CallerIRBuilder),
      TaskFlags,
      KmpTaskTWithPrivatesTySize,
      SharedsTySize,
      CallerIRBuilder.CreatePointerBitCastOrAddrSpaceCast(
          TaskEntry, KmpRoutineEntryPtrTy)};
  auto *NewTask = emitRuntimeCall(
      createRuntimeFunction(OpenMPRuntimeFunction::OMPRTL__kmpc_omp_task_alloc,
                            Caller->getParent()),
      AllocArgs, "", CallerIRBuilder);
  auto *NewTaskNewTaskTTy = CallerIRBuilder.CreatePointerBitCastOrAddrSpaceCast(
      NewTask, KmpTaskTWithPrivatesPtrTy);
  // NOTE: here I diverge from the code generation sequence in OMP clang. See
  // the corresponding function for more details.
  auto *KmpTaskTPtr = CallerIRBuilder.CreateInBoundsGEP(
      KmpTaskTWithPrivatesTy, NewTaskNewTaskTTy,
      {CallerIRBuilder.getInt32(0), CallerIRBuilder.getInt32(0)});
  auto *KmpTaskDataPtr = CallerIRBuilder.CreateInBoundsGEP(
      KmpTaskTTy, KmpTaskTPtr,
      {CallerIRBuilder.getInt32(0), CallerIRBuilder.getInt32(0)});
  auto *KmpTaskData = CallerIRBuilder.CreateAlignedLoad(
      KmpTaskDataPtr,
      DL.getTypeAllocSize(KmpTaskDataPtr->getType()->getPointerElementType()));
  auto *AggCapturedToI8 = CallerIRBuilder.CreatePointerBitCastOrAddrSpaceCast(
      AggCaptured, Type::getInt8PtrTy(C));
  CallerIRBuilder.CreateMemCpy(
      KmpTaskData, AggCapturedToI8,
      CallerIRBuilder.getInt64(DL.getTypeAllocSize(SharedsTy)), 8);

  return NewTask;
}

// Creates a struct that contains elements corresponding to the arguments
// of F.
StructType *PIRToOpenMPPass::createSharedsTy(Function *F) {
  LLVMContext &C = F->getParent()->getContext();
  auto FnParams = F->getFunctionType()->params();

  if (FnParams.size() == 0) {
    return StructType::create("anon", Type::getInt8Ty(C), nullptr);
  }

  return StructType::create(FnParams, "anon");
}

// Emits some boilerplate code to kick off the task work and then calls the
// function that does the actual work.
Function *PIRToOpenMPPass::emitProxyTaskFunction(
    Module *M, Type *KmpTaskTWithPrivatesPtrTy, Type *SharedsPtrTy,
    Function *TaskFunction, Value *TaskPrivatesMap) {

  auto OutlinedToEntryIt = OutlinedToEntryMap.find(TaskFunction);

  if (OutlinedToEntryIt != OutlinedToEntryMap.end()) {
    return OutlinedToEntryIt->second;
  }

  auto &C = M->getContext();
  auto *Int32Ty = Type::getInt32Ty(C);
  ArrayRef<Type *> ArgTys = {Int32Ty, KmpTaskTWithPrivatesPtrTy};
  auto *TaskEntryTy = FunctionType::get(Int32Ty, ArgTys, false);
  auto *TaskEntry = Function::Create(TaskEntryTy, GlobalValue::InternalLinkage,
                                     ".omp_task_entry.", M);
  TaskEntry->addFnAttr(Attribute::NoInline);
  TaskEntry->addFnAttr(Attribute::UWTable);
  DataLayout DL(M);
  auto *EntryBB = BasicBlock::Create(C, "entry", TaskEntry, nullptr);

  IRBuilder<> IRBuilder(EntryBB);
  auto *RetValAddr = IRBuilder.CreateAlloca(Int32Ty, nullptr, "retval");
  RetValAddr->setAlignment(DL.getTypeAllocSize(Int32Ty));

  // TODO replace this with a call to startFunction
  auto Args = TaskEntry->args();
  std::vector<Value *> ArgAllocas(TaskEntry->arg_size());
  auto ArgAllocaIt = ArgAllocas.begin();
  for (auto &Arg : Args) {
    auto *ArgAlloca = IRBuilder.CreateAlloca(Arg.getType(), nullptr, "addr");
    ArgAlloca->setAlignment(DL.getTypeAllocSize(Arg.getType()));
    *ArgAllocaIt = ArgAlloca;
    ++ArgAllocaIt;
  }

  ArgAllocaIt = ArgAllocas.begin();
  for (auto &Arg : Args) {
    IRBuilder.CreateAlignedStore(&Arg, *ArgAllocaIt,
                                 DL.getTypeAllocSize(Arg.getType()));
    ++ArgAllocaIt;
  }

  auto GtidParam = IRBuilder.CreateAlignedLoad(ArgAllocas[0],
                                               DL.getTypeAllocSize(ArgTys[0]));

  auto TDVal = IRBuilder.CreateAlignedLoad(ArgAllocas[1],
                                           DL.getTypeAllocSize(ArgTys[1]));
  auto TaskTBase = IRBuilder.CreateInBoundsGEP(
      TDVal, {IRBuilder.getInt32(0), IRBuilder.getInt32(0)});
  auto PartIDAddr = IRBuilder.CreateInBoundsGEP(
      TaskTBase, {IRBuilder.getInt32(0), IRBuilder.getInt32(2)});
  auto *SharedsAddr = IRBuilder.CreateInBoundsGEP(
      TaskTBase, {IRBuilder.getInt32(0), IRBuilder.getInt32(0)});
  auto Shareds = IRBuilder.CreateAlignedLoad(
      SharedsAddr,
      DL.getTypeAllocSize(SharedsAddr->getType()->getPointerElementType()));
  auto SharedsParam =
      IRBuilder.CreatePointerBitCastOrAddrSpaceCast(Shareds, SharedsPtrTy);
  auto TDParam = IRBuilder.CreatePointerBitCastOrAddrSpaceCast(
      TDVal, Type::getInt8PtrTy(C));
  auto PrivatesParam = ConstantPointerNull::get(Type::getInt8PtrTy(C));

  Value *TaskParams[] = {GtidParam,       PartIDAddr, PrivatesParam,
                         TaskPrivatesMap, TDParam,    SharedsParam};

  IRBuilder.CreateCall(TaskFunction, TaskParams);
  IRBuilder.CreateRet(IRBuilder.getInt32(0));

  auto &Elem = OutlinedToEntryMap.FindAndConstruct(TaskFunction);
  Elem.second = TaskEntry;

  return TaskEntry;
}

Function *PIRToOpenMPPass::emitTaskOutlinedFunction(Module *M,
                                                    Type *SharedsPtrTy,
                                                    Function *ForkedFn) {
  auto ExtractedToOutlinedIt = ExtractedToOutlinedMap.find(ForkedFn);

  if (ExtractedToOutlinedIt != ExtractedToOutlinedMap.end()) {
    return ExtractedToOutlinedIt->second;
  }

  auto &C = M->getContext();
  DataLayout DL(M);

  auto *CopyFnTy =
      FunctionType::get(Type::getVoidTy(C), {Type::getInt8PtrTy(C)}, true);
  auto *CopyFnPtrTy = PointerType::getUnqual(CopyFnTy);

  auto *OutlinedFnTy = FunctionType::get(
      Type::getVoidTy(C),
      {Type::getInt32Ty(C), Type::getInt32PtrTy(C), Type::getInt8PtrTy(C),
       CopyFnPtrTy, Type::getInt8PtrTy(C), SharedsPtrTy},
      false);
  auto *OutlinedFn = Function::Create(
      OutlinedFnTy, GlobalValue::InternalLinkage, ".omp_outlined.", M);
  // Twine OutlinedName = ForkedFn->getName() + ".omp_outlined.";
  // auto *OutlinedFn = (Function *)M->getOrInsertFunction(
  //     OutlinedName.str(), Type::getVoidTy(C), Type::getInt32Ty(C),
  //     Type::getInt32PtrTy(C), Type::getInt8PtrTy(C), CopyFnPtrTy,
  //     Type::getInt8PtrTy(C), SharedsPtrTy, nullptr);
  StringRef ArgNames[] = {".global_tid.", ".part_id.", ".privates.",
                          ".copy_fn.",    ".task_t.",  "__context"};
  int i = 0;
  for (auto &Arg : OutlinedFn->args()) {
    Arg.setName(ArgNames[i]);
    ++i;
  }

  OutlinedFn->setLinkage(GlobalValue::InternalLinkage);
  OutlinedFn->addFnAttr(Attribute::AlwaysInline);
  OutlinedFn->addFnAttr(Attribute::NoUnwind);
  OutlinedFn->addFnAttr(Attribute::UWTable);

  auto ParamToAllocaMap = startFunction(OutlinedFn);
  auto *ContextArg = &*std::prev(OutlinedFn->args().end());
  auto It = ParamToAllocaMap.find(ContextArg);
  assert(It != ParamToAllocaMap.end() && "Argument entry wasn't found");
  auto *ContextAddr = It->second;
  auto *EntryBB = &*OutlinedFn->begin();
  IRBuilder<> IRBuilder(EntryBB->getTerminator());

  // Load the context struct so that we can access the task's accessed data
  auto *Context = IRBuilder.CreateAlignedLoad(
      ContextAddr,
      DL.getTypeAllocSize(ContextAddr->getType()->getPointerElementType()));

  std::vector<Value *> ForkedFnArgs;
  for (unsigned i = 0; i < ForkedFn->getFunctionType()->getNumParams(); ++i) {
    auto *DataAddrEP = IRBuilder.CreateInBoundsGEP(
        Context, {IRBuilder.getInt32(0), IRBuilder.getInt32(i)});
    auto *DataAddr = IRBuilder.CreateAlignedLoad(
        DataAddrEP,
        DL.getTypeAllocSize(DataAddrEP->getType()->getPointerElementType()));
    // auto *Data = IRBuilder.CreateAlignedLoad(
    //     DataAddr,
    //     DL.getTypeAllocSize(DataAddr->getType()->getPointerElementType()));
    ForkedFnArgs.push_back(DataAddr);
  }

  IRBuilder.CreateCall(ForkedFn, ForkedFnArgs);

  auto &Elem = ExtractedToOutlinedMap.FindAndConstruct(ForkedFn);
  Elem.second = OutlinedFn;

  return OutlinedFn;
}

void PIRToOpenMPPass::emitTaskwaitCall(Function *Caller,
                                       IRBuilder<> &CallerIRBuilder,
                                       const DataLayout &DL) {
  ArrayRef<Value *> Args = {DefaultOpenMPLocation,
                            getThreadID(Caller, CallerIRBuilder)};
  emitRuntimeCall(
      createRuntimeFunction(OpenMPRuntimeFunction::OMPRTL__kmpc_omp_taskwait,
                            Caller->getParent()),
      Args, "", CallerIRBuilder);
}

// NOTE check CodeGenFunction::EmitSections for more details
void PIRToOpenMPPass::emitSections(Function *F, LLVMContext &C,
                                   const DataLayout &DL, Function *ForkedFn,
                                   Function *ContFn) {
  auto Int32Ty = Type::getInt32Ty(C);
  auto LB = createSectionVal(Int32Ty, ".omp.sections.lb.", DL,
                             ConstantInt::get(Int32Ty, 0, true));
  // NOTE For now the num of sections is fixed to 1 and as a result simple
  // (i.e. non-nested) regions are handled.
  auto GlobalUBVal = StoreIRBuilder->getInt32(1);
  auto UB = createSectionVal(Int32Ty, ".omp.sections.ub.", DL, GlobalUBVal);
  auto ST = createSectionVal(Int32Ty, ".omp.sections.st.", DL,
                             ConstantInt::get(Int32Ty, 1, true));
  auto IL = createSectionVal(Int32Ty, ".omp.sections.il.", DL,
                             ConstantInt::get(Int32Ty, 0, true));
  auto IV = createSectionVal(Int32Ty, ".omp.sections.iv.", DL);
  auto ThreadID = getThreadID(F);

  auto BodyGen = [this, F, &C, &DL, &IV, ForkedFn, ContFn]() {
    auto *ExitBB = BasicBlock::Create(C, ".omp.sections.exit");
    auto *SwitchStmt =
        StoreIRBuilder->CreateSwitch(emitAlignedLoad(IV, DL), ExitBB, 2);
    auto CaseGen = [this, F, &C, ExitBB, SwitchStmt](int TaskNum,
                                                     Function *TaskFn) {
      auto CaseBB = BasicBlock::Create(C, ".omp.sections.case");
      emitBlock(F, *StoreIRBuilder, CaseBB);
      StoreIRBuilder->CreateCall(TaskFn);
      SwitchStmt->addCase(StoreIRBuilder->getInt32(TaskNum), CaseBB);
      emitBranch(ExitBB, *StoreIRBuilder);
    };

    CaseGen(0, ForkedFn);
    CaseGen(1, ContFn);
    emitBlock(F, *StoreIRBuilder, ExitBB, true);
  };

  auto ForStaticInitFunction =
      createForStaticInitFunction(F->getParent(), 32, true);
  // NOTE this is code emittion for our specific case; for a complete
  // implementation
  // , in case it is needed later, check emitForStaticInitCall in Clang
  llvm::Value *Args[] = {
      getOrCreateDefaultLocation(F->getParent()),
      ThreadID,
      ConstantInt::get(Int32Ty,
                       OpenMPSchedType::OMP_sch_static), // Schedule type
      IL,                                                // &isLastIter
      LB,                                                // &LB
      UB,                                                // &UB
      ST,                                                // &Stride
      StoreIRBuilder->getIntN(32, 1),                    // Incr
      StoreIRBuilder->getIntN(32, 1)                     // Chunk
  };
  emitRuntimeCall(ForStaticInitFunction, Args, "", *StoreIRBuilder);

  auto *UBVal = emitAlignedLoad(UB, DL);
  auto *MinUBGlobalUB = StoreIRBuilder->CreateSelect(
      StoreIRBuilder->CreateICmpSLT(UBVal, GlobalUBVal), UBVal, GlobalUBVal);
  emitAlignedStore(MinUBGlobalUB, UB, DL);
  auto *LBVal = emitAlignedLoad(LB, DL);
  emitAlignedStore(LBVal, IV, DL);

  emitOMPInnerLoop(F, C, DL, IV, UB, BodyGen);
  emitForStaticFinish(F, DL);
}

AllocaInst *PIRToOpenMPPass::createSectionVal(Type *Ty, const Twine &Name,
                                              const DataLayout &DL,
                                              Value *Init) {
  auto Alloca = AllocaIRBuilder->CreateAlloca(Ty, nullptr, Name);
  Alloca->setAlignment(DL.getTypeAllocSize(Ty));
  if (Init) {
    StoreIRBuilder->CreateAlignedStore(Init, Alloca, DL.getTypeAllocSize(Ty));
  }
  return Alloca;
}

Constant *PIRToOpenMPPass::createForStaticInitFunction(Module *M,
                                                       unsigned IVSize,
                                                       bool IVSigned) {
  assert((IVSize == 32 || IVSize == 64) &&
         "IV size is not compatible with the omp runtime");
  auto &C = M->getContext();
  auto Name = IVSize == 32 ? (IVSigned ? "__kmpc_for_static_init_4"
                                       : "__kmpc_for_static_init_4u")
                           : (IVSigned ? "__kmpc_for_static_init_8"
                                       : "__kmpc_for_static_init_8u");
  auto ITy = IVSize == 32 ? Type::getInt32Ty(C) : Type::getInt64Ty(C);
  auto PtrTy = PointerType::getUnqual(ITy);
  llvm::Type *TypeParams[] = {
      getIdentTyPointerTy(),                             // loc
      Type::getInt32Ty(C),                               // tid
      Type::getInt32Ty(C),                               // schedtype
      llvm::PointerType::getUnqual(Type::getInt32Ty(C)), // p_lastiter
      PtrTy,                                             // p_lower
      PtrTy,                                             // p_upper
      PtrTy,                                             // p_stride
      ITy,                                               // incr
      ITy                                                // chunk
  };
  FunctionType *FnTy =
      FunctionType::get(Type::getVoidTy(C), TypeParams, /*isVarArg*/ false);
  return M->getOrInsertFunction(Name, FnTy);
}

void PIRToOpenMPPass::emitForLoopCond(const DataLayout &DL, Value *IV,
                                      Value *UB, BasicBlock *Body,
                                      BasicBlock *Exit) {
  auto IVVal = emitAlignedLoad(IV, DL);
  auto UBVal = emitAlignedLoad(UB, DL);
  auto Cond = StoreIRBuilder->CreateICmpSLE(IVVal, UBVal);
  Cond->setName("cmp");
  StoreIRBuilder->CreateCondBr(Cond, Body, Exit);
}

void PIRToOpenMPPass::emitOMPInnerLoop(Function *F, LLVMContext &C,
                                       const DataLayout &DL, Value *IV,
                                       Value *UB,
                                       const function_ref<void()> &BodyGen) {
  auto CondBlock = BasicBlock::Create(C, "omp.inner.for.cond");
  emitBlock(F, *StoreIRBuilder, CondBlock);

  auto ExitBlock = BasicBlock::Create(C, "omp.inner.for.end");
  auto LoopBody = BasicBlock::Create(C, "omp.inner.for.body");
  auto Continue = BasicBlock::Create(C, "omp.inner.for.inc");

  emitForLoopCond(DL, IV, UB, LoopBody, ExitBlock);
  emitBlock(F, *StoreIRBuilder, LoopBody);
  BodyGen();
  emitBlock(F, *StoreIRBuilder, Continue);
  emitForLoopInc(IV, DL);
  emitBranch(CondBlock, *StoreIRBuilder);
  emitBlock(F, *StoreIRBuilder, ExitBlock);
}

void PIRToOpenMPPass::emitForLoopInc(Value *IV, const DataLayout &DL) {
  auto IVVal = emitAlignedLoad(IV, DL);
  auto Inc = StoreIRBuilder->CreateAdd(IVVal, StoreIRBuilder->getInt32(1), "",
                                       false, true);
  Inc->setName("inc");
  emitAlignedStore(Inc, IV, DL);
}

void PIRToOpenMPPass::emitForStaticFinish(Function *F, const DataLayout &DL) {
  llvm::Value *Args[] = {getOrCreateDefaultLocation(F->getParent()),
                         getThreadID(F)};
  emitRuntimeCall(
      createRuntimeFunction(OpenMPRuntimeFunction::OMPRTL__kmpc_for_static_fini,
                            F->getParent()),
      Args, "", *StoreIRBuilder);
}

// By the end of emitBlock the IRBuilder will be positioned at the start
// BB
void PIRToOpenMPPass::emitBlock(Function *F, IRBuilder<> &IRBuilder,
                                BasicBlock *BB, bool IsFinished) {
  auto *CurBB = IRBuilder.GetInsertBlock();
  emitBranch(BB, IRBuilder);

  if (IsFinished && BB->use_empty()) {
    delete BB;
    return;
  }

  if (CurBB && CurBB->getParent())
    F->getBasicBlockList().insertAfter(CurBB->getIterator(), BB);
  else
    F->getBasicBlockList().push_back(BB);
  // IRBuilder.SetInsertPoint(BB);
}

void PIRToOpenMPPass::emitBranch(BasicBlock *Target, IRBuilder<> &IRBuilder) {
  auto *CurBB = IRBuilder.GetInsertBlock();

  if (!CurBB || CurBB->getTerminator()) {

  } else {
    IRBuilder.CreateBr(Target);
  }

  // StoreIRBuilder->ClearInsertionPoint();
  IRBuilder.SetInsertPoint(Target);
}

Value *PIRToOpenMPPass::emitAlignedLoad(Value *Addr, const DataLayout &DL) {
  auto *Val = StoreIRBuilder->CreateLoad(Addr);
  Val->setAlignment(DL.getTypeAllocSize(Val->getType()));
  return Val;
}

void PIRToOpenMPPass::emitAlignedStore(Value *Val, Value *Addr,
                                       const DataLayout &DL) {
  auto *Temp = StoreIRBuilder->CreateStore(Val, Addr);
  Temp->setAlignment(DL.getTypeAllocSize(Val->getType()));
}

// NOTE check CGOpenMPRuntime::getThreadID for more details
Value *PIRToOpenMPPass::getThreadID(Function *F, IRBuilder<> &IRBuilder) {
  Value *ThreadID = nullptr;
  auto I = OpenMPThreadIDLoadMap.find(F);
  if (I != OpenMPThreadIDLoadMap.end()) {
    ThreadID = I->second;
    assert(ThreadID != nullptr && "A null thread ID associated to F");
    return ThreadID;
  }

  auto I2 = OpenMPThreadIDAllocaMap.find(F);
  // If F is a top-level region function; get its thread ID alloca and emit
  // a load
  if (I2 != OpenMPThreadIDAllocaMap.end()) {
    DataLayout DL(F->getParent());
    auto Alloca = I2->second;
    auto ThreadIDAddrs = IRBuilder.CreateLoad(Alloca);
    ThreadIDAddrs->setAlignment(DL.getTypeAllocSize(ThreadIDAddrs->getType()));
    ThreadID = IRBuilder.CreateLoad(ThreadIDAddrs);
    ((LoadInst *)ThreadID)
        ->setAlignment(DL.getTypeAllocSize(ThreadID->getType()));
    auto &Elem = OpenMPThreadIDLoadMap.FindAndConstruct(F);
    Elem.second = ThreadID;
    return ThreadID;
  }

  // TODO if we are not in a top-level region function a runtime call should
  // be emitted instead; since there is no implicit argument representing
  // the thread ID.
  auto GTIDFn = createRuntimeFunction(
      OpenMPRuntimeFunction::OMPRTL__kmpc_global_thread_num, F->getParent());
  ThreadID = emitRuntimeCall(GTIDFn, {DefaultOpenMPLocation}, "", IRBuilder);
  auto &Elem = OpenMPThreadIDLoadMap.FindAndConstruct(F);
  Elem.second = ThreadID;

  return ThreadID;
}

Value *PIRToOpenMPPass::getThreadID(Function *F) {
  LoadInst *ThreadID = nullptr;
  auto I = OpenMPThreadIDLoadMap.find(F);
  if (I != OpenMPThreadIDLoadMap.end()) {
    ThreadID = (LoadInst *)I->second;
    assert(ThreadID != nullptr && "A null thread ID associated to F");
    return ThreadID;
  }

  // TODO if we are not in a top-level region function a runtime call should
  // be emitted instead; since there is no implicit argument representing
  // the thread ID.
  return nullptr;
}

Type *PIRToOpenMPPass::getOrCreateIdentTy(Module *M) {
  if (M->getTypeByName("ident_t") == nullptr) {
    IdentTy = StructType::create(
        "ident_t", Type::getInt32Ty(M->getContext()) /* reserved_1 */,
        Type::getInt32Ty(M->getContext()) /* flags */,
        Type::getInt32Ty(M->getContext()) /* reserved_2 */,
        Type::getInt32Ty(M->getContext()) /* reserved_3 */,
        Type::getInt8PtrTy(M->getContext()) /* psource */, nullptr);
  }

  return IdentTy;
}

Type *PIRToOpenMPPass::createKmpTaskTTy(Module *M,
                                        PointerType *KmpRoutineEntryPtrTy) {
  auto &C = M->getContext();
  auto *KmpCmplrdataTy =
      StructType::create("kmp_cmplrdata_t", KmpRoutineEntryPtrTy, nullptr);
  auto *KmpTaskTTy = StructType::create(
      "kmp_task_t", Type::getInt8PtrTy(C), KmpRoutineEntryPtrTy,
      Type::getInt32Ty(C), KmpCmplrdataTy, KmpCmplrdataTy, nullptr);

  return KmpTaskTTy;
}

Type *PIRToOpenMPPass::createKmpTaskTWithPrivatesTy(Type *KmpTaskTTy) {
  auto *KmpTaskTWithPrivatesTy =
      StructType::create("kmp_task_t_with_privates", KmpTaskTTy, nullptr);
  return KmpTaskTWithPrivatesTy;
}

void PIRToOpenMPPass::emitKmpRoutineEntryT(Module *M) {
  if (!KmpRoutineEntryPtrTy) {
    // Build typedef kmp_int32 (* kmp_routine_entry_t)(kmp_int32, void *); type.
    auto &C = M->getContext();
    auto *Int32Ty = Type::getInt32Ty(C);
    ArrayRef<Type *> KmpRoutineEntryTyArgs = {Int32Ty, Type::getInt8PtrTy(C)};
    KmpRoutineEntryPtrTy = PointerType::getUnqual(
        FunctionType::get(Int32Ty, KmpRoutineEntryTyArgs, false));
  }
}

DenseMap<Argument *, Value *> PIRToOpenMPPass::startFunction(Function *Fn) {
  auto *M = Fn->getParent();
  auto &C = M->getContext();
  DataLayout DL(M);
  DenseMap<Argument *, Value *> ParamToAllocaMap;
  auto *EntryBB = BasicBlock::Create(C, "entry", Fn, nullptr);
  IRBuilder<> IRBuilder(EntryBB);
  auto *RetTy = Fn->getReturnType();
  AllocaInst *RetValAddr = nullptr;
  if (!RetTy->isVoidTy()) {
    RetValAddr = IRBuilder.CreateAlloca(RetTy, nullptr, "retval");
    RetValAddr->setAlignment(DL.getTypeAllocSize(RetTy));
  }

  auto Args = Fn->args();
  std::vector<Value *> ArgAllocas(Fn->arg_size());
  auto ArgAllocaIt = ArgAllocas.begin();
  for (auto &Arg : Args) {
    auto *ArgAlloca =
        IRBuilder.CreateAlloca(Arg.getType(), nullptr, Arg.getName() + ".addr");
    ArgAlloca->setAlignment(DL.getTypeAllocSize(Arg.getType()));
    *ArgAllocaIt = ArgAlloca;
    auto &ArgToAllocaIt = ParamToAllocaMap.FindAndConstruct(&Arg);
    ArgToAllocaIt.second = ArgAlloca;
    ++ArgAllocaIt;
  }

  ArgAllocaIt = ArgAllocas.begin();
  for (auto &Arg : Args) {
    IRBuilder.CreateAlignedStore(&Arg, *ArgAllocaIt,
                                 DL.getTypeAllocSize(Arg.getType()));
    ++ArgAllocaIt;
  }

  if (RetTy->isVoidTy()) {
    IRBuilder.CreateRetVoid();
  } else {
    auto *RetVal =
        IRBuilder.CreateAlignedLoad(RetValAddr, DL.getTypeAllocSize(RetTy));
    IRBuilder.CreateRet(RetVal);
  }

  return ParamToAllocaMap;
}

PointerType *PIRToOpenMPPass::getIdentTyPointerTy() const {
  return PointerType::getUnqual(IdentTy);
}

Value *PIRToOpenMPPass::getOrCreateDefaultLocation(Module *M) {
  if (DefaultOpenMPPSource == nullptr) {
    const std::string DefaultLocStr = ";unknown;unknown;0;0;;";
    StringRef DefaultLocStrWithNull(DefaultLocStr.c_str(),
                                    DefaultLocStr.size() + 1);
    DataLayout DL(M);
    // NOTE I am not sure this is the best way to calculate char alignment
    // of the Module target. Revisit later.
    uint64_t Alignment = DL.getTypeAllocSize(Type::getInt8Ty(M->getContext()));
    Constant *C = ConstantDataArray::getString(M->getContext(),
                                               DefaultLocStrWithNull, false);
    // NOTE Are heap allocations not recommended in general or is it OK here?
    // I couldn't find a way to statically allocate an IRBuilder for a Module!
    auto *GV =
        new GlobalVariable(*M, C->getType(), true, GlobalValue::PrivateLinkage,
                           C, ".str", nullptr, GlobalValue::NotThreadLocal);
    GV->setAlignment(Alignment);
    GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    DefaultOpenMPPSource = cast<Constant>(GV);
    DefaultOpenMPPSource = ConstantExpr::getBitCast(
        DefaultOpenMPPSource, Type::getInt8PtrTy(M->getContext()));
  }

  if (DefaultOpenMPLocation == nullptr) {
    // Constant *C = ConstantInt::get(Type::getInt32Ty(M->getContext()), 0,
    // true);
    ArrayRef<Constant *> Members = {
        ConstantInt::get(Type::getInt32Ty(M->getContext()), 0, true),
        ConstantInt::get(Type::getInt32Ty(M->getContext()), 2, true),
        ConstantInt::get(Type::getInt32Ty(M->getContext()), 0, true),
        ConstantInt::get(Type::getInt32Ty(M->getContext()), 0, true),
        DefaultOpenMPPSource};
    Constant *C = ConstantStruct::get(IdentTy, Members);
    auto *GV =
        new GlobalVariable(*M, C->getType(), true, GlobalValue::PrivateLinkage,
                           C, "", nullptr, GlobalValue::NotThreadLocal);
    GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    GV->setAlignment(8);
    DefaultOpenMPLocation = GV;
  }

  return DefaultOpenMPLocation;
}

FunctionType *PIRToOpenMPPass::getOrCreateKmpc_MicroTy(LLVMContext &Context) {
  if (Kmpc_MicroTy == nullptr) {
    Type *MicroParams[] = {PointerType::getUnqual(Type::getInt32Ty(Context)),
                           PointerType::getUnqual(Type::getInt32Ty(Context))};
    Kmpc_MicroTy =
        FunctionType::get(Type::getVoidTy(Context), MicroParams, true);
  }

  return Kmpc_MicroTy;
}

PointerType *PIRToOpenMPPass::getKmpc_MicroPointerTy(LLVMContext &Context) {
  return PointerType::getUnqual(getOrCreateKmpc_MicroTy(Context));
}

void PIRToOMPPass::run(Function &F, FunctionAnalysisManager &AM) {
  // auto &PRI = AM.getResult<ParallelRegionAnalysis>(F);
}

char PIRToOpenMPPass::ID = 0;

INITIALIZE_PASS_BEGIN(PIRToOpenMPPass, "pir2omp", "Lower PIR to OpenMP", true,
                      true)
INITIALIZE_PASS_DEPENDENCY(ParallelRegionInfoPass)
INITIALIZE_PASS_END(PIRToOpenMPPass, "pir2omp", "Lower PIR to OpenMP", true,
                    true)

namespace llvm {
FunctionPass *createPIRToOpenMPPass() { return new PIRToOpenMPPass(); }
} // namespace llvm
