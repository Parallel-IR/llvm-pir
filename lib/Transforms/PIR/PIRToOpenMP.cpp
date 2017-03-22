#include "llvm/Transforms/PIR/PIRToOpenMP.h"
// TODO the way I traverse the regions now results in every one being
// an top-level region. Modify that to traverse in reverse order instead.
// But first make it work for simple regions.

using namespace llvm;

void PIRToOMPPass::run(Function &F, FunctionAnalysisManager &AM) {
  // auto &PRI = AM.getResult<ParallelRegionAnalysis>(F);
}

// TODO double check for best practices, look at others code

Type *PIRToOpenMPPass::getOrCreateIdentTy(Module *M) {
  if (M->getTypeByName("ident_t") == nullptr) {
    IdentTy = StructType::create("ident_t",
                                 Type::getInt32Ty(M->getContext()) /* reserved_1 */,
                                 Type::getInt32Ty(M->getContext()) /* flags */,
                                 Type::getInt32Ty(M->getContext()) /* reserved_2 */,
                                 Type::getInt32Ty(M->getContext()) /* reserved_3 */,
                                 Type::getInt8PtrTy(M->getContext()) /* psource */,
                                 nullptr);    
  }

  return IdentTy;
}

PointerType *PIRToOpenMPPass::getIdentTyPointerTy() const {
  return PointerType::getUnqual(IdentTy);
}

FunctionType *PIRToOpenMPPass::getOrCreateKmpc_MicroTy(LLVMContext& Context) {
  if (Kmpc_MicroTy == nullptr) {
    Type *MicroParams[] = {PointerType::getUnqual(Type::getInt32Ty(Context)),
                           PointerType::getUnqual(Type::getInt32Ty(Context))};
    Kmpc_MicroTy = FunctionType::get(Type::getVoidTy(Context), MicroParams,
                                     true);
  }

  return Kmpc_MicroTy; 
}

PointerType *PIRToOpenMPPass::getKmpc_MicroPointerTy(LLVMContext& Context) { 
  return PointerType::getUnqual(getOrCreateKmpc_MicroTy(Context));
}

void PIRToOpenMPPass::getOrCreateDefaultLocation(Module *M) {
  if (DefaultOpenMPPSource == nullptr) {
    const std::string DefaultLocStr = ";unknown;unknown;0;0;;";
    StringRef DefaultLocStrWithNull(DefaultLocStr.c_str(),
                                    DefaultLocStr.size() + 1);
    DataLayout DL(M);
    // NOTE I am not sure this is the best way to calculate char alignment
    // of the Module target. Revisit later.
    uint64_t Alignment = DL.getTypeAllocSize(Type::getInt8Ty(M->getContext()));
    Constant *C = ConstantDataArray::getString(M->getContext(), DefaultLocStrWithNull, false);
    // NOTE Are heap allocations not recommended in general or is it OK here?
    // I couldn't find a way to statically allocate an IRBuilder for a Module!
    auto *GV = new GlobalVariable(*M, C->getType(), true, GlobalValue::PrivateLinkage,
                                  C, ".str", nullptr, GlobalValue::NotThreadLocal);
    GV->setAlignment(Alignment);
    GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    DefaultOpenMPPSource = cast<Constant>(GV);
    DefaultOpenMPPSource = ConstantExpr::getBitCast(DefaultOpenMPPSource,
                                                    Type::getInt8PtrTy(M->getContext()));
  }

  if (DefaultOpenMPLocation == nullptr) {
    // Constant *C = ConstantInt::get(Type::getInt32Ty(M->getContext()), 0, true);
    ArrayRef<Constant *> Members = {
      ConstantInt::get(Type::getInt32Ty(M->getContext()), 0, true),
      ConstantInt::get(Type::getInt32Ty(M->getContext()), 2, true),
      ConstantInt::get(Type::getInt32Ty(M->getContext()), 0, true),
      ConstantInt::get(Type::getInt32Ty(M->getContext()), 0, true),
      DefaultOpenMPPSource
    };
    Constant *C = ConstantStruct::get(IdentTy, Members);
    auto *GV = new GlobalVariable(*M, C->getType(), true, GlobalValue::PrivateLinkage,
                                  C, "", nullptr, GlobalValue::NotThreadLocal);
    GV->setUnnamedAddr(GlobalValue::UnnamedAddr::Global);
    GV->setAlignment(8);
    DefaultOpenMPLocation = GV;
  }
}

Constant *PIRToOpenMPPass::createRuntimeFunction(OpenMPRuntimeFunction Function,
                                                 Module *M) {
  Constant *RTLFn = nullptr;
  switch(Function) {
  case OMPRTL__kmpc_fork_call:
    Type *TypeParams[] = {getIdentTyPointerTy(), Type::getInt32Ty(M->getContext()),
                          getKmpc_MicroPointerTy(M->getContext())};
    FunctionType *FnTy = FunctionType::get(Type::getVoidTy(M->getContext()), TypeParams,
                                           true);
    RTLFn = M->getOrInsertFunction("__kmpc_fork_call", FnTy);
    break;
  }
  return RTLFn;
}

CallInst *PIRToOpenMPPass::emitRuntimeCall(Value *Callee,
                                           ArrayRef<Value*> Args,
                                           const Twine &Name,
                                           BasicBlock *Parent) const {
  IRBuilder<> Builder(Parent);
  CallInst *call = Builder.CreateCall(Callee, Args, None, Name);
  return call;
}

void emitForkCall(Value *OutlinedFn) {
  
}

Function* PIRToOpenMPPass::emitTaskFunction(const ParallelRegion &PR,
                                            bool IsForked) const {
  auto &F = *PR.getFork().getParent()->getParent();
  auto &M = *(Module*)F.getParent();

  // Generate the name of the outlined function for the task
  auto FName = F.getName();
  auto PRName =  PR.getFork().getParent()->getName();
  auto PT = IsForked? PR.getForkedTask() : PR.getContinuationTask();
  auto PTName = PT.getEntry().getName();
  auto PTFName = FName + "." + PRName + "." + PTName;

  auto PTFunction = (Function*)M.getOrInsertFunction(PTFName.str(),
                      Type::getVoidTy(M.getContext()),
                      NULL);

  ParallelTask::VisitorTy Visitor = [PTFunction](BasicBlock &BB, const ParallelTask &PT) -> bool {
    BB.removeFromParent();
    BB.insertInto(PTFunction);
    return true;
  };

  PT.visit(Visitor, true);
  auto &LastBB = PTFunction->back();
  assert((dyn_cast<HaltInst>(LastBB.getTerminator())
          || dyn_cast<JoinInst>(LastBB.getTerminator()))
         && "Should have been halt or join");
  LastBB.getTerminator()->eraseFromParent();

  BasicBlock *PTFuncExitBB = BasicBlock::Create(M.getContext(), "exit",
                                                PTFunction, nullptr);
  ReturnInst::Create(M.getContext(), PTFuncExitBB);

  BranchInst::Create(PTFuncExitBB, &LastBB);

  return PTFunction;
}

void PIRToOpenMPPass::emitRegionFunction(const ParallelRegion &PR) {
  assert(PR.hasTwoSingleExits() && "More than 2 exits is yet to be handled");

  auto &F = *PR.getFork().getParent()->getParent();
  auto &M = *(Module*)F.getParent();
  auto &C = M.getContext();
  DataLayout DL(&M);

  // Generate the name of the outlined function for the region
  auto FName = F.getName();
  auto ForkBB = (BasicBlock*)PR.getFork().getParent();
  auto PRName =  ForkBB->getName();
  auto PRFName = FName + "." + PRName;
  FunctionType *RFunctionTy = nullptr;
  Function *RFunction = nullptr;

  if (PR.isTopLevelRegion()) {
    RFunction = (Function*)M.getOrInsertFunction(PRFName.str(),
                                                 getOrCreateKmpc_MicroTy(C));
  } else {
    RFunctionTy = FunctionType::get(Type::getVoidTy(C), false);
    RFunction = (Function*)M.getOrInsertFunction(PRFName.str(),
                                                 RFunctionTy);
  }

  if (PR.isTopLevelRegion()) {
    auto NullArg = ConstantPointerNull::get(PointerType::getUnqual(Type::getInt32Ty(C)));
    ArrayRef<Value *> Args = { NullArg, NullArg };
    CallInst::Create(RFunction, Args, "", ForkBB);
  } else {
    CallInst::Create(RFunction, "", ForkBB);
  }
  BasicBlock *PRFuncEntryBB = BasicBlock::Create(C, "entry", RFunction,
                                                 nullptr);
  BasicBlock *PRFuncExitBB = BasicBlock::Create(C, "exit", RFunction,
                                                nullptr);

  IRBuilder<> ForkBBIRBuilder(ForkBB);
  JoinInst *JI = dyn_cast<JoinInst>(PR.getContinuationTask().getHaltsOrJoints()[0]);
  BranchInst::Create(JI->getSuccessor(0), ForkBB);
  JI->setSuccessor(0, PRFuncExitBB);

  // Emit 2 outlined functions for forked and continuation tasks
  auto ForkedFunction = emitTaskFunction(PR, true);
  auto ContFunction = emitTaskFunction(PR, false);

  PR.getFork().eraseFromParent();

  if (PR.isTopLevelRegion()) {
    // NOTE check clang's CodeGenFunction::EmitFunctionProlog for a general
    // handling of function prologue emition

    // NOTE according to the docs in CodeGenFunction.h, it is preferable
    // to insert all alloca's at the start of the entry BB. But I am not
    // sure about the technical reason for this. To check later.
    auto Int32Ty = Type::getInt32Ty(C);
    Value *Undef = UndefValue::get(Int32Ty);
    auto AllocaInsertPt = new llvm::BitCastInst(Undef, Int32Ty, "allocapt",
                                                PRFuncEntryBB);

    IRBuilder<> EntryBBIRBuilder2(PRFuncEntryBB,
                                  ((Instruction*)AllocaInsertPt)->getIterator());
    IRBuilder<> EntryBBIRBuilder(PRFuncEntryBB); 
    auto &ArgList = RFunction->getArgumentList();

    auto ArgI = ArgList.begin();
    ArgI->setName(".global_tid.");
    ArgI->addAttr(llvm::AttributeSet::get(C,
                                          ArgI->getArgNo() + 1,
                                          llvm::Attribute::NoAlias));
    auto GtidAlloca = EntryBBIRBuilder2.CreateAlloca(ArgI->getType(), nullptr,
                                                   ".global_tid..addr");
    GtidAlloca->setAlignment(DL.getTypeAllocSize(ArgI->getType()));
    EntryBBIRBuilder.CreateAlignedStore(&*ArgI, GtidAlloca,
                                        DL.getTypeAllocSize(ArgI->getType()));

    ++ArgI;
    ArgI->setName(".bound_tid.");
    ArgI->addAttr(llvm::AttributeSet::get(C,
                                          ArgI->getArgNo() + 1,
                                          llvm::Attribute::NoAlias));
    auto BtidAlloca = EntryBBIRBuilder2.CreateAlloca(ArgI->getType(), nullptr,
                                  ".bound_tid..addr");
    BtidAlloca->setAlignment(DL.getTypeAllocSize(ArgI->getType()));
    EntryBBIRBuilder.CreateAlignedStore(&*ArgI, BtidAlloca,
                                        DL.getTypeAllocSize(ArgI->getType()));

    AllocaInsertPt->eraseFromParent();
  }
  CallInst::Create(ForkedFunction, "", PRFuncEntryBB);
  CallInst::Create(ContFunction, "", PRFuncEntryBB);
  BranchInst::Create(PRFuncExitBB, PRFuncEntryBB);
  ReturnInst::Create(M.getContext(), PRFuncExitBB);
}

bool PIRToOpenMPPass::runOnFunction(Function &F) {
  PRI = &getAnalysis<ParallelRegionInfoPass>().getParallelRegionInfo();

  if (PRI->getTopLevelParallelRegions().size() > 0) {
    auto M = (Module*)F.getParent();
    getOrCreateIdentTy(M);
    getOrCreateDefaultLocation(M);
  }
  
  for (auto Region : PRI->getTopLevelParallelRegions())
    emitRegionFunction(*Region);

  return false;
}

void PIRToOpenMPPass::getAnalysisUsage(AnalysisUsage& AU) const {
  AU.setPreservesAll();
  AU.addRequired<ParallelRegionInfoPass>();
}

void PIRToOpenMPPass::print(raw_ostream &OS, const Module *) const {
  PRI->print(OS);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
void PIRToOpenMPPass::dump() const { PRI->dump(); }
#endif

char PIRToOpenMPPass::ID = 0;

INITIALIZE_PASS_BEGIN(PIRToOpenMPPass, "pir2omp",
                      "Lower PIR to OpenMP", true, true)
INITIALIZE_PASS_DEPENDENCY(ParallelRegionInfoPass)
INITIALIZE_PASS_END(PIRToOpenMPPass, "pir2omp",
                    "Lower PIR to OpenMP", true, true)
namespace llvm {
  FunctionPass *createPIRToOpenMPPass() {
    return new PIRToOpenMPPass();
  }
}
