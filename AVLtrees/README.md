# Intrusive AVL Tree Storage Engine

A production-grade, zero-allocation, intrusive AVL tree implementation built from scratch in C++. This component serves as the core ordering subsystem for a high-performance in-memory key-value database (similar to Redis's Sorted Sets / `ZSET`), enabling guaranteed $O(\log N)$ range-based queries, rank tracking, and lookups.

Unlike standard textbook generic data structures, this library utilizes an **intrusive architecture** and professional low-level system designs to optimize cache locality and eliminate internal heap overhead.

---

## 🚀 Key Engineering Highlights

### 1. Intrusive Architecture (Zero-Allocation Nodes)

In a standard binary tree, the tree object allocates an independent wrapper node for every value inserted, which creates massive memory fragmentation and pointer-chasing overhead on the CPU cache.

This engine uses an **intrusive layout**: the data container itself owns the structural `AVLNode`. The tree handles arrangement entirely by reaching inside your database rows.

```cpp
// Your custom database payload
struct Data {
    uint32_t val;
    std::string name;
    double score;
    
    // The structural tree component is embedded directly inside the row!
    AVLNode node; 
};

```

To travel from a raw tree node back up to your database fields, we use the macro:

```cpp
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

```

### 2. Branchless Pointer-to-Pointer Rewiring (`from`)

When a tree executes a structural rotation, parent-child links must be completely adjusted. Traditional code relies on verbose, branch-heavy checks to figure out if the current node is its parent's `left` child or `right` child.

This engine completely eliminates these branches using a dynamic **pointer-to-pointer (`from`)** alias trick inside `avlFix`:

```cpp
AVLNode **from = &root;
AVLNode *parent = root->parent;
if (parent) {
    // Capture the exact memory address of the parent's connection arm
    from = parent->left == root ? &parent->left : &parent->right;
}

// ... execute rotations ...
*from = new_sub_root; // Updates parent branch directly without conditional forks!

```

### 3. Identity Theft Deletion (`*successor = *node;`)

When deleting a node that possesses two active children, we cannot safely modify or move its container payload on the heap because external structures (such as our database's key lookup hashtable) maintain active references straight to that container's physical address.

Instead of relocating data payloads, we perform **structural identity theft**:

1. We locate the node's sorted successor at the bottom of the tree and detach it cleanly.
2. We copy only the structural tracking fields of the victim straight over the successor using value cloning: `*successor = *node;`.
3. The successor instantly assumes the position, pointers, and height relationships of the deleted node, leaving the user's data record locked safely at its original memory address.

---

## 🛠️ Deep Dive: Balancing & Rotation Layouts

An AVL tree enforces a strict geometric rule: the height difference between the left and right subtrees of any node can never exceed 1. If an insertion or deletion trips this limit, the system heals itself via pointer pivots.

### Single Left Rotation (RR Imbalance)

Triggered when the right child's right side stretches too deep. The system re-parents the inner subtree and pulls the right child up to restore balance factor equilibrium.

```text
       BEFORE (Lopsided Right)                       AFTER (Balanced)
          20 (root)                                    40 (newRoot)
         /  \                                         /  \
       10    40 (newRoot)                           20    50
            /  \                                   /  \     \
   (inner) 30   50                               10    30    60
                  \
                   60

```

### Double Left-Right Rotation (LR Imbalance)

Triggered when the inner child introduces a zig-zag weight discrepancy. `avlFixLeft` automatically identifies this layout and divides the resolution into two distinct stages:

```cpp
static AVLNode *avlFixLeft(AVLNode *root) {
    if (avlHeight(root->left->left) < avlHeight(root->left->right)) {
        // Step 1: Straighten the zig-zag into a straight line via child rotation
        root->left = rotateLeft(root->left); 
    }
    // Step 2: Flatten the straight line globally
    return rotateRight(root); 
}

```

---



## 💻 API Reference & Specifications

### Struct Layout

```cpp
struct AVLNode {
    AVLNode *parent = NULL;
    AVLNode *left   = NULL;
    AVLNode *right  = NULL;
    uint32_t height = 0; // Absolute subtree vertical level count
    uint32_t cnt    = 0; // Total nodes nested within this subtree branch
};

```

### Core Interface

| Function Signature | Description | Time Complexity | Space Complexity |
| :--- | :--- | :--- | :--- |
| `void avlInit(AVLNode *node)` | Initializes a detached standalone node wrapper. Sets default structure metrics (`height = 1`, `cnt = 1`). | $O(1)$ | $O(1)$ |
| `uint32_t avlHeight(AVLNode *node)` | Safely retrieves the height of the node (returns `0` if `node` is `nullptr`). | $O(1)$ | $O(1)$ |
| `uint32_t avlCnt(AVLNode *node)` | Safely retrieves the subtree node count of the node (returns `0` if `node` is `nullptr`). | $O(1)$ | $O(1)$ |
| `AVLNode *avlFix(AVLNode *node)` | Re-balances the AVL tree from the mutated node up to the root, executing corrective rotations. Returns the new root. | $O(\log N)$ | $O(1)$ |
| `AVLNode *avlDel(AVLNode *node)` | Safely detaches a node from the tree and re-balances. Returns the new root. | $O(\log N)$ | $O(1)$ |
| `AVLNode *avl_offset(AVLNode *node, int64_t offset)` | Jumps relative to the given node by a positive or negative index offset using subtree count fields. | $O(\log N)$ | $O(1)$ |
| `int64_t avlRank(AVLNode *node)` | Computes the 1-based rank (sorted order index) of a node within the tree by climbing to the root. | $O(\log N)$ | $O(1)$ |

---

## 🧪 Comprehensive Verification Pipeline

To ensure the zero-allocation engine never corrupts pointers or violates tree constraints under chaotic workloads, the implementation is matched against an exhaustive testing framework (`test_avl.cpp`) employing several advanced validation systems:

1. **Differential Oracle Testing:** Validates every change against a trusted reference structure (`std::multiset`). Every single insertion or deletion maps parity results back and forth to confirm that no data drops out or shifts order.
2. **Recursive Invariant Assertions:** Scans the entire active tree after operations to forcefully verify that:
* Parent-child connections match symmetrically (`node->parent->child == node`).
* Left subtree elements are consistently less than or equal to the current node.
* Node height balances are structurally sound ($\Delta \text{height} \le 1$).


3. **Stochastic Chaos Testing:** Drives random sequence injections and deletions via automated fuzz runs to isolate and fix edge cases across complex multi-tier balance transitions.