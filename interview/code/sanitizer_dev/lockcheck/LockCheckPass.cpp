designed tockcheck GLockCheckPass.cp ..
Y/ LockneckPass. cpp - a self-designed sanitizer: "guarded fields are only
// accessed while their mutex is held".
1/ Companion to interview/sanitizer_development.md (Part 2: designing your own).
//
// Source annotation (see lockcheck.h):
struct Account (int id; long balance GUARDED_BY (g_mu); ):
1/ expands to _attribute_((annotate("lockcheck.guarded_by:g_mu"))).
//
// Clang lowers every access to an annotated field through
//
%p = call ptr @11vm.ptr.annotation(ptr %field, ptr @"lockcheck.guarded_by:g_mu",...)
load/store •.. %p
/l so the pass:
/1
1. finds 11vm.ptr. annotation calls whose string starts with our prefix,
//
2. resolves the mutex name to a global (@g_mu) in this module,
//
3. inserts
lockcheck_access&g_mu, is write, site) before every load/store
through the annotated pointer.
// The runtime (lockcheck_rt.) keeps a per-thread set of held mutexes by
// intercepting pthread_mutex_lock/unlock and reports accesses without the lock.
//
I/ Scope limits (documented in the design doc): the mutex must be a global in / the same translation unit; accesses through a pointer that escaped the
//annotation (eg. "long *p = &a.balance; *p= 1; in another function) are
/ not checked.
#include "11vm/IR/Constants.h"
#include "11vm/IR/DebugInfoMetadata.h"
#include "11vm/IR/GlobalVariable.h"
#include "11vm/IR/IRBuilder.h"
#include "11vm/IR/InstIterator.h"
#include "11vm/IR/Instructions.h"
#include "11vm/IR/IntrinsicInst.h"
#include "11vm/IR/Module.h"
#include "11vm/Passes/PassBuilder.h"
#include "11vm/Passes/PassPlugin.h"
#include "11vm/Support/raw_ostream.h"
using namespace 1lvm;
namespace {
constexpr StringRef Prefix - "lockcheck.guarded_by:";
// Read the C string an annotation operand points to.
static std: optional<StringRef> annotationString(Value *v) {
auto
*GV = dyn_cast<GlobalVariable>(V->stripPointerCasts());
if (IGV || IGV-›hasInitializer()) return std::nullopt;
auto CDA = dyn_cast<ConstantDataArray>(GV->getInitializer()):
if (ICDA || IDA-›isCString()) return std::nullopt;
return CDA->getAsCString();
struct LockCheckPass : PassInfoMixin<LockCheckPass> (
PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
LLVMContext &C = M-getContext();
Type *132 - Type: :getInt32Ty(C);
PointerType *Ptr = PointerType:: getUnqual(C);
FunctionCallee Access = M.getOrInsertFunction(
"_lockcheck_access", Type::getVoidTy(C), Ptr, I32, Ptr);
unsigned NChecks = 0;
for (Function &F: M) {
if (F.isDeclaration() || F-getName() starts_with("_lockcheck"')) continue;
// Annotation call -> mutex global.
std:: vector«std::pair<IntrinsicInst *, GlobalVariable *> Annots;
for (Instruction &I : instructions(F)) {
auto *II - dyn_cast<IntrinsicInst>(&I);
if (III || II-›getIntrinsicID() |- Intrinsic::ptr_annotation) continue;
auto 5 • annotationString(II->getArgOperand(1));
if (IS || 15-›starts_with(Prefix)) continue;
StringRef MuName - S->drop_front(Prefix.size());
GlobalVariable *Mu = M.getNamedGLobal(MuName) ;
if (SMu) {
// A design decision: fail loudly at compile time rather than
/I silently skip a check the programmer asked for.
errs) « "lockcheck; error; mutex * « MuName «< "* named in GUARDED_BY is not a "
«< "global in " « M.getName() «<
" (function " «‹ F.getName() « ") In":
report_fatal_error("Lockcheck: unresolved GUARDED_BY mutex", false);
Annots. push_back((II, Mu});
for (auto &[II, Mu] : Annots) {
// Every load/store whose address is the annotated pointer.
std:: vector<Instruction *> Uses;
for (User *U: II->users)) {
if (auto *L - dyn_cast<LoadInst>(U); L 88 L->getPointerOperand() -- II)
Uses. push_back(L);
else if (auto *S - dyn_cast<StoreInst>(U); 5 && S->getPointerOperand() -- II)
Uses. push_back(S);
}
for (Instruction *Acc : Uses) {
std: :string Loc - F.getName().str:
if (const DebugLoc &DL - Acc->getDebugLoc())

Loc - (DL-›getFilename() +";"+Twine(DL.getLine()) + * in * + F.getName()).str():
IrBuilder<> B(Acc);
Value *Site = B.CreateGlobalStringPtr(Loc, "lockcheck. site"):
B.CreateCal1(Access, (Mu, ConstantInt: get(132, isa<StoreInst>(Acc)), Site)):
++NChecks;
return NChecks? PreservedAnalyses: :none) : PreservedAnalyses::a11();
35
} /1 namespace
extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo 11vmGetPassPluginInfo() {
return {LLVM_PLUGIN_API_VERSION, "LockCheck", "0.1", [](PassBuilder &PB) {
I/ Run before the optimizer, same reasoning as MiniAsan: the optimizer can / merge or sink accesses across lock/unlock calls it can see through.
PB.registerPipelineStartEPCallback(
[](ModulePassManager &MPM, OptimizationLevel) { MPM.addPass(LockCheckPass()); 3):
PB.registerpipelineParsingcallback(
[](StringRef Name, ModulePassManager 8MPM, ArrayRef<PassBuilder: :PipelineElement>) {
if (Name |- "lockcheck") return false;
MPM. addPass (LockCheckPass()):
return true;
}):
