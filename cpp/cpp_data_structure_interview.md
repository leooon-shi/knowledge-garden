<!-- docs/cpp/cpp_data_structure_interview.md -->

## C++ standard container underlying-structure interview questions

Answers:
[`cpp_data_structure_interview_answers.md`](cpp_data_structure_interview_answers.md) (same
numberings; `Q42` is at `#q42`, code question 3 at `#c3`, final question 2 at `#f2`.)

## General container concepts

1. Does the C++ standard mandate the exact data structure used to implement each
 container?

2. What is the difference between sequence containers, associative containers, unordered
 associative containers, and container adaptors?

3. How do contiguous storage, node-based storage, and segmented storage differ?

4. What factors determine whether a container provides good cache locality?

5. Why can a container with better asymptotic complexity still perform worse in practice?

6. What is the difference between size, capacity, and maximum size?

7. Which containers store elements directly, and which usually storg nodes connected
 through pointers?

8. What does allocator support mean for standard containers?

9.  Why can dynamic allocation and pointer indirection affect performance?

10. What properties should you consider when selecting a container?

- Access pattern
- Insertion and deletion location
- Lookup requirements
- Memory usage
- Iterator stability
- Cache locality
- Ordering requirements
- Thread-access pattern

## `std::array`

11. What is the underlying structure of `std::array`?

12. How is 1std::array<T, N>` different from a built-in array?

13. Does `std::array` perform dynamic memory allocation?

14. Where are the elements of a local `std::array` usually stored?

15. What is the effect of copying a `std::array`?

16. What happens when `N` is zero?

17. Why can `std::array` provide better type safety than a built-in array?

18. Compare the memory layout of these types:

```cpp
std::array<int, 4> a;
int b[4];
```

19. When would `std::array` be preferable to `std::vector`?

20. How does the size of an `std::array` affect stack usage and object layout?

## `std::vector`

21. What is the usual underlying structure of std::vector?

Expected answer: a dynamically allocated contiguous array.

22. What information does a typical std::vector object store?

Usually:

- Pointer to the allocated storage
- Current size
- Current capacity

The exact representation is implementation-dependent.

23. What is the difference between `size()` and `capacity()`?

24. Why does `std::vector` need to reallocate?

25. What happens internally when a vector exceeds its capacity?

   Typical sequence:

	1. Allocate a larger memory block.
	2. Move or copy existing elements.
	3. Construct the new element.
	4. Destroy old elements if necessary.
	5. Release the old storage.

26. Why is `push_back` usually amortized constant time?

27. What is the difference between `reserve()` and `resize()`?

28. Does calling `reserve()` construct elements?

29. Does calling `resize()` change capacity?

30. When does vector growth invalidate pointers, references, and iterators?

31. When does inserting into the middle of a vector invalidate iterators?

32. Why is insertion at the beginning of a vector expensive?

33. Why is vector iteration usually cache-friendly?

34. What are the advantages of vector's contiguous storage?

- Constant-time random access
- Good spatial locality
- Efficient interaction with CAPIs
- Efficient sequential iteration
- Possible SIMD-friendly access

35. What are the disadvantages of vector's contiguous storage?

- Reallocation can be expensive.
- Insertions and removals in the middle require shifting elements.
- References and iterators may be invalidated.
- Large reallocations can temporarily require additional memory.

36. What is the difference between `clear()`, `erase()`, `resize(0)`, and
    `shrink to_fit()`?

37. Does `clear()` release a vector's allocated memory?

38. Is `shrink_to_fit()` guaranteed to reduce capacity?

39. Why can `std::vector<bool>` behave differently from other vector specializations?

Expected points:

- It is commonly implemented as a packed bit representation.
- `operator[]` does not return a normal `bool&`.
- It may use proxy reference objects.
- It saves memory but can have different performance and API behavior.

40. When would `std::vector` be preferable to `std::list` even for frequent insertions?

## `std::deque`

41. What is the usual underlying structure of `std::deque`?

Expected answer: segmented storage, commonly implemented using multiple fixed-size blocks
and an indexing structure.

The standard does not require one exact implementation.

42. Why does `std::deque` support efficient insertion at both ends?

43. Why is `std::deque` not generally stored as one contiguous memory block?

44. Does `std::deque` provide constant-time random access?

45. Why can deque random access be slower than vector random access?

46. How does deque's memory layout affect cache locality?

47. What happens when a deque grows at the front or back?

48. What is the difference between deque and vector iterator invalidation?

49. Does adeque guarantee that its elements remain at stable memory addresses?

Expected answer: no. Operations that modify the deque can invalidate references, pointers,
or iterators according to the operation and implementation rules.

50. When is `std::deque` preferable to `std::vector`?

51. Why is deque commonly useful for queue and sliding-window workloads?

52. Compare these two designs:

```cpp
std::deque<Packet> packets;
std::vector<Packet> packets;
```

Which is more appropriate when packets are frequently added and removed from both ends?

## `std::list`

53. What is the usual underlying structure of std::list?

Expected answer: a doubly linked list, with one node per element.

54. What does a typical list node contain?

Usually:

- The stored value
- Pointer to the previous node
- Pointer to the next node

The exact representation is implementation-dependent.

55. Why can `std::list` insert or erase an element in constant time when an iterator is
    already available?

56. Why does finding the insertion position in a list still take linear time?

57. Why does `std::list` have poor cache locality?

58. What is the memory overhead of using a linked-list noder

59. Why can `std::list` be slower than `std::vector` even when insertion complexity is
    better?

Expected points:

- Pointer chasing
- Poor cache locality
- Separate allocations
- Allocation and deallocation overhead
- Larger memory footprint
- More cache and TLB misses

60. Are list iterators invalidated when another element is inserted?

61. What happens to iterators referring to erased list elements?

62. Why does `std::list::splice()` often avoid moving or copying element values?

63. What are the requirements for efficient `splice()`?

64. When is `std::list` a good choice?

65. When should `std::list` generally be avoided?

66. Compare `std::list` with std::ivector" for a workload that repeatedly scans all
    elements.

## `std::forward_list`

67. What is the underlying structure of `std::forward_list`?

Expected answer: a singly linked list.

68. How does `std::forward_list` differ from `std::list`?

- One link instead of two
- Lower per-node memory overhead
- Forward iteration only
- No constant-time operation at the end
- No bidirectional iterators
- Uses `before_begin()` for certain operations

69. Why does `forward_list` not provide `size()` in the same way as most containers?

70. Why is `insert_after()` used instead of ordinary `insert()` ?

71. When could a singly linked list be preferable to a doubly linked list?

72. Why can `forward_list` still perform poorly for cache-intensive workloads?

73. What is the purpose of `before_begin()`?

## `std::map` and `std::set`

74. What is the usual underlying structure of `std::map` and `std::set`?

Expected answer: a balanced search tree, commonly a red-black tree.

The standard specifies complexity and behavior, not the exact tree type.

75. Why does a map need a balanced tree?

76. What is the typical complexity of lookup, insertion, and deletion in a map?

Expected answer: logarithmic complexity.

77. What is stored in a `std::map` node?

Typically:
- A key-value pair
- Tree links
- Some balancing information
- Possibly allocator or implementation-specific metadata

78. Why is the key in `std::map` effectively immutable through its iterator?

79. Why would modifying a key directly break the tree's ordering?

80. How does a map maintain sorted order?

81. What is the role of the comparator in `std::map`?

82. What does strict weak ordering mean?

83. What can happen if a comparator does not provide a valid strict weak ordering?

84. Why can map lookup be slower than unordered-map lookup in practice?

85. Why might `std::map` have better worst-case guarantees than `std::unordered_map`?

86. How does a map node-based structure affect cache locality?

87. Are map iterators invalidated by inserting another element?

88. What happens to iterators when a map element is erased?

89. What is the difference between `std::map` and `std::multimap`?

90. What is the difference between `std::set` and `std::map`?

91. What is the difference between `std::map<key, bool> `and `std::set<key>`?

92. Why might `std::flat_map` be faster than `std::map` for some workloads?

For C++23 implementations that provide it, a flat map generally keeps keys and values in
sorted contiguous storage. Lookup can benefit from cache locality, but insertion and
deletion may require shifting elements.

## `std::unordered map` and `std::unordered_set`

93. What is the usual underlying structure of an unordered map?

Expected answer: a hash table.

94. What are the main components of a hash table?

- Bucket array
- Hash function
- Collision-resolution mechanism
- Stored keys and values
- Load-factor management

95. What is a bucket?

96. How is a key mapped to a bucket?

97. What is a hash collision?

98. How can hash tables resolve collisions?

Possible techniques include:

- Separate chaining
- Open addressing
- Robin Hood hashing
- Other implementation-specific strategies

The C++ standard does not require one specific strategy.

99. What is the average complexity of unordered-map lookup?

100. What is the worst-case complexity of unordered-map lookup?

101. What is the load factor?

102. What do `bucket_count()` and `max_load_factor()` represent?

103. What happens during `rehash()`?

104. When does an unordered map rehash automatically?

105. Which operations can invalidate unordered-map iterators?

106. Why can rehashing be expensive?

107. How can `reserve()` improve unordered-map performance?

108. Why does a poor hash function reduce performance?

109. What properties should a good hash function have?

- Equal keys must produce equal hash values.
- It should distribute keys effectively.
- It should be sufficiently fast.
- It should avoid excessive clustering.

110. Why can an unordered map use more memory than a vector?

111. Why might an unordered map still be slower than a vector for small collections?

Expected points:

- Hash computation
- Bucket lookup
- Pointer indirection
- Poor Locality
- Allocation overhead
- Branches and collision handling

112. When is `std::unordered_map` preferable to `std::map`?

113. When might `std::map` be preferable to `std::unordered_map`?

114. What happens if the key type has an inconsistent `operator==` and hash function?

115. Why must equivalent keys produce equivalent hash values?

116. How can adversarial input affect hash-table performance?

## Container adaptors

117. What is a container adaptor?

118. Which standard container adaptors are commonly used?

- `std::stack`
- `std::queue`
- `std::priority_queue`

119. What container is commonly used internally by `std::stack`?

120. What container is commonly used internally by `std::queue`?

121. What container is commonly used internally by `std::priority_queue`?

122. Why is `std::priority_queue` usually implemented using a heap?

123. What is the difference between a heap and a binary search tree?

124. Why does `priority_queue` provide access only to the highest-priority element?

125. What are the typical complexity characteristics of `priority_queue`?

126. Can a different underlying container be supplied to an adaptor?

127. What requirements must the underlying container satisfy for `std::stack`?

128. Why is std::vector usually a suitable underlying container for
    `std::priority_queue` ?

129. Why might `std::deque` be used instead of `std::vector` for a stack or queue?

## `std::string`

130. What is the usual storage model of `std::basic_string`?

131. What is small-string optimization?

Expected answer: short strings may be stored inside the string object itself instead of
using a heap allocation.

132. Is small-string optimization required by the C++ standard?

Expected answer: no. It is a common implementation optimization.

132a. What is the "SSO threshold"?

Expected answer: it is the maximum string length (in characters) that an implementation
can store **inside the string object itself**, in its small inline buffer, before it must
fall back to a separate heap allocation. Strings up to the threshold cause **zero heap
allocation**; strings longer than it allocate one heap block.

- The exact value is **implementation-defined** because it dapends on the object's size
  and bit-layout. Typical values on 64-bit:
  - libstdc++ (GCC): **15 bytes**
  - libc++ (Clang/LLVM): **22 bytes** (tighter bit-packing)
  - MSVC STL: **15 bytes**
- The standard does not mandate SSO at all, let alone a specific threshold, so code must
  never assume a particular value.
- Practical consequence: below the threshold, construction, copy, and destruction touch
  only the object's own bytes no allocator call, good cache ocality above it, those
  operations may hit the heap.

133. How does small-string optimization affect allocation and performance?

134. Why can the size of `std::string` be larger than expected?

135. What happens when a string exceeds its current capacity?

136. When can string operations invalidate pointers, references, and iterators?

137. Does `std::string::reserve()` guarantee a particular growth strategy?

138. Why should code not depend on the exact string growth factor?

139. Compare these two representations:

```cpp
std::vector<std::string> names;
std::vector<std::string_view> names;
```
Discuss:

- Ownership
- Allocation
- Lifetime
- Indirection
- Cache behavior

140. What is the difference between `std::string`, `std::string_view`, and a C-style
     string?

## Views and non-owning structures

141. What is the underlying structure of `std::span`?

142. Does `std::span` own its elements?

143. What information does a typical dynamic-extent `std::span` contain?

Usually:

- Pointer to the first element
- Number of elements

144. Why can `std::span` improve API flexibility?

145. What lifetime problems can occur when returning a span?

146. How is `std::span` different from `std::vector`?

147. What is the difference between `std::span<T>` and `std::span<const T>`?

148. How does a `std::string_view` differ from an owning string?

149. Why can a view improve performance while introducing lifetime risks?

## Iterator and invalidation questions

150. What is an iterator abstraction?

152. What are the main iterator categories?

- Input
- Output
- Forward
- Bidirectional
- Random access
- Contiguous

153. Which standard containers provide contiguous iterators?

Typically:

- `std::array`
- `std::vector`
- `std::string`

154. Why is a list iterator not a random-access iterator?

155. What operations are possible with a random-access iterator that are not possible with a
     list iterator?

156. What does iterator invalidation mean?

157. Why is iterator invalidation important when modifying a vector?

158. Compare iterator invalidation behavior for:

- `std::vector`
- `std::deque`
- `std::list`
- `std::map`
- `std::unordered_map`

158. Why should an iterator not be used after its element has been erased?

159. What is the erase-return idiom?

```cpp
for (auto it = values. begin(); it != values.end();) {
	if (should_remove(*it)) {
		it = values. erase(it);
	} else {
		++it;
	}
}
```

160. Why can storing pointers or references to vector elements be dangerous?

161. How can `reserve()` reduce vector iterator invalidation?

## Memory and performance questions

162. Why does a node-based container usually require more memory than a contiguous
     container?

163. What is pointer chasing?

164. How does pointer chasing affect CPU cache utilization?

165. Why does a contiguous container usually work well with hardware prefetching?

166. What is spatial locality?

167. What is temporal locality?

168. How can object size affect the number of elements loaded per cache line?

169. Can reducing object size ever reduce performance?

Expected answer: yes. For example, removing padding between frequently modified fields can
increase false sharing.

170. What is false sharing in a container?

171. How can a vector of objects differ from a vector of pointers in cache behavior?

172. When might a vector of pointers be useful despite worse locality?

Possible reasons:

- stable object addresses
- Polymorphic objects
- Large objects
- Separately managed lifetimes
- Avoiding expensive object movement

173. Why can frequent heap allocation dominate the performance of a list or map?

174. How can `std::pmr` resources improve container allocation behavior?

175. What is the purpose of `std::pmr::monotonic_buffer_resource`?

176. What are the risks of using a monotonic memory resource?

177. How can object pooling improve node-based container performance?

178. What are the trade-offs of using a flat or sorted vector instead of a tree or hash table?

179. Why should container performance be measured using realistic data sizes and access
     patterns?

## Code-based interview questions

### Question 1: vector invalidation

what is wrong with this code?
```cpp
std::vector<int> values(1, 2, 3);

auto it = values.begin();
values.push_back(4);

std::cout < *it << '\n';
```

Expected discussion:

- `push_back()` may reallocate.
- A reallocation invalidates the iterator.
- Dereferencing `it` afterward may be undefined behavior.
- `reserve()` may avoid reallocation for a known capacity, but iterator validity still
  depends on later operations.

### Question 2: list complexity

Is this operation constant time?

```cpp
std::list<int> values;

auto it = std::find(values.begin(), values.end(), 42);
values.insert(it, 10);
```

Expected answer:

- The insertion itself is constant time once `it` is available.
- Finding `42` is linear time.
- The complete operation is therefore dominated by the search.

### Question 3: map versus unordered map

Choose a suitable container for each requirement:

1. Keys must remain sorted.
2. Average constant-time lookup is desired.
3. Worst-case logarithmic lookup is preferred.
4. Elements are scanned sequentially many times.
5. Elements are frequently inserted and removed from both ends.

Expected choices:

- Sorted keys: `std::map`
- Average constant-time lookup: `std::unordered_map`
- worst-case logarithmic lookup: `std::map`
- Sequential scanning: often `std::vector`, depending on insertion requirements
- Both-end insertion and removal: `std::deque`

### Question 4: vector of objects versus pointers

Compare:

```cpp
std::vector<Packet> packets;
```

with:

```cpp
std::vector<std::unique_ptr<packet>> packets;
```

Ask the candidate to discuss:

- Number of allocations
- Cache locality
- Pointer indirection
- object movement during reallocation
- Stable addresses
- Ownership
- Polymorphism
- Destruction cost

### Question 5: unordered-map rehashing

What can happen here?

```cpp
std::unordered_map<int, std::string> table;

auto it = table.begin();
table.reserve(10'000);

std::cout << it->first << '\n';
```

Expected discussion:

- `reserve()` can trigger rehashing.
- Rehashing invalidates iterators.
- The iterator must not be used afterward if invalidated.
- References and pointers to elements have different invalidation guarantees from
  iterators, so the candidate should distinguish them carefully.

### Question 6: map key modification

why is this not allowed?

```cpp
std::map<int, std::string> table;
table.emplace(1, "one");

auto it = table.begin();
// it->first = 2;
```

Expected answer:

- The key determines the node's position in the ordered tree.
- Changing it directly could violate the ordering invariant.
- The element should be erased and reinserted, or the node-handle interface can be used
  where appropriate.

### Question 7: node handles

What is the benefit of this C++17 feature?

```cpp
std::map<int, std::string> first;
std::map<int, std::string> second;

first.emplace(1, "one");

auto node = first.extract(1);
second.insert(std::move(node));
```

Expected discussion:

- The node can be transferred without copying or moving the stored value.
- Ownership of the node changes between containers.
- The destination must accept the key and comparator requirements.

### Question 8: dangling view

What is wrong with this function?

```cpp
std::string_view get_name() {
    return std::string("temporary");
}
```

Expected answer:

- The returned view refers to a temporary string.
- The string is destroyed when the function returns.
- The returned `string_view` becomes dangling.
- Return an owning `std::string` or refer to storage whose lifetime is guaranteed.

### Question 9: `std::list` and cache behavior

why might this loop be slower than expected?

```cpp
std::list<Packet> packets;

for (const auto& packet : packets) {
    process(packet);
}
```

Expected discussion:

- Each node may be separately allocated.
- Nodes may be distributed throughout memory.
- Pointer chasing causes cache and TLB misses.
- A `std::vector<Packet>` may perform better for sequential processing.

### Question 10: container selection

Design a container for a high-throughput packet-forwarding path with these requirements:

- Sequential packet processing
- Frequent append operations
- Occasional removal from the front
- Lookup by tunnel key
- Stable configuration entries
- Per-thread statistics

A strong answer should consider using different containers for different responsibilities,
for example:

- `std::vector` for compact, frequently scanned packet data
- `std::deque` or a ring buffer for front-and-back queue operations
- `std::unordered_map` for average constant-time key Lookup
- `std::map` if sorted traversal or ordered lookup is required
- Per-thread counters to reduce contention
- Separate hot forwarding data from cold configuration data

## Final assessment questions

1. Explain why the C++ standard specifies container behavior and complexity rather than
    always specifying the exact data structure.

2. Compare the memory layout of vector, deque, list, map, and unordered map.

3. Which container gives the strongest cache locality and why?

4. Which containers commonly allocate one node per element?

5. Why can `std::list` insertion be constant time but still slower than vector insertion?

6. How does reallocation affect vector performance and iterator validity?

7. How does rehashing affect unordered-container performance?

8. Why are map keys treated as immutable?

9.  How do container choices affect multithreaded false sharing?

10. Select containers for a packet-processing system and justify each choice based on:

- Access pattern
- Memory layout
- Cache locality
- Allocation behavior
- Iterator stability
- Lookup complexity
- Thread contention
- Measured performance
