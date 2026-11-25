<!-- docs/cpp/11vm_directory_structure.md -->

Add a **quiz and practical assessment** to every directory. The learner should pass each phase before moving to the next one.
11vm-learning/ - 01-ir-inspection/ - 02-ir-builder/- 03-kaleidoscope/H
64-analysis-pass/ 05-transformation-pass/ - 06-clang-tool/ - o7-orc-jit/ H
08-backend-inspection/ L README.md
Each directory can use this structure:
01-ir-inspection/ H src/ - input/ | output/ | tests/ I quiz.md - assessment.md
- README.md
## Common assessment format
Each phase should contain:
Knowledge quiz: 30% Implementation task: 40% Debugging task: 20%
Explanation/documentation: 10%
A reasonable passing condition is:
-   At least 70% overall
-   No serious conceptual errors
-   Implementation compiles and runs
-   Tests pass
-   Learner can explain the result without copying documentation
# 01-ir-inspection
## Learning goals
The learner should understand:
-   How C++ becomes LLVM IR
-   Modules, functions, basic blocks, and instructions
-   SSA values
-   "alloca"
, "load , and "store
- Branches and phi nodes

# el-Ir-inspection
## Quiz
1.    What is the role of Clang in the LLVM compilation process?
2.    What is the difference between LLVM assembly and LLVM bitcode?
3.    What is a basic block?
4.    Why does LLVM IR use SSA form?
5.    What problem does a phi instruction solve?
6.    What is the purpose of alloca'?
7.    What is the difference between
"load and "store?
1.    What is the difference between -00 and -02 ?
2.    Which tool converts LLVM assembly into bitcode?
3.     Which tool converts LLVM IR into assembly?
## Practical task
Create this program:
int choosebool condition, int xint y) {
if (condition)
return x;
return y;
｝
Generate IR at • and and -02
***bash
clang++ -S -emit-1lvm -00 choose.cpp -o choose-o0.11 clang++ -S -emit-11vm -02 choose. cpp -o choose-02.11
The learner must identify:

		## Practical task
		  The entry block
  - The conditional branc		  

- The merge bl
		  k


- Any phi" instru
		  ion



- At least two differences between the unoptimized and opti
mized IR
## Debugging task
Give the learner invalid or modified IR containing one of these errors:
-   A branch targets a nonexistent block
-   A phi node has the wrong number of incoming values
-   An instruction uses a value defined in an unrelated function
-   A return instruction has the wrong type
They must use LLV tools to locate and explain the error.
## Pass criteria
The learner can draw the control-flow graph and explain every basic block in their generated IR.
# 02-ir-builder
## Learning goals
The learner should understand:
-   "LLVMContext*
-   "Module"
-   "IRBuilder"
-   LLVM types
-   Function declarations and definitions
-   Basic-block creation
-   Verification
## Quiz

1.    What is the purpose of "LLVMContext?
2.    What does an LLVM "Module" represent?
3.    What does "IRBuilder" simplify?
4.    What is the difference between a declaration and a definition?
5.    Why must LLVM IR be verified?
6.    What is the difference between an LLVM "Value" and an LLVM "Type?
7.    Why should the LLVM API version match the installed LLVM tools?
8.    What is the purpose of *11vm: :verifymodule ?
9.    What does a function's return type describe?
10.     Why should ownership of LLVM objects be handled carefully?
## Practical task
Write a C++ program using the LLV C++ API that creates a function equivalent to:
•**cpp
int add (int a, int b) {
return a + b;
The program must:
-   Create a module
-   Create the function type
-   Create a basic block
-   Use "IRBuilder"
-   Generate an integer addition
-   Return the result
-   Verify the module
-   Print the generated IR
## Extension task
Generate:
***cpp
int max_value(int a, int b) {
if (a > b)
    return a
return b

The generated IR should contain:|
-   A comparison
-   A conditional branch
-   Two return paths
## Debugging task
Provide code with one of these problems:
-   The builder inserts instructions into the wrong block
-   The function return type does not match the returned value
-   A basic block has no terminator
-   The module contains malformed IR
The learner must identify the issue and fix it.
## Pass criteria
The learner can write a small IR-generation program without relying on clang to generate the target function.
# 03-kaleidoscope
#* Learning goals
The learner should understand the full compiler pipeline:
Source text • Lexer Parser AST • LLVM IR \ JIT or executable Icode
## Quiz
1.    What is the responsibility of a lexer?
2.    What is the responsibility of a parser?
3.    What is an AST?
4.    why should parsing and code generation be separate stages?
5.    What is operator precedence?
6.    What is recursive descent parsing?

1.    Why does a compiler need a symbol table?
2.    What is the difference between syntax errors and semantic errors?
3.    Why are basic blocks needed for if expressions?
4.     What is the role of the JIT in Kaleidoscope?
## Practical task
Implement a small language supporting:
-   Numeric literals
-   Binary arithmetic
-   Function definitions
-   Function calls
-   Conditional expressions
-   User-defined variables
Example:
def double(x) x * 2; double(21);
The learner must show:
1.    Tokens produced by the lexer
2.    The AST
3.    Generated LLVM IR
4.    Final execution result
#* Extension task
Add one feature:
-   Boolean values
-   "while loops
-   Arrays
-   User-defined external functions
-   Function parameters with type checking
## Debugging task
Give the learner a parser containing an operator-precedence bug. For example:

1+2米
3
is incorrectly interpreted as:
（1+2）*3
They must correct the parser and add a regression test.
## Pass criteria
The learner can trace one expression from source text to executed machine code.
# 04-analysis-pass
#* Learning goals
The learner should understand:
- The New Pass Manager
-   Module, function, and loop passes
-   LLVM analyses
-   -Preservedanalyses®
-   Pass registration
-   "FileCheck"
##Quiz
1.    What is an analysis pass?
2.    What is a transformation pass?
3.    What does "PreservedAnalyses" describe?
4.    What is the difference between a module pass and a function pass?
5.    Why should an analysis avoid modifying IR?
6.    What is a control-flow graph?
7.    What is a dominator?
8.    What information can loop analysis provide?
9.    Why are analyses cached?
10.     What is "FileCheck' used for?

## Practical task
write a function analysis pass that reports:
-   Function name
-   Number of basic blocks
-   Number of instructions
-   Number of function calls
-   Number of return instructions
Example output:
Function: process_packet Basic blocks: 5 Instructions: 42 Calls: 3 Returns: 1
## Extension task
Use LLV analyses to report:
-   Loops in each function
-   Unreachable blocks
-   Functions containing indirect calls
-   Basic blocks with multiple successors
## Testing task
Create a FileCheck test:
•*11vm
; RUN:
; RUN: opt -load-pass-plugin %shlibdir/Yourpass%shlibext |
-passes-"your-pass" -disable-output %s 2>81 | Filecheck %s
; CHECK: Function: test ; CHECK: Basic blocks:

The learner can explain which analyses their pass uses and correctly state what the pass preserves.

	- Safe IR mutation
	- Replacing instructions
	- Removing instructions
	- Updating uses
	- Maintaining IR validity
	- Transformation testing
# 05-transformation-pass
## Learning goals
The learner should understand:
# 04-analysis-pass >
## Pass criteria
## quiz
1.    What does it mean to replace all uses of an instruction?
2.    Why must an instruction be removed only after its uses are handled?
3.    Why must every basic block have a valid terminator?
4.    What is dead code?
5.    What is constant folding?
6.    What is the difference between local and global transformation?
7.    Why should a transformation preserve program semantics?
8.    What can happen if a pass invalidates an analysis?
9.    Why should transformed IR be verified?
•
10. Why are before-and-after tests important?
## Practical task
Implement a transformation pass that replaces a selected pattern:
I
•**11vm
%result = add i32 10, 20
with:
•**11vm
%result = 30
Alternatively, implement a pass that inserts a call to a logging function at the beginning of every selected function.
Extension task


rass criteria
## Extension task
Implement one of:
-   Remove unreachable blocks
-   Replace branches with constant conditions
-   Count function entries at runtime
-   Attach custom metadata to selected instructions
-   Simplify redundant arithmetic
## Testing task
For every transformation, provide:
-   Input IR
-   Expected output behavior
-   FileCheck assertions
-   A test proving that unrelated code is unchanged
-  A test with malformed or unusual input
## Pass criteria
The learner demonstrates that the transformed program produces the same result as the original program for the tested cases.
# 06-clang-tool
## Learning goals
The learner should understand:
-   Clang ASTS
-   AST matchers
-   Source locations
-   Diagnostics
-   Compiler arguments
-   The difference between AST analysis and LLVM IR analysis
## Quiz

1.    What information does the clang AST preserve that LLVM IR usually does not?
2.    What is an AST matcher?
3.    What is a "SourceLocation?
4.    What is a "Frontendaction?
5.    What is a compilation database?
6.    why is "compile_commands. json useful?
7.    When should you use clang tooling instead of an LLVM pass?
8.    What is the difference between "Decl
9.    What does "RecursiveASTVisitor" do?
"Stmt", and "Expr?
10. Why can macros complicate source analysis?
## Practical task
Create a clang tool that detects calls to selected blocking functions inside functi marked as event-loop callbacks.
For example, report:
warning: blocking call to read() inside reactor callback
The tool should:
-   Parse a real C+ file
-   Match function calls
-   Print the function and source location
-   Return a nonzero status when violations are found
-   Include at least five tests
## Extension task
Implement one of:
-   Find raw pointer members
-   Detect calls to deprecated APIS
-   Find functions exceeding a complexity threshold
-   Report all uses of a selected class
-   Add a source rewrite
#* Pass criteria

The learner can explain why this problem is better solved with clang AST tooling than wit an LLVM IR pass.
# 07-orc-jit
## Learning goals
The learner should understand:
-   Runtime compilation
-   "LLJIT
-   "ThreadsafeContext"
-   "ThreadSafeModule"
-   Symbol lookup
-   Runtime symbol registration
-   JIT lifetime and ownership
## Quiz
1.    What is the difference between ahead-of-time compilation and JIT compilation?
2.    What does "LLJIT" provide?
3.    What is a "JITDylib'?
4.    Why are thread-safe LLVM contexts important?
5.    How does a JITed function become callable from C++?
6.    What is symbol lookup?
7.    Why might a JIT need access to native runtime functions?
8.    What is lazy compilation?
9.    What are the costs of JIT compilation?
10.     When would a lookup-table implementation be better than a JIT?
## Practical task
Build a JIT program that generates and executes:
** cpp
int classify(int protocol, int port);
...

The generated function should return different values based on the inputs.
The program must:
-   Construct LLVM IR
-   Compile it with ORC
-   Look up the function
-   Call it from C++
-   Check the result against a normal C++ implementation
## Extension task
Build a small rule engine:
protocol = GRE && port = 4789
Compile each rule into a JITed function and benchmark it against:
-   A normal "if statement
-   A switch
-   A lookup table
## Pass criteria
The learner reports both:
-   Runtime execution performance
-   Compilation and setup overhead
They should not claim that JIT is faster without measuring both.
# 08-backend-inspection
## Learning goals
The learner should understand:
- Target triples
- 
-   CPU features
-   LLVM IR to assembly
-   Instruction selection
-   Register allocation
-   Calling conventions
-   Assembly inspection
-   Basic performance analysis
## Quiz
1.    What is a target triple?
2.    What is the difference between generic and CPU-specific code generation?
3.    What is instruction selection?
4.    What is register allocation?
5.    What is instruction scheduling?
6.    What is an ABI?
7.    Why can the same LLVM IR produce different assembly?
8.    What does •-march control?
9.    What does -mcpu" control?
10.     What problem does "11vm-mca" help analyze?
## Practical task
Compile a small function using different target options:
• * *bash
clang++ -02 -S -march=x86-64 add.cpp -o generic.s clang++ -02 -S -march=native add. cpp -o native.s
...
Compare:
	- Instructions generated
	- Register usage
	- Function prologue and epilogue
	• Vector instructions
	- Code size



## Extension task
#bckend-inspection >
## Practical task
Analyze a packet-processing or buffer-copying function and investigate:
-   Whether it is vectorized
-   Whether branches were removed
-   Whether calls were inlined
-   Whether aliasing prevents optimization
-   whether target-specific instructions are used
Use:
• - bash
1lvm-mca generated.s
•..
## Pass criteria
The learner can explain why a specific assembly instruction exists and connect it to the original C+ or LLVM IR operation.
# Final integration assessment
After completing all directories, build one project containing the complete pipeline:
Mini language or configuration • Parser and AST • LLVM IR generation + custom analysis pass + Custom transformation pass • ORC JIT \ Native C++ runtime d Benchmark and tests
## Suggested final project
Implement a small rule compiler for packet classification:
if protocol == GRE and domain == 10: consumer = 1 else: consumer = 2
The project should include:
-   Lexer and parser
-   AST
-   LLVM IR generation

## Suggested final project
acocosment
-   IR verification
-   At least one analysis pass
-   At least one transformation pass
-   ORC JIT execution
-   C++ runtime integration
-   Correctness tests
-   Performance benchmarks
-   Error handling
-   Documentation
## Final oral or written questions
The learner should be able to answer:
1.    Where is C++ syntax handled?
2.    Where is type checking handled?
3.    What is the role of LLVM IR?
4.    Why does LLVM use SSA?
5.    Where do optimizations occur?
6.    What is the difference between an analysis and a transformation pass?
7.    When should clang tooling be used?
8.    When should an LLVM pass be used?
9.    What does the backend add?
10.     When is ORC JIT useful?
11.     What are the costs of JIT compilation?
12.     How did the learner verify correctness?
13.     How did the learner measure performance?
14.     which parts of the project are target-independent?
15.     Which parts depend on the target cPu?
I
## Recommended 'README.md" template
Every directory should include:
•markdown
# Phase name
## objectives
** Concepts learned

## objectives
#
Concepts learned	
##
Commands used	
## Implementation	
	
#
Practical assessment	
## Debugging exercise	
##
Expected results	
## Common mistakes	
## Completion checklist	
I
		- []I completed the quiz
		- [lI implemented the exercise
	［	1 My program builds
-		] My tests pass
-		I verified the generated IR
		] I can explain the result
		- [JI recorded what I learned
••。
This turns the repository from a collection of examples into a progressive, testable LLVM course.