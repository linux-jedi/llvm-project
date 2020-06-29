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
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

namespace {
  // TODO: Lookup how to document constants with llvm/doxygen
  // When determining if a function is eligible for memoization, all
  // function calls within a function are recursively checked for 
  // side effects and memoization eligibility. MAX_DEPTH of 10 is arbitrarily
  // chosen by the authors of the paper as the maximum depth the transofrmation
  // will travel. 
  size_t MAX_DEPTH = 10;

  class Memoize : public ModulePass {
    public:
    static char ID;
    Memoize() : ModulePass(ID) {}

    bool runOnModule(Module &M) override {
      errs() << "Memoize: ";
      errs().write_escaped(M.getName()) << '\n';

      // Setup
      callStackDepth = 0;

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
    bool isGlobalSafe(const Function &F);
    bool isMemoizable(const Function &F);
    bool isMemoizableLib(const Function &F);
    bool isMemoizablePointer(const Argument &A);
    bool isProperArguments(const Function &F);
    bool checkFunctionCalls(const Function &F);
    bool mayBeOverridden(const Function &F);

    void replaceCallSites(Function &F);

    SmallVector<Argument*, 4> getArguments(const Function &F);
    void getGlobalsByFunction(const Function &F, SmallPtrSetImpl<const GlobalVariable*> *globals);
    SmallVector<Type::TypeID, 5> getPrototype(const Function &F);

    size_t findIndex(SmallVectorImpl<Argument*> &args, Argument* target);

    size_t callStackDepth;

  };
}

char Memoize::ID = 0;
static RegisterPass<Memoize> X("memoize", "Function Memoize Pass",
                             false /* Call site replacement modifies CFG */,
                             false /* This is not an analysis pass */);

// Check function's use of global variables 
bool Memoize::isGlobalSafe(const Function &F) {
  callStackDepth = 0;
  const GlobalVariable *global = nullptr;

  for (const BasicBlock &BB: F.getBasicBlockList()) {
    for (const Instruction &I: BB.instructionsWithoutDebug()) {
      for (const Use &O: I.operands()) {
        auto *globalVariable = dyn_cast<GlobalVariable>(&O);
        if (!globalVariable) continue;

        // If a function uses a global that is not a basic type
        // (float, double, int) do not memoize it
        PointerType *pointerType = globalVariable->getType();
        Type::TypeID opType = pointerType->getTypeID();
        if (opType != Type::IntegerTyID || 
            opType != Type::FloatTyID || opType != Type::DoubleTyID)
            return false;

        // If more than one global variable is in use, the function 
        // should not be memoized.
        if (!global) {
          global = globalVariable;
          continue;
        }
        if (globalVariable == global) continue;

        return false;
      }
    }
  }
  return true;
}

bool Memoize::isMemoizable(const Function &F) {
  if (F.isDeclaration() ||
      F.isIntrinsic() || F.isVarArg() || mayBeOverridden(F))
    return false;

  if (isMemoizableLib(F))
    return true;

  callStackDepth = 0;
  if (isProperArguments(F) && isGlobalSafe(F) && checkFunctionCalls(F))
    return true;

  return false;
}

bool Memoize::isMemoizablePointer(const Argument &A) {
  // check each use of A
  //   if any are load or store then not memoizable
  for (const User *U: A.users()) {
    if (!isa<LoadInst>(U) && !isa<StoreInst>(U))
      return false;
  }
  return true; // TODO: reread paper to determine if this is true
}

bool Memoize::isProperArguments(const Function &F) {
  for (const Argument &A: F.args()) {
    if (A.getType()->isPointerTy()) {
      if (isMemoizablePointer(A))
        continue;
      return false;
    }
  }
  return true;
}

bool Memoize::checkFunctionCalls(const Function &F) {
  // Check to see if function is safe for memoization
  for (const BasicBlock &BB: F.getBasicBlockList()) {
    for (const Instruction &I: BB.instructionsWithoutDebug()) {
      auto CI = dyn_cast<CallInst>(&I);
      if (!CI) continue;

      Function* calledFunction = CI->getCalledFunction();

      // TODO: figure out how to add `calledFunction == I`
      if (calledFunction->isSpeculatable()) { 
        errs() << "Pure Function: ";
        errs().write_escaped(calledFunction->getName()) << '\n';
        continue;
      }
      
      callStackDepth++;
      if (callStackDepth >= MAX_DEPTH) return false;

      if (isMemoizable(*calledFunction)) continue;

      return false;
    }
  }
  return true;
}

void Memoize::replaceCallSites(Function &F) {
  SmallPtrSet<const GlobalVariable*, 10> globals;
  getGlobalsByFunction(F, &globals);

  auto prototype = getPrototype(F);
  SmallVector<Argument*, 6> newArgs;

  std::sort(prototype.begin(), prototype.end());

  const Twine newName("_memoized_" + F.getName());
  Twine callString(newName);

  // Replace each call site 
  for (auto CS: F.users()) {
    // Args must be sorted so that memoization is consistent
    auto args = getArguments(F);

    // Add globals
    args.append(globals.begin(), globals.end()); // TODO: convert globals to arguments
    newArgs.append(args.begin(), args.end());    
    std::sort(newArgs.begin(), newArgs.begin());

    for (Argument* x: newArgs) {
      // TODO: figure out what isPresent check is for

      // get index of each argument
      size_t index = findIndex(newArgs, x);

      // remove constants from call string
      if (ConstantInt* c = dyn_cast<ConstantInt>(x)) {
        prototype.erase(prototype.begin() + index);
        newArgs.erase(newArgs.begin() + index);

        SmallString<10> constantValString;
        c->getValue().toString(constantValString, 10, true);
        callString.concat(constantValString + ",)");
      }
    }

    if (!F.users().empty()) {
      // Create new function
      Function *newFunction = Function::Create(
        FunctionType::get(F.getReturnType(), false),
        Function::LinkageTypes::ExternalLinkage,
        newName,
        *F.getParent()
      );

      // replace uses with
      F.replaceAllUsesWith(newFunction);

      // Print call string to file 
      errs() << "Memoized Function: ";
      errs() << callString << "\n";
    }
  }
}

SmallVector<Argument*, 4> Memoize::getArguments(const Function &F) {
  SmallVector<Argument*, 4> args(F.args().begin(), F.args().end()); // TODO: delete this function and inline this code
  return args;
}

void Memoize::getGlobalsByFunction(const Function &F, SmallPtrSetImpl<const GlobalVariable*> *globals) {
  for (const BasicBlock &BB: F) 
    for (const Instruction &I: BB) 
      for (const Value *Op: I.operands())
        if (const GlobalVariable* G = dyn_cast<GlobalVariable>(Op))
          globals->insert(G);    
}

SmallVector<Type::TypeID, 5> Memoize::getPrototype(const Function &F) {
  // TODO
}

size_t Memoize::findIndex(SmallVectorImpl<Argument*> &args, Argument* target) {
  // TODO
}