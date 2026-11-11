<!-- autervew/template_name_lookup_ast_ad1.md -->

# C++ Template Name Lookup: Two-Phase Lookup, Dependent Bases, the AST View, and ADL

How C++ resolves names in templates: the two-phase model, the two classic failures
(non-dependent name declared too late; member inherited from a dependent base), how it
maps onto parsing/AST construction, and how Argument-Dependent Lookup (ADL) fits in.

## 1. The big picture: why templates need special lookup

A template is compiled in **two conceptual passes** (mandated by the standard; clang and
GCC enforce it strictly, MSVC historically did not):

- **phase 1- at the point of definition.** The compiler parses the template body *once*,
  before it knows `T`. It must decide the syntactic structure (bulld the AST) and resolve
  every name it can* resolve without knowing `T'.
- **phase 2 - at each point of instantiation,** For a concrete `T`, the compiler revisits
  the parts it had to defer and resolves the **dependent** names.

Everything hinges on classifying each name as **dependent** (depends on a template
parameter) or **non-dependent**.

## 2. Dependent vs. non-dependent names

- **Non-dependent name:** does not depend on any template parameter. Resolved in **phase
  1**, by **ordinary lookup at the definition point**, Its meaning is fixed forever at
  definition time.
  - `global_func(42)` - argument `42` is non-dependent -> the name is non-dependent.
- **Dependent name:** depends on a template parameter (argument of type qualified by
  a dependent type, accessed via `this` in a class template). Resolution is **deferred to
  phase 2**.
  - `global_func(x)` where `x` has type `T` - looked up at instantiation, with ADL over
  	`T`'s associated namespaces.

Two consequences, each producing a classic interview error:

- (a) A non-dependent name **cannot see declarations that appear later** in the file.
- (b) Unqualified lookup for a non-dependent name **never searches dependent base
  classes**.

## 3. Failure (a): non-dependent name declared after the template

```cpp
template typename<T> void bad_call(T x) { global_func(42); }
void global_func(int) {}
```

`clang++ -std-c++17 -fsyntax-only` :

```
error: use of undeclared identifier 'global_func'
```

`global_func(42)` mentions nothing dependent on `T`, so it is bound at the definition
point (Line 1), where `global_func` isn't declared yet. It never gets a phase-2 retry. The
later definition in the same TU does **not** help - lookup is not at end-of-file and not
at instantiation.

**Fixes:**

```cpp
// Fix 1 (idiomatic): declare before the template
void global_func(int);
template <typename T> void bad_call(T x) { global_func(42); }
void global_func(int) {}

// Fix 2: move the definition above the template

// Fix 3 (instructive, usually wrong): make the call dependent
template <typename T> void bad_call(T) {global_func(x); }
```

Fix 3 defers lookup to phase 2, but phase 2 only adds **ADL** results (see §6). For
`T = int`, a fundamental type has no associated namespace, so ADL finds nothing - fragile.
Fix 1 is the answer,

## 4. Failure (b); the function lives in a template base class

```cpp
template stypename T> struct Base { void base_func() {} };

template <typename T>
struct Derived Base<T> {
	void call() { base_func(); }	// error
};
```

```
error: use of undeclared identifier `base_func`
note: must qualify identifler to find this declaration in dependent base class
```

**Why:** `base_func()` is unquallfled and non-dependent, so it's looked up in phase 1 -
and

> **the compiler does NOT look into dependent base classes during unqualified lookup.**

`Base<T>`'s contents are unknown until instantlation (an explicit specialization
`Base<int>` might not have `base_func` at all, so it's not searched. This is the #1
CRTP/mixin compile error and the most common surprise when porting MSVC code.

**Three fixes - make the name dependent so lookup is deferred:**

```cpp
void call() { this->base_func(); }        // 1. recommended
void call() { BasesT>::base fune(); }     // 2. explicitqualification
using Base<T>::base_func;                 // 3. using-declaration in Derived, then base_func();
```

- `this` has type `Derived<T>*` - type-dependent - so `this->base_func` is a dependent
  member access resolved at instantiation, when `Base<T>` is complete.
- **Caveat on Fix 2:** qualified calls suppress virtual dispatch. Prefer `this->` uniess
  you specifically want that or need to disambiguate a hidden name.
- C++23 **deducing `this`** (`void call(this auto& self) { self.base_func(); }`) also
  makes the access dependent and is an alternative to classic CRTP.

**Contrast:**

|                    | `global_func(42)`                    | `base_func()` in `Derived<T>`                  |
| ------------------ | ------------------------------------ | ---------------------------------------------- |
| Name kind	         | Non-dependent (arg is `42`)          | Non-dependent (no `T` in the call)             |
| Should be found in | Global/enclosing scope               | Inherited from `Base<T>`                       |
| Why phase 1 falls	 | Not declared yet at definition point | Dependent bases never searched                 |
| Fix                | Declare before the template          | Make dependent: `this->`, `Base<t>::`, `using` |

Same rule both times: non-dependent unqualified names are bound at definition time and can
see neither later declarations nor dependent-base members.

## 5. The parsing / AST view - three distinct disambiguators

The parser must build an AST in phase 1, but three constructs are **syntactically
ambiguous** when a name is dependent. C++ resolves them with default assumptions plus
keywords:

### (a) TYPE or VALUE? -> `typename`

```cpp
template <class T> void f() {
	T::X *p;               // parsed as MULTIPLICATION by default!
	typename T::X * p2;    // pointer declaration
}
```

Default: **a dependent qualified name is a value unless prefixed with `typename`.** (C++20
relaxed this in some contexts where only a type can appear, e.g. return types and alias
targets.)

### (b) TEMPLATE? -> `template`

```cpp
template <class T> void f(T obj) {
    obj.get<int>();            // parsed as (obj.get) < int > ()
    obj.template get<int>();   // `<` starts a template-argument list
}
```

Default: **a dependent member name is not a template**, so `<` is less-than.

### (c) FINDABLE AT ALL? -> `this->` / qualification

Names from a dependent base aren't found by phase-1 lookup (§4).

**Misconception: `template` is a fourth fix for the dependent-base problem."** No.
`template` fixes *parsing* of an already-located dependent name; it doesn't change *where*
lookup searches. `template base_func();` is ill-formed (it needs a preceding `::`, `.`, or
`->`). The two combine only when the inherited member is itself a **member template**:

```cpp
template <typename T> struct Base { template <int N> void base_tmpl() {} };
template <typename T> struct Derived : Base<T> {
	void call() {
		this->template base_tmpl<3>();    // this-> = find it; template = parse <3>
		// or: Base<T>::template base_tmpl<3>();
	}
}
```

| Problem | Symptom | Tool |
| ---------------------------------------- | ------------------------------------------------- | ------------------------------ |
| Name not found (dependent base)          | "use of undeclared identifier"                    | `this->`, `Base<T>::`, `using` |
| Dependent member template `<` mis-parsed | parse error around `<`                            | `.template` / `::template`     |
| Dependent member **type** mis-parsed     | "missing 'typename' prior to dependent type name" | `typename`                     |

**AST takeaway:** in phase 1 Clang builds a partially-typed AST with "resolve me later"
nodes - `CXXDependentScopeMemberExpr`, `DependentScopeDecRefExpr`,
`UnresolvedLookupExpr`, `DependentNameType`. The disambiguating keywords tell the parser
*which kind* of dependent node to create; template argument substitution replaces them
with concrete nodes at instantiation.

## 6. Lookup mechanisms, precisely

- **Ordinary (unqualified) lookup** - local -> class (and **non-dependent** bases) ->
  namespace -> global, stopping at the first scope containing the name. For non-dependent
  names in templates this happens at the definition point.
- **Qualified lookup** - `A::b` / `obj.b` / `ptr->b` looks only in the named scope (and
  its bases). Deferred to instantiation if the qualifier is dependent.
- **ADL** - an *augmentation* of unqualified lookup for **function calls** that also
  searches the namespaces/classes associated with the argument types.

## 7. ADL (Argument-Dependent / Koenig lookup)

For an unqualified call `f(args...)`, lookup also considers the **associated namespaces
and classes of the argument types** - why `std::cout << x;` and unqualifled `swap(a, b);`
work.

```cpp
namespace n { struct S {}; void g(S) {} } 
n::S s;
g(s);          // finds n::g via ADL
```

**associated entities** (roughly): the namespace a class is declared in, its base classes,
enclosing classes, and the associated entities of class-template type arguments.
Fundamental types have none.

**ADL in templates.** For an unqualified **dependent** call, the candidate set is the
**union** of:

1. ordinary-lookup results visible **at the template definition point**, and
2. ADLresults at the **instantiation point**.

Ordinary lookup is **not** redone at instantiation - only ADL. This is exactly why Fix 3
in §3 fails for `int` but would work for a class type whose namespace declares
`global_func`.

**pitfalls and idioms:**

- `swap` two-step: `using std::swap; swap(a, b);` so both `std::swap` and an ADL-found
  overload are candidates. std: :swapa, b)" disables customization.
- ADL can pull in **unexpected overloads** from argument namespaces. **Hidden friends**
  (friend functions defined inside the class) are only findable via ADL - the clean way to
  expose operators without polluting overload sets.
- C++20 **ranges CPOs /niebloids** (`std::ranges::begin`, `std::ranges::swap`) are
  function *objects*, so ADL doesn't apply to calling them; internally they perform a
  controlled ADL lookup (with "poison pill" deleted overloads to block `std::`
  candidates). This is how modern libraries implement customization points safely.
- ADL applies to functions/operators only, not to variables or types.

## 8. Worked mapping

| Expression                         | Dependent? | Phase            | How resolved                        | Declared later / in dependent base      |
| ---------------------------------- | ---------- | ---------------- | ----------------------------------- | --------------------------------------- |
| `global_func(42)`                  | No         | 1                | ordinary lookup only                | **fails**                               |
| `global_func(x)`, `X:T`            | Yes        | 2                | ordinary(def-point) + ADL(on T)     | found only if visible at def or via ADL |
| `base_func()` in `Derived:Base<T>` | No         | 1                | ordinary, **skips dependent base**  | **fails**                               |
| `this->base_func()`                | Yes        | 2                | member lookup in `Base<T>`          | **found**                               |
| `obj.template get<int>()`          | Yes        | 2 (+ parse hint) | member lookup; `template` fixes     | found                                   |
| `typename T::X`                    | Yes        | 2 (+ parse hint) | qualified lookup; it's a type       | found                                   |

## 9. Practical / tooling notes

- **See the AST;** `clang++ -Xclang -ast-dump -fsyntax-only file.cpp` shows the dependent
  nodes before instantiation; dumping an instantiated specialization shows the resolved
  tree
- **Recognize diagnostics instantly;** "must qualify identifler to find this declaration
  in dependent base class" = §4; "missing 'typename" prior to dependent type name" =
  §5(a).
- **Portability:** MSVC without `/permissive-` searched dependent bases and deferred more
  to instantiation, so MSVC-only code often breaks on Clang/GCC for exactly these reasons.
  `/permissive-` (default in new VS projects and with `/std:c++20`) brings it into
  conformance,

## Likely interview questions

- *"Why does this compile on MSVC but not GCC?"* - Two-phase lookup: NSVC's permissive
  mode searched dependent bases.
- *"Why `this->` in CRTP?"* - Makes the access dependent so lookup is deferred into
  `Base<T>`.
- *"Difference between `this->f()` and `Bases<T>::f()`?"* - Qualified call suppresses
  virtual dispatch.
- *"Is `template` an alternative to `this->`?"* - No: parsing vs. lookup; both needed only
  for an Inherited member template.
- *"why `using std::swap; swap(a,b);`?"* - Union of ordinary + ADL candidates; enables
  customization.
- *"Why are `std::ranges` CPOs objects, not functions?"* - To control/disable ADL on the
  call itself and run a well-defined customization lookup.

## Synthesis

Template name resolution is governed by one classification - **dependent vs.
non-dependent*- applied in two phases. Non-dependent names are bound at the **definition
point** by ordinary lookup, which cannot see later declarations (`global_func(42)` fails)
and never searches dependent bases (`base_func()` in `Derived<T>` fails). The fix is
either to make the name visible at definition time or to make it dependent (`this->`,
`Base<T>::`, `using`) so it resolves at **instantiation**. Because the parser must build
an AST before knowing `T`, `typename` (it's a type) and `template` (it's a template)
resolve the parse ambiguities - orthogonal to the lookup fix. **ADL** augments unqualified
function lookup with the argument types' associated namespaces; in templates it runs at
instantiation and is unioned with definition-point results, which is why dependent calls
can find customizations (the `swap` two-step, hidden friends, ranges CPOs) while
non-dependent calls cannot.
