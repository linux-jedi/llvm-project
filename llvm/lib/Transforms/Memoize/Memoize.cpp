//===- Memoize.cpp - Compile Time Function Memoization Pass ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the compile time function memoization pass as described
// in Suresh et al.'s "Compile Time Function Memoization". At a high level this
// pass performs the following:
//   
//  1. Identify functions eligible for memoization
//  2. Creates a new memoized function for each eligible function
//  3. Replace calls to original function with memoized function
//  4. Generate metadata so if-memo can create a memoization table
//
//===----------------------------------------------------------------------===//

#include "llvm/Pass.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
  class Memoize : public ModulePass {
    public:
    static char ID;
    Memoize() : ModulePass(ID) {}

    bool runOnModule(Module &M) override {
      errs() << "Memoize: ";
      errs().write_escaped(M.getName()) << '\n';

      // Do work
      for (Function &F: M.getFunctionList()) {
        if (isMemoizable(F)) {
          // make function memoizable

          // update all call sites 

          // send meta data to if-memo
        }
      }
      return false;
    }

    private:
    bool isGlobalSafe(Function &F);
    bool isMemoizable(Function &F);
    bool isMemoizableLib(Function &F);
    bool isMemoizablePointer(Argument &A);
    bool isPointerType(Argument &A);
    bool isProperArguments(Function &F);
    bool checkFunctionCalls(Function &F);
    bool mayBeOverridden(Function &F);

  };
}

char Memoize::ID = 0;
static RegisterPass<Memoize> X("memoize", "Function Memoize Pass",
                             false /* Call site replacement modifies CFG */,
                             false /* This is not an analysis pass */);

bool Memoize::isGlobalSafe(Function &F) {
  // check if function uses global variable
}

bool Memoize::isMemoizable(Function &F) {
  if (F.isDeclaration() 
    || F.isIntrinsic() || F.isVarArg() || mayBeOverridden(F))
    return false;

  if (isMemoizableLib(F))
    return true;

  size_t count = 0;
  if (isProperArguments(F) && isGlobalSafe(F) && checkFunctionCalls(F))
    return true;

  return false;
}

bool Memoize::isMemoizablePointer(Argument &A) {
  // check each use of A
  //   if any are load or store then not memoizable
  for (User *U: A.users()) {
    if (!isa<LoadInst>(U) && !isa<StoreInst>(U))
      return false;
  }
  return true; // TODO: reread paper to determine if this is true
}

bool Memoize::isProperArguments(Function &F) {
  for (Argument &A: F.args()) {
    if (isPointerType(A)) {
      if (isMemoizablePointer(A))
        continue;
      return false;
    }
  }
}

bool Memoize::checkFunctionCalls(Function &F) {
  // Check to see if function is safe for memoization
}
