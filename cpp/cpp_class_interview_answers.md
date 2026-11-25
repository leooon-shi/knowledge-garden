<!-- cpp/cpp_class_interview_answers.md -->

# C++ class and object interview answers

Answers to the numbered questions in [`cpp_class_interview.md`](cpp_class_interview.md)
(`Q1`-`Q84`). The code-based questions already have answers in that file. Link to an
answer by its anchor, for example `#q1`.

Try the questions first, then compare.

## Basic concepts

#### Q1

**What is the difference between a class and an object?**

A class is a type: a compile-time blueprint that describes the data members and member
functions and their access rules. An object is a run-time instance of that type that
occupies storage and has a lifetime. One class can have many objects; the class itself has
no storage (ignoring its static members).

#### Q2

**What is the difference between `class` and `struct` in C++?**

The only language-level difference is the default access and default inheritance: members
and base classes are `private` by default in a `class` and `public` by default in a
`struct`. Everything else (virtual functions, constructors, templates, etc.) is identical.
By convention `struct` is used for passive aggregates and `class` for types with
invariants, but that is style, not a rule.

#### Q3

**What are the default access specifiers for `class` and `struct`?**

For `class`, members and bases default to `private`. For `struct`, they default to
`public`. This applies both to data/function members and to the access level of base
classes.

#### Q4

**What is the difference between a data member and a member function?**

A data member is a variable that is part of each object's state and (unless `static`)
exists once per object. A member function is code that operates on an object; non-static
member functions receive an implicit `this` pointer and exist once for the whole program,
not once per object.

#### Q5

**Can a class be empty? What is the typical size of an empty class, and why? (See Question
8 below.)**

Yes. An empty class has `sizeof` of at least 1 (typically exactly 1) so that every
complete object has a distinct address and arrays work correctly: if the size were 0, two
array elements would share an address. As a base class the empty-base optimization (EBO)
or `[[no_unique_address]]` on an empty member can make it contribute 0 bytes.

#### Q6

**Can an object be created without explicitly defining a constructor?**

Yes. If you declare no constructors, the compiler implicitly declares a default
constructor (and it is defined/usable when its members and bases are
default-constructible). Aggregates can also be created with aggregate initialization. Note
that declaring any other constructor suppresses the implicit default constructor.

#### Q7

**What is the difference between a declaration and a definition of a class member
function?**

A declaration (usually inside the class, in a header) introduces the name and signature; a
definition provides the body. A function defined inside the class body is implicitly
`inline`; one declared in the class and defined out of line (`void C::f() {}`) is not
inline unless marked so. The one-definition rule allows many declarations but essentially
one definition.

#### Q8

**What is the purpose of the `this` pointer?**

`this` is an implicit parameter in every non-static member function that points to the
object the function was invoked on, letting the function access that object's members and
disambiguate names (e.g. `this->x = x;`). In a `const` member function its type is
pointer-to-const. It enables member chaining via `return *this;`.

#### Q9

**Can `this` be null inside a non-static member function?**

No, not in a valid program. Calling a non-static member function through a null pointer is
undefined behavior, so by the time control enters the function the standard assumes `this`
is non-null. Code that "checks `if (this == nullptr)`" is relying on UB and modern
compilers may optimize the check away. *Red flag:* a candidate who says null-`this` checks
are a legitimate technique.

#### Q10

**What is the difference between static and non-static data members?**

A non-static data member exists once per object and is part of each object's state. A
static data member exists once for the entire class, is shared by all objects, has static
storage duration, and must be defined (or be `inline`/`constexpr`) outside the class in
C++14, though `inline static` since C++17 lets you define it in the class. Static members
are accessed as `Class::member` and are not counted in `sizeof` of an object.

## Constructors and destructors

#### Q11

**What is a constructor? Can it return a value?**

A constructor is a special member function that initializes an object during its
construction, establishing the class invariant. It has no return type and cannot return a
value (not even `void`); you also cannot take its address. It can be overloaded,
defaulted, deleted, `explicit`, or `constexpr`.

#### Q12

**What is a default constructor?**

A default constructor is one callable with no arguments, either taking no parameters or
having defaults for all of them. The compiler generates one implicitly only if you declare
no constructors at all; you can force one with `= default`. It is used for value/default
initialization and is required by containers that default-construct elements.

#### Q13

**What is the difference between these constructors?**

```cpp
A();
A(int x);
A(const A& other);
A(A&& other);
```

`A()` is the default constructor (no arguments). `A(int x)` is a converting/value
constructor taking an `int` (mark it `explicit` to prevent implicit conversions).
`A(const A&)` is the copy constructor, invoked when constructing from an lvalue of `A`.
`A(A&&)` is the move constructor, invoked from an rvalue, and should leave the source in a
valid but unspecified state.

#### Q14

**In what order are data members initialized?**

```cpp
class Example {
    int first;
    int second;
public:
    Example()
        : second(2), first(1) {}
};
```

Members are initialized in the order they are declared in the class, regardless of the
order in the member-initializer list. Here `first` is initialized before `second` even
though the list writes `second` first. Listing them out of declaration order is a common
source of `-Wreorder` warnings and subtle bugs when one member's initializer reads
another.

#### Q15

**What is the difference between initialization and assignment?**

Initialization gives an object its first value as it is created (member-initializer list,
braces, or `= value` at the point of definition). Assignment replaces the value of an
already-constructed object via `operator=`. For members, writing `x = v;` inside the
constructor body default-constructs `x` first and then assigns, whereas `: x(v)`
initializes directly; the distinction matters for `const`, references, and
expensive-to-construct types.

#### Q16

**Why should members generally be initialized using a member-initializer list?**

Because it initializes members directly instead of default-constructing then assigning,
which is more efficient and is the only way to initialize `const` members, reference
members, members without a default constructor, and base classes. It also avoids a window
where a member holds a default/garbage value. Remember to list members in declaration
order to match the actual initialization order.

#### Q17

**What happens if a `const` data member is not included in the constructor's initializer
list?**

A `const` (non-static, non-defaulted) member has no default value, so failing to
initialize it is ill-formed: the program does not compile. If the member has an in-class
default initializer or a default constructor, that is used; otherwise you must initialize
it in the mem-init list.

#### Q18

**What happens if a reference data member is not initialized?**

The program is ill-formed. A reference must be bound when the object is constructed, so a
reference member must appear in every constructor's initializer list (or have an in-class
initializer); otherwise compilation fails. References cannot be rebound later, which is
why they cannot be left for the constructor body.

#### Q19

**In what order are constructors and destructors called in an inheritance hierarchy?**

Construction proceeds base-to-derived: virtual bases first, then direct non-virtual bases
in declaration order, then members in declaration order, then the derived constructor
body. Destruction is the exact reverse: derived body, then members in reverse, then bases
in reverse. This guarantees a member or base is fully constructed before it is used and
still alive while it is torn down.

#### Q20

**Why should a base class destructor often be virtual?**

So that `delete`ing a derived object through a base-class pointer invokes the correct
(derived) destructor. If the base destructor is non-virtual and you delete a derived
object via `Base*`, the behavior is undefined (typically only the base part is destroyed,
leaking the derived part). Make the base destructor `virtual` (or `protected` non-virtual
if polymorphic deletion is not intended).

#### Q21

**What happens when a constructor throws an exception?**

The object is never fully constructed, so its destructor is not called, but all subobjects
(bases and members) that were already fully constructed are destroyed in reverse order. If
the throwing constructor allocated with `new`, the matching operator delete is called
automatically for that allocation. This is why RAII members clean themselves up and raw
resources acquired in the constructor body need care.

#### Q22

**Can a constructor be virtual? Why or why not?**

No. Virtual dispatch requires an already-existing object with an initialized vptr, but a
constructor is what brings the object (and its vptr) into existence, so there is nothing
to dispatch on. The usual workaround for "virtual construction" is a virtual `clone()` or
a factory function.

#### Q23

**Can a destructor be pure virtual?**

Yes. A pure virtual destructor makes a class abstract while still being a destructor, but
it must still have a definition, because every derived object's destruction chains into
the base destructor. For example:

```cpp
struct Base {
    virtual ~Base() = 0;
};
Base::~Base() {}   // definition is mandatory
```

## Copying and moving

#### Q24

**What is the difference between a shallow copy and a deep copy?**

A shallow copy duplicates member values bitwise, so a pointer member ends up pointing at
the same underlying resource in both objects. A deep copy duplicates the pointed-to
resource so each object owns an independent copy. For types owning raw resources, the
compiler-generated copy is shallow, which leads to double-free and aliasing bugs.

#### Q25

**What are the copy constructor and copy-assignment operator?**

The copy constructor `T(const T&)` creates a new object as a copy of an existing one; the
copy-assignment operator `T& operator=(const T&)` replaces the state of an
already-existing object with a copy of another. Assignment must additionally release any
resource it already holds and handle self-assignment safely (the copy-and-swap idiom does
both).

#### Q26

**What are the move constructor and move-assignment operator?**

`T(T&&)` and `T& operator=(T&&)` transfer resources from a source rvalue instead of
copying them, leaving the source in a valid but unspecified state. They are selected for
rvalues (temporaries, `std::move`d objects) and should be `noexcept` so that containers
like `std::vector` can use them during reallocation.

#### Q27

**What is the Rule of Three?**

If a class needs a user-defined destructor, copy constructor, or copy-assignment operator,
it almost certainly needs all three, because that usually signals manual resource
ownership. Defining only one leaves the others with incorrect compiler-generated (shallow)
behavior.

#### Q28

**What is the Rule of Five?**

An extension of the Rule of Three for C++11: if you declare any of destructor, copy
constructor, copy assignment, move constructor, or move assignment, consider declaring all
five. Declaring a destructor or copy operation also suppresses or deprecates the implicit
move operations, so you may silently lose moves if you do not provide them.

#### Q29

**What is the Rule of Zero?**

Design classes so they need none of the five special members by delegating resource
ownership to RAII types (`std::unique_ptr`, `std::vector`, `std::string`, etc.). Then the
compiler-generated copy/move/destroy are all correct, and the class stays simple. This is
the preferred modern style.

#### Q30

**What does `= default` mean?**

It asks the compiler to generate the default implementation of a special member function,
while still letting you control its declaration (access level, `noexcept`, or re-enabling
a member that would otherwise be suppressed). A defaulted function can also be more
efficient and can be trivial, which matters for `std::is_trivially_copyable` and the like.

#### Q31

**What does `= delete` mean?**

It declares the function but forbids its use; any attempt to call or implicitly select it
is a compile error. It is commonly used to make a type non-copyable
(`T(const T&) = delete;`) or to ban unwanted overloads/conversions. A deleted function
still participates in overload resolution, so it can intentionally block specific calls.

#### Q32

**What is wrong with this class?**

```cpp
class Buffer {
    char* data;

public:
    Buffer(std::size_t size)
        : data(new char[size]) {}

    ~Buffer() {
        delete[] data;
    }
};
```

It owns a raw resource but violates the Rule of Three/Five: the implicitly generated copy
constructor and copy assignment do a shallow copy, so two `Buffer`s point at the same
array and `delete[]` runs twice (double free). Copying or assigning a `Buffer` is
therefore unsafe, and there are no move operations.

#### Q33

**What happens if an object of `Buffer` is copied?**

The compiler-generated copy copies the `data` pointer value, so both objects share the
same heap array. When both destructors run, `delete[]` is called twice on the same pocket
of memory, which is undefined behavior (double free); modifying one buffer also corrupts
the other.

#### Q34

**How would you make the previous class safe?**

The simplest fix is Rule of Zero: store the data in an owning RAII type so
copies/moves/destruction are correct automatically.

```cpp
class Buffer {
    std::vector<char> data;      // or std::unique_ptr<char[]>
public:
    explicit Buffer(std::size_t size) : data(size) {}
};
```

Alternatively, delete copying, or implement deep copy plus `noexcept` move operations
(Rule of Five).

#### Q35

**When is `std::move` actually moving an object?**

Never by itself. `std::move` is only an unconditional cast of its argument to an rvalue
reference; no data is touched. Movement happens later, if and only if overload resolution
then selects a move constructor or move-assignment operator (or a function taking by
value). If no move operation exists, the rvalue binds to a copy and you get a copy.

#### Q36

**What happens when a moved-from object is used?**

For standard-library types the object is in a valid but unspecified state, so operations
with no preconditions (destruction, assignment, `clear()`, `size()`) are well-defined, but
you must not assume any particular value. Calling operations with preconditions (e.g.
`front()` on a possibly-empty container) may be UB. For user types it behaves however the
author defined the move operations.

#### Q37

**Why is a moved-from object still required to be valid?**

Because its destructor (and possibly further assignments) will still run, so it must be in
a state where those operations are safe and leak-free. The standard requires move
operations to leave a valid but unspecified state precisely so generic code and
destructors keep working. "Unspecified" means you should not rely on the contents, only on
the invariants.

## Access control and member functions

#### Q38

**What is the difference between `public`, `protected`, and `private`?**

`public` members are accessible to any code. `protected` members are accessible within the
class and its derived classes (and friends), but not to outside code. `private` members
are accessible only within the class itself and its friends. Access control is a
compile-time rule about names, not about runtime memory protection.

#### Q39

**Can a derived class access private members of its base class?**

No. A derived class can access `public` and `protected` members of its base but not
`private` ones; privacy encapsulates the base's implementation even from subclasses. It
may, however, use the base's public/protected interface, and `friend` can grant
exceptions. Use `protected` if subclasses legitimately need access.

#### Q40

**What is a friend function?**

A friend function is a non-member (or a function/class from elsewhere) that the class
grants access to its `private` and `protected` members via a `friend` declaration.
Friendship is granted by the class, is not inherited, and is not transitive. It is
commonly used for symmetric operators like `operator<<` and `operator==`.

#### Q41

**What are the advantages and disadvantages of using `friend`?**

Advantages: it enables natural non-member operators (e.g. stream insertion, symmetric
binary operators) and tightly-coupled helper types to access internals without exposing
them publicly. Disadvantages: it breaks encapsulation for the friended entity, couples
that code to the class's private layout, and can be abused instead of designing a proper
interface. Prefer the minimum friendship necessary.

#### Q42

**What is a `const` member function?**

```cpp
class User {
public:
    int id() const;
};
```

A `const` member function promises not to modify the observable state of the object;
inside it `this` is a pointer to `const`, so non-`mutable` members cannot be changed and
only other `const` member functions can be called on `this`. It can be called on both
`const` and non-`const` objects, and is part of the function's signature for overloading.

#### Q43

**Can a `const` member function modify a data member?**

Normally no, because `this` is pointer-to-const. The exceptions are members declared
`mutable` (meant for caches, mutexes, etc.) and data reached indirectly: a `const` method
can still modify what a pointer member points to, since constness applies to the pointer,
not the pointee. Casting away const to modify a genuinely const object is UB.

#### Q44

**What is `mutable` used for?**

`mutable` lets a specific non-static data member be modified even inside a `const` member
function or when the object is `const`. It is intended for members that do not affect the
object's logical/observable state, such as a cached computation, a memoization flag, or a
`std::mutex` used for thread-safe const access. Overuse defeats const-correctness.

#### Q45

**What is the difference between these functions?**

```cpp
void process();
void process() const;
```

They are two overloads distinguished by the constness of the implicit `this`. A
non-`const` object prefers `process()`; a `const` object can only call the `const`
overload. This pair is the standard way to provide, for example, both mutating and
read-only accessors (like `begin()`/`begin() const`).

#### Q46

**Can a static member function access non-static data members directly?**

No. A static member function has no `this` pointer, so it has no object to apply a
non-static member to; it can only touch non-static members through an object it is
explicitly given (e.g. a parameter). It can freely access static data members and other
static members.

#### Q47

**Why does a static member function not have a `this` pointer?**

Because it belongs to the class, not to any particular object, and can be called with just
`Class::f()` without an instance. With no associated object there is nothing for `this` to
point to. This is also why static member functions cannot be `const`, `virtual`, or
`volatile`.

#### Q48

**What is the difference between a member function defined inside and outside the class
definition?**

A function defined inside the class body is implicitly `inline`, which allows its
definition to appear in multiple translation units via the header. A function merely
declared in the class and defined out of line (`ret Class::f() {...}`) is not inline by
default and should normally be defined once in a `.cpp` file. Semantically they behave
identically; the difference is linkage/inlining and where the body lives.

## Inheritance and polymorphism

#### Q49

**What is inheritance?**

Inheritance lets a derived class acquire the members and interface of a base class,
modeling an "is-a" relationship and enabling code reuse and polymorphism. The derived
object contains a base subobject. It should be used for genuine substitutability; for mere
code reuse composition is often better.

#### Q50

**What is the difference between public, protected, and private inheritance?**

`public` inheritance models "is-a" and keeps base access levels as-is, so a `Derived*`
converts to `Base*`. `protected` inheritance makes the base's public members protected in
the derived class, and `private` inheritance makes them private; both model
"implemented-in-terms-of" rather than "is-a" and do not allow public conversion to the
base. Private inheritance is roughly composition with extra access to protected members
and the ability to override virtuals.

#### Q51

**What is runtime polymorphism?**

It is choosing the function to call at run time based on the dynamic type of the object,
achieved via virtual functions called through a base pointer or reference. The same call
expression (`p->draw()`) can invoke different derived overrides depending on what `p`
actually points to. It trades a small indirection cost for extensibility.

#### Q52

**What is compile-time polymorphism?**

It is selecting behavior at compile time, via function/operator overloading and templates
(including CRTP). There is no runtime dispatch cost and types are resolved statically,
enabling inlining, but the set of types must be known at compile time. Templates provide
"static" interfaces checked at instantiation.

#### Q53

**What is the purpose of a virtual function?**

A virtual function enables runtime polymorphism: a call through a base pointer/reference
dispatches to the most-derived override for the actual object type. It lets base-class
code call into derived behavior without knowing the concrete type. Overriders should match
the signature; use `override` to have the compiler check that.

#### Q54

**What is a pure virtual function?**

A pure virtual function is declared with `= 0` and has no required implementation in the
base (though one may be provided); it defines an interface that derived classes must
override. Declaring one makes the class abstract so it cannot be instantiated directly.
Derived classes remain abstract until they override all pure virtuals.

#### Q55

**What is an abstract class?**

An abstract class is one that has at least one pure virtual function (directly or
inherited and not overridden) and therefore cannot be instantiated on its own. It is used
purely as an interface/base type; you can still have pointers and references to it and it
can contain data and non-virtual logic. Concrete derived classes override the remaining
pure virtuals.

#### Q56

**Can an abstract class have constructors and destructors?**

Yes. An abstract class can (and often should) have constructors to initialize its data
members and a destructor, usually virtual; the constructors are invoked as part of
constructing derived objects even though the abstract class itself cannot be instantiated.
Calling a pure virtual function from within its constructor/destructor, however, does not
dispatch to derived overrides and may be UB if it is pure with no definition.

#### Q57

**What is object slicing?**

```cpp
class Base {};
class Derived : public Base {};

Derived d;
Base b = d;
```

Slicing happens when a derived object is copied into a base-class object by value: only
the base subobject is copied and the derived-specific members (and dynamic type) are lost.
The result is a genuine `Base`, so virtual calls resolve to `Base`, not `Derived`. It
silently discards state, which is why passing polymorphic types by value is a bug.

#### Q58

**How can object slicing be avoided?**

Handle polymorphic objects through base-class pointers or references (including smart
pointers) rather than by value, so the dynamic type is preserved. Pass parameters as
`const Base&` or `Base*`, store `std::unique_ptr<Base>` in containers, and consider
disabling copying of the base or making it abstract. A virtual `clone()` is the right way
to copy polymorphically.

#### Q59

**What is the difference between overriding and overloading?**

Overloading is providing multiple functions with the same name but different parameter
lists in the same scope, resolved at compile time by argument types. Overriding is a
derived class redefining a base class's virtual function with the same signature, resolved
at run time by dynamic type. A different signature in a derived class hides rather than
overrides the base function.

#### Q60

**What is the purpose of the `override` keyword?**

`override` tells the compiler that the function is intended to override a base-class
virtual; if it does not actually match a base virtual (typo, wrong signature, missing
`const`), compilation fails. It documents intent and prevents silent "hiding" bugs. It has
no runtime cost.

#### Q61

**What is the purpose of the `final` keyword?**

`final` on a virtual function forbids further overriding in derived classes; on a class it
forbids deriving from that class at all. It expresses design intent and can enable
devirtualization optimizations because the compiler knows no further override exists.
Attempting to override/derive past `final` is a compile error.

#### Q62

**What happens in this example?**

```cpp
class Base {
public:
    virtual void run();
};

class Derived : public Base {
public:
    void run() override;
};

Base* ptr = new Derived;
ptr->run();
```

Because `run` is virtual and the dynamic type is `Derived`, the call dispatches to
`Derived::run()` through the vtable. The static type of `ptr` (`Base*`) only determines
what names are visible, not which override runs. (Note the code leaks the object and
`Base` lacks a virtual destructor, so deleting through `ptr` would be UB.)

#### Q63

**What is the difference between a virtual function and a pure virtual function?**

A virtual function has an implementation in the base and may be overridden; the base is
still instantiable. A pure virtual function (`= 0`) makes the class abstract and generally
must be overridden by concrete derived classes, although it may optionally have a
definition that overrides can call. In short: virtual = overridable with a default; pure
virtual = required interface, class becomes abstract.

#### Q64

**What is the typical memory cost of adding virtual functions to a class?**

Adding the first virtual function typically adds one vtable pointer per object (commonly 8
bytes on 64-bit), regardless of how many virtual functions you add. There is also one
shared vtable per class (not per object) in static storage. Per call there is an extra
indirection and it inhibits inlining unless the compiler can devirtualize.

#### Q65

**What is a vtable and what is a vptr?**

A vtable (virtual table) is a per-class, compiler-generated array of function pointers to
the class's virtual function overrides. A vptr is a hidden per-object pointer to its
class's vtable, set during construction. A virtual call reads the vptr, indexes the
vtable, and jumps. These are an implementation technique, not mandated by the standard.

#### Q66

**Is the existence and exact layout of a vtable guaranteed by the C++ standard?**

No. The standard only specifies the observable behavior of virtual dispatch; it never
mentions vtables or vptrs. They are the common implementation (Itanium/MSVC ABIs) but the
exact mechanism, layout, and size are implementation-defined. Portable code must not
depend on vtable layout or on a vptr being at a particular offset.

#### Q67

**What is the diamond inheritance problem?**

When a class derives from two classes that both derive from a common base, the
most-derived object ends up with two separate copies of that base subobject. This causes
ambiguity when referring to the base's members and duplicated state. For example
`D : B, C` where both `B` and `C` derive from `A` gives `D` two `A` subobjects.

#### Q68

**How does virtual inheritance solve the diamond problem?**

Declaring the intermediate inheritance `virtual` (e.g. `class B : virtual A`) makes all
paths share a single common base subobject, eliminating duplication and ambiguity. The
most-derived class is then responsible for initializing the virtual base directly. The
trade-off is extra indirection to locate the shared base and more complex construction
order.

## Object lifetime

#### Q69

**What is an object's lifetime?**

An object's lifetime begins when its storage is obtained and its initialization (e.g.
constructor) completes, and ends when its destructor starts (or storage is
released/reused). Using an object outside this window (before construction or after
destruction) is undefined behavior. Lifetime, storage duration, and scope are related but
distinct concepts.

#### Q70

**What is the difference between automatic, static, thread-local, and dynamic storage
duration?**

Automatic objects (locals, non-static) live from their definition to the end of the
enclosing block and are destroyed in reverse order. Static objects live for the whole
program (constructed at or before first use, destroyed at exit). Thread-local objects have
one instance per thread, living for that thread's duration. Dynamic objects (`new`,
allocators) live until explicitly freed, so the programmer controls their lifetime.

#### Q71

**What is RAII?**

Resource Acquisition Is Initialization: tie a resource's lifetime to an object's lifetime
by acquiring it in the constructor and releasing it in the destructor. Because destructors
run deterministically at scope exit (including during stack unwinding), resources are
released automatically and exactly once. It is the foundation of exception-safe, leak-free
C++ (`std::lock_guard`, `std::unique_ptr`, `std::fstream`).

#### Q72

**Why is RAII especially useful in exception-safe C++ code?**

When an exception propagates, the stack unwinds and destructors of all fully-constructed
automatic objects run, so RAII guarantees resources are released even on the error path
without explicit `try/catch` cleanup. This makes it easy to achieve the basic and strong
exception-safety guarantees. Manual acquire/release code, by contrast, leaks whenever an
exception skips the release.

#### Q73

**What happens when an object is created using `new`?**

`new` does two things: it calls `operator new` to allocate raw storage, then constructs
the object in that storage, returning a typed pointer. If the constructor throws, the
matching `operator delete` is called to free the storage so there is no leak. The object
then has dynamic storage duration and lives until a matching `delete`.

#### Q74

**What is the difference between `new/delete` and `new[]/delete[]`?**

`new`/`delete` create and destroy a single object; `new[]`/`delete[]` create and destroy
an array, constructing/destroying each element and tracking the element count. You must
pair them correctly: `delete` on array-`new` or `delete[]` on scalar-`new` is undefined
behavior. In modern C++ prefer `std::vector` or `std::make_unique<T[]>` to avoid manual
array `new`.

#### Q75

**What is wrong with this code?**

```cpp
int* values = new int[10];
delete values;
```

The memory was allocated with `new[]` but freed with scalar `delete`, which is undefined
behavior; it must be `delete[] values;`. The mismatch can corrupt the heap or skip
per-element destruction for non-trivial types. The modern fix is
`std::vector<int> values(10);` or `auto values = std::make_unique<int[]>(10);`.

#### Q76

**What is the difference between a pointer and a reference as a data member?**

A pointer member can be null, reseated, and copied, and the default copy/assign work
(shallow), but it does not express ownership by itself. A reference member must be bound
in every constructor's initializer list, cannot be null or rebound, and makes the class
non-assignable by default (the implicit copy-assignment is deleted). Prefer a pointer (or
smart pointer) when you need optional, rebindable, or owning semantics.

#### Q77

**What is a dangling pointer?**

A dangling pointer is one that still holds the address of an object whose lifetime has
ended (freed memory, a destroyed local, or a reallocated container's old buffer).
Dereferencing or deleting it is undefined behavior. Setting freed pointers to `nullptr`
and using smart pointers/ownership discipline avoids them.

#### Q78

**What is a dangling reference?**

A dangling reference is a reference bound to an object that no longer exists, such as a
reference returned to a local or temporary. Using it is undefined behavior just like a
dangling pointer. A classic case is returning `const T&` to a local variable or temporary.

#### Q79

**What is use-after-free?**

Use-after-free is accessing memory through a pointer/reference after the underlying object
has been freed or destroyed. It is undefined behavior and a serious security vulnerability
(it can be exploited to corrupt state or hijack control flow). Tools like AddressSanitizer
detect it; RAII and smart pointers prevent most cases.

#### Q80

**Why should owning raw pointers generally be avoided in modern C++?**

Because manual ownership via raw pointers is error-prone: leaks, double frees,
use-after-free, and unclear ownership, especially under exceptions. Smart pointers
(`unique_ptr`, `shared_ptr`) encode ownership in the type system, release automatically,
and are exception-safe. Raw pointers/references are fine as non-owning observers.

#### Q81

**When would you use `std::unique_ptr`?**

When you need exclusive, single-owner ownership of a heap resource with automatic release
and zero runtime overhead compared to a raw pointer. It is move-only (no copying),
expresses transfer of ownership clearly, and supports custom deleters. It is the default
smart pointer; reach for it unless shared ownership is genuinely required.

#### Q82

**When would you use `std::shared_ptr`?**

When ownership is genuinely shared and the resource must live until the last owner is
gone, with lifetime decided at run time (e.g. shared nodes in a graph, objects captured by
multiple async tasks). It uses atomic reference counting, so it has more overhead than
`unique_ptr` and should not be the default. Prefer `make_shared` and pass by `const&`
where you are not sharing ownership.

#### Q83

**What problem does `std::weak_ptr` solve?**

It provides a non-owning observer of a `shared_ptr`-managed object that does not keep it
alive, letting you break reference cycles and safely detect whether the object still
exists via `lock()`. It is used for back-pointers in parent/child graphs and caches. You
must `lock()` it into a `shared_ptr` before use.

#### Q84

**What is a cyclic ownership problem?**

When two or more `shared_ptr`s refer to each other in a cycle, their reference counts
never reach zero, so the objects are never destroyed and leak. The fix is to make one
direction of the cycle a non-owning `std::weak_ptr` (or a raw pointer) so ownership is not
circular. This is the main reason `weak_ptr` exists.
