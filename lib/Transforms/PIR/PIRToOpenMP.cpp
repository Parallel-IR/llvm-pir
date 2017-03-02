#include "llvm/Transforms/PIR/PIRToOpenMP.h"

using namespace llvm;

void PIRToOMPPass::run(Function &F, FunctionAnalysisManager &AM) {
  // auto &PRI = AM.getResult<ParallelRegionAnalysis>(F);
}

// TODO double check for best practices, look at others code

Function* emitTaskFunction(const ParallelRegion &PR, bool IsForked) {
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

  // PTFunction->dump();

  return PTFunction;
}

void emitRegionFunction(const ParallelRegion &PR) {
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

  //  ParallelTask::VisitorTy Visitor = [RFunction](BasicBlock &BB, const ParallelTask &PT) -> bool {
  //  BB.removeFromParent();
  //  BB.insertInto(RFunction);
  //  return true;
  //};

  //PR.visit(Visitor, true);
  BasicBlock *PRFuncEntryBB = BasicBlock::Create(M.getContext(), "entry", RFunction,
                                                 nullptr);
  BasicBlock *PRFuncExitBB = BasicBlock::Create(M.getContext(), "exit", RFunction,
                                                nullptr);

  JoinInst *JI = dyn_cast<JoinInst>(PR.getContinuationTask().getHaltsOrJoints()[0]);
  CallInst::Create(RFunction, "", ForkBB);
  BranchInst::Create(JI->getSuccessor(0), ForkBB);
  JI->setSuccessor(0, PRFuncExitBB);

  // emit 2 outlined functions for forked and continuation tasks
  auto ForkedFunction = emitTaskFunction(PR, true);
  auto ContFunction = emitTaskFunction(PR, false);

  //PRFuncEntryBB->getInstList().splice(PRFuncEntryBB->getFirstInsertionPt(),
  //                                    PR.getFork().getParent()->getInstList(),
  //                                    PR.getFork().getIterator());
  PR.getFork().eraseFromParent();

  CallInst::Create(ForkedFunction, "", PRFuncEntryBB);
  CallInst::Create(ContFunction, "", PRFuncEntryBB);
  BranchInst::Create(PRFuncExitBB, PRFuncEntryBB);

  ReturnInst::Create(M.getContext(), PRFuncExitBB);

  // for (auto BB = RFunction->begin(); BB != RFunction->end(); ++BB) {
  //   auto I = BB->getTerminator();

  //   if (ForkInst *FI = dyn_cast<ForkInst>(I)) {
  //     BranchInst::Create(FI->getForkedBB(), I);
  //     I->eraseFromParent();
  //   } else if (HaltInst *HI = dyn_cast<HaltInst>(I)) {
  //     BranchInst::Create(HI->getContinuationBB(), I);
  //     I->eraseFromParent();
  //   } else if (JoinInst *JI = dyn_cast<JoinInst>(I)) {
  //     BranchInst::Create(JI->getSuccessor(0), I);
  //     I->eraseFromParent();
  //   }
  // }

  // RFunction->dump();
  // F.dump();
}

bool PIRToOpenMPPass::runOnFunction(Function &F) {
  PRI = &getAnalysis<ParallelRegionInfoPass>().getParallelRegionInfo();

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
