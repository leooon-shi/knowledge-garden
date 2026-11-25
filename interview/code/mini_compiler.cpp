// mini_compiler.cpp
// A from-scratch mini-compiler pipeline to learn compiler analysis concepts
// hands-on.Companionto interview/compiler_knowledge_interview_qa.md and
// interview/compiler_knowledge_study_guide.md.
//
// It implements, for a tiny imperative language:
// 1. Lexer (characters -> tokens)
// 2. Parser (tokens -> AST, recursive descent, precedence climbing)
// 3. AST (nodes + a Visitor-style printer)
// 4. CFG (AST -> basic blocks + edges; lowering if/while)
// 5. Dominators (iterative data-flow; dominator tree)
// 6. Liveness (backward, may/union data-flow)
// 7. Reaching defs (forward, may/union data-flow)

// The point is pedagogy and readability, NOT production quality: it uses simple
// O(n^2) algorithms (iterative dominators, not Lengauer-Tarjan) and sets rather
// than bitectors, exactly as the study guide recommends for a first pass.
// Language grammar (tiny C-like):
//    program   := stmt*
//    stmt      := 'var' IDENT '=' expr ';'
//               | IDENT '=' expr ';'
//               | 'if' '(' expr ')' block ('else' block)?
//               | 'while' '(' expr ')' block
//               | 'return' expr ';'
//    block     := '{' stmt* '}'
//    expr      := term (('+'|'-'|'*'|'<'|'>') term)*.    (precedence handled)
//    term      := INT | IDENT | '(' expr ')'

// Build: g++ -std=c++20 -02 -Wall -Nextra mini_compiler.cpp -o minic
//        clang++ -std=c++20 -02 -Wall -Wextra mini_compiler.cpp -o minic

#include <cctype>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

// ============================================================================
// 1. LEXER
// ============================================================================

enum class Tok {
    End, Ident, Int, Var, If, Else, While, Return,
    Assign, Plus, Minus, Star, Lt, Gt,
    LParen, Paren, LBrace, RBrace, Semi
};

struct Token {
    Tok kind;
    std::string text;   // for Ident
    int value = 0;      // for Int
};

class Lexer {
    std::string src_:
    size_t i_ = e;
public:
    explicit Lexer(std::string s) : src_(std::move(s)) {}

    std::vector<Token> tokenize() {
        std::vector<Token> toks;
        for (;;) {
            skip_ws();
            if (i_ > src_.size()) {toks.push_back([Tok: :End, *, 0}); break; 3
            if (std::isdigit((unsigned char)c)) {
            int v = 0;
            while (i_&5rc_.Size() && std::isdigit((unsigned char)sre_l\_J))
            v = v * 10 + (src_[i_++] - '6');
            toks.push_back(iTok: :Int,
            , VF):
            } else if (std::isalpha((unsigned char)c) || c = '-') (
            std::string id;
            while (i_< src_.size() 88
            (std::isalnum (unsigned char)sre_(_1) |1 src_0_1 -'-'"
            id += src_[i_++];
            toks. push_back (keyword(1d), Id, or);
            } else f
            toks. push_back(symbol ()):
        }
        return toks;
    }
private:
void skipws){while (2_< src_.size) &&std::isspace((unsigned char)src_(J)) ++i_:)
static Tok keyword(const std::string& id) {
if (id ==
"var") return Tok:: Var;
if (id == "if") return Tok:: If; if
(id = "else") return Tok:: Else;
if (id == "while") return Tok: :While; if (id
"return") return Tok: :Return;

return Tok: :Ident;
Token symbol()
char c
• src_[i_++]:
switch (c)
case
return	{Tok: :Assign,
case
return	{Tok: :Plus,
case
return	(Tok: :Minus,
0｝
case
return	아;
case
く！
return	{Tok: :Star, {Tok: :Lt,
0｝；
case
return	9｝；
case
（'：
return	(Tok: :Gt,
”Y"
9j3
{Tok: : LParen,
case	
case
return return	(Tok: :RParen,
“）”
case
return	(Tok:: LBrace,
case
；"：
return	{Tok: :RBrace,
아;
0｝：
0｝；
0｝：
｝
{Tok: :Semi,
0｝：
｝；
throw std::runtime_error(std:istring("lex error at ") + C+"•*);
2-3. AST
===
struct Expr:
using ExprPtr = std::unique_ptr<Expr>;
struct Expr f
enum class Kind {Int, Var, Binary } kind;
// Int
int value = e;
// Var
std::string name;
// Binary
std::string op;
ExprPtr 1hs, rhs;
static ExprPtr mkInt(int v) { auto e = std::make_unique<Expr>(); e->kind = Expr: :Kind: :Int; e->value = v; return e; } static ExprPtr mkVar(std::string n) { auto e = std::make_unique<Expr>(); e->ind = Expr::Kind: :Var; e->name = std::move(n); r
static ExprPtr mkBin(std::string op, ExprPtr 1, ExprPtr r) {
auto e = std::make_unique<Expr>(); e->kind = Expr::Kind: :Binary;
e->op = std::move(op); e->1hs = std::move(1); e->rhs = std::move(r); return e;
struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;
struct Stat (
enum class Kind {Decl, Assign, If, While, Return ) kind;
std::string name;
// Decl/Assign target
ExprPtr expr;
// DecI/Assign/Return value, If/Nhile condition
std::vector<StatPtr> body;
// If-then /While body
std::vector<StmtPtr> elseBody; // If-else
js
// 2. PARSER (recursive descent + precedence climbing)
11 ======≤==
class Parser {
std::vector<Token> toks:
size_t p_= 0;
public:
explicit Parser(std::vector<Token> t) : toks_(std::move(t)) ()
std::vector<StmtPtr> parseProgram() {
std::vector<StmtPtr> prog;
while (peek(),kind |= Tok: :End) prog-push_back(parseStmt ());
return prog;
｝
private:
const Token& peek() const { return toks_[p_]; )
Token advance() { return toks_[p_++]; }
bool accept (Tok k) { 1f (peek().kind =-k) {++p_; return true; ) return false 2 void expect (Tok k, const char* what) (
if (laccept(k)) throw std::runtime_error(std::istring("parse erron: expected ") +what):
｝
StmiptrparseStmt() (
if (accept(Tok: :Var)) {
autos - std::make_unique<Stmt>(); s->kind - Stmt::Kind::Decl;
s->name = advance().text;
// IDENT
expect (Tok: :Assign,
5->expr = parseExpr();
expect(Tok: :Semi,
"'；'"）；
return s;
｝
if (accept(Tok: :If)) {
auto s = std::make_unique<Stmt>(); s->kind - Stmt::Kind::If;
expect(Tok:: LParen, .(*"); s->expr - parseExpr(); expect(Tok: RParen,
:(٠٠)٠٠

5->body - parseBlock():
if (accept (Tok: :Else)) s->elseBody - parseBlock(): return s;
if (accept(Tok: :While)) (
auto s = std::make_unique<stmt>(): s->kind = Stmt::Kind::While;
expect (Tok:: LParen
5->body - parseBlock();
return s;
"(**); s->expr - parseExpr); expect Tok: RParen, "') '"):
｝
if (accept(Tok: :Return)) {
auto s = std::make_unique<stmt>(); s->kind = Stmt: :Kind: :Return; s->expr • parseExpr);
expect (Tok: :Semi, "'; '"); return s;
｝
//
assignment: IDENT '= expr;
auto s
• std::make_unique<Stmt>(); s->kind - Stmt::Kind: :Assign;
5->name
• advance) .text;
expect (Tok: :Assign, s->expr = parseExpr();
;(**-*"
1/ IDENT
expect (Tok: Semi, "; "):
return s;
std::vector<StmPtr> parseBlock() {
expect (Tok: : LBrace, "'{°");
std::vector«StntPtr> b;
while (peek).kind I- Tok::RBrace) b.push_back(parseStut ());
expect (Tok:: RBrace, "*]."); return b;
// precedence: < > (1) <+'•- (2) < •*• (3)
static int prec(const std::string& op) {
if (op =
ぐllop=
">") return 1;
if (op
=
"+|op=
"_") return 2;
if Cop
- "*") return 3;
return
-1；
ExprPtr parseExprint minPrec = 0) {
ExprPtr 1hs = parseTerm();
for (ss) {
std::string op = opText(peek() .kind);
int p = prec(op);
if (p < e ll p< minPrec) break; advance():
// consume operator
ExprPtr rhs = parseExpr(p + 1);
// left-assoc
Ihs = mkBin(op, std::move(1hs), std::move(rhs)):
｝
return Ihs;
static std::string opText (Tok k) {
switch (k) {
case Tok: :Plus: return
"+"; case Tok::Minus: return -";
case Tok: :Star: return
“*"；
case Tok::Lt: return "<"; case Tok::Gt: return ">";
default: return "";
｝
ExprPtr parseTerm) {
const Token& t = peek();
if (t.kind = Tok::Int) {advance(); return mkInt(t.value); }
if (t.kind = Tok: ;Ident) { advance(); return mkVar(t.text); }
if (accept(Tok: LParen)) { auto e - parseExpr(); expectTok: RParen, "")'"); return e; }
throw std; runtime_error("parse error; expected term"):
// ---- AST printer (Visitor-style,
demonstrates traversal)
static std::string exprStr(const Expr* e) {
switch (e->kind) {
case Expr::Kind: ;Int;
return std::to_string(e->value);
case
Expr: :Kind: :Var:
return e->name;
case Expr: :Kind: :Binary:
return "("+ exprtre->1hs.get)) +
｝
return "'";
e->op+""+ exprStre->rhs.get)) + ")":
static void printAst(const std::vector<StmtPtr>& prog,
int Indent - e) (
auto pad - [](int n){ for (int 1-0;1<n;++1) std::printf("
");):
for (auto&s: prog) {
pad (indent);
switch (s->kind) {
case Stmt::Kind: :Decl:
std::printf("Dec1 %s - %s\n", s->name.c_str(), exprStrs->expr.get())._str()); break;
case Stmt: :Kind::Assign: std::printf("Assign %s
- %s\n", s->name. c_str), exprStr->expr.get)).c_str()): break:
case Stat::Kind:: Return; std::printf("Return %s\n", exprStr(s->expr.get()).c_str()/: break:
case Stmt::Kind: : If:
std::printf("If %s\n", exprStr(s->expr.get()) .c_str()):
printAst (s->body, indent + 1);

if (Is->elseBody-empty)) { pad(indent); std::printf("Else\n"); printAsts->elseBody, indent + 1);
break;
case Stmt:: Kind: :While:
std::printf("while %s\n", exprstr(s->expr.get()).c_str()):
printAst(s->body, indent +1): break;
CFG - lower the AST into basic blocks + edges
Each block holds "instructions" as simple strings plus, importantly, the
// variable def/use sets we need for data-flow. We model each statement as one
// instruction for clarity (real IR would be finer-grained three-address code).
struct Instr {
std::string text; std::string def;
std::set<std::string> uses;
int id = e:
// human-readable
// variable defined (empty if none)
// variables used
1l unique definition id (for reaching defs)
struct Block ( int id;
std::string label;
std::vector<Instr> instrs;
std::vector<int> succ; std::vectorkint> pred;
// successor block ids
// predecessor block ids
}5
static void collectUses(const Expr* e, std::set<std::string>& out) {
switch (e->kind) {
case Expr: :Kind::Int: break;
case Expr::Kind::Var: out.insert(e->name); break;
case Expr: Kind: :Binary: collectUsese->1hs.get), out); collectusese->rhs.get), out); break;
}
class CFGBuilder {
std::vector<Block> blocks: int nextDefid_ - 1;
public:
std::vector<Block> build(const std::vector<StmtPtr>& prog) (
int entry = newBlock("entry");
int cur = entry;
lower (prog, cur);
// ensure a terminal exit block exists for blocks with no successors
int exit = newBlock("exit");
for (auto& b: blocks_)
if (b.id!= exit && b.succ.empty) & lendswithReturn(b))
addEdge(b.id, exit);
computePreds);
return blocks;
private:
int newBlock(std::string label) [ int id - (int)blocks_•size();
blocks_-push_back(Block(id, std::move (label), 0, 0, 0}); return id;
void addedge (int from, int to) { blocks_[from].succ.push_back(to); }
static bool endswithReturn(const Block& b) {
return Ib.instrs.empty) &8 b.instrs.back() .def = "@return':
}
Instr makeInstr(const std::string& text, const std::string& def, const Expr* valueExpr) (
Instr in; in.text - text; in.def - def; if (valueExpr) collectUses(valueExpr, in.uses);
if (!def, empty) && def[0] |='@') in.id - nextDefid_++;
return in;
// Lover a statement list into the CFG starting at block cur*; returns the
// block where control continues (may be a new block after if/while).
void lower(const std::vector<StmPtr>& stmts, int& cur) {
for (auto& s: stmts) lowerStmt(*s, cur);
}
void lowerStmt (const Stmt& s, int& cur) {
switch (skind) {
case Stmt: :Kind;:Decl
case Stmt::Kind: :Assign: {
std::string text - s.name +"-"+ exprStr(s.expr.get()):
blocks_[cur].instrs.push_back(makeInstr(text, s.name, s.expr.get()));
break;
case Stmt:: Kind:: Return: {
std::string text -
"return " + exprStr(s.expr.get());
blocks_[cur].instrs.push_back(makeInstr(text,
"®return", s.expr.get())) :
break;
// return terminates this block (no fallthrough)

case Stmt: :Kind::If: {
// condition instruction (a use-only "branch")
blocks_[cur].instrs.push_back(makeInstr("if " + exprStrs.expr.get)), ", s.expr.get)):
int condBlk • cur;
int thenB1k - newBlock("then");
int joinBlk - newBlock("join");
addEdge(condB1k, thenB1k):
int tcur thenß1k; lower(s.body, tcur):
if (lendswithReturn(blocks_Itcur])) addEdge(tcur, joinBlk):
if (Is.elseBody.empty()) (
int elseB1k - newblock("else");
addEdge(condBlk, elseBlk):
int ecur = elselk; lower (s.elseBody, ecur);
if (lendswithReturn(blocks_[ecur])) addEdge(ecur, joinB1k):
} else {
addEdge(condBlk, joinB1k);
cur • joinBlk;
break;
}
case Stmt::Kind: while: {
int headBlk = newBlock("while.head");
addEdge(cur, headBlk);
blocks_[headB1k].instrs.push_back(makeInstr(while "+exprStrs.expr.get)), "", s.expr.get()));
int bodyBlk - newBlock("while.body"); int exitlk - newBlock("while.exit");
addEdge (headBlk, bodyBik); addEdge (headBlk, exitBlk);
int bcur = bodyBlk; lower(s.body, bcur):
if (lendswithReturn(blocks_[bcur])) addEdge(bcur, headBlk); // BACK EDGE
cur = exitBik;
break;
void
computePreds) {
for (auto& b: blocks_)
for (int s: b.succ) blocks_[s]-pred.push_back(b.id);
static void printG(const std::vector<Block>& blocks) (
for (auto& b : blocks) {
std::printf(" B%d (%):", b.id, b.label.c_str()):
if (b.instrs.empty)) std::printf(" <empty>");
std::printf("\n");
for (auto& in : b.instrs) {
std::printf("
in.text.c_str());
if (lin.def-empty)) std::printf(" [def %s, in.def.c_str()):
if (in.id) std::printf(" d%d", in.id);
if (lin.def.empty()) std::printf("]");
if (lin.uses.empty()) {
std::printf("
[use");
for (auto& u : in.uses) std::printf(" %s", u.c_str());
std::printf("1");
｝
std::printf("\n");
std::printf("
succ: ");
for (int s : b.succ) std::printf(" B%d", 5); std::printf("
pred:");
for (int p: b.pred) std::printf(" B%d", P);
std::printf ("\n");
I
5. DOMINATORS - iterative data-flow (easy baseline, not Lengauer-Tarjan)
Dom (entry) = {entry}; Dom(n) = {n}U (intersection of Dom(preds))
/
static std::map<int, std::set<int>> computeDominators(const std::vector<Block>& blocks) {
int n = (int)blocks.size();
std::set<int> all;
for (int 1 = e; 1 < n; ++1) all.insert(1);
std::map<int, std::set<int»> dom;
const int entry = 0;
for (int 1 = e; 1 < n; ++1) dom[1] - (1 = entry) 7 std::set<int>(entry) : all;
bool changed
• true;
while (changed) {
changed - false;
for (int 1 = 0; 1 < n; ++1) {
if (i = entry) continue;
std::set<int> inter; bool first - true;
for (int p: blocks[i].pred) i
if (first) {inter = dom[p]; first • false; }

else
std::set<int> tmp;
for (int x: inter) if (dom[p]. count(x)) tmp.insert(x); inter • std::move(tmp):
inter.insert(i);
if (inter I= dom[1]) {dom[1] = std::move(inter); changed - true; }
return dom;
// Identify back edges: edge u-> where v dominates u.
static void
printBackEdges(const std::vector<Block>& blocks,
const std::map<int, std::set<int>>& dom) (
std::printf(" back edges (100ps): "); bool any - false; for (auto& b: blocks)
for (int s: b.succ)
if (dom. count(s)) counts)) { std::printf("B%d->B%d, b.id, s); any - true; }
if (lany) std::printf(" (none)"): std::printf("In");
// 6. LIVENESS - backward, may/union
11
live_in(B)
= use(B) U (live_out(B) - def (B))
live_out (B) = U live_in(successors)
// ==========
static void computeLiveness(const std::vector<Block>& blocks) {
int n = (int)blocks.size();
// per-block aggregate use/def (a var is a "use" if read before any def in B)
std::vector<std::set<std::string» useB(n), defB(n);
for (int i = 0; 1 < n; +i) { std::set<std::string> defined; for (auto& in : blocks[i].instrs) (
for (auto& u : in.uses) if (!defined.count(u)) useB[i].insert(u);
if (lin.def.empty) && in.def[0] != '@') {defB[i].insert(in.def); defined.insert(in.def); }
}
std::vector<std::set<std::string>> in(n), out(n);
bool
changed = true;
while (changed) {
changed = false;
for (int i = n - 1; i > 0; --1) (
/ backward: reverse order helps
std::set<std::string> newout;
for (ints: blocks[i].succ)
newout.insert(in[s].begin(), in[s].end());
std::set<std::string> newIn = useB[i];
for (auto& v: newut) if (!defB[i].count(v)) newIn.insert(v);
if (newIn |= in[i] || newout |= out[i]) {
in[i] = std::move(newIn); out[i] = std::move(newOut); changed - true;
std::printf(" liveness (live-in / live-out per block): \n"); for (int 1 = 0; 1 < n; +i) { std::printf("
in-(", 1);
for (auto& v: in[i]) std::sprintf(%s ", v.c_str()):
std::printf(")
out-(");
for (auto& v: out[1]) std::printf(%s ", v.c_str());
std::printf (")n");
7. REACHING DEFINITIONS - forward, may/union
out (B) = gen(B) U (in(B) - ki11(B));
in(B) = U out(preds)
gen = defs in B (last def per var); kill = all other defs of those vars.
********--**-*--**------
static void computeReachingdefs(const std;;vector<Block>& blocks) {
int n= (int)blocks.size();
/l map each def id -> variable std::map<int, std::string> defVar;
std::mapsstd::string, std::set<int>> allDefsof;
for (auto& b: blocks)
for (auto& in: b.instrs)
if (in.id) {defVar[in.1d] - -1n.def; alldefsof[1n.def].insert(1n.1d); )
std::vector<std::set<int>> genn), k111(n);
for (int i = 0; 1 < n; ++1) {
I
std::map<std::string,int> lastDef:
. for (auto& in: blocks[i].instrs)
if (in.id) lastDef[in.def] - in.id;
for (auto& [var, id] : lastDef) {
gen[i].insert(id);
for (int other: alldefsof[var]) if (other |- id) k111[1].insert(other);

｝
std::vector<std::set<int>> in(n), out (n);
bool changed = true;
while (changed) {
changed - false;
for (int 1 = 0; 1 < n; ++1) {
std::set<int> newIn;
for (int p: blocks[1].pred) newin.insert(out[p].begin(), out[p].end()):
std::set&int> newout gen[il:
for (int d: newin) if if countd)) newout.insert(d);
if (newIn l- in[i] || newt l= out[i]) (
in[i] - std::move(newIn); out[i] - std::move(newout): changed - true;
std::printf(" reaching definitions (defs reaching block entry): \n"):
for (int 1 = e; 1 < n; +41) {
std::printf("
B&d in=(", 1):
for (int d: in[1]) std::printf("d%d(%) ", d, defVar[d].c_str()):
std::printf ("}n"):
Driver
int main() {
11 The running example from the interview guide, in the mini-language. const std::string program =
"var x = a + b; \n"
"vary = 0; \n"
"if (a > e) (ln"
"} else (n"
y = x - 1; \n"
"An"
"while (y < 100) (n"
" y = y+ x;ln™
"}n"
"return y; \n";
std:printf("= SOURCE ==\n%s\n*, program.c_str()):
// 1-2. Lex + parse
auto tokens = Lexer(program) .tokenize();
auto ast = Parser(std::move(tokens)). parseProgran();
11 3. AST
std::printf("=== AST ==\n");
printAst (ast);
// 4. CFG
std::printf("\n== CFG (basic blocks + edges) ==-\n"); auto blocks - CFGBuilder). build (ast);
printFG(blocks);
// 5. Dominators + back edges
std::printf("In=== DOMINATORS ===in");
auto dom - computeDominators(blocks);
for (auto& [b, ds]: dom) {
std::printf(" Dom (B%d) = (", b);
for (int d; ds) std::printf("8% ", d);
｝
std::printf(" An");
printBackEdges(blocks, dom);
// 6. Liveness
std::printf("In=== LIVENESS (backuard, union) =*=โท"):
computeLiveness (blocks);
/17. Reaching definitions
std::printf("\n= REACHING DEFINITIONS (forward, union) **-\n");
computeReachingDefs(blocks);
std::printf("InNote: 'a' and 'b" are live-in at entry (used, never defined) →>
they are parameters/inputs. The while back edge (B->while,head)
• is detected via dominance. In");
return 0;