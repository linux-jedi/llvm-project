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
        errs() << "Function: ";
        errs().write_escaped(F.getName()) << '\n';

        if (isMemoizable(F)) {
          // make function memoizable

          // update all call sites 
          replaceCallSites(F);

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

    void getGlobalsByFunction(const Function &F, SmallPtrSetImpl<const GlobalVariable*> *globals);
    SmallVector<Type*, 5> getPrototype(const Function &F);

    size_t findIndex(SmallVectorImpl<Value*> &args, Value* target);

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

bool Memoize::isMemoizableLib(const Function &F) {
  return F.getName().contains("_memoized__");
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

  SmallVector<Type*, 5> prototype = getPrototype(F);
  std::sort(prototype.begin(), prototype.end(), [] (Type* a, Type* b) {
    return a->getTypeID() < b->getTypeID();
  });

  SmallVector<const Value*, 10> args;
  SmallVector<const Value*, 10> newArgs;

  Twine callString = "_memoized__" + F.getName();

  // Replace each call site 
  for (auto CS: F.users()) {
    CallInst* callSite = dyn_cast<CallInst>(CS);
    if (!callSite) {
      continue;
    }
    // TODO: UNIT tests
    // TODO: intengration test + sanity checks

    for (const Use &U: callSite->data_ops()) {
      args.push_back(U.get());
    }
    for (const GlobalVariable *GV: globals) {
      args.push_back(GV);
    }

    // 2. Sort arguments + globals
    newArgs.append(args.begin(), args.end());
    std::sort(newArgs.begin(), newArgs.begin(), [] (const Value* a, const Value* b) {
      return a->getType()->getTypeID() < b->getType()->getTypeID(); // sort by type
    });

    // 3. Remove constants
    for (const Value* x: args) {
      // TODO: figure out what isPresent check is for

      // get index of each argument
      size_t index = std::distance(args.begin(),
        std::find(args.begin(), args.end(), x)
      );

      // remove constants from call string
      if (const ConstantInt* c = dyn_cast<ConstantInt>(x)) {
        prototype.erase(prototype.begin() + index);
        args.erase(args.begin() + index);

        SmallString<10> constantValString;
        c->getValue().toString(constantValString, 10, true);
        //callString.concat(constantValString + ",)");
      }
    }

    if (!F.users().empty()) {
      // Call Site replacement procedure
      // 1) Create args properly and make sure the actual and formal parameter types should be same.
      //     If not add a cast.
      // 2) Make sure the return types are same.
      // 3) Set the calling conventions if any.
      // 4) Replace use if any.
      // 5) Then replace the instruction.

      // I. Build function type from sorted arguments + return type
      SmallVector<Type*, 5> signature;
      for (const Value* &arg: args) {
        signature.push_back(arg->getType());
      }
      FunctionType *functionType = FunctionType::get(
        F.getReturnType(),
        signature,
        false
      );

      // II. Create new function
      Function *newFunction = Function::Create(
        functionType,
        Function::LinkageTypes::ExternalLinkage,
        "_memoized__" + F.getName(),
        *F.getParent()
      );

      // III. Assign argument names
      size_t i = 0;
      for (Argument &A: newFunction->args()) {
        A.setName(args[i]->getName()); // TODO: fix up Twine/StringRef conversion
        i++;
      }
      

      // IV. replace uses with
      F.replaceAllUsesWith(newFunction);

      // V. Replace CallInst (maybe?)

      // Print call string to file
      errs() << "Memoized Function: ";
      errs() << "_memoized__" + F.getName() << "\n";

      // clean up time
      args.clear();
      newArgs.clear();
    }
  }
}

bool Memoize::mayBeOverridden(const Function &F) {
  return false; // TODO: add real implementation for this
}

void Memoize::getGlobalsByFunction(const Function &F, SmallPtrSetImpl<const GlobalVariable*> *globals) {
  for (const BasicBlock &BB: F) 
    for (const Instruction &I: BB) 
      for (const Value *Op: I.operands())
        if (const GlobalVariable* G = dyn_cast<GlobalVariable>(Op))
          globals->insert(G);    
}

SmallVector<Type*, 5> Memoize::getPrototype(const Function &F) {
  SmallVector<Type*,5> types;
  for (const Argument &A: F.args()) {
    types.push_back(A.getType());
  }
  return types;
}