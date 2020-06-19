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
      return false;
    }
  };
}

char Memoize::ID = 0;
static RegisterPass<Memoize> X("memoize", "Function Memoize Pass",
                             false /* Call site replacement modifies CFG */,
                             false /* This is not an analysis pass */);
