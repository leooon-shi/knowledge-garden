// MiniASanPass. cpp - a minimal adress-sanitizer instrumentation pass.
// Companion to interview/sanitizer_development.md.
Il For every load/store in user code, insert a call
11
masan_check(ptr, size, is write, site_id)
I/ before the access. The runtime (masan_rt.c) checks the address against its
// shadow memory and reports bad accesses. site_id indexes a table of
"file: line:col" strings so reports name the source location.
// Deliberately simple (real ASan inlines the shadow check and only calls the
I1 runtime on the slow path; see sanitizers_and_codegen.md). Calling out on
// every access keeps the pass short so the structure is easy to read.
I/ Build:
// Use:
see interview/code/sanitizer_dev/build.sh
#include "11vm/IR/DebugInfoMetadata.h"
clang -g -fpass-plugin=./MiniASanPass.so prog.c masan_rt.c
#include "11vm/IR/IRBuilder.h" #include "11vm/IR/InstIterator.h" #include "11vm/IR/Instructions.h"
#include "11vm/IR/Module.h"
#include
"11vm/Passes/PassBuilder.h"
#include "11vm/Passes/PassPlugin.h"
using namespace llvm;
namespace
struct MiniASanPass : PassInfoMixin<MiniASanPass> {
PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
LLVMContext &C - M.getContext():
const DataLayout &DL = M.getDataLayout():
Type *132 - Type: :getInt32Ty(C), *164 - Type: :getInt64Ty(C);
PointerType *ptr = PointerType: : getUnqual(C);
// void masan_check(ptr addr, i64 size, 132 iswrite,132 site)
FunctionCallee Check = M.getOrInsertFunction(
"_masan_check", Type: :getVoidTy(C), Ptr, I64, I32, I32);
std::vector(std: :string> Sites;
bool Changed = false;
for (Function &F : M) {
// Never instrument declarations or the runtime itself (it would recurse).
if (F.iDeclaration() || F.getName().starts with("_masan"))
continue;
Il Collect first, mutate after: inserting while iterating invalidates iterators.
std: :vector<Instruction *> Accesses; for (Instruction &1 : instructions (F))
if (isa<LoadInst>(I) || isa‹StoreInst>(I))
Accesses-push_back(&I);
for (Instruction *I : Accesses) {
Value *Addr; Type *AccTy; bool Iswrite;
if (auto *L = dyn_cast<LoadInst>(I)) {
Addr - L-›getPointerOperand(); Accy - L-›getType(); Iswrite - false;
} else {
auto *5 = cast<StoreInst>(I);
Addr - S-›getPointerOperand(); AccTy = S->getValueOperand()->getType(): Iswrite - true;
}
// Skip non-default adress spaces (GPU memory, etc.) to keep it simple.
if (Addr->getType()-getPointerAddressSpace() I- 0) continue; std::string Loc - "<unknown›";
if (const DebugLoc &DLoc - I->getDebugLoc())
Loc - (DLoc->getFi1ename () +
":" + Twine(DLoc.getLine())+ ":"+
Tuine(DLoc-getCo]())),str();
Sites-push_back(Loc);
IRBuilder<> B(I); /l insertion point: immediately before the access
B.CreateCal1(Check,
{Addr, ConstantInt: :get(I64, DL-getTypeStoreSize(AccTy)).
ConstantInt: :get(132, IsWrite),
ConstantInt: :get(I32, Sites.size() - 1)});
5 MiniASanPass.cpp Xv O M I
Changed - true;
}
if (!Changed) return PreservedAnalyses: :a11();
// Emit the site table:
const char *_masan_sites_‹n›[] = {"f.c13:5", .}
// and register it with the runtime from a constructor.
std:: vector<Constant *> Strs;
for (auto &5 : Sites) {
Constant *Str = ConstantDataArray:: getString(C, S);
auto
*GV - new GlobalVariable(M, Str->getType(), true, GlobalValue: :PrivateLinkage, Str);
Strs. push_back(GV);
ArrayType *ATy - ArrayType: :get(Ptr,
Strs.size()):
auto *Table = new GlobalVariable(M,
ATy, true, GlobalValue: :PrivateLinkage,
ConstantArray:: get(ATy, Strs),
"_masan_sites*):
FunctionCallee Reg - M.getOrInsertFunction(
masan_register_sites", Type:: getVoidTy(C), Ptr, I32);
Function *Ctor = Function: :Create(FunctionType: :get (Type: :getVoldTy(C), false),

Globalvalue: InternalLinkage, "masan_module_ctor", MD:
IRBuilder<> (BasicBlock: :Create(C, "", Ctor)):
B.CreateCall(Reg, (Table, ConstantInt::get(I32, Strs.size())}):
B.CreateRetVoid();
appendtoglobalctors(M, Ctor, ®):
return PreservedAnalyses: none);
static void appendtoGlobalctors(Module &M, Function *F, int Prio) {
// Minimal 11vm.global_ctors append (avoids pulling in Transformutils).
LLVMContext &C= M-getContext):
PointerType *Ptr = PointerType:: getUnqual(C);
Structtype *ET StructType::getType: :getInt32Ty(C), Ptr, Ptr;
std::vector<Constant *> Elts;
if (GlobalVariable *Old = M.getNamedGlobal("11vm.global_ctors")) {
if (auto *Arr = dyn_cast_or_null<ConstantArray> (Old->getInitializer()))
for (auto 80p Arr-›operands)) Elts.push_back(cast<Constant>(Op)) :
Old-eraseFromParent ();
}
Elts-push_back(ConstantStruct::get(ET, (ConstantInt: :getType::getInt32TyC), Prio), F,
ConstantPointerNull: get(Ptr)})):
ArrayType *AT - ArrayType: :get(ET, Elts.size());
new GlobalVariable(M, AT, false, GlobalValue: :AppendingLinkage,
111111 11 | ConstantArray::get(AT, Elts), "11vm.global_ctors");
}
} / namespace
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo 11vmGetPassPluginInfo() {
return (LLVM PLUGIN_ API_VERSION, "MiniAsan", "0.1", [](PassBuilder 8PB)(
// WHERE to run is a design decision, and it decides what you can catch.
// OptimizerLast (after all optimization) produces the least code, but the
// optimizer may have already used the bug to delete the access: at -02
// clang folds an out-of-bounds read to "undef" and drops the load, and at 11-01 it forwards stored values and deletes a heap block that's only / written and read locally. Nothing is left to instrument.
11
// PipelineStart (before the optimizer) instruments every access the source
// contains. Our masan_check calls are opaque to the optimizer, so it
I can't delete the accesses they guard. Cost: checks on code that would 1/ otherwise be optimized away. That's why real ASan also instruments
// early-ish and why this lab defaults to it.
11 Set MINI ASAN_LATE=1 at compile time to see the late placement miss bugs. static const bool Late - geten("MINI_ASAN_LATE") |= nullptr; if (Late)
PB.registeroptimizerLastEPCallback(
[](ModulePassManager 8MPM, OptimizationLevel) {MPM.addPass(MiniASanPass()); });
else
PB.registerPipelineStartEPCal1back(
[1(ModulePassManager (ModulePassManager OptimizationLeve1) OptimizationLevel) {MPM.addPass(MiniASanPass()): 3):
// Also allow: opt -load-pass-plugin=./MiniASanPass.so - passes-mini-asan
PB.registerpipelineParsingCallback(
[](StringRef Name, ModulePassManager 8MPM, ArrayRef<PassBullder::PipelineElement>) {
if (Name |- "mini-asan") return false;
MPM, addPass (MiniASanPass());
return true;
3);
}
