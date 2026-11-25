<!-- docs/cpp/llvm_component.md -->

## LLVM component architecture

LLVM is best understood as a collection of reusable compiler components, not a single
compiler program.

```
                Source code
                    |
                    v
          +-------------------+
          |     Frontend       |
          | Clang, Rustc, etc. |
          +-------------------+
                    |
                    v
              LLVM IR
                    |
                    v
          +-------------------+
          | IR analyses and    |
          | optimization passes|
          +-------------------+
                    |
                    v
          +-------------------+
          | Target-independent |
          | code generation    |
          +-------------------+
                    |
                    v
          +-------------------+
          | Target-dependent   |
          | backend            |
          | X86, ARM, RISC-V   |
          +-------------------+
                    |
                    v
          Assembly or object file
                    |
                    v
          +-------------------+
          | Linker            |
          | LLD               |
          +-------------------+
                    |
                    v
                Executable
```

## 1. Frontend

The frontend understands a programming language and converts source code into LLVM IR.

For C and Ctt, the frontend is **clang**.

C++ source | v clang lexer | v clang parser I v clang AST | v LLVM IR

clang performs:

- Preprocessing
- Lexical analysis
- Parsing
- Type checking
- Name lookup
- Template instantiation
- Semantic analysis
- C++ diagnostic reporting
- LLVM IR generation

For example:

```cpp
int add (int a, int b) {
	return a + b;
}
```

clang understands C++ syntax and semantics. LLVM itself does not understand concepts such
as:

- C++ classes
- Templates
- Function overloading
- `std::vector`
- C++ access control
- C++ exceptions at the source-language level

Those are handled by clang before LLVM receives the program.

other languages can use different frontends:

C/C++ -> Clang Rust -> rust Swift -> Swift frontend Fortran -> Flang

They can all produce LLVM IR.

## 2. Clang AST

Before generating LLVM IR, Clang builds an **Abstract Syntax Tree**, or AST.

For this code:

```cpp
int result = add (1, 2);
```

The AST represents concepts such as:

Variable declaration: result | +-- Function call: add | +-- Integer literal: 1 +-- Integer
literal: 2

The AST preserves high-level source information. It is useful for:

- Static analysis
- Refactoring
- Source-to-source transformation
- Code formatting
- API migration
- `Clang-tidy` checks

Use Clang AST tools with:

```bash
clang++ -Xclang -ast-dump -fsyntax-only example.cpp
```

Clang AST and LLVM IR serve different purposes:

Clang AST: High-level C++ source representation

LLVM IR: Lower-level representation suitable for optimization and code generation

## 3. LLVM IR

LLVM IR is the central interface between frontends, optimizers, and backends.

It has three common representations:

LLVM assembly .ll Human-readable text LLVM bitcode bc Binary representation In-memory IR
C++ API representation

Example:

```bash
clang++ -S -emit-llvm -O0 example.cpp -o example.ll
```
LLVM IR contains:

- Modules
- Global variables
- Functions
- Basic blocks
- Instructions
- Types
- Control flow
- Metadata
- Attributes
- Debug information

A simplified IR structure looks like this:
Module | +-- Global variables | +-- Function | +-- Basic block | | +-- Instruction | +--
Instruction | +-- Basic block | +-- Instruction

A function is divided into **basic blocks**. A basic block is a sequence of instructions
with:

- One entry point
- Usually one terminating control-flow instruction

LLVM IR uses SSA form. In SSA, each temporary value is assigned once.

For example, a conditional expression may produce a `phi` instruction:

entry: branch to then or else

then: value = 10 branch to merge

else: value = 20 branch to merge

merge: result = phi(value from then, value from else)

Important tools:

```bash
llvm-dis input.bc -o output.ll
llvm-as input.ll -o output.bc
opt -S input.ll -o output.ll
llc input.ll -o output.s
```

## 4. LLVM IR libraries

LLVM provides Ct+ classes for constructing and inspecting IR.

Important classes include:

llvm::Module llvm::Function llvm::BasicBlock llvm::Instruction llvm::Value llvm::Type
llvm::IRBuilder llvm::LLVMContext

Their relationships are approximately:
LLVMContext | +-- Module | +-- Function | +-- BasicBlock | +-- Instruction | +-- Value

`IRbuilder` is commonly used to create instructions:

```cpp
llvm::IRBuilder<> builder(block);
builder.Createadd(Ihs, rhs, "sum");
```

The exact API depends on your LLVM version, so use documentation matching the installed
version.

## 5.Analysis passes

An analysis pass examines the program without changing it.

Examples:

- Count functions
- Count instructions
- Find 100ps
- Compute dominators
- Perform alias analysis
- Determine whether a function can be inlined
- Find call sites
- Calculate control-flow relationships

The result of an analysis can be used by transformation passes.

LLVM IR | v Analysis pass | +-- Function information +-- Loop information +-- Alias
information +-- Control-flow information

Example:

Input function | v Instruction-count analysis | v "Function contains 42 instructions"

An analysis should normally preserve the input IR.

## 6. Transformation passes

A transformation pass modifies LLVM IR.

- Constant folding
- Dead-code elimination
- Function inlining
- Loop unrolling
- Branch simplification
- Global optimization
- Vectorization
- Instruction combining

Examples:

A simplified optimization pipeline looks like this:

Initial LLVM IR | v Mem2Reg | v Constant propagation | v Instruction combining | v
Dead-code elimination | v Inlining | v Loop optimization | v Vectorization | v optimized
LLVM IR

LLVM's optimization pipeline uses the **New Pass Manager**.

Important concepts include:

- `ModulePass`
- `Functionpass`
- `LoopPass`
- `AnalysisManager`
- `PreservedAnalyses`
- Pass pipelines
- Pass plugins
 
You can inspect optimization behavior with:

```bash
clang++ -O2 -fdebug-pass-manager example.cpp -c -o example.o
```

You can also run selected passes manually:

```bash
opt -passes="mem2reg" -S input.ll -o output.ll
```

## 7. LLVM middle-end

The middle-end contains most target-independent optimization logic.

LLVM IR | +-- Scalar optimizations +-- Control-flow optimizations +-- Function and module
optimizations +-- Loop optimizations +-- Alias analysis +-- Interprocedural analysis +--
Vectorization | v Optimized LLVM IR

The middle-end generally tries to avoid depending on a particular CPU.

For example, dead-code elimination can apply to x86, ARM, and RISC-V equally.

Target-specific decisions are handled later by the backend.

## 8. Target machine and backend

The backend converts optimized LLVM IR into instructions for a specific target.

optimized LLVM IR | v TargetMachine | v Instrucion selection | v Machine IR | v
Instruction scheduling | v Register allocation | v Assembly emission

The backend handles target-dependent concerns:

- CPU instructions
- Registers
- Instruction constraints
- Calling conventions
- ABI rules
- Stack frames
- Register allocation
- Instruction scheduling
- CPU features
- Assembly syntax

Common LLVM targets include:

X86-64 ARM AArch64 RISC-V WebAssembly PowerPC MIPS

You can inspect generated assembly:

```bash
clang++ -O2 -S example.cpp -o example. s
```

You can compare target architectures:

```bash
clangt+ -O2 -target x86_64-linux-gnu -S example.cpp -o x86.s
clang++ -O2 -target aarch64-linux-gnu -S example.cpp -o arm.s
```

The exact cross-compilation setup may require an appropriate syroot and compiler support.

## 9. Machine IR

LLVM first operates on LLVM IR, but backend processing uses a lower-level representation
called **Machine IR**， often abbreviated as MIR.

The general flow is:

LLVM IR | v Target-independent code generation | v Machine IR | v Target-specific machine
instructions | v Assembly

Machine IR represents details such as:

- Physical registers
- Virtual registers
- Target instructions
- Register classes
- Stack slots
- Instruction constraints

You normally study MIR aftğr becoming comfortable with LLVM IR and optimization passes.

## 10. MC layer

The LLVM MC layer handles machine-code and assembly-related tasks.

It supports:

- Assembly parsing
- Assembly printing
- Object-file generation
- Instruction encoding
- Instruction decoding
- Disassembly

Simplified architecture:

Machine instructions | +-- Assembly printer | | | v | Assembly text | +-- object writer
| v object file

Tools associated with this area include:

```bash
llvm-mc
llvm-objdump
llvm-readobj
llvm-readelf
```
For example:

```bash
llvm-objdump -d program
llvm-readobj -h program.o
```

## 11. Linker: LLD

LLVM does not require LLD, but the LLVM project includes it.

LLD combines object files and libraries into an executable or shared library.

main.o library o runtime libraries | v LLD | v executable or shared library

Typical usage:

```bash
clang++ -fuse-ld=lld main. cpp -o program
```

The linker resolves:

- External symbols
- Function references
- Global variables
- Relocations
- Shared-Library dependencies
- Static-library members

Compilation and linking are separate stages:

C++ source v Compiler | v object file | v Linker | v Executable

## 12. Runtime libraries

Generated code often depends on runtime libraries.

Examples include:

- C runtime
- C++ standard library
- Compiler runtime
- Exception-handling runtime
- Sanitizer runtime
- OpenMP runtime

LLVM's compiler runtime includes libraries such as:

- `compiler-rt`
- `libunwind`
- OpenMP runtime components

For example, operations that are not directly supported by the target CPU may become calls
to compiler runtime functions.

## 13. ORC JIT

ORC iS LLV's modern JIT compilation framework.

Instead of producing a permanent executable, it compiles LLVM IR during program execution.

Runtime-generated code | v LLVM IR | v ORC JIT | v Native machine code in memory | v
Function lookup and execution

Important ORC concepts include:

- `LLJIT`
- `JITDylib`
- `ThreadSafeContext`
- `ThreadsafeModule`
- Symbol lookup
- Runtime symbol registration
- object layers
- Lazy compilation

Example use cases:

- Language interpreters
- Dynamic expressions
- Database query execution
- Regular-expression engines
- Runtime packet classification
- Dynamic rule systems

For a high-throughput forwarding service, ORC could compile classification or forwarding rules, but it should be benchmarked against simpler alternatives such as:

- Precomputed lookup tables
- Function pointers
- `switch` statements
- Policy objects
- Specialized templates

## 14. MLIR

MIR is a separate but related compiler infrastructure project in the LLVM ecosystem.

LLVM IR is relatively low-level. MLIR supports multiple abstraction levels.

Domain-specific representation | v High-level MLIR dialect | v Lower-level MLIR dialect | v LLVM dialect |
v LLVM IR | v Machine code

MLIR is useful for:

- Domain-specific languages
- Tensor and machine-learning compilers
- GPU programming
- Database auerv svstems
- Hardware accelerators
- High-level transformations

A dialect defines operations suitable for a particular domain.

For example, a tensor dialect can express tensor operations directly, while LLV IR would
require much more explicit low-level detail.

Study MLIR after learning basic LLVM IR unless your immediate goal is building a
domain-specific compiler.

## 15. Important command-line tools

clang C/C++ frontend and driver clang++ C++ compiler driver opt LLVM IR optimization tool
llc LLVM IR to assembly compiler lli LLV IR interpreter/JIT runner llvm-as LLVM assembly
to bitcode llvm-dis LLVM bitcode to LLVM assembly llvm-mc assembler/disassembler
infrastructure llvm-objdump object-file disassembler llvm-readobj object-file inspection
llvm-mca machine-code performance analysis 1ld LLVM linker lldb debugger

A minimal practical workflow is:

```bash
# Generate LLVM IR
clang++ -S -emit-llvm -O0 example.cpp -o example.ll

# optimize LLVM IR
opt -passes="default<O2>" -S example.ll -o optimized.ll

# Generate assembly
llc optimized.ll -o example.s

# Generate an executable directly
clang++ -O2 example.cpp -o example
```

## 16. The most useful mental model

For your first stage of study, remember this division:

Clang AST = What does the Ctt source mean?

LLVM IR = What operations does the program perform?

LLVM optimization passes = Can those operations be simplified or improved?

Backend = How should those operations execute on this CPU?

LLD = How should all compiled pieces be combined?

ORC JIT = Can this compilation happen dynamically at runtime?.

A good study order is:

Clang command line | v LLVM IR | v IrBuilder and LLVM CH API | v Analysis passes | v
Transformation passes | v Clang AST tooling | v ORC JIT | v Backend and Machine IR | v
MLIR

For practice, create one repository with separate directories:

llvm-learning/ |-- 01-ir-inspection/ |-- 02-ir-builder/ |-- 03-kaleidoscope/ |--
04-analysis-pass/ |—- 05-transformation-pass/ |-— 06-clang-tool/ |-- 07-orc-jit/ |--
08-backend-inspection/ |__ README.md

Each directory should contain:
	
- A small source example
- The commands used
- Input IR
- Output IR or assembly
- Tests
- A short explanation of what changed and why

