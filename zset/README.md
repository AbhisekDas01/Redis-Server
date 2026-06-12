# Hybrid Sorted Set (ZSet) Storage Engine

A production-grade, zero-allocation-overhead **Sorted Set (ZSet)** implementation built from scratch in C++. This component combines an **intrusive AVL tree** and an **intrusive hashtable** to deliver a high-performance ordering and lookup subsystem matching Redis's `ZSET` semantics.

By fusing two distinct data structures into a single co-located memory allocation, this engine achieves $O(1)$ point lookups, $O(\log N)$ updates, and $O(\log N)$ rank-based range queries with exceptional cache locality and minimal memory fragmentation.

---

## 🚀 Key Engineering Highlights

### 1. Dual-Indexed Intrusive Architecture

Unlike standard database engines that store secondary indexes in completely separate structures with independent pointer graphs, this engine uses a **co-located intrusive layout**. 

A single data record (`ZNode`) contains both the AVL tree metadata and the Hashtable node metadata inline:

```cpp
struct ZNode {
    // Structural nodes embedded directly inside the user data!
    AVLNode tree;  // Indexed by (score, name) for ordered traversal
    HNode hmap;    // Indexed by name for O(1) point lookups

    double score;  // Score value used for AVL ordering
    size_t len;    // Length of the member name string
    char name[0];  // Dynamic payload block (Zero-length array pattern)
};
```

This dual-index design enables:
* **AVL Tree (Score + Lexicographical Order):** Fast range seeks, offset jumps, and rank calculations in $O(\log N)$ time.
* **Hashtable (Name Lookup):** Check if a member exists, look up its current score, or delete it in $O(1)$ average time.

To move between structural nodes (`tree` or `hmap`) and the wrapper `ZNode`, we use the compile-time offset macro `container_of`:
```cpp
ZNode *node = container_of(found_hnode, ZNode, hmap);
```

---

### 2. Contiguous Memory Allocation (Zero-Length Array Pattern)

To eliminate multiple heap hops and cache misses, the name string is not stored as a separate `std::string` or heap-allocated pointer. Instead, we utilize the **Zero-length array pattern (`char name[0]`)** to allocate the structure metadata and the variable-length member name within a single contiguous block of memory.

```cpp
static ZNode *znodeNew(const char *name, size_t len, double score) {
    // Single malloc allocates memory for the struct AND the variable length string!
    ZNode *node = (ZNode *)malloc(sizeof(ZNode) + len);
    
    avlInit(&node->tree);
    node->hmap.next = NULL;
    node->hmap.hcode = strHash((uint8_t *)name, len);
    node->score = score;
    node->len = len;
    memcpy(&node->name, name, len); // Copied directly into the trailing block

    return node;
}
```

> [!TIP]
> **Cache Performance Advantage**
> Because the `name` payload directly succeeds the structural nodes, CPU cache lines prefetch the string data automatically during hashtable traversals and AVL tree key comparisons.

---

### 3. Coordinated Update Operations (Detach-Re-insert Pattern)

When updating the score of an existing member, we face a major challenge: the element's position in the AVL tree will change, but its identity in the hashtable remains identical since the key (`name`) is unchanged.

Because our data structures are intrusive, we avoid recreating the node. We execute a **local re-indexing**:

```text
           [ Start ZSet Update ]
                     │
                     ▼
           / Does score match? \
          /                     \
         [Yes]                 [No]
          │                      │
          ▼                      ▼
  [ Return: No-Op ]      [ Detach from AVL Tree ]
  (Keep current node)         (using avlDel)
                                 │
                                 ▼
                         [ Re-init AVL Node ]
                            (using avlInit)
                                 │
                                 ▼
                         [ Set New Score ]
                                 │
                                 ▼
                         [ Insert back into AVL ]
                            (using treeInsert)
                                 │
                                 ▼
                         [ Update Completed ]
                     (Hashtable link untouched)
```


This pattern keeps the hashtable pointer stable, avoiding expensive removals or rehashes in the hash table, while maintaining the sorted order in the AVL tree with $O(\log N)$ operations.

---

### 4. Advanced Range & Rank Queries (`ZQUERY`)

The combination of the AVL tree's size counting mechanism (`cnt` field in `AVLNode`) and lexicographical tie-breakers allows the system to solve complex range queries:

> *"Give me $L$ members starting from score $S$ (or name $N$ if tied), skipping the first $O$ offset elements."*

This is implemented in four highly optimized stages:

```text
  ┌─────────────────────────────────────────────────────────────┐
  │ 1. Binary Seek (zsetSeekge)                                 │
  │    Find the first tree node >= (score, name) in O(log N).    │
  └──────────────────────────────┬──────────────────────────────┘
                                 │
                                 ▼
  ┌─────────────────────────────────────────────────────────────┐
  │ 2. Find Starting Rank (avlRank)                             │
  │    Climb from node to root, accumulating left subtree count │
  │    fields to obtain its absolute 0-based index.             │
  └──────────────────────────────┬──────────────────────────────┘
                                 │
                                 ▼
  ┌─────────────────────────────────────────────────────────────┐
  │ 3. Offset Jump (znodeOffset)                                │
  │    Add offset index, then descend from root using subtree   │
  │    sizes to teleport straight to target starting node.      │
  └──────────────────────────────┬──────────────────────────────┘
                                 │
                                 ▼
  ┌─────────────────────────────────────────────────────────────┐
  │ 4. Limit Stream (In-order successor traversal)              │
  │    Follow successor pointers sequentially to retrieve       │
  │    exactly limit elements in O(Limit) time.                 │
  └─────────────────────────────────────────────────────────────┘
```

---

## 🛠️ Data Layout & Ordering Guarantees

### Strict Tuple Comparison

To prevent duplicate entries and ensure deterministic order, nodes are sorted using a **strict tuple comparison rule**:
$$\text{Order} = (\text{score}, \text{name})$$

If scores are equal, the engine falls back to lexicographical comparison of the raw bytes of their names:

```cpp
static bool zLess(AVLNode *lhs, AVLNode *rhs) {
    ZNode *zl = container_of(lhs, ZNode, tree);
    ZNode *zr = container_of(rhs, ZNode, tree);

    if (zl->score != zr->score) {
        return zl->score < zr->score;
    }
    
    int rv = memcmp(zl->name, zr->name, min(zl->len, zr->len));
    return (rv != 0) ? (rv < 0) : (zl->len < zr->len);
}
```

---

## 💻 API Reference

### Struct Layouts

```cpp
struct ZSet {
    AVLNode *root = NULL; // Root pointer of the intrusive AVL tree
    HMap hmap;           // Internal hash map for string lookup
};
```

### Core Interface

| Function Signature | Description | Time Complexity | Space Complexity |
| :--- | :--- | :--- | :--- |
| `bool zsetInsert(ZSet *zset, const char *name, size_t len, double score)` | Inserts a new member or updates the score of an existing one. Returns `true` if inserted, `false` if updated. | $O(\log N)$ | $O(1)$ (Reuse) / $O(L)$ (New Alloc) |
| `ZNode *zsetLookup(ZSet *zset, const char *name, size_t len)` | Looks up a member by name using the internal hashtable. | $O(1)$ average | $O(1)$ |
| `void zsetDelete(ZSet *zset, ZNode *node)` | Removes a member node from both the AVL tree and the hashtable, and deallocates it. | $O(\log N)$ | $O(1)$ |
| `int64_t zsetRank(ZSet *zset, const char *name, size_t len)` | Finds the 0-based rank of a member. Returns `-1` if not found. | $O(\log N)$ | $O(1)$ |
| `ZNode *zsetSeekge(ZSet *zset, double score, const char *name, size_t len)` | Seeks the first node in the tree whose score/name tuple is $\ge$ the query. | $O(\log N)$ | $O(1)$ |
| `ZNode *znodeOffset(ZNode *node, int64_t offset)` | Jumps relative to a given node by a positive or negative index offset. | $O(\log N)$ | $O(1)$ |
| `void zsetClear(ZSet *zset)` | Fully deallocates all nodes in the set using post-order tree traversal. | $O(N)$ | $O(\log N)$ stack |

---

## ⚠️ Memory Management Safety

> [!CAUTION]
> Because the data structures are **intrusive**, clearing or destroying a `ZSet` requires manual traversal. Simply discarding the `ZSet` container pointer will leak all active nodes.
> You **must** call `zsetClear(ZSet *zset)` to trigger post-order deallocation:

```cpp
static void treeDispose(AVLNode *node) {
    if (!node) return;
    treeDispose(node->left);  // Post-order left child
    treeDispose(node->right); // Post-order right child
    znodeDel(container_of(node, ZNode, tree)); // Free memory
}

void zsetClear(ZSet *zset) {
    hmClear(&zset->hmap); // Clear hashtable slots
    treeDispose(zset->root); // Recursively free tree nodes
    zset->root = NULL;
}
```
