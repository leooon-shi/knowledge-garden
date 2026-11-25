
<!-- docs/cpp/cpp_class_interview.md -->

## C++ class and object interview questions

Answers: [`cpp_class_interview_answers.md`](cpp_class_interview_answers.md) (same
numbering; `Q42` is at `#q42`. The code-based questions keep their answers inline.)

### Basic concepts

1. What is the difference between a class and an object?

2. What is the difference between `class` and `struct` in C++?

3. What are the default access specifiers for `class` and `struct`?

4. What is the difference between a data member and a member function?

5. Can a class be empty? What is the typical size of an empty class, and why? (See
    Question 8 below.)

6. Can an object be created without explicitly defining a constructor?

7. What is the difference between a declaration and a definition of a class member
    function?

8. What is the purpose of the `this` pointer?

9. Can `this` be null inside a non-static member function?

10. What is the difference between static and non-static data members?

### Constructors and destructors

11. What is a constructor? Can it return a value?

12. What is a default constructor?

13. What is the difference between these constructors?

```cpp
A();
A(int x);
A(const A& other);
A(A&& other);
```

14. In what order are data members initialized?

```cpp
class Example {
    int first;
    int second;

public:
    Example()
        : second(2), first(1) {}
};
```

15. What is the difference between initialization and assignment?

16. Why should members generally be initialized using a member-initializer list?

17. What happens if a `const` data member is not included in the constructor's initializer
    list?

18. What happens if a reference data member is not initialized?

19. In what order are constructors and destructors called in an inheritance hierarchy?

20. Why should a base class destructor often be virtual?

21. What happens when a constructor throws an exception?

22. Can a constructor be virtual? Why or why not?

23. Can a destructor be pure virtual?

### Copying and moving

24. What is the difference between a shallow copy and a deep copy?

25. What are the copy constructor and copy-assignment operator?

26. What are the move constructor and move-assignment operator?

27. What is the Rule of Three?

28. What is the Rule of Five?

29. What is the Rule of Zero?

30. What does `= default` mean?

31. What does `= delete` mean?

32. What is wrong with this class?

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

33. What happens if an object of `Buffer` is copied?

34. How would you make the previous class safe?

35. When is `std::move` actually moving an object?

36. What happens when a moved-from object is used?

37. Why is a moved-from object still required to be valid?

### Access control and member functions

38. What is the difference between `public`, `protected`, and `private`?

39. Can a derived class access private members of its base class?

40. What is a friend function?

41. What are the advantages and disadvantages of using `friend`?

42. What is a `const` member function?

```cpp
class User {
public:
    int id() const;
};
```

43. Can a `const` member function modify a data member?

44. What is `mutable` used for?

45. What is the difference between these functions?

```cpp
void process();
void process() const;
```

46. Can a static member function access non-static data members directly?

47. Why does a static member function not have a `this` pointer?

48. What is the difference between a member function defined inside and outside the class
    definition?

### Inheritance and polymorphism

49. What is inheritance?

50. What is the difference between public, protected, and private inheritance?

51. What is runtime polymorphism?

52. What is compile-time polymorphism?

53. What is the purpose of a virtual function?

54. What is a pure virtual function?

55. What is an abstract class?

56. Can an abstract class have constructors and destructors?

57. What is object slicing?

```cpp
class Base {};
class Derived : public Base {};

Derived d;
Base b = d;
```

58. How can object slicing be avoided?

59. What is the difference between overriding and overloading?

60. What is the purpose of the `override` keyword?

61. What is the purpose of the `final` keyword?

62. What happens in this example?

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

63. What is the difference between a virtual function and a pure virtual function?

64. What is the typical memory cost of adding virtual functions to a class?

65. What is a vtable and what is a vptr?

66. Is the existence and exact layout of a vtable guaranteed by the C++ standard?

67. What is the diamond inheritance problem?

68. How does virtual inheritance solve the diamond problem?

### Object lifetime

69. What is an object's lifetime?

70. What is the difference between automatic, static, thread-local, and dynamic storage
    duration?

71. What is RAII?

72. Why is RAII especially useful in exception-safe C++ code?

73. What happens when an object is created using `new`?

74. What is the difference between `new/delete` and `new[]/delete[]`?

75. What is wrong with this code?

```cpp
int* values = new int[10];
delete values;
```

76. What is the difference between a pointer and a reference as a data member?

77. What is a dangling pointer?

78. What is a dangling reference?

79. What is use-after-free?

80. Why should owning raw pointers generally be avoided in modern C++?

81. When would you use `std::unique_ptr`?

82. When would you use `std::shared_ptr`?

83. What problem does `std::weak_ptr` solve?

84. What is a cyclic ownership problem?

## Code-based interview questions

### Question 1: initialization order

What does this print?

```cpp
#include <iostream>

class Example {
    int first;
    int second;
public:
    Example()
        : second(2), first(second) {}
    void print() const {
        std::cout << first << ' ' << second << '\n';
    }
};

int main() {
    Example e;
    e.print();
}
```

What should the candidate notice?

<details>
<summary>Expected discussion</summary>

Members are initialized in the order of declaration, not the order in the initializer
list. `first` is initialized before `second`, so `first(second)` uses `second` before it
has been initialized. This is a bug and may produce an indeterminate value.

</details>

### Question 2: virtual destructor

What is wrong with this code?

```cpp
class Base {
public:
    ~Base() = default;
};

class Derived : public Base {
    int* data;

public:
    Derived()
        : data(new int[10]) {}

    ~Derived() {
        delete[] data;
    }
};

int main() {
    Base* object = new Derived;
    delete object;
}
```

<details>
<summary>Expected discussion</summary>

Deleting a derived object through a base pointer requires a virtual destructor in the base
class:

```cpp
class Base {
public:
    virtual ~Base() = default;
};
```

Without it, the behavior is undefined.

</details>

### Question 3: copy constructor

What problem exists here?

```cpp
class Resource {
    int* value;

public:
    explicit Resource(int x)
        : value(new int(x)) {}

    ~Resource() {
        delete value;
    }
};
```

<details>
<summary>Expected discussion</summary>

The compiler-generated copy constructor performs a shallow copy. Two objects then own the
same pointer, resulting in double deletion.

Possible solutions include:

- Implementing deep copy and move operations
- Deleting copying
- Replacing the raw pointer with `std::unique_ptr<int>`

</details>

### Question 4: temporary lifetime

Is this safe?

```cpp
const std::string& get_name() {
    return std::string("temporary");
}
```

<details>
<summary>Expected discussion</summary>

No. The temporary string is destroyed when the function returns, leaving a dangling
reference. Return by value instead:

```cpp
std::string get_name() {
    return "temporary";
}
```

</details>

### Question 5: `const` correctness

Which functions can be called here?

```cpp
class Counter {
    int value{};

public:
    int get() const {
        return value;
    }

    void increment() {
        ++value;
    }
};

const Counter a;
Counter b;

a.get();
a.increment();

b.get();
b.increment();
```

<details>
<summary>Expected discussion</summary>

`a.get()` is valid because `get()` is `const`. `a.increment()` is invalid because it
modifies the object. Both functions can be called on `b`.

</details>

### Question 6: object slicing

What is printed?

```cpp
#include <iostream>

class Base {
public:
    virtual void print() const {
       std::cout << "Base\n";
    }
};

class Derived : public Base {
public:
    void print() const override {
        std::cout << "Derived\n";
    }
};

void print_by_value(Base object) {
    object.print();
}

void print_by_reference(const Base& object) {
    object.print();
}

int main() {
    Derived d;

    print_by_value(d);
    print_by_reference(d);
}
```

<details>
<summary>Expected discussion</summary>

The output is:

Base Derived

Passing by value slices the `Derived` part. Passing by reference preserves polymorphism.

</details>

### Question 7: defaulted move operations

What can happen when a class contains a `std::unique_ptr`?

```cpp
class Owner {
    std::unique_ptr<int> value;

public:
    Owner()
        : value(std::make_unique<int>(42)) {}
};
```

<details>
<summary>Expected discussion</summary>

`Owner` is non-copyable because `std::unique_ptr` is non-copyable. It is movable, assuming
the relevant special member functions are generated or explicitly enabled.

</details>

### Question 8: size of an empty class

What do these print on a typical 64-bit platform, and why?

```cpp
#include <iostream>

struct Empty {};

struct WithVirtualFunction {
    virtual void f();
};

struct Derived : Empty {
    int value;
};

struct Holder {
    [[no_unique_address]] Empty tag;
    int value;
};

int main() {
    std::cout << sizeof(Empty) << '\n';
    std::cout << sizeof(WithVirtualFunction) << '\n';
    std::cout << sizeof(Derived) << '\n';
    std::cout << sizeof(Holder) << '\n';
}
```

<details>
<summary>Expected discussion</summary>

Typical output is `1`, `8`, `4`, `4`. The exact sizes are implementation-defined.

**`sizeof(Empty)` is at least 1.** Each complete object must have a unique address. If
`Empty` had size zero, the elements of `Empty objects[2]` would share an address, because
advancing by the element size would advance by zero. The byte stores no data; it gives the
object an address and makes arrays of the type work.

```cpp
static_assert(sizeof(Empty) >= 1);
```

**A class with no data members can still need storage.** `WithvirtualFunction` declares no
data, but each object holds a vtable pointer, commonly 8 bytes on 64-bit systems.
Alignment and other members can also make a class larger.

**Empty base optimization (EBO).** `Empty` on its own has size 1, but as a base class it
may occupy no extra space, so `sizeof(Derived) == sizeof(int)` is commonly true.

**`[[no_unique_address]]` (C++20).** An empty *member* normally costs at least one byte
plus padding. The attribute lets the compiler place `tag` in padding or at the same
address as another member when allowed, so `Holder` can be the size of an `int`.

Summary:

- Ordinary empty object: at least one byte.
- Empty base class: may add zero bytes.
- `[[no_unique_address]]` empty member: may add zero bytes.
- Virtual functions, alignment, or other members can make the class larger.

*Follow-up:* Why do standard containers and `std::unique_ptr` care about this? Their
allocator and deleter are usually empty types; EBO or `[[no_unique_address]]` keeps them
from adding size to every container or smart pointer.

</details>

## Suggested assessment format

For each practice session:

- Answer 5 conceptual questions without looking up the answers.
- Solve 2 code-tracing questions.
- Implement 1 small class.
- Compile with warnings enabled:

```bash
g++ -std=c++20 -Wall -Wextra -Wpedantic -Wconversion \
    -fsanitize=address,undefined \
    source.cpp
```

- Explain:
  - Who owns each resource?
  - When is each object constructed?
  - When is each object destroyed?
  - Can the class be copied?
  - Can it be moved?
  - Is it safe under exceptions?
  - Is polymorphism involved?

A strong interview candidate should be able to explain not only whether code works, but
also object lifetime, ownership, exception safety, copying behavior, and the reason behind
the design.
