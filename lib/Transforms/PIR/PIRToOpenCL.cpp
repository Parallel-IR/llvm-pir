#include "llvm/Transforms/PIR/PIRToOpenCL.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"

#include "axtor/metainfo/ModuleInfo.h"
#include "axtor/backend/AxtorBackend.h"
#include "axtor_ocl/OCLModuleInfo.h"
#include "axtor_ocl/OCLBackend.h"
#include "axtor/util/llvmTools.h"
#include "axtor/console/CompilerLog.h"
#include "axtor/Axtor.h"

#include <fstream>

using namespace llvm;

#define DUMP(IRObj) errs() << #IRObj ":"; IRObj->dump();

// Make sure that any value shared accross the parallel region bounds is a
// pointer and not a primitive value.
bool PIRToOpenCLPass::verifyExtractedFn(Function *Fn) const {
#if !defined(NDEBUG)
  for (auto *ParamTy : Fn->getFunctionType()->params()) {
    if (!ParamTy->isPointerTy())
      return false;
  }
#endif

  return true;
}

bool PIRToOpenCLPass::runOnModule(Module &M) {
  for (auto &F : M) {
    if (F.isDeclaration()) {
      continue;
    }

    PRI = &getAnalysis<ParallelRegionInfoPass>(F).getParallelRegionInfo();

    LoopInfo &LI = getAnalysis<LoopInfoWrapperPass>(F).getLoopInfo();
    DominatorTree &DT =
      getAnalysis<DominatorTreeWrapperPass>(F).getDomTree();
    ScalarEvolution &SE = getAnalysis<ScalarEvolutionWrapperPass>(F).getSE();

    for (auto Region : PRI->getTopLevelParallelRegions())
      startRegionEmission(*Region, LI, DT, SE);
  }

  return false;
}

static void copyComdat(GlobalObject *Dst, const GlobalObject *Src) {
  const Comdat *SC = Src->getComdat();
  if (!SC)
    return;
  Comdat *DC = Dst->getParent()->getOrInsertComdat(SC->getName());
  DC->setSelectionKind(SC->getSelectionKind());
  Dst->setComdat(DC);
}

void PIRToOpenCLPass::startRegionEmission(const ParallelRegion &PR,
                                          LoopInfo &LI, DominatorTree &DT,
                                          ScalarEvolution &SE) {
  // A utility function used to find calls that replace extracted parallel
  // region function.
  auto FindCallToExtractedFn = [](Function *SpawningFn,
                                  Function *ExtractedFn) {
    // Find the call instruction to the extracted region function: RegionFn.
    CallInst *ExtractedFnCI = nullptr;
    for (auto &BB : *SpawningFn) {
      if (CallInst *CI = dyn_cast<CallInst>(BB.begin())) {
        // NOTE: I use pointer equality here, is that fine?
        if (ExtractedFn == CI->getCalledFunction()) {
          ExtractedFnCI = CI;
          break;
        }
      }
    }

    assert(ExtractedFnCI != nullptr &&
           "Couldn't find the call to the extracted region function!");

    return ExtractedFnCI;
  };

  // Check if \p PR is a parallel loop
  Loop *L = LI.getLoopFor(PR.getFork().getParent());
  bool IsParallelLoop = (L && PR.isParallelLoopRegion(*L, DT));

  auto &ForkInst = PR.getFork();
  Function *SpawningFn = ForkInst.getParent()->getParent();
  auto *M = SpawningFn->getParent();
  DataLayout DL(M);

  auto *Int32Ty = Type::getInt32Ty(M->getContext());

  if (IsParallelLoop) {
    auto *CIV = L->getCanonicalInductionVariable();
    assert(CIV && "Non-canonical loop");

    auto *LoopLatch = L->getLoopLatch();
    assert(LoopLatch &&
           "Only parallel loops with a single latch are supported.");

    auto *LoopPreHeader = L->getLoopPreheader();
    auto *LoopHeader = L->getHeader();
    auto *LoopExit = L->getExitBlock();

    auto *LatchCount = SE.getExitCount(L, L->getExitingBlock());
    SCEVExpander Exp(SE, DL, "LatchExpander");
    Exp.setInsertPoint(&*LoopHeader->begin());
    auto *LatchCountVal = Exp.expandCodeFor(LatchCount);

    auto &ForkedTask = PR.getForkedTask();
    auto &ContTask = PR.getContinuationTask();
    auto &ForkedEntry = ForkedTask.getEntry();

    auto *WorkItemID = CallInst::Create(
        createBuiltInFunction(OpenCLBuiltInFunction::OCL__get_global_id, M),
        {ConstantInt::get(Int32Ty, 0)}, None, "wi_id", &*ForkedEntry.begin());
    Value *WorkItemIDCast = nullptr;

    if (CIV->getType()->getScalarSizeInBits() <
        WorkItemID->getType()->getScalarSizeInBits()) {
      WorkItemIDCast = new TruncInst(WorkItemID, CIV->getType(), "",
                                     &*std::next(WorkItemID->getIterator()));
    } else if (CIV->getType()->getScalarSizeInBits() >
               WorkItemID->getType()->getScalarSizeInBits()) {
      WorkItemIDCast =
          CastInst::CreateZExtOrBitCast(WorkItemID, CIV->getType(), "",
                                        &*std::next(WorkItemID->getIterator()));
    }

    CIV->replaceAllUsesWith(WorkItemIDCast);

    std::vector<BasicBlock *> LoopBodyBBs;

    ParallelTask::VisitorTy ForkedVisitor = [&LoopBodyBBs](
        BasicBlock &BB, const ParallelTask &PT) -> bool {
      LoopBodyBBs.push_back(&BB);
      return true;
    };

    ForkedTask.visit(ForkedVisitor, true);
    removePIRInstructions(ForkInst, ForkedTask, ContTask);

    CodeExtractor LoopBodyExtractor(LoopBodyBBs, &DT, false, nullptr, nullptr,
                                    SpawningFn->getName() + "_loop_body");
    Function *LoopBodyFn = LoopBodyExtractor.extractCodeRegion();
    auto *ExtractedFnCall = FindCallToExtractedFn(SpawningFn, LoopBodyFn);

    ValueToValueMapTy VMap;
    Function *OCLKernelFn = declareOCLRegionFn(LoopBodyFn, VMap);

    SmallVector<ReturnInst*, 1> Returns;
    CloneFunctionInto(OCLKernelFn, LoopBodyFn, VMap, false, Returns);

    // Update address spaces of kernel arguments.
    auto &ArgList = OCLKernelFn->getArgumentList();

    for (auto &Arg : ArgList) {
      for (auto *User : Arg.users()) {
        if (auto *GEP = dyn_cast<GetElementPtrInst>(User)) {
          auto *AddrSpaceQualTy = PointerType::get(
              ((PointerType *)Arg.getType())->getElementType(),
              ((PointerType *)Arg.getType())->getPointerAddressSpace());
          GEP->mutateType(AddrSpaceQualTy);
        }
      }
    }

    // Clone the kernel function and the functions that it calls into a new
    // OpenCL module.
    auto ShouldCloneDefinition = [&OCLKernelFn](const GlobalValue *GV) {
      return GV == OCLKernelFn ||
             std::any_of(GV->user_begin(), GV->user_end(),
                         [&OCLKernelFn](const User *User) {
                           auto *UserInst = dyn_cast<Instruction>(User);
                           return UserInst &&
                                  (UserInst->getParent()->getParent() ==
                                   OCLKernelFn);
                         });
    };

    std::unique_ptr<Module> New =
        llvm::make_unique<Module>(M->getModuleIdentifier(), M->getContext());
    New->setDataLayout(M->getDataLayout());
    New->setTargetTriple(M->getTargetTriple());
    New->setModuleInlineAsm(M->getModuleInlineAsm());

    for (const Function &I : *M) {
      if (ShouldCloneDefinition(&I)) {
        Function *NF = Function::Create(cast<FunctionType>(I.getValueType()),
                                        I.getLinkage(), I.getName(), New.get());
        NF->copyAttributesFrom(&I);
        VMap[&I] = NF;
      }
    }

    for (const Function &I : *M) {
      if (I.isDeclaration())
        continue;

      if (ShouldCloneDefinition(&I)) {
        Function *F = cast<Function>(VMap[&I]);

        Function::arg_iterator DestI = F->arg_begin();
        for (Function::const_arg_iterator J = I.arg_begin(); J != I.arg_end();
             ++J) {
          DestI->setName(J->getName());
          VMap[&*J] = &*DestI++;
        }

        SmallVector<ReturnInst *, 8> Returns; // Ignore returns cloned.
        CloneFunctionInto(F, &I, VMap, /*ModuleLevelChanges=*/true, Returns);

        if (I.hasPersonalityFn())
          F->setPersonalityFn(MapValue(I.getPersonalityFn(), VMap));

        copyComdat(F, &I);
      }
    }

    std::string OCLKernelFileName = OCLKernelFn->getName().str() + ".cl";
    std::ostream *OutStream =
        new std::ofstream(OCLKernelFileName, std::ios::out);

    emitKernelFile(&*New, OCLKernelFn, *OutStream);
    delete OutStream;

    // Driver call emission

    // Prepare arguments for the OpenCL driver call and make the call.
    auto *InvokeDriverFn = declareInvokeDriver(M);
    IRBuilder<> IRBuilder(LoopPreHeader,
                          LoopPreHeader->getTerminator()->getIterator());
    auto *FileNameConst = IRBuilder.CreateGlobalStringPtr(OCLKernelFileName);
    auto *KernelNameConst =
        IRBuilder.CreateGlobalStringPtr(OCLKernelFn->getName());
    auto *TripCountVal =
      IRBuilder.CreateAdd(LatchCountVal, IRBuilder.getInt32(1), "trip.cnt");
    auto *Int8PtrTy = Type::getInt8PtrTy(M->getContext());
    auto *Int32Ty = Type::getInt32Ty(M->getContext());

    auto *ArgPtrs = IRBuilder.CreateAlloca(
        Int8PtrTy, IRBuilder.getInt32(ExtractedFnCall->getNumArgOperands()),
        "arg.ptrs");
    auto *ArgSizes = IRBuilder.CreateAlloca(
        Int32Ty, IRBuilder.getInt32(ExtractedFnCall->getNumArgOperands()),
        "arg.sizes");
    auto *ArgTypes = IRBuilder.CreateAlloca(
        Int32Ty, IRBuilder.getInt32(ExtractedFnCall->getNumArgOperands()),
        "arg.ptrs");

    int i = 0;
    for (auto &Arg : ExtractedFnCall->arg_operands()) {
      auto *ArgPtr = Arg.get();
      // NOTE a very naive assumption that arrays accessed with a parallel loop
      // always contain TripCountVal elements and the access starts from 0.
      auto *NumElemsVal = TripCountVal;
      auto *ArgTypeVal = IRBuilder.getInt32(OpenCLKernelArgType::Array);

      // Demote scalar arguments to memory to get a pointer that can passed to
      // OpenCL API.
      if (!Arg.get()->getType()->isPointerTy()) {
        ArgPtr = DemoteRegToStack(*dyn_cast<Instruction>(Arg.get()));
        NumElemsVal = IRBuilder.getInt32(1);
        ArgTypeVal = IRBuilder.getInt32(OpenCLKernelArgType::Scalar);
      }

      auto *ArgPtrCast = IRBuilder.CreateBitCast(ArgPtr, Int8PtrTy);
      auto *ArgPtrElem =
          IRBuilder.CreateInBoundsGEP(ArgPtrs, {IRBuilder.getInt32(i)});
      auto *ArgPtrStore = IRBuilder.CreateStore(ArgPtrCast, ArgPtrElem);

      auto *ArgSize =
          IRBuilder.CreateMul(NumElemsVal,
                              IRBuilder.getInt32(DL.getTypeAllocSize(
                                  ArgPtr->getType()->getPointerElementType())));
      auto *ArgSizeElem =
        IRBuilder.CreateInBoundsGEP(ArgSizes, {IRBuilder.getInt32(i)});
      auto *ArgSizeStore = IRBuilder.CreateStore(ArgSize, ArgSizeElem);

      auto *ArgTypeElem =
        IRBuilder.CreateInBoundsGEP(ArgTypes, {IRBuilder.getInt32(i)});
      auto *ArgTypeStore = IRBuilder.CreateStore(ArgTypeVal, ArgTypeElem);
      ++i;
    }

    auto *DriverCall = IRBuilder.CreateCall(
        InvokeDriverFn,
        {FileNameConst, KernelNameConst, TripCountVal, IRBuilder.getInt32(i),
         ArgPtrs, ArgSizes, ArgTypes});

    // Cleanup

    // Re-calculate LoopInfo since the parallel loop structure has changed.
    auto &LI2 = getAnalysis<LoopInfoWrapperPass>(*SpawningFn).getLoopInfo();
    Loop *L2 = LI2.getLoopFor(LoopHeader);

    // Extract the loop into a function in preparation to delete the loop.
    CodeExtractor LoopExtractor(DT, *L2);
    auto *LoopFn = LoopExtractor.extractCodeRegion();
    auto *LoopFnCall = FindCallToExtractedFn(SpawningFn, LoopFn);
    LoopFnCall->eraseFromParent();
    LoopFn->eraseFromParent();

    LoopBodyFn->eraseFromParent();
    OCLKernelFn->eraseFromParent();
  } else {
    assert(false && "Non-Loop regions are not supported yet!");
  }
}

void PIRToOpenCLPass::removePIRInstructions(ForkInst &ForkInst,
                                            const ParallelTask &ForkedTask,
                                            const ParallelTask &ContTask) {
  // Replace fork, halt, and join instructions with br, otherwise, we will end
  // up in an infinite loop since the region's extracted function will contain
  // a "new" parallel region.

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
}

Function *PIRToOpenCLPass::declareOCLRegionFn(Function *RegionFn,
                                              ValueToValueMapTy &VMap) {
  auto *Module = RegionFn->getParent();
  auto &Context = Module->getContext();
  DataLayout DL(Module);

  std::vector<Type *> FnParams;
  std::vector<StringRef> FnArgNames;
  std::vector<AttributeSet> FnArgAttrs;

  auto &RegionFnArgList = RegionFn->getArgumentList();

  int ArgOffset = 0;

  // For RegionFn argument add a corresponding argument to the new function.
  for (auto &Arg : RegionFnArgList) {
    if (Arg.getType()->isPointerTy()) {
      // TODO Fix address spaces to work properly with different hardware.
      // For now only global space is needed and I use 1.
      FnParams.push_back(
          PointerType::get(((PointerType *)Arg.getType())->getElementType(),
                           /*ADDRESS_SPACE_GLOBAL*/ 1));
    } else {
      FnParams.push_back(Arg.getType());
    }

    FnArgNames.push_back(Arg.getName());

    if (Arg.getType()->isPointerTy()) {
      FnArgAttrs.push_back(
          AttributeSet::get(Context, ++ArgOffset, Attribute::NoCapture));
    } else {
      FnArgAttrs.push_back(AttributeSet());
      ++ArgOffset;
    }
  }

  // Create the function and set its argument properties.
  auto *VoidTy = Type::getVoidTy(Context);
  auto *OCLRegionFnTy = FunctionType::get(VoidTy, FnParams, false);
  auto Name = RegionFn->getName() + "_OCL_kernel";
  Function *OCLRegionFn = dyn_cast<Function>(
      Module->getOrInsertFunction(Name.str(), OCLRegionFnTy));
  auto &FnArgList = OCLRegionFn->getArgumentList();

  auto ArgIt = FnArgList.begin();
  auto ArgNameIt = FnArgNames.begin();
  int i = 0;

  for (auto &ArgAttr : FnArgAttrs) {
    if ((*ArgIt).getType()->isPointerTy()) {
      (*ArgIt).addAttr(ArgAttr);
    }

    (*ArgIt).setName("arg_" + Twine(i));
    ++ArgIt;
    ++ArgNameIt;
    ++i;
  }

  auto OCLArgIt = OCLRegionFn->arg_begin();

  // Map corresponding arguments in RegionFn and OCLRegionFn
  for (auto &Arg : RegionFnArgList) {
    VMap[&Arg] = &*OCLArgIt;
    ++OCLArgIt;
  }

  return OCLRegionFn;
}

int PIRToOpenCLPass::emitKernelFile(Module *M, Function *OCLKernel,
                                    std::ostream &Out) {
  axtor::OCLBackend Backend;
  axtor::OCLModuleInfo ModInfo(M, Out);
  axtor::translateModule(Backend, ModInfo);

  return 0;
}

Constant *PIRToOpenCLPass::createBuiltInFunction(OpenCLBuiltInFunction Function,
                                                 Module *M) {
  Constant *OCLFn = nullptr;
  auto *SizeTy = Type::getInt64Ty(M->getContext());
  auto *Int32Ty = Type::getInt32Ty(M->getContext());

  switch (Function) {
  case OCL__get_global_id: {
    Type *TypeParams[] = {Int32Ty};
    FunctionType *FnTy = FunctionType::get(SizeTy, TypeParams, false);
    OCLFn = M->getOrInsertFunction("get_global_id", FnTy);
    break;
  }
  }

  return OCLFn;
}

Constant *PIRToOpenCLPass::declareInvokeDriver(Module *M) {
  auto *VoidTy = Type::getVoidTy(M->getContext());
  auto *Int8PtrTy = Type::getInt8PtrTy(M->getContext());
  auto *Int8PtrPtrTy = PointerType::getUnqual(Type::getInt8PtrTy(M->getContext()));
  auto *Int32Ty = Type::getInt32Ty(M->getContext());
  auto *Int32PtrTy = Type::getInt32PtrTy(M->getContext());
  Type *TypeParams[] = {Int8PtrTy, Int8PtrTy, Int32Ty, Int32Ty,
                        Int8PtrPtrTy, Int32PtrTy, Int32PtrTy};
  FunctionType *FnTy = FunctionType::get(VoidTy, TypeParams, false);
  return M->getOrInsertFunction("invokeDriver", FnTy);
}

void PIRToOpenCLPass::getAnalysisUsage(AnalysisUsage &AU) const {
  // AU.setPreservesAll();
  AU.addRequired<DominatorTreeWrapperPass>();
  AU.addRequired<ParallelRegionInfoPass>();
  AU.addRequired<LoopInfoWrapperPass>();
  AU.addRequired<ScalarEvolutionWrapperPass>();
}

void PIRToOpenCLPass::print(raw_ostream &OS, const Module *) const {
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void PIRToOpenCLPass::dump() const { }
#endif

char PIRToOpenCLPass::ID = 0;

INITIALIZE_PASS_BEGIN(PIRToOpenCLPass, "pir2ocl", "Lower PIR to OpenCL", true,
                      true)
INITIALIZE_PASS_DEPENDENCY(ParallelRegionInfoPass)
INITIALIZE_PASS_END(PIRToOpenCLPass, "pir2ocl", "Lower PIR to OpenCL", true,
                    true)

namespace llvm {
  ModulePass *createPIRToOpenCLPass() { return new PIRToOpenCLPass(); }
} // namespace llvm
