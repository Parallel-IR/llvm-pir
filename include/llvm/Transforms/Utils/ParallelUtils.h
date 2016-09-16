//===- PIR/Utils/ParallelUtils.h --- Parallel utilities -------*- C++ -*---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines some parallel transformation utilities.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PIR_UTILS_PARALLELUTILS_H
#define LLVM_PIR_UTILS_PARALLELUTILS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SetVector.h"

namespace llvm {
class Type;
class Twine;
class Module;
class Function;
class LoopInfo;
class BasicBlock;
class ParallelTask;
class DominatorTree;
class TerminatorInst;
class ParallelRegion;

bool isTaskTerminator(TerminatorInst *TI);

Function *outlineTask(ParallelTask *PT, DominatorTree *DT, LoopInfo *LI);

Function *getOrCreateFunction(Module &M, StringRef Name, Type *RetTy = nullptr,
                              ArrayRef<Type *> ArgTypes = {});

void removeTrivialForks(ParallelRegion &PR);

void simplifyForks(ParallelRegion &PR);

void makeOutgoingCommunicationExplicit(SetVector<BasicBlock *> &Blocks);
void separateTasks(ArrayRef<ParallelTask *> Tasks, DominatorTree *DT = nullptr,
                   LoopInfo *LI = nullptr);

}
#endif
