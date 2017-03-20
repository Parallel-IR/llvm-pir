#include "llvm/Transforms/PIR/PIRToOpenMP.h"

using namespace llvm;

void PIRToOMPPass::run(Function &F, FunctionAnalysisManager &AM) {
  // auto &PRI = AM.getResult<ParallelRegionAnalysis>(F);
}

// TODO double check for best practices, look at others code

PointerType *PIRToOpenMPPass::getIdentTyPointerTy() const {
  assert(IdentTy != nullptr && "IdentTy should have been initialized!");
  return PointerType::getUnqual(IdentTy);
}

Type *PIRToOpenMPPass::getKmpc_MicroPointerTy(LLVMContext& Context) {
  if (Kmpc_MicroTy == nullptr) {
    Type *MicroParams[] = {PointerType::getUnqual(Type::getInt32Ty(Context)),
                           PointerType::getUnqual(Type::getInt32Ty(Context))};
    Kmpc_MicroTy = FunctionType::get(Type::getVoidTy(Context), MicroParams,
                                     true);
  }
  return PointerType::getUnqual(Kmpc_MicroTy);
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
  // TODO check CodeGenFunction::EmitRuntimeCall for a more complete
  // version of this
  IRBuilder<> Builder(Parent);
  CallInst *call = Builder.CreateCall(Callee, Args, None, Name);
  return call;
}

Function* PIRToOpenMPPass::emitTaskFunction(const ParallelRegion &PR, bool IsForked) const {
  auto &F = *PR.getFork().getParent()->getParent();
  auto &M = *(Module*)F.getParent();

  // Generate the name of the outlined function for the task
  auto FName = F.getName();
  auto PRName =  PR.getFork().getParent()->getName();
  auto PT = IsForked? PR.getForkedTask() : PR.getContinuationTask();
  auto PTName = PT.getEntry().getName();
  auto PTFName = FName + "." + PRName + "." + PTName + ".outlined"
    + (IsForked ? ".fork" : ".cont");

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

void PIRToOpenMPPass::emitRegionFunction(const ParallelRegion &PR) const {
  assert(PR.hasTwoSingleExits() && "More than 2 exits is yet to be handled");

  auto &F = *PR.getFork().getParent()->getParent();
  auto &M = *(Module*)F.getParent();

  // Generate the name of the outlined function for the region
  auto FName = F.getName();
  auto ForkBB = (BasicBlock*)PR.getFork().getParent();
  auto PRName =  ForkBB->getName();
  auto PRFName = FName + "." + PRName + ".outlined";

  auto RFunction = (Function*)M.getOrInsertFunction(PRFName.str(),
                     Type::getVoidTy(M.getContext()),
                     NULL);
  BasicBlock *PRFuncEntryBB = BasicBlock::Create(M.getContext(), "entry", RFunction,
                                                 nullptr);
  BasicBlock *PRFuncExitBB = BasicBlock::Create(M.getContext(), "exit", RFunction,
                                                nullptr);

  IRBuilder<> ForkBBIRBuilder(ForkBB);
  ForkBBIRBuilder.CreateAlloca(getIdentTyPointerTy());
  CallInst::Create(RFunction, "", ForkBB);
  JoinInst *JI = dyn_cast<JoinInst>(PR.getContinuationTask().getHaltsOrJoints()[0]);
  BranchInst::Create(JI->getSuccessor(0), ForkBB);
  JI->setSuccessor(0, PRFuncExitBB);

  // Emit 2 outlined functions for forked and continuation tasks
  auto ForkedFunction = emitTaskFunction(PR, true);
  auto ContFunction = emitTaskFunction(PR, false);

  PR.getFork().eraseFromParent();

  CallInst::Create(ForkedFunction, "", PRFuncEntryBB);
  CallInst::Create(ContFunction, "", PRFuncEntryBB);
  BranchInst::Create(PRFuncExitBB, PRFuncEntryBB);
  ReturnInst::Create(M.getContext(), PRFuncExitBB);
}

bool PIRToOpenMPPass::runOnFunction(Function &F) {
  PRI = &getAnalysis<ParallelRegionInfoPass>().getParallelRegionInfo();

  if (PRI->getTopLevelParallelRegions().size() > 0) {
    auto M = (Module*)F.getParent();
    if (M->getTypeByName("ident_t") == nullptr) {
      IdentTy = StructType::create("ident_t",
                                   Type::getInt32Ty(F.getContext()) /* reserved_1 */,
                                   Type::getInt32Ty(F.getContext()) /* flags */,
                                   Type::getInt32Ty(F.getContext()) /* reserved_2 */,
                                   Type::getInt32Ty(F.getContext()) /* reserved_3 */,
                                   Type::getInt8PtrTy(F.getContext()) /* psource */,
                                   nullptr);
    }
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
