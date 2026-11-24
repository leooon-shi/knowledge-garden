<!-- docs/cpp/hash_table_collision.md -->

# Hash Table Collisions

Hash table collisions occur when two different keys hash to the same index in the
underlying array.

## Why they happen

A hash function maps a potentially infinite key space to a finite number of buckets. By
the pigeonhole principle, collisions are inevitable once the number of keys exceeds the
number of buckets (and likely well before that).

*# Resolution strategies

Chaining (separate chaining)

- Each bucket holds a linked list (or other collection) of entries.
- On collision, the new entry is appended to the list at that index.
- Lookup walks the list, comparing keys.
- Degrades to o(n) per bucket in the worst case; o(1) average with a good hash and lowLoad factor.

open addressing

- All entries live in the array itself. on collision, probe for the next available slot.
- Common probing schemes:
- *Linear probing* - check index+1, index+2, -. (simple but prone to clustering)
- *Quadratic probing* - check index+12, index+2, - (reduces clustering)
- *Double hashing* - use a second hash function to determine the step size
- Deletion is trickier (requires tombstone markers or rehashing).

Robin Hood hashing

- A variant of open addressing where entries can be displaced if the new entry has a
- longer probe distance, reducing variance in lookup times.

## Mitigating collisions

- **Good hash function** - uniform distribution across buckets (e.g., MurmurHash, SipHash,
  xxHash).
- **Load factor management** - resize (typically double) the table when load factor
  exceeds a threshold (commonly 0.7- 0.75).
- **Prime-sized tables** - help distribute entries more evenly with certain hash functions.
- 
## Quick complexity summary

strategy	Average lookup	I Worst case lookup |
Chaining	----
0(1)	---o(n)
I open addressing	| 0(1)	10(n)

Both degrade to o(n) only under pathological hash functions or adversarial input. In
practice, with a decent hash and reasonable load factor, lookups stay near o(1).

Let me to dive deeper into any specific aspect - implementation details, a particular language's approach, or performance trade-offs?

> Here are examples in both languages showing collisions in action.

## C++

```cpp
#include <iostream>
#include < vector>
#include < list>
#include <string>

// Simple hash table with chaining to demonstrate collisions class HashTable { static
const int SIZE = 4; // intentionally small to force collisions
std:: vector/std:: list\std::pair\<std::string, int>> buckets;
int hash(const std: string& key) {
int sum = 0;
for (char c : key) {
Sum += c;
return sum% SIZE;
}

public: HashTable() : buckets(SIZE) +
void insert(const std: :string& key, int value) {
int idx = hash(key);
std:: cout « "Key \*" « key « "|" -> hash index " « idx « "\n"; buckets[idx]. emplace_back(key, value);

void print() {
for (int i = 0; i < SIZE; i++) { std:: cout « "Bucket " « i «": ";
for (auto& [k, v] : buckets[i]) {
std: cout « "[" « k«"-"«< v «"] ";
std:: cout « "\n";
｝
int main() { HashTable ht;
// These keys will collide because our hash is just sum of chars % 4
ht.insert("ab", 1); || 'a'(97) + 'b'(98) = 195, 195 % 4 = 3
ht.insert(ba", 2); // 'b' (98) + 'a'(97) = 195, 195 % 4 = 3 <- collision! ht.insert(" cd", 3); // 'c' (99) + "d(100) = 199, 199% 4 = 3 <- collision! ht.insert("he11o", 4);
std:: cout « "InFinal state: \n"; ht.print();
return ø;
..•

output: Key "ab" -> hash index 3 Key "ba" -> hash index 3 Key "cd" -> hash index 3 Key
"hello" -> hash index 0
Final state: Bucket 0: [hello=4] Bucket 1: Bucket 2: Bucket 3: [ab=1] [ba=2] [cd=3]
"ab" and "ba" collide because the naive hash (sum of ASCII values) is order-independent.
"cd" also lands in the same bucket because 99 + 100=199 which is also 3 mod 4.
##
Go
go package main
import "frt"
const tablesize = 4
type entry struct f key string value int }
type Hashtable struct {buckets [tableSize][Jentry }
func hash(key string) int { sum := @ for L, c:= range key { sum += int(c)} return sum % tablesize }
func (ht \*HashTable) insert(key string, value int) {idx := hash(key) fmt.Printf("Key %a
> hash index %d\In", key, idx)ht.buckets[idx] = append(ht.buckets[idx], entrykey,
value}) }
func (ht \*HashTable) print() { for i, bucket := range ht.buckets { fmt.Printf("Bucket %d: , i) for
}
L, e := range bucket { fmt.Printf("[%s=%d] ", e.key, e.value) } fmt.PrintIn() }
func main() {ht := HashTablef)
I
// Same demonstration: "ab" and "ba" have identical char sums
ht.insert("ab"
, 1)
// 97+98 = 195, 195%4 = 3
ht.insert("ba"
, 2)
// 98+97 = 195, 195%4 = 3 <- collision!

ht.insert ("cd", 3)
// 99+100 = 199, 199%A = 3 <- collision!
ht. insert ("hello", 4)
fit.Println("\nFinal state:")
ht.print()
Output: Key "ab" - hash index 3 Key "ba" -> hash index 2 Key "cd" - hash index 3 Key
"hello" -> hash index o
Final state: Bucket 0: [hello=4] Bucket 1: Bucket 2: [ba=2] Bucket 3: [ab=1] [cd=3]
Wait - Go's range over a string yields runes (same values here), but let me recheck: 195%
4 = 3 for both "ab" and "ba", so they do collide. The output above should show both at
index 3. (I corrected the output.)
Corrected output: Key "ab" -> hash index 3 Key "ba" - hash index 3 Key "cd"-> hash index
3 Key "hello" - hash index o
Final state: Bucket 0: [hello=4] Bucket 1: Bucket 2: Bucket 3: [ab=1] [ba=2] [cd=3]
## Key takeaway
The collision happens because the hash function loses information - it only considers the sum of character values, not their order or position. A better hash (like FNV, MurmurHash, or Go's built-in map hash) incorporates position and bit mixing to minimize this, but can never eliminate it entirely since you're mapping an infinite key space to a finite index range.
• Credits: 0.20 • Time: 20s
1% > and provide an example of Resolution strategies to preceding two snippets of code
I
> Here are both snippets extended with the two main resolution strategies.
## C++ - Chaining vs Open Addressing (Linear Probing)
con #include «iostream> #include <vector> #include <list> #include <string>
#include

<optional>
// shared hash function int hash(const std: string& key, int size) fint sum = 0; for
(char c : key) { sum += c; } return sum % size; }
//
// Strategy 1: Chaining //
—class Chainingtable {static const int SI
= 4; std: :vector\std: :list\std: pair\std: string, int>> buckets;
public: chainingTable() : buckets(SIZE) {}
void insert(const std: string& key, int value) {
int id = hash(key, SIZE);
// check if key already exists, update if so
for (auto& [k, v] : buckets[idx]) {
if (k == key) {
V = value;
return;
}
buckets[idx]. emplace_back(key, value);
}
std: :optional<int> lookup(const std: string& key) f
int id = hash (key, SIZE);
for (auto& [k, v] : buckets[idx]) {
if (k == key) return v;
}
return std:: nullopt;
}
void remove(const std: string& key) {
int id = hash(key, SIZE);
buckets [idx]. remove_if([&](auto& p) {return p.first = key; });
}
I
void print() {
for (int i = 0; i < SIZE; i++) {
std:: cout << " Bucket " «< i «<"; ";


for (auto& [k, v]: buckets [1]) {
std: cout « "[" « k «"=" « v « "]";
}
std: cout « "\n";
};
//
Il Strategy 2: Open Addressing (Linear
Probing) // -
— class OpenAddressingTable {
static const int SIZE = 8; // needs extra room for probing struct Slot { std: istring key;
int value = 0; bool occupied = false; bool deleted = false; // tombstone marker }:
std:: vector<Slot> table;
public: OpenAddressingTable() : table(SIZE) {}
void insert(const std: string& key, int value) {
int id = hash(key, SIZE);
int start = id;
while (true) {
if (!table[idx].occupied || table[idx].deleted) {
table[idx] = {key, value, true, false};
return;
}
if (table[idx].key == key) {
table[idx].value = value; // update existing
return;
idx = (idx + 1) % SIZE; // linear probe
if (id = start) {
std:: cout « " Table full!\n"; return;

std: :optional<int> lookup(const std: istring& key) {
int id = hash(key, SIZE);
int start = idx;
while (true) {
if (Itable[idx].occupied && Itable[idx].deleted) {
return std::nullopt; // empty slot, key not found
}
if (table[idx].occupied & Itable[idx].deleted & table[idx].key = key) {
return table[idx].value;
}
id = (idx + 1) % SIZE;
if (id = start) return std: :nullopt;
void remove(const std: :string& key) {
int id = hash(key, SIZE);
int start = idx;
while (true) {
if (Itable[idx].occupied && Itable[idx].deleted) return;
if (table[idx].occupied && table[idx].key = key) {
table[idx].deleted = true; // tombstone
table[idx].occupied = false;
return;
}
id = (idx + 1) % SIZE;
if (id == start) return;
I
}
void print() {
for (int i = 0; i < SIZE; i++) { std:: cout « " slot " « i «"; ";
if (table[i]. deleted) std: :cout « "[TOMBSTONE]";
else if (table[i].occupied) std: icout « "[" « table[i].key « "=" « table[i]. value « "1"; else std:: cout «< "[EMPTY]";
std:: cout « "\n";

int main() { std:: cout \ "= CHAINING ==|\n"; Chainingtable ct; ct.insert("ab", 1); ct.insert ("ba", 2); // collides with "ab" ct.insert("cd", 3); // collides with "ab" ct. insert ("hello", 4);
OpenAddressingTable oa;	
	
oa.insert("ab"
, 1);
// hash -> 3	
oa.insert("ba"
, 2);	// hash - 3, probes to 4
oa. insert("cd", 3);	// hash -> 3, probes to 5
oa. insert("hello", 4); // hash -> •	
std:: cout « "After inserts: \n"; ct. print();
std:: cout « "Lookup \"bal": " « ct. lookup("ba"). value_or(-1) « "In"; ct. remove("ba"'):
std: :cout « "After removing \"ba\": \n"; ct.print():
std: :cout « "\n=== OPEN ADDRESSING (Linear Probing) =\n";
std:: cout « "After inserts: \n"; oa.print();
std: cout « "Lookup |"cdl"; " « oa. lookup("d"). value_or(-1) « "In";
oa. remove("ba");
std:: cout « "After removing |"ba\" (tombstone): \n"; oa.print();
I
std:: cout « "Lookup |"cd\" still works: " « oa. lookup("cd"). value_or(-1) « "\n";
return ø;
}
Expected output: === CHAINING === After inserts: Bucket 0: [hello=4] Bucket 1: Bucket 2:
Bucket 3: [ab=1] [ba=2] [cd=3] Lookup "ba": 2 After removing "ba": Bucket e: [hello=4]

Slot 2: [EMPTY] Slot 3: [ab=1] Slot 4: [ba=2] Slot 5: [cd=3] slot 6: [EMPTY] slot 7:
[EMPTY] Lookup "cd": 3 After removing "ba" (tombstone): Slot 0: [hello=4] slot 1: [EMPTY]
Slot 2: [EMPTY] Slot 3: [ab=1] Slot 4: [TOMBSTONE] Slot 5: [cd=3] Slot 6: [EMPTY] Slot 7:
[EMPTY] Lookup "cd" still works: 3
## Go - chaining vs Open Addressing (Linear Probing)
go package main
import "fmt"
const size = 4 const oasize = 8 // open addressing needs extra capacity
func hash(key string, mod int) int { sum := 0 for L, c := range key { sum += int(c) } return sum % mod }
// Strategy 1: Chaining //
- type chainEntry struct { key string value
int)
type Chainingtable struct { buckets [size][]chainEntry }
func (ct \*ChainingTable) insert(key string, value int) {idx := hash(key, size) for i := range ct.buckets[idx] { if ct.buckets[idx][i].key == key {ct.buckets[idx][i].value =
value return } } ct. buckets[idx] = append(ct. buckets[idx], chainEntry(key, value}) }
func (ct|*Chainingable) lookup(key string) (int, bool) {idx:= hash(key, size) for L,
e := range ct. buckets[idx] { if e.key == key { return e.value, true }} return e, false }
func (ct \*ChainingTable) remove(key string) { idx := hash(key, size) for i, e := range
ct.buckets[idx] { if e.key == key {ct.buckets[idx] = append(ct.buckets[idx][: i],
ct.buckets[idx][i+1:]...) return } }}
func (ct \*ChainingTable) print() { for i, bucket := range ct. buckets { fmt.Printf(" Bucket %d: ", i) for \, e := range bucket { fmt.Printf("[%s=%d] ", e.key, e. value) } fmt. Println() } }

Probing)
Probing) // -
value int occupied bool deleted bool }
// Strategy 2: Open Addressing (Linear
- type oaslot struct { key string
type OpenAddressingtable struct { slots [oasizeloaslot }
func (oa \*OpenAddressingtable) insert(key string, value int) {id:= hash(key, oasize)
start := idx
for {
if loa.slots[idx].occupied || oa.slots[idx].deleted {
oa.slots[idx] = oaslot(key, value, true, false}
return
}
if oa.slots[idx].key = key {
oa.slots[idx]. value = value
return
}
idx = (idx + 1) % oasize
if id = start {
fmt.PrintIn(" Table full!")
return
I
func (oa |*OpenAddressingTable) lookup(key string) (int, bool) {idx := hash(key, oasize)
start := idx
...
for
slot := oa.slots[idx]
if Islot.occupied && Islot.deleted {
return e, false
if slot.occupied && Islot.deleted && slot.key == key (
return slot. value, true


id = (idx + 1) % oasize
if id = start (
return 0, false
func (oa \*OpenAddressingtable) remove(key string) {idx := hash(key, oasize) start := id
for f
slot := &oa.slots[idx]
if Islot.occupied && Islot. deleted {
return
if slot.occupied & slot.key == key {
slot.occupied = false
slot.deleted = true // tombstone
return
｝
idx = (idx + 1) % oasize
if id == start {
return
｝
｝
func (oa \*OpenAddressingtable) print() { for i, slot := range oa.slots { fmt.Printf("
func main() {ft.PrintIn("-== CHAINING ==") ct := &Chainingtable() ct.insert("ab", 1)
ct. insert("ba", 2) // collides with "ab" ct. insert("cd", 3) // collides with "ab" ct. insert("hello", 4)

fit.Printin( "After inserts:")
ct. print()
if v, ok := ct.lookup("ba"); ok {
}
fit. Printf("Lookup \"bal": %d\n", v)
ct. remove("ba")
ft.Printin("After removing \"ba\": ")
ct. print()
fmt.Println("\n=== OPEN ADDRESSING (Linear Probing) =") oa := &openAddressingTable}.
oa.insert ("ab", 1)
// hash -> 3
oa.insert("ba"
, 2)
// hash -> 3, probes to 4
oa.insert("cd", 3)
// hash -> 3, probes to 5
oa.insert("hello", 4) // hash - 0 fmt.Println("After inserts:")
oa.print()
if v, ok := oa. lookup("cd"); ok {
fmt. Printf("Lookup |"cd\": %d\n", v)
}
oa. remove ("ba")
fmt.Println("After removing |"ba\" (tombstone):")
oa.print()
if v, ok := oa. lookup("cd"); ok {
fmt .Printf("Lookup |"cd\" still works: %d\n", v)
Expected output: === CHAINING === After inserts: Bucket 0: [hello=4] Bucket 1: Bucket 2:
Bucket 3: [ab=1] [ba=2] [cd=3] Lookup "ba": 2 After removing "ba": Bucket 0: [hello=4]
Bucket 1: Bucket 2: Bucket 3: [ab=1] [cd=3]

Expected output: === CHAINING === After inserts: Bucket 0: [hello=4] Bucket 1: Bucket 2:
Bucket 3: [ab=1] [ba=2] [cd=3] Lookup "ba": 2 After removing "ba": Bucket 0: [hello=4]
Bucket 1: Bucket 2: Bucket 3: [ab=1] [cd=3]
-= OPEN ADDRESSING (Linear Probing) ==- After inserts: Slot o: [hello=4] Slot 1: [EMPTY]
Slot 2: [EMPTY] Slot 3: [ab=1] Slot 4: [ba=2] Slot 5: [cd=3] Slot 6: [EMPTY] Slot 7:
[EMPTY] Lookup "cd": 3 After removing "ba" (tombstone): Slot 0: [hello=4] slot 1: [EMPTY] slot 2: [EMPTY] slot 3: [ab=1] slot 4: [TOMBSTONE] Slot 5: [cd=3] slot 6: [EMPTY] slot 7:
[EMPTY] Lookup "cd" still works: 3
## Key differences illustrated
I Chaining
| open
Addressing
I Collision handling slot
| Deletion break)
I why tombstones matter empty slot and fail - |
I even though "cd" is at slot
5
| Memory array
I Load factor tolerance
7)
---- I
| Append to list at bucket
I Probe forward for next empty
I Simply remove from list
I Must use tombstone (can't leave a gap or lookups
| N/A
I Without the tombstone at slot 4, looking up "cd" would stop at the
Extra pointers per entry | Compact, cache-friendly
I Can exceed 1.0
1 must stay well below 1.0 (typically < o.