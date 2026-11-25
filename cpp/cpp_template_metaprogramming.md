<!-- cpp/cpp_template_metaprogramming.md -->

## 1. Template metaprogramming basics

Template metaprogramming (TMP) performs computation or type selection at compile time
using templates.

Common tools:

- Function and class templates
- Specialization
- conster and consteval
- Type traits
- SFINAE
- Concepts
- Parameter packs and fold expressions
- Prefer modern C++ features such as if constexpr and concepts when possible.

**1.1 What "compile time" buys you**

Three things, in rough order of practical value:

- **Type safety** - errors become compile errors instead of runtime failures. A unit
  system that rejects `metres + seconds` at compile time is the canonical example.
- **Zero-overhead abstraction** - the abstraction is resolved during compilation, so the
  generated code is as if it had been written by hand. `std::sort` beats `qsort` precisely
  because the comparator is a template parameter, not a function pointer.
- **Code generation** - write one template instead of near-identical overloads.

The costs are real and worth stating up front: compile times grow, error messages get
worse the further you go from concepts, and debuggers show instantiated template names
rather than your intent. TMP is a tool for library and infrastructure code, not something
to spread across application logic.

**1.2 The two-phase name lookup rule**

Templates are compiled in two phases: names that do not depend on template parameters are
looked up when the template is *defined*; dependent names are looked up when it is
*instantiated*. This is the source of two errors that surprise everyone once:
  
```cpp
template <typename T>
struct Base { using Value = int; void helper(); };

template <typename T>
struct Derived: Base<T> {
void f() {
	// ERROR: Value is a dependent name; the compiler assumes it is not a type
	// Value v{};
	typename Base<T>::Value v{};   // OK: 'typename' disambiguates
	
	// ERROR: helper) is not found by unqualified lookup in a dependent base
	// helper();
	this->helper();                // OK: makes the lookup dependent
	}
};
```

Rules of thumb: prefix dependent types with `typename`, dependent member templates with
`template`, and dependent inherited members with `this->`.

**1.3 constexpr, consteval, constinit**

Modern TMP does arithmetic with ordinary functions rather than recursive template
instantiation. Prefer the first form below; the second exists mainly in older code.

```cpp
// Modern: a normal function, usable at compile time and at runtime
constexpr std::size_t factorial(std::size_t n) {
	return n <= 1 ? 1: n * factorial(n - 1);
}
static_assert(factorial(5) = 120);

// Legacy: recursive class template instantiation
template <std::size_t N> struct Factorial { static constexpr std::size_t value = N * Factorial<N - 1>::value; };
template <> 			 struct Factorial<0> { static constexpr std::size_t value = 1; };
```

The keywords differ in *when* evaluation is allowed:

| Keyword                | Meaning                                                                                 |
| ---------------------- | --------------------------------------------------------------------------------------- |
| `constexpr (function)` | *May* run at compile time; falls back to runtime                                        |
| `consteval`            | *Must* run at compile time; runtime call is a compile error                             |
| `constexpr` (variable) | Value is fixed at compile time, implicitly `const`                                      |
| `constinit`            | Initialised at compile time, but mutable afterwards - avoids static-init-order problems |

```cpp
consteval int must_be_compile_time(int x) { return x * 2; }
constexpr int a = must_be_compile_time (21);  // OK
// int b = must_be_compile_time(runtime_value); // ERROR

constinit int counter = factorial(5);           // no dynamic initialisation, still writable
```


Since C++20 a `constexpr` function may allocate, use `try`/`catch`, and call virtual
functions, provided any allocation is freed before the constant evaluation ends. That
makes compile-time `std::vector` and `std::string` usable inside a single evaluation,
though such objects cannot escape into a `constexpr` variable.

**1.4 Instantiation, specialization, and ODR**

A template is not code; each set of arguments produces a distinct entity.

```cpp
template <typename T> void f(T);   // template, no code emitted

f(1);        // implicit instantiation of frint>
f(1.0);      // implicit instantiation of f<double>

template void f<char> (char);       // explicit instantiation definition - emit it here
extern template void f<long>(long); // explicit instantiation declaration - do not emit; link elsewhere
```

The `extern template` pair is the standard cure for compile-time blowup in large
codebases: instantiate a heavily used specialization once in a `.cpp` file and suppress it
everywhere else. The standard library does this for `std::basic_string<char>`.

Two related pitfalls:

- Definitions must be visible at the point of instantiation, which is why templates
  normally live in headers. Splitting a template into a `.cpp` file only works if you
  explicitly instantiate every specialization you need.
- A specialization must be declared before its first use in every translation unit that
  uses it. Declaring it in one TU and not another is an ODR violation the compiler is not
  required to diagnose.

## 2. CRTP

CRTP (Curiously Recurring Template Pattern) is inheritance where the base class is
parameterized on the derived class:

```cpp
template <typename Derived> class Base {/* ... */};
class Derived : public Base<Derived> {};   // "curiously recurring"
```

It gives the base class compile-time knowledge of its own derived type. That enables
**static polymorphism** - the base can call into the derived class with no table, no
indirect call, and full intining.

**2.1 The basic pattern**

```cpp
template <typename Derived>
class Base {
public:
	void interface() {
		// Non-virtual dispatch to the derived class
		self().implementation();
	}

	void common() {
		// Shared implementation, written once*
	}
private:
	// Access helpers - write these once instead of casting everywhere
	Derived& 	   self()       { return static_cast<Derived&> (*this); }
	const Derived& self() const { return static_cast<const Derived&>(*this); }
};

class Concrete : public Base<Concrete> {
public:
	void implementation() {
		// Concrete-specific behaviour
	}
};
```

Usage:

```cpp
Concrete c;
c.interface();   // resolved at compile time, typically inlined to implementation()
```

How it works: inside `Base<Concrete>`, the template parameter *is* `Concrete`, so

```cpp
static_cast<Derived*>(this)
```

is a valid downcast (`this` really does point into a `Concrete` object) and the subsequent
call is an ordinary, statically bound member call. Compare with the virtual equivalent,
which needs a table load and an indirect call that the compiler usually cannot
devirtualize.

Provide both `const` and non-`const` `self()` overloads. Forgetting the `const` one is the
single most common source of confusing errors in CRTP code, because the failure surfaces
at the call site inside the base, far from the mistake.

**2.2 Why it is not a drop-in replacement for virtual functions**

The dispatch is resolved at compile time, so you cannot store heterogeneous types in one
container or pass them through a uniform runtime interface.

The fundamental Limitation: CRTP base classes are templates, so `Base<Derived1>` and
`Base<Derived2>` are unrelated types. There is no common base to point to.

```cpp
// This doesn't work with CRTP alone: std::vector<Base*> objects; // Base of what?
template <typename Derived>
class CrtpBase {
	void doThing() { static_cast<Derived*>(this)->impl(); }
};

class A : public CrtpBase<A> { void impl(); };
class B : public CrtpBase<B> { void impl(); };
// CrtpBase<A> and CrtpBase<B> are completely unrelated types
```

Consequences worth spelling out:

- No std::vector<Base*>, no `std::unique_ptr<Base>` holding an arbitrary derived object.
- No `dynamic_cast`, no RTTI-based checks across the hierarchy.
- No ABI boundary: the caller must see the concrete type, so implementations cannot be
  swapped without recompiling callers.
- A factory cannot return "some derived type" without additional type erasure.

CRTP is the right tool when:

- You know the concrete type at the call site
- You want zero-overhead dispatch (no vtable indirection)
- You're injecting shared behavior into unrelated types (e.g•, mixin patterns, operator
  generation)
- You're in a hot loop where virtual dispatch overhead matters
- The set of derived types is fixed and known at compile time

Virtual polymorphism is the right tool when:

- You need a collection of heterogeneous objects
- The concrete type is determined at runtime (factory patterns, plugin systems)
- You're writing interfaces consumed by code that shouldn't know about implementations
- You need type erasure
- You want an ABI-stable seam, or to mock the interface in tests
 

|                       | CRTP                               | Virtual                     |
| --------------------- | ---------------------------------- | --------------------------- |
| Dispatch resolved     | Compile time                       | Runtime                     |
| Per-call cost         | None (inlinable)                   | Vtable load + indirect call |
| Per-object cost       | None (empty base)                  | One vptr                    |
| Common base type      | No                                 | Yes                         |
| Heterogeneous storage | No                                 | Yes                         |
| Code size             | One instantiation per derived type | One shared copy             |
| Diagnostics on misuse | Poor unless constrained            | Clear                       |

**2.3 Use case: static interface (compile-time "pure virtual"**

CRP alone does not require the derived class to supply `implementation()` - the error
only appears when `interface()` is instantiated, and the message is usually about a
missing member deep inside the base. Constrain it explicitly:

```cpp
#include <concepts>
template < typename T>
concept HasImplementation = requires (T& t) {
{ t.implementation() } -> std::same_as<void>;
};

template <typename Derived>
Class Base {
public:
	void interface () {
		static_assert (HasImplementation<Derived>,
						"Derived must provide void implementation()");
		self().implementation();
	}
};
```

The `static_assert` must live inside a member function body, not ih the class body - see
the incomplete-type rule in 2.7.

**2.4 Use case: mixins and operator generation**

Deriving comparison and arithmetic operators from one or two primitives is the classic
mixin application. The base supplies the boilerplate; the derived type supplies only the
primitive.

```cpp
template <typename Derived>
struct EqualityComparable {
	friend bool operator!=(const Derived& a, const Derived& b) {
		return !(a == b);
	}
};

struct Point: EqualityComparable<Point> {
	int x;
	int y;

	friend bool operator=-(const Point& a, const Point& b) {
		return a.x == b.x && a.y == b.y;
	}
};
```

The same idea scales to a full ordering derived from operator<":

```cpp
template <typename Derived>
struct Ordered {
	friend bool operator> (const Derived& a, const Derived& b) { return b < a; }
	friend bool operator<=(const Derived& a, const Derived& b) { return !(b < a); }
	friend bool operator>=(const Derived& a, const Derived& b) { return !(a < b); }
};
```

Note that in C++20 this particular use case is largely obsolete: `operator<=>` and the
rewritten-candidate rules generate the whole comparison set from a single declaration,
often `= default`. Reach for the mixin only when the comparison cannot be expressed as a
defaulted three-way comparison.

Mixins compose, and because the bases are empty they normally cost nothing in object size
(empty base optimization):

```cpp
template <typename Derived>
struct Addable {
	friend Derived operator+(Derived a, const Derived& b) { a += b; return a; }
};

template <typename Derived>
struct Scalable {
	friend Derived operator* (Derived a, int k) { a *= k; return a; }
};

struct Vec2 : Addable<Vec2>, Scalable<Vec2> {
	int x{}, y{};
	Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
	Vec2& operator*=(int k)         { x *= k;   y *= k;   return *this; }
};
static_assertsizeof(Vec2) = 2 * sizeof(int));   // empty bases add nothing
```

EBO only applies while the empty bases are of *distinct* types. Two bases of the same
empty type in one class must have different addresses, so the second one does occupy a
byte.

**2.5 Use case: fluent interfaces that keep the derived type**

A base-class setter returning `Base&` truncates the type and breaks chaining in derived
classes. CRTP returns the derived type instead:

```cpp
template <typename Derived>
class BuilderBase {
public:
	Derived& withName(std::string n) { name_ = std::move(n); return self(): }
	Derived& withid(int id)          { id_ = id;             return self(); }

protected:
	std::string name_;
	int         id_{};
private:
	Derived& self() { return static_cast<Derived&>(*this); }
};

class SessionBuilder : public BuilderBase<SessionBuilder> {
public:
	SessionBuilder& withApn(std::string apn) { apn_ = std::move(apn); return *this; }
	Session build() const;
private:
	std::string apn_;
};

// Chaining across base and derived setters in any order:
auto s = Sessionbuilder().withName("ue-1").withApn("internet").withId(7).build();
```

Without CRTP, `withName(...)` would return `BuilderBase&` and the subsequent
`.withApn(...)` would not compile.

**2.6 Use case: per-type static state**

Because each `Base<Derived>` is a distinct class, its static members are distinct too -
one counter per derived type, for free:

```cpp
template <typename Derived>
class InstanceCounted {
public:
	static std::size_t liveCount() { return count; }

protected:
	InstanceCounted()                       { ++count_; }
	InstanceCounted(const InstanceCounted&) { ++count_; }
	~InstanceCounted()                      { --count_; }

private:
	static inline std::size_t count_ = 0;
};

class Widget : public InstanceCounted<widget> {};
class Gadget : public InstanceCounted<Gadget> {};
// Widget::liveCount) and Gadget::liveCount) are independent
```

The standard library uses the same shape in `std::enable_shared_from_this<T>`, where the
base needs the derived type to hand back a correctly typed `shared_ptr<T>`.

**2.7 Caveat: `Derived` is incomplete inside the base**

When the compiler instantiates `Base<Concrete>`, `Concrete` is still being defined.
Anything in the base's *class body* that requires a complete `Derived` is ill-formed:

```cpp
template
<typename Derived>
class Base {
	// ERROR: Derived is incomplete here
	static_assert(sizeof (Derived) > 0);
	using Value = typename Derived::ValueType;
	Derived members_;

	// OK: member function bodies and default arguments are instantiated later,
	// when Derived is complete
	void ok() { typename Derived::Valuetype v{}; (void) v; }
	auto size() const { return sizeof(Derived); }
};
```

Rules of thumb: put `static_assert`s, `sizeof`, and dependent typedef uses inside member
function bodies. If the base genuinely needs a type from the derived class in its own
signature, pass it as an extra template parameter or a traits class rather than digging
into `Derived`:

```cpp
template <typename Derived, typename ValueType>
class Container { Valuetype* data_; /* ... */ };

class IntVec : public Container<Intvec, int> {};
```

**2.8 Caveat: the derived type must pass itself**

```cpp
class Correct : public Base<correct> {};   // fine
class Right {};
class Wrong : public Base<Right> {};       // compiles, then UB at runtime
```

`Wrong` inherits a base whose `self()` casts `this` to `Right*`, which is not a `Right`.
This is undefined behaviour, not a diagnostic. Make it a compile error by giving the base
a private constructor and befriending only `Derived`:

```cpp
template <typename Derived>
class Base {
public:
	void interface() { self().implementation(); }

private:
	Base() = default; // only Derived can construct the base
	friend Derived;
	
	Derived&       self()       { return static_cast<Derived&>(*this); }
	const Derived& self() const { return static_cast<const Derived&>(*this); }
};

class Correct : public Base<Correct> {};   // Correct is a friend - oK
class Wrong   : public Base<Right>   {};   // ERROR: Base<Right>::Base() is private
```

A cheaper partial check is a `static_assert(std::derived_from<Derived, Base>)` inside a
member function, but that only fires if the member is instantiated.

**2.9 Caveat: code bloat and build cost**

Every derived type gets its own instantiation of every base member it uses. A virtual base
emits one copy of shared code; a CRTP base emits N. With a handful of derived types this
is irrelevant and the inlining usually wins. With dozens of derived types and large base
member functions it is a real cost in binary size, instruction cache pressure, and compile
time. Factor type-independent logic into a non-template base or a free function and keep
the CRTP layer thin.

Related: the performance argument is weakest in debug builds, where nothing is inlined and
CRP Loses its main advantage while keeping its costs.

**2.10 Combining CRTP with virtual polymorphism**

If you need both - static dispatch and code reuse among concrete types, but runtime
polymorphism for ownership and storage -put a non-template abstract base on top:

```cpp
class IShape {
public:
	virtual ~IShape() = default;
	virtual void draw() const = 0;
	virtual std::unique_ptr<IShape> clone() const = 0;
};

template <typename Derived>
class Shapertp : public IShape {
public:
	// Written once, correct for every shape: no per-class clone() boilerplate
	std::unique_ptr<IShape> clone() const override {
		return std::make_unique<derived>(self());
	}

protected:
	const Derived& self() const { return static_cast<const Derived&>(*this); }
};

class Circle : public ShapeCrtp<Circle> {
public:
	void draw() const override { /*... */}
};

class Square : public ShapeCrtp<square> {
public:
	void draw() const override { /*... */}
};

std::vector<std::unique_ptr<IShape>> shapes;   // heterogeneous storage works
```

This gives runtime polymorphism through `IShape*` while CRTP eliminates the repetitive
`clone()` implementations. The vtable cost is paid only at the `IShape` boundary. The same
trick removes boilerplate for `accept()` in visitors, `typeName()`, serialization
dispatch, and equality-by-downcast.

The other direction - runtime storage without a virtual base - is type erasure: wrap the
concrete object in a handle that holds a function table or a `std::function`, giving value
semantics and a common type without an inheritance relationship. `std::function` and
`std::any` are the standard examples.

**2.11 C++23: `deducing this` replaces much of CRTP**

An explicit object parameter lets a *non-template* base deduce the derived type per call,
so the base no longer has to be a template:

```cpp
struct Base {
	template <typename self>
	void interface(this Self&& self) {
		self.implementation();      // deduced as the most-derived type
	}
};

struct Concrete: Base {
	void implementation() { /* ... */ }
};

Concrete c;
c.interface();                      // Self = Concrete
```

Advantages over classic CRTP: no template parameter to get wrong (2.8 cannot happen), no
`self()` helpers, no `const`/non-`const` duplication, and `Base` is a single ordinary type
- so `Concrete1` and `Concrete2` do share a base class.

It does not, however, give runtime polymorphism: `interface()` is still statically bound,
so calling it through a `Base*` would invoke nothing derived. When objects must be handled
through a common runtime base, the answer is still a virtual function.

**2.12 Traditional use cases in the wild**

The sections above describe the mechanics; these are the places CRTP actually shows up in
production libraries.

*Interface facades - derive a wide API from a few primitives.* The most common industrial
use. The derived type implements the minimal core, and the CRTP base synthesizes the rest
of the interface:

- `boost::iterator_factory<MyIter, T, Tag>` - you write `depreference()`, `increment()`,
  `equal`; the base generates `operator*`, `operator++`, `operator--`, `operator+=`,
  `operator[]`, all the comparisons, and the iterator typedefs.
- `std::ranges::view_interface<Derived>` (C++20) - you write `begin()`/`end()`; you get
  `empty()`, `operator bool`, `front()`, `back()`, `size()`, `operator[]`, `data()`. CRTP
  in the current standard library, not a legacy trick.
- `boost::operators` / `boost::totally_ordered<T>` - the pre-C++20 ancestor of the mixins
  in 2.4.

```cpp
class CountIter: public std::ranges::view_interface<CountIter> {
public:
	int* begin();
	int* end();
	// size(), empty(), front(), operator[]] come from the base
```

*"The base needs my exact type."* Any time the base must return, construct, or compare
using the derived type. `std::enable_shared_from_this<T>::shared_from_this()` returns
`shared_ptr<T>`; a non-template base could only return `shared_ptr<Base>`. Same shape as
the `clone()` example in 2.10.

*Expression tempLates.* Eigen, Blaze, uBLAS. Every expression node derives from
`MatrixBase<Derived>`, so `a + b * c` builds a compile-time expression tree that is
evaluated once into the destination with no temporaries. CRTP is what lets the base define
`operator+`, `operator*`, and `eval()` in terms of the concrete node type while staying
zero-overhead. Arguably the use case with no alternative.

*static visitor dispatch in compilers and parsers.* LLVM and clang use
`Instvisitor<Derived>` and `RecursiveASTVisitor<Derived>`: the base performs the
traversal, the derived class overrides the "Visitxxx" methods, and dispatch is resolved at
compile time so unimplemented cases fold away to nothing. See also
`llvm::Trailingobjects<Derived, ...>`.

*per-type registration.* The counter in 2.6, generalized: a `Registered<Derived>` base
holds a static member whose initialization inserts `Derived` into a factory registry keyed
by name. Each `Base<Derived>` being a distinct class is the entire mechanism.

*Boilerplate injection into unrelated classes.* `operator==` from a field tuple, `hash()`,
`to_string()`, `accept()` for visitors - the generic "same derived-type-aware member in N
unrelated classes" case.

*Singleton bases.*
`template <class D> class Singleton { static D& instance() { static D d; return d; } };` -
common in older codebases, usually a design smell today.

Note how this interacts with `deducing this` (2.11): it replaces the pattern-level uses -
static interface, mixins, fluent chaining - but not the cases where the base needs
`Derived` as a *type* in its own declarations. `shared_from_this`, `clone()`, factories,
per-type statics, and expression templates still need the template parameter.

**Summary**

CRTP is not a replacement for virtual polymorphism when objects must be handled through a
common runtime base:

```cpp
BaseInterface* p = new Derived;   // I needs a vtable, not a template
```

Use CRTP for compile-time code reuse and zero-overhead dispatch on known types; use
virtual functions the moment the concrete type has to be erased. The common trap is
replacing a virtual hierarchy with CRTP for performance, then discovering a `Base*` is
still needed somewhere and adding the virtual back - now with both mechanisms in play.

## 3. Type traits

Type traits are compile-time templates that inspect or transform types.

Include:

```cpp
#include <type_traits>
```

**Type properties**

```cpp
static_assert(std::is_integral_v<int>);
static_assert(std::is_floating_point_v<double>);
static_assert(std::is_same_v<int, int>);
static_assert(std::is_pointer_v<int*>);
```

The \_v form is a C++17 variable template shorthand:

```cpp
std::is_integral_v<T>
```
is equivalent to:

```cpp
std::is_integral<T>::value
```

**Type transformations**

```cpp
using T1 = std::remove_reference_t<int&>;  // int
using T2 = std::remove_cv_t<const int>;    // int
using T3 = std::add_pointer_t<int>         // int*
using T4 = std::decay_t<const int&>        // int
```

Common transformations include:

```cpp
std::remove_reference_t<T>
std::remove_cv_t<T>
std::remove_cvref_t<T>    // C++20
std::decay_t<T>
std::add_const_t<T>
std::add_pointer_t<T>
```

**3.1 The four families**

Traits fall into four groups, and knowing which one you are reaching for avoids most
confusion:

| Family          | Yields                      | Examples                                              |
| --------------- | --------------------------- | ----------------------------------------------------- |
| Predicates      | `bool` via `::value` / `_v` | `is_integral`, `is_same`, `is_base_of`                |
| Transformations | a type via `::type*` / `_t` | `remove_reference`, `decay`, `conditional`            |
| Queries         | an integral value           | `rank`, `extent`, `alignment_of`, `tuple_size`        |
| Relationships   | `bool` between two types    | `is_convertible`, `is_constructible`, `is_assignable` |

**3.2 `decay_t` and `remove_cvref_t` - which one**

`decay_t` models pass-by-value: it strips cv-qualifiers and references, decays arrays to
pointers and functions to function pointers. `remove_cref_t` (C++20) strips only cv and
references. Use `remove_cvref_t` when you want the "real" type of a forwarding reference
and want arrays to stay arrays.

```cpp
static_assert(std::s_same_vestd::decay_t<const int&>,      int>);
static_assert(std::s_same_v<std::decay_t<int[5]>,          int*>);       // array decays
static_assert(std::s_same_vestd::remove_cvref_t<int[5]>,   int[5]>);     // array preserved
static_assert(std::is_same_vestd::decay_t&void (int)>,     void(*)(int)>);

template <typename T>
void sink(T&& value) {
	using Bare = std::remove_cvref_t<>;   // the usual choice in forwarding code
	static_assert(!std::is_reference_v<Bare>);
}
```

**3.3 `conditional_t` - compile-time type selection**

The type-level equivalent of the ternary operator:

```cpp
template <typename T>
using Storage = std::conditional_t<(sizeof(T) <= sizeof(void*)),
									T,            // small: store by value
									T*>;          // large: store indirectly
```

Both branches are instantiated, so neither may be ill-formed. If one branch is only valid
under the condition, you need a specialization or `if constexpr` instead.

**3.4 `void_t` and detection idioms**

`std::void_t` maps any well-formed type list to `void`, which makes it a SINA trigger
for "does this expression compile":

```cpp
template <typename T, typename = void>
struct has_size : std::false_type {};

template <typename T>
struct has_size<T, std::void_t<decltype(std::declval<T>().size())>> : std::true_type {};

static_assert(has_size<std::vector<int>>::value);
static_assert(!has_size<int>::value);
```

`std::declval<T>()` produces a value of type `T` in an unevaluated context, which is how
you "call" a member without constructing an object. In C++20 the same check is a one-liner
concept - see section 6- and that is the form to prefer in new code.

**3.5 Traits that drive real decisions**

Traits are most useful when they change the generated code rather than merely asserting
something:

``` cpp
template <typename It>
void relocate(It first, It last, It dest) {
	using T= typename std::iterator_traits<It>::value_type;
	if constexpr (std::is_trivially_copyable_v<T> &&
			      std::contiguous_iterator<It>) {
	std::memmove(std::to_address(dest), std::to_address(first),
				(last - first) * sizeof(T));
	} else {
		std::uninitialized_move(first, last, dest);
	}
}
```

Note the two conditions: trivially copyable is not sufficient, the iterators must also be
contiguous for the addresses to be meaningful. `std::to_address` is what converts an
iterator to a raw pointer without dereferencing it (so it is valid for an end iterator).

The traits that most often matter in this role: `is_trivially_copyable`
`is_nothrow_move_constructible` (governs whether `vector` reallocation can be `noexcept`),
`is_empty` (EBO, see 2.4), and `alignment_of`.

Two frequent mistakes:

- Forgetting that a trait sees the type you gave it, cv-qualifiers and references
  included. `std::is_integral_v<const int&>` is `false`; decay first.
- using `std::is_same_v` where `std::is_convertible_v` or a concept is meant. `is_same` is
  exact-match and rejects `int` vs `long`, derived vs base, and anything requiring a
  conversion.

**3.6 Writing traits into signatures**

`std::true_type` and `std::false_type` are just `integral_constant<bool, ...>` inheriting
from them gives your trait `::value`, `::type`, an implicit conversion to `bool`, and a
call operator for free. Use them rather than writing `static constexpr bool value` by hand
- it makes the trait usable as a tag for overload dispatch:

```cpp
template <typename T> void impl(T v, std::true_type /*integral*/);
template <typename T> void impl(T v, std::false_type /*other*/);

template <typename T>
void entry(T v) { impl(v, std::is_integral<T>{}); }
```

Tag dispatch like this predates `if constexpr` and is still occasionally clearer when the
two branches are large or need different signatures.

## 4.1f constexpr

if conster conditionally compiles a branch based on a compile-time condition.

```cpp
#include <type_traits>
#include <iostream>

template <typename T>
void print_info(const T& value) {
	if constexpr (std::is_integral_v<T>) {
		std::cout << "integer: " << value << '\n';
	} else if constexpr (std::is_floating point_v<T>) {
		std::cout << "floating point: " << value << '\n';
	} else {
		std::cout << "other type\n";
	}
}
```

Unlike an ordinary if, the discarded branch is not instantiated.

This is usually clearer than older FINA techniques.

**4.1 "Not instantiated" has a precise meaning**

The discarded branch is not instantiated *only if it depends on a template parameter*.

Non-dependent code in a discarded branch is still fully checked:

```cpp
template <typename T>
void f(T value) {
	if constexpr (std::is_integral_v<T>) {
		value.method();          // fine: never instantiated for integral T
	} else {
	static_assert (false);       // ill-formed before C++23, even when discarded:
	                             // the condition does not depend on T
	}
}
```

The idiomatic pre-C++23 workaround is to make the assertion dependent:

```cpp
template <typename> inline constexpr bool always_false = false;

template < typename T>
void f(T) {
	if constexpr (std::is_integral_v<T>)	 { /* ... */ }
	else if constexpr (std::is_pointer_v<T>) { /* ... */ }
	{ static_assert(always_false<T>, "unsupported type"); }
}
```

C++23 relaxed this: `static_assert(false)` in a discarded branch is now valid.

**4.2 Where it replaces older techniques**

A recursive tuple walk is the standard example. Compare the pre-C++17 version, which
needed a terminating overload:

```cpp
// Pre-C++17: two overloads, SFINAE-selected
template <std::size_t I = 0, typename... Ts>
std::enable_if_t<I == sizeof...(Ts)> print(const std::tuple<Ts...>&) {}

template <std::size_t I = 0, typename... Ts>
std::enable_if_t<I < sizeof...(Ts)> print(const std::tuple<Ts...>&t) {
	std::cout << std::get<I>(t) <<' ';
	print<I + 1>(t);
}

// C++17: one function
template <std::size_t I = 0, typename... Ts>
void print(const std::tuple<Ts...>& t) {
	if constexpr (I < sizeof...(Ts)) {
		std::cout << std::get<I>(t) << ' ';
		print<I + 1>(t);
	}
}
```

Without `if constexpr` the recursive call in the second version would be instantiated
forever; with it, the chain terminates because the false branch is never instantiated.

**4.3 Returning different types from one function**

Each branch can return a different type as long as the return type is deduced:

```cpp
template <typename T>
auto unwrap(T value) {
	if constexpr (std::is_pointer_v<T>)              return *value;
	else if constexpr (requires { value.get(); })    return *value.get();
	else                                             return value;
}
```

This is impossible with a runtime if, which requires all return statements to agree.

**4.4 what it does not do**

- It does not affect overload resolution. `if constepr` chooses inside a function that
  has already been selected; it cannot make a function disappear from the candidate set.
  When you need that, use a concept or `enable_if`.
- It is not a substitute for partial specialization when the *class layout* must differ
  per type - only member function bodies can branch.
- Combined with `constexpr` functions it is easy to write code that silently falls into an
  unintended branch. Prefer an explicit final `else` with a `static_assert` over a
  permissive fall-through, so a new type produces an error rather than wrong behaviour.

## 5. Custom type traits

A trait is commonly represented by a class template containing a compile-time value or
type.

```cpp
template <typename T>
struct is_pointer {
static constexpr bool value = false;
template <typename T> struct is _pointer<™*>
1
static constexpr bool value = true;
template <typename T>
inline constexpr bool is_pointer_v = is_pointer<T>::value;
```

Usage:

```cpp
static_assert (is_pointer_v<int*>); static_assert(lis_pointer_v<int>);
```

The second definition is a **partial specialization** for pointer types.
A type-producing trait can use type:

```cpp
template <typename T> struct add_const_pointer (
using type = const T*;
};
template <typename T>
using add_const_pointer_t = typename add_const pointer<T>::type;
```

The typename keyword is required because T: type is a dependent name.

**5.1 Prefer inheriting from "integral_constant**

writing "value by hand works, but inheriting gives you the whole standard trait interface:

```cpp
template <typename T> struct is_pointer          : std::false_type {};
template <typename T> struct is_pointer<T*>      : std::true_type {};
template <typename T> struct is_pointer<T* const>: std::true_type  {};   // don't forget cv
template <typename T> struct is_pointer<T* volatile>       : std::true_type {};
template <typename T> struct is_pointer<T* const volatile> : std::true_type {};
```

The cv-qualified specializations are the part everyone forgets: `int* const` is a
different type from `int*` and matches only the primary template. Standard traits handle
this; hand-rolled ones usually do not.

**5.2 Specialization matching rules**

Partial specializations are ranked by how specific they are, and the most specialized
match wins. Ambiguity is an error, not a silent pick:

```cpp
template <typename T, typename U> struct Pair;	        // primary
template <typename T>		      struct Pair<t, T>;    // both same
template <typename T>		      struct Pair<T, int>;  // second is int
// Pair<int, int> is ambiguous between the last two - hard error	
```

Two constraints worth remembering:

- Function templates cannot be *partially* specialized. Use overloading, or specialize a
  class template that the function delegates to.
- A specialization must live in the same namespace as the primary template. Specializing
  `std::hash` for your own type is the common legitimate case:

```cpp
struct Sessionid { std::uint64_t v; };

template <>
struct std::hash<SessionId> {
	std::size_t operator() (const SessionId& s) const noexcept {
		return std::hash<std:::uint64_t>{}(s.v);
	}
}
```

**5.3 Recursive traits**

Traits that walk a type are written with recursion plus a base case:

```cpp
// Count pointer levels: int*** -> 3
template <typename T> struct pointer_depth : std::integral_constant<int, 0> {};
template <typename T> struct pointer_depth<T*>
	: std::ntegral_constant<int, 1+ pointer_depth<>::value> {};

static_assert(pointer_depth<int***>::value == 3);

// Strip all pointers: int***-> int
template &typename T> struct remove_all_pointers           { using type = T; };
template <typename T> struct remove_all_pointers<t*>       { using type = typename remove_all_pointers<>::type; }:
template <typename T> using remove_all_pointers_t = typename remove_all_pointers<T>::type;
```

**5.4 Traits over parameter packs**

A common practical need is "do all of these types satisfy X". Since C++17 a fold is
enough; the older form needed recursion:

```cpp
template <typename... Ts>
inline constexpr bool all_integral_v = (std::is_integral_v<Ts> && ...);

// "is T one of Ts"
template <typename T, typename... Ts>
inline constexpr bool is_any_of_v = (std::is_same_v<T, Ts> || ...);

static_assert(all_integral_v<int, long, char>);
static_assert(is_any_of_v<int, float, int, double>);
```

**5.5 Prefer a concept when the trait is only a constraint**

If the trait exists solely to gate a template, define a concept instead - the diagnostics
are better and there is no specialization boilerplate:

```cpp
// Trait form
template <typename T, typename = void> struct is_serializable : std::false_type {};
template <typename T> struct is_serializable<T, std::void_t&decitype(std::declval<const T&>().serialize())>> : std::true_type {};

// Concept form - same meaning, a third of the code
template <typename T>
concept Serializable = requires (const T& t) { t.serialize(); };
```

Keep the trait form when you need the answer as a *value* to compute with - in a
`conditional_t`, a fold over a pack, or a partial specialization condition - since
concepts cannot be stored or passed around as template arguments.

## 6.std::enable_if and SFINAE

**SFINAE** means Substitution Failure Is Not An Error. A template is removed from overload
resolution when substitution fails.

Legacy example:

```cpp
template < typename T>
std::enable_if_t<std::s_integral_v<T>, void>
process (T value) {
	// Enabled only for integral types
}
```

A simpler modern version is:

```cpp
template <typename T>
void process(T value) {
static_assert(std::s_integral_v<T>,
			  "T must be an integral type");
}
```

With C++20 concepts, write:

```cpp
#include <concepts>

template <std::integral T>
void process(T value) {
	// Enabled only for integral types
}
```

Or:

```cpp
template <typename T>
requires std::integral<T>
void process(T value) {
}
```

Concepts generally provide better diagnostics and readability than enable_if.

**6.1 Where `enable_if` may appear**

Only substitution into the *immediate context* of the signature counts. An error in a
function body is a hard error, not a FINA removal. The three placements:

```cpp
// 1. Return type - obscures the real return type
template <typename T>
std::enable_if_t<std::is_integral_v<T>, T> twice(T v) { return v * 2; }

// 2. Default template argument - the most common, keeps the signature readable
template <typename T, std::enable_if_t<std::s_integral_v<T>, int> = 0>
T twice(T v) { return v * 2; }

// 3. Function parameter - works for constructors, which have no return type
template <typename T>
void log(T v, std::enable_if_t<std::s_integral_v<T>, int> = 0);
```

Form 2 is the default choice. Note that two overloads distinguished only by a `enable_if`
in the *return type* are distinct declarations, whereas two distinguished only by a
default template argument are not - a frequent redefinition error:

```cpp
template <typename T, std::enable_if_te std::is_integral_v<T>, int>= 0> void f(T); template typename T, std::enable_if_t<lstd::s_integral_v<T>, int>=e> void f(T); // oK: different types
```

**6.2 Hard error vs substitution failure**

```cpp
template <typename T>
void f(typename T::value_type v);
// substitution failure if T has no value_type - removed
template <typename T>
void g(Tv) {typename T::value_type x; } // hard error: the body is not the immediate context
```

This is the single most important rule about FINA: move anything you want to be
*detectable* into the signature.

**6.3 The constructor trap**

An unconstrained template constructor is a better match than the copy constructor for
non-const lvalues, which silently breaks copying:

```cpp
struct Widget {
	template <typename T>
	Widget (T&& value); // hijacks Widget w2(w1); for non-const w1
	Widget (const Widget&); // never selected in that case
};
```

Constrain it:

```cpp
struct Widget {
	template <typename T, std::enable_if_t<!std::s_same_v<std::remove_cvref_t<T>, widget>, int> = 0>
	Widget (T&& value);

	Widget(const Widget&);
};

// C++20:
struct Widget2 {
	template <typename T>
		requires (!std::same_as<std::remove_cvref_t<T>, Widget2>)
	Widget2(T&& value);
};
```

**6.4 Constraint subsumption: what concepts add**

with `enable_if`, overloads must be made mutually exclusive by hand - miss a case and you
get an ambiguity error. Concepts are ordered*: a more constrained overload wins over a
less constrained one, so you can write a general case and refine it.

```cpp
template <std::integral T>        void f(T);   // (1)
template <std::signed_integral T> void f(T);   // (2) more constrained
f(1);   // calls (2): signed_integral subsumes integral
```

The equivalent `enable_if` version needs `is_integral_v<T> && !is_signed_v<T>` on the
first overload. This ordering is why concepts scale to more than two or three alternatives
and `enable_if` does not.

**6.5 " requires expressions in detail**

A requires expression is a compile-time predicate with four kinds of requirement:

```cpp
template <typename T>
concept Container = requires (T c, const T cc) {
	typename T::value_type;                             // type requirement
	c.begin();                                          // simple requirement: must compile
	{ c.size() } -> std::convertible_to<std::size_t>;    // compound: type constraint on result
	{ cc.empty() } noexcept;                            // must not throw
	requires std::copyable typename T::value_type>;     // nested requirement
}
```

Note the difference between `requires (...)` - the expression, introducing parameters -
and the `requires` *clause* that attaches a constraint to a template. The doubled form
`requires requires { ... }` is legal and means "constrain this template by this ad-hoc
expression"; prefer a named concept in anything but throwaway code.

**6.6 Migration guidance**

| You have                                                         | Replace with                                          |
| ---------------------------------------------------------------- | ----------------------------------------------------- |
| enable_if on a single simple condition                           | Concept in the template parameter list                |
| void_t detection idiom                                           | `requires` expression                                 |
| Tag dispatch between two branches                                | `if constexpr`                                        |
| Overload set made mutually exclusive by hand                     | Constrained overloads, letting subsumption order them |
| enable_if in a public API of a library that must build pre-C++20 | Leave it, but wrap the condition in a named trait     |

## 7. Variadic templates and fold expressions

A **parameter pack** represents zero or more template or function arguments:

```cpp
template <typename... Ts>
void call(Ts... args) {
    // Ts... and args... are parameter packs
}
```

A fold expression applies an operator to all elements:

```cpp
template <typename... Ts>
auto sum(Ts... values) {
    return (values + ...);
}
```

This is a unary right fold:

```cpp
(values + ...)
```

A comma fold executes an operation for every argument:

```cpp
template <typename... Ts>
void print_all(const Ts&... values) {
	(std::cout << ... << values);
}
```

For function calls:

```cpp
template <typename... Ts>
void call_all(Ts&&... values) {
	(process (std::forward<Ts>(values)), ...);
}
```

The comma operator guarantees left-to-right evaluation in this context.

**7.1 The four fold forms**


| Form                    | Name              | Expands to                |
| ----------------------- | ----------------- | ------------------------- |
| `(pack op ...)`         | unary right fold  | `a op (b op (c op d))`    |
| `(... op pack)`         | unary left fold   | `((a op b) op c) op d`    |
| `(init op ... op pack)` | binary left fold  | `((init op a) op b) op c` |
| `(pack op ... op init)` | binary right fold | `a op (b op (c op init))` |

The associativity matters as soon as the operator is not associative:

```cpp
template <typename... Ts> auto sub_left (TS... v) { return (... - v); } // ((1-2)-3) = -4
template <typename... Ts> auto sub_right(TS... v) { return (v - ...); } // (1-(2-3)) = 2
```

Use the binary form to give an empty pack a sensible result. A unary fold over an empty
pack is only valid for `&&` (`true`), `||` (`false`), and `,` (`void`); anything else is a
compile error:

```cpp
template <typename... Ts> auto sum(Ts... v) {return (0 + ... + v); }   // sum() = 0, compiles
```

**7.2 Pack size and expansion contexts**

```cpp
template <typename... Ts>
void f(Ts... args) {
	static_assert(sizeof...(Ts) == sizeof...(args));   // count, not size
}
```

Expansion is allowed wherever a list is expected - base classes, template arguments,
initializer lists, capture lists, `noexcept` specifications. The pattern before `...` is
repeated for each element, which lets you apply a transformation per element:

```cpp
template <typename... Ts>
struct Wrapper : Ts... {                              // inherit from all of them
	using Ts::operator()...;                          // pack expansion in a using-declaration
};
template <typename... Ts>
auto to_tuple_of_ptrs(Ts&... args) {
	return std::tuple<Ts*...>(&args...);              // pattern applied to each element
}
```

**7.3 Ordering and the initializer-list trick**

Function arguments have unspecified evaluation order, so this is a bug when the calls have
side effects:

```cpp
template <typename... Ts> void bad(Ts... v) { consume(process(v)...); }   // order unspecified
```

Fold with a comma, or use a braced-init-list, both of which are ordered left to right:

```cpp
template <typename... Ts> void good(Ts... v) { (process(v), ...); }               // C++17
template <typename... Ts> void old (Ts... v) { int _[] = {0, (process(v), 0)...}; (void)_; }
```

The `int _[]` form is what pre-C++17 code uses; recognise it, don't write it.

**7.4 Indexing into a pack**

Packs are not randomly accessible before Ct26. The usual route is `std::index_sequence`
plus a lambda:

```cpp
template <typename Tuple, typename F>
void for_each_element(Tuple&d t, Ff) {
	[&]<std::size_t... I>(std::index_sequence<I...>) {
		(f(std::get<I>(std::forward<Tuple>(t))), ...);
	}(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tuple>>>{});
}
```

The lambda exists only to give the `I...` pack a name; this shape appears in every
tuple-manipulating library. `std::apply` covers the common case and should be preferred
when you just want to unpack a tuple into a call.

**7.5 Perfect forwarding with packs**

```cpp
template <typename T, typename... Args>
std::unique_ptr<T> make(Args&&... args) {
	return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}
```

Three rules that cover almost every mistake here:

- `Args&&...` is a *forwarding* reference pack only when `Args` is deduced.
  `std::vector<T>&&` in a class template member is an rvalue reference, not a forwarding
  reference.
- Always `std::forward<Args> (args)...`, never `std::move(args)...` - the latter turns
  value arguments into rvalues and silently steals from the caller.
- Forward each argument exactly once. A forwarded argument used twice may be read after
  being moved from.

**7.6 Variadic class templates and recursion**

Where a fold is not enough - because you need one member or one base per element - the
pattern is inheritance recursion, or since C++17 an overload-set trick:

```cpp
// One overload per type, selected by overload resolution
template <typename... Ts>
struct Overloaded : Ts... { using Ts::operator()...; };

std::variant<int, sta::string> v = 42;
std::visit(Overloaded{
	[](int i)                { std::cout << "int "    << i << '\n'; },
	[](const std::string& s) { std::cout << "string " << s << '\n'; },
}, v);
```

This is the standard `overloaded` idiom for `std::visit`, and it is the clearest
demonstration of pack expansion in both a base-class list and a using-declaration.

## 8. Key concepts to remember

- **Primary** template: general template definition.
- **Full specialization**: specialization for an exact type.
- **Partial specialization**: specialization for a type pattern.
- **Type trait**: compile-time type property or transformation.
- **SFINAE**: removes invalid template candidates from overload resolution.
- **CRTP**: compile-time polymorphism through self-referential inheritance.
- **if constexpr**: compile-time conditional compilation.
- **Concepts**: readable compile-time constraints, available since C++20.
- **Fold expression**: applies an operator to a parameter pack.
- **Two-phase lookup**: non-dependent names bind at definition, dependent names at
  instantiation - hence `typename`, `template`, and `this->`.
- **Immediate context**: the part of a declaration where substitution failure is
  recoverable; errors in a function body are not.
- **Subsumption**: the partial ordering that lets a more constrained overload win over a
  less constrained one.

A useful modern preference order is:

1, if constexpr 2, Concepts and requires 3, Standard type traits 4, Custom traits 5,
enable_if and complicated SFINAE when maintaining older code or implementing advanced
libraries.

**8.1 Choosing a mechanism**

| Problem                                                      | Reach for                                     |
| ------------------------------------------------------------ | --------------------------------------------- |
| Branch on a type inside one function                         | `if constexpr`                                |
| Constrain who may call a template                            | Concept in the parameter list                 |
| Several alternatives, one more specific than another         | Constrained overloads (subsumption)           |
| Class layout must differ per type                            | Partial specialization                        |
| Reuse implementation across unrelated types, no runtime base | CRTP, or deducing this in C++23               |
| Heterogeneous storage, runtime-chosen type                   | Virtual functions or type erasure             |
| Operate on Narguments or N types                             | Parameter pack plus fold                      |
| Compile-time arithmetic                                      | `constexpr` function, not recursive templates |

**8.2 Diagnosing template errors**

The output is long because the compiler prints the whole instantiation chain. Read it in
this order:

1. Find the *last* "required from here" line - that is your code; everything above it is
   library internals.
2. Look for the first `error:` rather than the first line of output.
3. Reproduce the failing condition in a `static_assert` at the call site.
   `static_assert(std::is_copy_constructible_v<T>)` at the top of a function turns 200
   lines of `vector` internals into one message.
4. Add `-fconcepts-diagnostics-depth=2` (GCC) or `-ftemplate-backtrace-limit=0` when the
   chain is truncated.
5. Prefer constraining the template so the error appears at the call site instead of deep
   inside - this is the main practical argument for concepts.

Tools worth knowing: `-ftime-trace` (Clang) produces a flame graph of where compile time
goes, which usually points at a handful of heavy instantiations; `-ftemplate-depth` raises
the recursion limit when a genuinely deep recursion hits the default 900-1024.

**8.3 Cost contro1**

Template code is duplicated per instantiation, and compile time grows with the number of
distinct instantiations, not with the number of call sites.

- Factor type-independent logic out of templates into ordinary functions (the "thin
  template" idiom) - the same advice as 2.9.
- Use `extern template` for heavily used specializations (1.4).
- Prefer fold expressions and `if constexpr` over recursive instantiation; a fold is one
  instantiation, recursion is N.
- Prefer alias templates to inheriting-from-a-trait when you only need a type - they do
  not create a new class.
- Measure before optimising. `-ftime-trace` almost always contradicts the guess.

**8.4 When not to use TMP**

The failure mode of this topic is enthusiasm. Reach for a simpler tool when:

- A runtime `if`, a virtual function, or `std::function` is fast enough - which is most of
  the time outside hot loops.
- The set of types is genuinely open-ended or determined at runtime; that is what
  polymorphism is for.
- The abstraction will be maintained by people who did not write it, and the compile-time
  version cannot be expressed with concepts. Poor diagnostics are a real maintenance cost.
- Debug-build performance matters: nothing is inlined, so the zero-overhead argument
  evaporates while the compile-time cost remains.

The reasonable default: write the straightforward runtime version, measure, and introduce
compile-time machinery only where it pays for itself in type safety or measured
performance.

Appendix A. Compile-check file Every snippet in this document that can stand alone is
collected here, tagged with the section it comes from. It is a regression check: if a
future edit breaks an example, this file stops compiling.

Build and run:

```sh
g++ -std=c++20 -Wall -Wextra -pedantic check.cpp -o check && ./check
```

Expected output:

```text
integral
other
1 2 1 2
signed
1 2.5
hi
int 42
123
120 42
```

Verified with g++ 13.3 under both `-std=c++20` and `-std=c++23`, clean under
`-wall -wextra -pedantic`.

Two things are deliberately absent because they cannot compile by construction:

- The `deducing this` example in 2.11-g++ 13 does not implement explicit object
  parameters. It needs g++ 14+, clang 18+, or MSVC 19.32+.
- The negative examples: `Pair<int, int>` in 5.2 (ambiguous), `static_assert(false)` in a
  discarded branch in 4.1 pre-C++23, the incomplete- `Derived` uses in 2.7, and
  `class Wrong: public Base<Right>` in 2.8. Instantiating any of them is the point, so
  they are shown but not exercised.

```cpp
// Compile-check for "cpp_template_metaprogramming.md"
// Every snippet in the document that can be compiled standalone appears here,
// tagged with the section it comes from.
//
// g++ - std=c++20 -Wall -Wextra -pedantic check. cpp -o check && ./check
//

#include <concepts>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <iterator>
#include <memory>
#include <ranges>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// ---------------------------------------------------------------- 1.2 two-phase lookup
namespace s12 {
template <typename T> struct Base { using Value = int; void helper() {}};

template <typename T> struct Derived : Base<T> {
	void f() {
		typename Base T>::Value v{};   // 'typename" required: dependent name
		(void) v;
		this->helper();                // 'this->' required: dependent base
	}
};
inline void use() { Derived<int>{}.f(); }
}  // namespace s12

// ---------------------------------------------------------------- 1.3 constexpr / consteval / constinit
namespace s13 {
constexpr std::size_t factorial(std::size_t n) { return n <= 1 ? 1: n * factorial(n - 1); }
static_assert(factorial(5) == 120);

// legacy recursive-instantiation form
template <std::size_t N> struct Factorial { static constexpr std::size_t value = N * Factorial<N - 1>::value; };
template<>               struct Factorial<0> { static constexpr std::size_t value = 1; };
static_assert(Factorial<5>::value == 120);

consteval int must_be_compile_time(int x) { return x * 2; }
constexpr int a = must_be_compile_time (21);
static_assert (a == 42);

constinit int counter = factorial(5);   // no dynamic init, still writable
}  // namespace s13

// ----------------------------------------------------------------  1.4 explicit instantiation
namespace s14 {
template <typename T> void f(T) {}
template void f<char>(char);            // definition: emit here
extern template void f<long>(long);     // declaration: link elsewhere
}  // namespace s14

// ----------------------------------------------------------------  3.2 decay_t vs remove_cvref_t
static_assert(std::is_same_v<std::decay_t<const int&>,    int>);
static_assert(std::is_same_v<std::decay_t<int[5]>,        int*>);      // array decays
static_assert(std::is_same_v<std::remove_cvref_t<int[5]>, int[5]>);    // array preserved
static_assert(std::is_same_v<std::decay_t<void(int)>,     void (*)(int)>);

template <typename T>
void sink(T&& value) {
	using Bare = std::remove_cvref_t<T>;
	static_assert(!std::is_reference_v<Bare>);
	(void) value;
}

// ----------------------------------------------------------------  3.3 conditional_t
template <typename T>
using storage = std::conditional_t<(sizeof(T) <= sizeof(void*)), T, T*>;
static_assert(std::is_same_v<Storage<char>, char>);

// ----------------------------------------------------------------  3.4 void_t detection
namespace s34 {
template <typename T, typename = void> struct has_size : std::false_type {};
template <typename T> struct has_sizesT, std::void_t<decltype(std::declval<T>().size())>> : std::true_type {};
static_assert( has_size<std::vector<int>>::value);
static_assert(!has_size<int>::value);
}  // namespace s34

// ----------------------------------------------------------------  3.5 traits that drive codegen
template <typename It>
void relocate(It first, It last, It dest) {
	using T = typename std::iterator_traits<It>::value_type;
	if constexpr (std::is_trivially_copyable_v<T> && std::contiguous_iterator<It>) {
		std::memmove(std::to_address(dest), std::to_address(first), (last - first) * sizeof(T));
	} else {
		std::uninitialized_move(first, last, dest);
	}
}

// ----------------------------------------------------------------  3.6 tag dispatch
namespace S36 {
template <typename T> void impl(T, std::true_type)  { std::cout << "integral\n"; }
template <typename T> void impl(T, std::false_type) { std::cout << "other\n"; }
template <typename T> void entry(Tv) {implv, std::is_integral<T>(J); }
inline void use() { entry(1); entry(1.0); }
}  // namespace 536

// ----------------------------------------------------------------  4.1 always_false
namespace s41 {
template <typename> inline constexpr bool always_false = false;
template <typename T>
void f(T) {
	if constexpr (std::is_integral_v<T>)     {/* ... */}
	else if constexpr (std::is_pointer_v<T>) {/* ... */}
	else { static_assert(always_falses<T>, "unsupported type";) }
}
inline void use() { int x = 0; f(1); f(&x); }
}  // namespace s41

// ----------------------------------------------------------------  4.2 recursion: SFINAE vs if constexpr
namespace s42 {
template <std::size_t I = 0, typename... Ts>
std::enable_if_t<I == sizeof... (Ts)> print_old(const std::tuple<Ts...>&) {}

template <std::size_t I = 0, typename... Ts>
std::enable_if_t<(I < sizeof...(Ts))> print_old(const std::tuple<Ts...>& t) {
	std::cout << std::get<I>(t) <<' ';
	print_old<I + 1>(t);
}

template <std::size_t I = 0, typename... Ts>
void print(const std::tuple<Ts...>& t) {
	if constexpr (I < sizeof...(Ts)) {
		std::cout << std::get<I>(t) <<' ';
		print<I + 1>(t);
	}
}
inline void use() { auto t = std::make_tuple(1, 2.0); print(t); print_old(t); std::cout << '\n'; }
}  // namespace s42

// ----------------------------------------------------------------  4.3 per-branch return type
namespace s43 {
template <typename T>
auto unwrap(T value) {
	if constexpr (std::is_pointer_v<T>)           return *value;
	else if constexpr (requires { value.get(); }) return *value.get():
	else                                          return value;
}
inline void use() {int x = 1; (void) unwrap(&x); (void) unwrap(3); }
}  // namespace s43

//
namespace s51 {
// ----------------------------------------------------------------  5.1 cv-qualified specializations
template <typename T> struct is_pointer                    : std::false_type {};
template <typename T> struct is_pointer<T*>                : std::true_type {};
template <typename T> struct is_pointer<T* const>          : std::true_type {};                                    
template <typename T> struct is_pointer<T* volatile>       : std::true_type {};
template <typename T> struct is_pointer<T* const volatile> : std::true_type {};
static_assert(is_pointer<int* const>::value);   // fails without the cv overloads
static_assert(!is_pointer<int>::value);
}  // namespace s51

// ----------------------------------------------------------------  5.2 specialization ranking / std::hash
namespace s52
template <typename T, typename u> struct Pair;         // primary
template <typename T>             struct Pair<T, T>;   // both same
template <typename T>             struct Pair<T, int>; // second is int
// Pair <int, int> would be ambiguous - deliberately not instantiated
} // namespace s52

struct Sessionid { std::uint64_t v; };

template <>
struct std::hash<SessionId> {
	std::size_t operator()(const SessionId& s) const noexcept {return std::hash<std::int64_t>{}(s.v); }
}

// ----------------------------------------------------------------  5.3 recursive traits
template <typename T> struct pointer_depth     : std::integral_constant<int, 0> {};
template <typename T> struct pointer_depth<T*> : std::integral_constant<int, 1 + pointer_depth<T>::value> {};
static_assert(pointer_depth<int***>::value == 3);

template <typename T> struct remove_all_pointers	 { using type = T; };
template <typename T> struct remove_all_pointers<T*> { using type = typename remove_all_pointers<T>::type; };
template <typename T> using  remove_all_pointers_t = typename remove_all_pointers<T>::type;
static_assert(std::is_same_v<remove_all_pointers_t<int***>,	int>);

// ----------------------------------------------------------------  5.4 traits over packs

template <typename... Ts>            inline constexpr bool all_integral_v = (std::is_integral_v<Ts> && ...);
template <typename T, typename... Ts> inline constexpr bool is_any_of_v = (std::is_same_v<T, Ts> || ...);
static_assert(all_integral_v<int, long, char>);
static_assert(is_any_of_v<int, float, int, double>);

// ----------------------------------------------------------------  5.5 trait vs concept
namespace s55 {
template <typename T, typename = void> struct is_serializable : std::false_type {};
template <typename T>
struct is_serializables<T, std::void_t<decltype(std::declval<const T&>().serialize())>> : std::true_type {};

template <typename T>
concept Serializable = requires (const T& t) { t.serialize(); };

struct S { void serialize() const {} };
static_assert(is_serializable<S>::value);
static_assert (Serializable<S>);
}  // namespace s55

// ----------------------------------------------------------------  6.1 the three enable_if placements
namespace s61a {   // return type
template <typename T> std::enable_if_t<std::is_integral_v<T>, T> twice(T v) { return v * 2; }
}
namespace s61b {   // default template argument (preferred)
template <typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0> T twice(T v) { return v * 2; }
}
namespace s61c { // function parameter
template <typename T> void log(T, std::enable_if_t<std::is_integral_v<T>, int> = 0) {}

template <typename T, std::enable_if_t< std::is_integral_v<T>, int> = 0> void f(T) {}
template <typename T, std::enable_if_t<!std::is_integral_v<T>, int> = 0> void f(T) {}
inline void use() { f(1); f(1.0); log(1); }
}  // namespace s61c

// ----------------------------------------------------------------  6.2 immediate context
namespace s62 {
template <typename T> void f(typename T::value_type) {}  // removed by SFINAE
template <typename T> void f(T) {}                       // chosen for T = int
inline void use() { f<int>(1); }
}  // namespace s62

// ----------------------------------------------------------------  6.3 the constructor trap
namespace s63 {
struct Widget {
	template <typename T, std::enable_if_t<!std::is_same_v<std::remove_cvref_t<T>, Widget>, int> = 0>
	Widget(T&&) {}
	Widget(const Widget&) {}
	Widget() = default;
};

struct Widget2 {
	template <typename T> requires (!std::same_as<std::remove_cvref_t<T>, Widget2>)
	Widget2(T&&) {}
	Widget2() = default;
};
inline void use() { Widget w; Widget w2(w); (void) w2; Widget2 a; Widget2 b(a); (void) b; }
} // namespace s63

// ----------------------------------------------------------------  6.4 subsumption
namespace s64 {
template <std::integral T>        void f(T) { std::cout << "integral\n"; }
template <std::signed_integral T> void f(T) { std::cout << "signed\n"; }
inline void use() { f(1); }
}  // namespace s64

// ----------------------------------------------------------------  6.5 requires expression
// more constrained: wins
namespace s65 {
template <typename T>
concept Container = requires (T c, const T cc) {
	typename T::value_type;                              // type requirement
	c.begin();                                           // simple requirement
	{ c.size() } -> std::convertible_to<std::size_t>;    // compound requirement
	{ cc.empty() } noexcept;                             // must not throw
	requires std::copyable<typename T::value_type>;      // nested requirement
};
static_assert(Container<std::vector<int>>);
static_assert(!Container<int>);
}
// namespace s65

// ----------------------------------------------------------------  7.1 fold associativity
template <typename... Ts> constexpr auto sub_left (Ts... v) { return (... - v); }   // ((1-(2-3)
template <typename... Ts> constexpr auto sub_right(Ts... v) { return (v - ...); }   // (1-(2-3))
static_assert(sub_left (1, 2, 3) == -4);
static_assert (sub_right(1, 2, 3) == 2);

template <typename... Ts> constexpr auto sum(Ts... v) { return (0 + ... + v); }      // binary: empty pack ok
static_assert(sum() == 0);

// ----------------------------------------------------------------  7.2 expansion contexts
template <typename... Ts>
void packsize(Ts... args) { static_assert(sizeof...(Ts) == sizeof...(args)); }

template <typename... Ts> struct Wrapper : Ts... { using Ts::operator()...; };

template <typename... Ts>
auto to_tuple_of_ptrs(Ts&... args) {return std::tuple<Ts*...>(&args...);}

// ----------------------------------------------------------------  7.3 evaluation order
inline void process(int) {}
template <typename... Ts> void good (Ts... v) { (process(v), ...); }                        // C++17
template <typename... Ts> void old_(Ts... v) {int _[] = {0, (process(v), 0)...}; (void) _;} // pre-C++17

// ----------------------------------------------------------------  7.4 index_sequence
template <typename Tuple, typename F>
void for each_element (Tuple&& t, Ff) {
	[&]<std::size_t... I>(std:::index_sequence<I...>) {
		(f(std::get<I>(std::forward<Tuple>(t))), ...);
	}(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<Tuple>>>{});
}

// ----------------------------------------------------------------  7.5 perfect forwarding
template <typename T, typename... Args>
std::unique_ptr<T> make(Args&&... args) { return std::unique_ptr<T>(new T(std::forward<Args>(args)...)); }

// ----------------------------------------------------------------  7.6 the overloaded idiom
template <typename... Ts> struct Overloaded: Ts... { using Ts::operator()...; };

// ----------------------------------------------------------------  2.12 view_interface (CRTP in the STL)
class CountIter: public std::ranges::view_interface<CountIter> {
public:
	int* begin();
	int* end();
	// size(), empty), front(), operator[] come from the base
};

int main() {
	s12::use();
	s36::use ();
	s41::use();
	s42::use();
	s43::use();
	s61c::use() ;
	s62::use();
	563::use();
	s64::use();

	good(1, 2);
	old_(1, 2);
	packsize(1, 2.0);

	int x = 1, y = 2;
	auto ptrs = to_tuple_of_ptrs(x, y) ;
	(void) ptrs;

	for_each_element (std::make_tuple(1, 2.5), [](auto v) { std::cout << v <<' '; });
	std::cout << '\n';

	auto p = make<std::string> ("hi");
	std::cout << *p << '\n';

	std::variant<int, std::string> v = 42;
	std::visit(Overloaded{
	[](int i)                { std::cout << "int "    << i << '\n'; },
	[](const std::string& s) { std::cout << "string " << s << '\n'; },
	}, v);

	std::vector<int> src{1, 2, 3}, dst(3);
	relocate(src.begin(), src.end(), dst.begin());
	std::cout << dst[0] << dst[1] << dst[2] << '\n';

	sink(x);
	std::cout << s13::counter << s61b::twice(21) << '\n';
	return 0;
}
```