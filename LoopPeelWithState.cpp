#include "llvm/Analysis/CGSCCPassManager.h"
#define DEBUG_TYPE "loop-peel-with-state"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Transforms/Utils/LoopPeel.h"
#include "llvm/Transforms/Utils/LoopUtils.h"

#include <set>
#include <vector>

namespace llvm {

class Loop;
class LPMUpdater;

class LoopPeelWithStatePass : public PassInfoMixin<LoopPeelWithStatePass> {
public:
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);

private:
  bool hasStateVariables(Loop &L);
  bool isDerivedFromIndVar(Value *DerivedValue, Loop &L);
};

} // namespace llvm

using namespace llvm;

static void printLoopSourceCode(const Loop &L, raw_ostream &OS) {
  StringMap<std::set<unsigned>> FileToLines;

  for (BasicBlock *BB : L.blocks()) {
    for (Instruction &I : *BB) {
      if (DILocation *DL = I.getDebugLoc()) {
        StringRef Filename = DL->getFilename();
        StringRef Directory = DL->getDirectory();

        SmallString<256> FullPath;
        if (!Directory.empty() && sys::path::is_relative(Filename)) {
          sys::path::append(FullPath, Directory, Filename);
        } else {
          FullPath = Filename;
        }

        FileToLines[FullPath.str().str()].insert(DL->getLine());
      }
    }
  }

  if (FileToLines.empty()) {
    OS << "  (No debug info available - compile with -g)\n";
    return;
  }

  OS << "  Source code:\n";

  for (auto &Entry : FileToLines) {
    std::string Filename = Entry.first().str();
    const std::set<unsigned> &Lines = Entry.second;

    if (Lines.empty())
      continue;

    unsigned MinLine = *Lines.begin();
    unsigned MaxLine = *Lines.rbegin();
    unsigned DisplayStart = (MinLine > 5) ? MinLine - 5 : 1;
    unsigned DisplayEnd = MaxLine + 5;

    OS << "  File: " << Filename << " (loop lines: " << MinLine << "-"
       << MaxLine << ")\n";

    auto FileOrErr = MemoryBuffer::getFile(Filename);
    if (!FileOrErr) {
      OS << "    (Could not open file: " << Filename << ")\n";
      continue;
    }

    StringRef BufRef = (*FileOrErr)->getBuffer();
    std::vector<std::string> AllLines;

    size_t Start = 0;
    size_t Pos = 0;
    while ((Pos = BufRef.find('\n', Start)) != StringRef::npos) {
      AllLines.push_back(BufRef.substr(Start, Pos - Start).str());
      Start = Pos + 1;
    }
    AllLines.push_back(BufRef.substr(Start).str());

    for (unsigned I = DisplayStart - 1; I < DisplayEnd && I < AllLines.size();
         ++I) {
      unsigned LineNum = I + 1;
      char Marker = Lines.count(LineNum) ? '>' : ' ';
      OS << "    " << Marker << " " << format("%4u", LineNum) << " | "
         << StringRef(AllLines[I]).rtrim() << "\n";
    }
  }
}

static void printDebugInfo(Loop &L, Value &IndVar, Value &Phi,
                           Value &LatchValue) {
  Function *F = L.getHeader()->getParent();
  LLVM_DEBUG(dbgs() << "\n=== LoopPeelWithStatePass triggered ===\n"
                    << "Function: " << demangle(F->getName().str()) << '\n'
                    << "Function (mangled): " << F->getName().str() << '\n'
                    << "Loop: " << L.getName() << '\n'
                    << "InductionVariable: " << IndVar << '\n'
                    << "Trigger phi:       " << Phi << '\n'
                    << "LatchValue:        " << LatchValue << '\n');
  LLVM_DEBUG({ printLoopSourceCode(L, dbgs()); });
  LLVM_DEBUG(dbgs() << "=== End LoopPeelWithStatePass info ===\n\n");
}

PreservedAnalyses LoopPeelWithStatePass::run(Loop &L, LoopAnalysisManager &AM,
                                             LoopStandardAnalysisResults &AR,
                                             LPMUpdater &U) {
  if (!L.isLoopSimplifyForm() || !L.getExitingBlock())
    return PreservedAnalyses::all();

  if (!hasStateVariables(L))
    return PreservedAnalyses::all();

  ValueToValueMapTy VM;
  if (!peelLoop(&L, 1, false, &AR.LI, &AR.SE, AR.DT, &AR.AC, true, VM))
    return PreservedAnalyses::all();

  return PreservedAnalyses::none();
}

bool LoopPeelWithStatePass::hasStateVariables(Loop &L) {
  BasicBlock *Header = L.getHeader();
  BasicBlock *Latch = L.getLoopLatch();

  Value *IndVar = L.getCanonicalInductionVariable();
  if (!IndVar)
    return false;

  for (PHINode &Phi : Header->phis()) {
    if (Phi.getBasicBlockIndex(Latch) < 0)
      continue;
    if (&Phi == IndVar)
      continue;

    Value *LatchValue = Phi.getIncomingValueForBlock(Latch);

    if (isDerivedFromIndVar(LatchValue, L)) {
      LLVM_DEBUG(printDebugInfo(L, *IndVar, Phi, *LatchValue));
      return true;
    }
  }

  return false;
}

bool LoopPeelWithStatePass::isDerivedFromIndVar(Value *DerivedValue, Loop &L) {
  Value *IndVar = L.getCanonicalInductionVariable();
  if (!IndVar || !DerivedValue)
    return false;

  if (DerivedValue == IndVar)
    return true;

  Value *V = DerivedValue;
  int I = 0;
  while (auto *CI = dyn_cast<Instruction>(V)) {
    V = CI->getOperand(0);
    if (!V)
      return false;
    if (V == IndVar)
      return true;
    if (V == DerivedValue)
      return false;
    if (I++ == 100)
      return false;
  }

  return false;
}

PassPluginLibraryInfo getLoopPeelWithStatePluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION, "LoopPeelWithState", LLVM_VERSION_STRING,
      [](PassBuilder &PB) {
        PB.registerVectorizerStartEPCallback([](llvm::FunctionPassManager &PM,
                                                OptimizationLevel Level) {
          PM.addPass(createFunctionToLoopPassAdaptor(LoopPeelWithStatePass()));
        });
        PB.registerPipelineParsingCallback(
            [](StringRef Name, FunctionPassManager &FPM,
               ArrayRef<PassBuilder::PipelineElement>) {
              if (Name == "loop-peel-with-state") {
                FPM.addPass(
                    createFunctionToLoopPassAdaptor(LoopPeelWithStatePass()));
                return true;
              }
              return false;
            });
      }};
}

#ifndef LLVM_BYE_LINK_INTO_TOOLS
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getLoopPeelWithStatePluginInfo();
}
#endif
