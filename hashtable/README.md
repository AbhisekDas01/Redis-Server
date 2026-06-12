# High-Performance Intrusive Hash Map Engine

This repository contains a production-grade, Redis-inspired key-value storage engine implementation. Designed for systems-level performance, it prioritizes hardware synchronization, eliminates "stop-the-world" allocation stalls, and optimizes CPU cache line usage.

---

## 1. Core Architectural Philosophies

### A. The Intrusive Data Structure Paradigm

Traditional hash maps wrap payloads in generic nodes containing a data pointer (e.g., `void *data`). This results in data scattering across the heap, forcing the CPU to continuously execute slow pointer-chasing operations.

Our engine embeds the structural link (`HNode`) **directly inside your application data struct**.

#### The Hardware Reality:

* **The Non-Intrusive Problem:** Traversing a list forces the CPU to fetch the structural node into a 64-byte cache line. However, because the data payload lives at an isolated heap address, the CPU experiences an immediate **Cache Miss**, stalling execution while it trips to main RAM.
* **The Intrusive Victory:** The structural node and your application variables sit side-by-side in the *exact same physical cache line*. Reading the node pulls your domain data into the ultra-fast L1/L2 hardware cache entirely for free.

#### Structural Advantages:

1. **Zero Plumbing Allocations:** The hash table never invokes `malloc()` or `free()`. The lifetime of the node is structurally identical to the lifetime of the data payload.
2. **Native Multi-Indexing (The Redis Secret Sauce):** An application struct can embed multiple independent structural hooks simultaneously, allowing the exact same object to live inside a hashtable and a skiplist at the same time with zero data duplication.

```c
struct UserProfile {
    std::string username;
    uint32_t score;

    // Dual structural hooks embedded inside a single data record
    HashNode  id_index;    // Maps into primary lookup Hashtable
    SkipNode  score_index; // Maps into sorting leaderboard Skiplist
};

```

3. **Zero-Overhead Heterogeneous Polymorphism:** Multiple unique data structures can share the same collection wire by placing an integer type-tag (`enum`) at byte-offset 0. This bypasses the runtime performance penalties associated with virtual method tables (VMTs) or Runtime Type Identification (RTTI).

---

## 2. Low-Level Mathematical Optimizations

### A. Bitwise Masking vs. Modulo Division

Traditional mappings route hashes to bucket arrays via division: $\text{index} = \text{hcode} \pmod N$. On modern CPU architectures, integer division is exceptionally slow, taking anywhere from **10 to 40 clock cycles** per instruction.

Our engine enforces that the bucket array size $N$ must strictly be a **power of two** ($N = 2^k$). This mathematical alignment allows us to replace slow division with an instantaneous **bitwise AND (`&`) instruction** executing in a **single CPU cycle**.

```text
  hcode (203):  1 1 0 0 1 0 1 1
  mask  (15):   0 0 0 0 1 1 1 1   <-- Acts as a digital stencil blocking high bits
  ─────────────────────────────
  Result (11):  0 0 0 0 1 0 1 1   (Matches 203 % 16 perfectly)

```

#### Why $N$ Must Be a Power of Two:

Subtracting 1 from a perfect power of two converts all lower-order bits into ones (e.g., $16 \to \text{00010000}_2$, $15 \to \text{00001111}_2$). This creates an unbroken bitmask. If $N$ is an arbitrary number like 13 ($\text{00001101}_2$), creating a mask via $13 - 1 = 12$ ($\text{00001100}_2$) introduces gaps of zeros that slice away structural data during masking, destroying the integrity of the array index.

### B. Hash Function Selection & Real-World Complexity

This system implements the **Fowler–Noll–Vo (FNV-1a)** algebraic hash variant for string payloads:

```cpp
static uint64_t strHash(const uint8_t *data, size_t len) {
    uint32_t h = 0x811C9DC5; // FNV Offset Basis
    for (size_t i = 0; i < len; i++) {
        h = (h + data[i]) * 0x01000193; // FNV Prime Bit-Mixing Step
    }
    return h;
}

```

* **The Magic Numbers:** The offset basis and prime work concurrently to generate an **avalanche effect**. Modifying a single character in a string shifts and cascades bits wildly across the 32-bit register space, ensuring uniform distribution.
* **Unsigned Overflow Safety:** Unsigned integer overflows are deterministic and safe in systems programming. When the value exceeds 32 bits, the high bits fall off the register boundary, acting as an implicit, hardware-level modulo operation ($\pmod{2^{32}}$).
* **The $O(1)$ Paradox:** Hashing a string key scales linearly based on string length ($O(L)$). In database environments, lookups are called $O(1)$ because performance bounds are analyzed against **$N$ (total keys stored)**. Since key sizes are clamped to constant ceilings ($L \le \text{Constant}$), the linear hashing phase scales as flat, invariant overhead.

---

## 3. Data Structure Topography

### `HNode` (The Structural Hook)

```cpp
struct HNode {
    HNode *next = nullptr;
    uint64_t hcode = 0; 
};

```

* **`next`**: Singly-linked connection pointer managing bucket collision chains.
* **`hcode`**: Caches the 64-bit hash code. During rehashing or deep list checks, comparing cached integers (`cur->hcode == target_hash`) filters out 99% of mismatches in registers, preventing expensive string-comparison jumps.

### `HTable` (The Fixed Bucket Array)

```cpp
struct HTable {
    HNode **tab = nullptr; 
    size_t mask = 0;       
    size_t size = 0;       
};

```

* **`HNode tab`**: A pointer-to-a-pointer representing a dynamically allocated continuous array of bucket-head pointers. Using a double pointer keeps the struct header compact (8 bytes) while allowing runtime allocation scaling via `calloc`.

### `HMap` (The Twin-Table Coordinator)

```cpp
struct HMap {
    HTable newer;
    HTable older;
    size_t migratePos = 0; 
};

```

* Coordinates the non-blocking progressive rehashing cycle. Under standard operation, `older.tab` remains `nullptr` and mutations stream straight into `newer`.

---

## 4. Advanced Pointer Mechanics & Operations

### A. The Pointer-to-Pointer (`HNode `) Loop Traversal Strategy

Traditional list traversal evaluates a node pointer (`HNode *cur`). This introduces a structural vulnerability during deletions: you cannot sever an element without tracking its predecessor (`prev`), which triggers an ugly conditional branch for the "list head" boundary.

Our lookup engine resolves this by traversing the **address of the pointer that led us to our current position**.

```cpp
static HNode **hLookup(HTable *htab, HNode *key, bool (*eq)(HNode *, HNode *)) {
    if (!htab->tab) return nullptr;
    
    size_t pos = key->hcode & htab->mask;
    HNode **from = &htab->tab[pos]; // Tracks the address of the bucket pointer itself
    
    for (HNode *cur; (cur = *from) != nullptr; from = &cur->next) {
        if (cur->hcode == key->hcode && eq(cur, key)) {
            return from; // Returns the exact pointer slot holding this node in place
        }
    }
    return nullptr;
}

```

* **The Mechanism:** `from` starts by holding the address of the table array index (`&htab->tab[pos]`). As the loop steps forward via `from = &cur->next`, it slides smoothly to point directly at the internal `next` field of the previous node.
* **The Return Value:** The function hands back the incoming connection wire. If the element is at the front of the list, it returns the table slot address. If it is in the middle, it returns the preceding node's `next` address.

### B. Branchless Detachment

Because `hLookup` surfaces an `HNode from`, the deletion routine (`hDetach`) strips away all `if/else` branching logic, collapsing a list removal down to a single instruction:

```cpp
static HNode *hDetach(HTable *htab, HNode **from) {
    HNode *node = *from; // Recover the real node address
    *from = node->next;  // Directly overwrite the incoming pointer with the outgoing pointer
    htab->size--;
    return node;
}

```

### C. Ownership and Memory Boundaries

`hDetach` explicitly **does not call `free()**`. Because this structure is intrusive, the `HNode` is embedded within the application payload. Calling `free(node)` on a middle hook passes an offset pointer directly into the kernel's memory allocator, resulting in instant heap corruption and a segmentation fault.

The engine unlinks the wiring and returns the raw `HNode*`. The application layer steps backward out of the plumbing using the `container_of` macro to recover the true parent start address and safely frees the payload.

---

## 5. Non-Blocking Progressive Rehashing

When a standard hash map grows beyond its load threshold, it allocates a larger array and moves every element simultaneously. This results in an $O(N)$ "stop-the-world" latency spike that compromises real-time networking protocols.

Our framework utilizes **Progressive Rehashing** to smoothly distribute migration overhead across daily operation.

```cpp
static void hmHelpRehash(HMap *hmap) {
    size_t work_done = 0;
    const size_t k_rehashing_work = 128; // Strict constant bounds

    while (work_done < k_rehashing_work && hmap->older.size > 0) {
        // Find the next active bucket to migrate
        HNode **from = &hmap->older.tab[hmap->migratePos];
        if (!*from) {
            hmap->migratePos++;
            continue;
        }

        // Evict the head node of this bucket and insert it into 'newer'
        HNode *node = hDetach(&hmap->older, from);
        hInsert(&hmap->newer, node);
        work_done++;
    }

    // Clean up table memory when the old container is entirely depleted
    if (hmap->older.size == 0 && hmap->older.tab) {
        free(hmap->older.tab);
        hmap->older = HTable(); // Reset to zero-state
    }
}

```

### The Operational Pipeline:

1. **The Tripwire (`hmTriggerRehashing`):** When `newer` exceeds `k_max_load_factor = 8`, the map transitions the active `newer` struct over to `older`, then executes a clean `hInit` on `newer` at twice its original capacity via `(mask + 1) * 2`.
2. **Distributed Migration:** Every database operation (`hmLookup`, `hmInsert`, `hmDelete`) triggers a localized background sweep via `hmHelpRehash`. It shifts a maximum of 128 elements per call, ensuring no single operation stalls.
3. **Dual Reads:** While the map is actively rehashing, read lookups execute seamlessly on `newer` first. If a key is missing, it structurally falls back to inspect `older` to guarantee read atomicity.

---

## 6. API Reference & Specifications

### Struct Layouts

```cpp
struct HNode {
    HNode *next = nullptr;
    uint64_t hcode = 0; 
};

struct HTable {
    HNode **tab = nullptr; 
    size_t mask = 0;       
    size_t size = 0;       
};

struct HMap {
    HTable newer;
    HTable older;
    size_t migratePos = 0; 
};
```

### Core Interface

| Function Signature | Description | Time Complexity | Space Complexity |
| :--- | :--- | :--- | :--- |
| `HNode *hmLookup(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *))` | Searches the map for the key node using the equality callback. Initiates progress rehashing. Returns the matching node or `nullptr`. | $O(1)$ average | $O(1)$ |
| `void hmInsert(HMap *hmap, HNode *node)` | Inserts a new node into the map. Automatically triggers and processes progressive rehashing if load threshold is exceeded. | $O(1)$ average | $O(1)$ |
| `HNode *hmDelete(HMap *hmap, HNode *key, bool (*eq)(HNode *, HNode *))` | Searches and detaches the key node from the map. Returns the detached node or `nullptr`. Does **not** free user payload memory. | $O(1)$ average | $O(1)$ |
| `void hmClear(HMap *hmap)` | Deallocates internal bucket tables (`tab`) for both tables. Does **not** free individual intrusive user data node payloads. | $O(1)$ | $O(1)$ |
| `size_t hmSize(HMap *hmap)` | Returns the total count of elements currently tracked inside both tables. | $O(1)$ | $O(1)$ |
| `void hmForeach(HMap *hmap, bool (*f)(HNode *, void *), void *arg)` | Iterates over all active nodes in both tables. Stops early if callback `f` returns `false`. | $O(N)$ | $O(1)$ |

> [!CAUTION]
> **Intrusive Memory Boundary Reminder**
> Like all intrusive data structure interfaces, `hmDelete` and `hmClear` only manage the internal pointer wiring of the map. They do **not** deallocate user data node payloads. The application layer must step backward to the container address using `container_of` and free payloads manually.

---

## 7. Syntax Navigation Cheat Sheet

When operating within this codebase, use this language-level logic reference for managing types and memory access.

### Dot (`.`) vs. Arrow (`->`) Syntax Rules

* **The Arrow Operator (`->`)** is a remote control indicator. It means the variable on the left is a **Pointer** storing an address. It instructs the CPU to travel across RAM to look inside that destination.
* **The Dot Operator (`.`)** is an in-hand indicator. It means the variable on the left is a concrete **Object** sitting right in front of the CPU. It opens an internal field directly.

```cpp
void manageState(HMap *hmap) { // 'hmap' is a pointer (holds an address)
    hmap->migratePos = 0;       // Use -> because hmap is a pointer
    hmap->newer.size = 0;       // Use -> for hmap, then . because 'newer' is a real object
}

```

### Type Transformations with Ampersand (`&`)

Passing a variable to an initialization routine like `hInit(&hmap->newer, n)` applies a compile-time type transformation:

1. `hmap` is an `HMap*` pointer $\rightarrow$ Use **`->`** to evaluate its fields.
2. `hmap->newer` resolves into a concrete **`HTable`** object structure.
3. Prepending the ampersand (`&hmap->newer`) captures its memory location, transforming the type instantly from an object (`HTable`) into a pointer (**`HTable*`**).
4. `hInit` receives this pointer as its primary argument, using **`->`** internally to initialize the true underlying database coordinates.

### Function Pointer Callback Architecture

The type signature `bool (*eq)(HNode *, HNode *)` defines a **Function Pointer Callback**. Because the intrusive map plumbing is completely dataless, it delegates matching decisions back to the application using this walkie-talkie mechanism:

```text
 hLookup Pipeline:
 [Hashtable Finds Hash Match] ──► Invokes eq(cur, key) ──► [Jumps to App RAM Address] 
                                                                   │
 [Returns true/false] ◄────────────────────────────────────────────┘

```

The CPU reads the raw machine code instructions residing at the address stored in the `eq` variable, jumps across execution memory to perform the type-casted string comparison, and returns a boolean response without ever exposing string formatting details to the core hashtable plumbing.