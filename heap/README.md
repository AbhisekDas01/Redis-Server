# Intrusive Min-Heap TTL (Time-To-Live) System

This directory implements an **Intrusive Min-Heap** to manage key expirations (TTL) in $O(\log N)$ time for updates/deletions and $O(1)$ time for finding the next expiring key.

---

## 1. Architecture Overview

In a typical heap implementation, elements are stored in a contiguous array, and lookup of a specific element within the heap is an $O(N)$ operation. This makes modifying or deleting a key's TTL slow.

To solve this, our design uses an **intrusive data structure**:
* The database entry (`Entry` in `server.cpp`) stores `heap_idx`, which tracks its index inside the `gData.heap` array.
* The `HeapItem` in the heap stores:
  * `val`: The monotonic expiration time (in milliseconds).
  * `ref`: A pointer back to `Entry::heap_idx`.
* When items are swapped or shifted inside the heap, the heap update functions (`heapUp`, `heapDown`) automatically update the `heap_idx` of the corresponding `Entry` via the `ref` pointer:
  `*heap[pos].ref = pos;`
* This allows **$O(1)$ lookup** of any entry's position in the heap, enabling **$O(\log N)$ updates (`heapUpsert`) and deletions (`heapDelete`)**.

---

## 2. Process Flows

### A. Expiration Cleanup Loop
The server uses a combination of passive and active expiration. In the main event loop, it calculates the time until the next key expires, sleeps for that duration using the `poll()` system call, and cleans up expired keys.

```mermaid
graph TD
    Start([Start Event Loop Iteration]) --> NextTimer["Call nextTimerMs()"]
    NextTimer --> CheckHeap{"Is Heap empty?"}
    
    CheckHeap -- No --> MinTimeout["timeout = min(idle_timeout, heap[0].val - now)"]
    CheckHeap -- Yes --> IdleTimeout["timeout = idle_timeout (or infinite)"]
    
    MinTimeout --> Poll["poll(fds, nfds, timeout)"]
    IdleTimeout --> Poll
    
    Poll --> HandleIO["Handle Socket I/O (Accept/Read/Write)"]
    HandleIO --> Timers["Call processTimers()"]
    
    subgraph Active Expiration
    Timers --> LoopHead{"Is heap[0].val &lt; nowMs<br>AND count &lt; 2000?"}
    LoopHead -- Yes --> Delete["Delete key from Hashmap (hmDelete)"]
    Delete --> Free["entryDel():<br>1. entrySetTTL(ent, -1) to remove from Heap<br>2. zsetClear() if ZSET<br>3. delete ent"]
    Free --> LoopHead
    end
    
    LoopHead -- No --> EndIteration([End Event Loop Iteration])
    EndIteration --> Start
```

#### Detailed Cleanup Phases:
1. **`nextTimerMs()` (Calculate Next Deadline)**:
   * Determines how long the server should block inside `poll()`.
   * It inspects the top of the TTL min-heap (`gData.heap[0].val`) to find the nearest expiration deadline.
   * It compares this deadline with connection idle timeout limits ($5\text{s}$) and active I/O timeout limits ($3\text{s}$).
   * Returns the shortest time-to-sleep in milliseconds (or `0` if a deadline has already passed).
2. **`poll()` (CPU-Friendly Sleep)**:
   * Passes the calculated timeout to the `poll()` system call.
   * This suspends the server process, avoiding high CPU usage (busy-waiting).
   * It wakes up immediately if I/O activity occurs on any client socket, or when the next TTL expiration time is reached.
3. **`processTimers()` (Active Purge)**:
   * Handles expired client connections first.
   * Then, in a loop, checks if the earliest key in the heap (`heap[0]`) has expired (`heap[0].val < nowMs`).
   * **Batch Limiting**: To prevent blocking client requests if a large number of keys expire at the same time, cleanup is capped at a maximum of `2000` keys per event loop iteration.
4. **`entryDel()` (Safe Memory Release)**:
   * Calls `entrySetTTL(ent, -1)` to detach the entry and delete its reference from the min-heap safely.
   * Clears any nested data structures (e.g., skips lists or hash nodes inside `T_ZSET` keys).
   * Deletes the `Entry` struct to release system memory.

---

### B. Command Routing Flow
Here is how commands like `PEXPIRE`, `PTTL`, and `PERSIST` interact with the heap:

```mermaid
graph TD
    cmd["Client Command"] --> PEXPIRE{"PEXPIRE key ms"}
    cmd --> PTTL{"PTTL key"}
    cmd --> PERSIST{"PERSIST key"}

    %% PEXPIRE Flow
    PEXPIRE -- Yes --> FindKey1{"Key exists?"}
    FindKey1 -- No --> RetZero["Return 0"]
    FindKey1 -- Yes --> NegCheck{"Is ms &lt;= 0?"}
    NegCheck -- Yes --> DelKey["Delete Key immediately"]
    NegCheck -- No --> Upsert["heapUpsert: Add/Update expiration"]
    DelKey --> RetOne["Return 1"]
    Upsert --> RetOne

    %% PTTL Flow
    PTTL -- Yes --> FindKey2{"Key exists?"}
    FindKey2 -- No --> RetMinus2["Return -2"]
    FindKey2 -- Yes --> HasTTL{"Has TTL?<br>(heap_idx != -1)"}
    HasTTL -- No --> RetMinus1["Return -1"]
    HasTTL -- Yes --> TimeLeft["Calculate: expireMs - now"]
    TimeLeft --> RetTime["Return remaining ms"]

    %% PERSIST Flow
    PERSIST -- Yes --> FindKey3{"Key exists?"}
    FindKey3 -- No --> RetZero
    FindKey3 -- Yes --> HasTTL2{"Has TTL?<br>(heap_idx != -1)"}
    HasTTL2 -- No --> RetZero
    HasTTL2 -- Yes --> HeapDel["heapDelete: Remove from heap"]
    HeapDel --> ResetIdx["Set heap_idx = -1"]
    ResetIdx --> RetOne
```

---

## 3. Data Structures & API Reference

### `HeapItem`
```cpp
struct HeapItem {
    uint64_t val;       // Expiration time in monotonic milliseconds
    size_t *ref = NULL; // Pointer to Entry::heap_idx
};
```

### Functions (`heap.h`)

| Function | Complexity | Description |
|---|---|---|
| `heapUpsert(heap, pos, t)` | $O(\log N)$ | Inserts a new expiration or updates an existing expiration. |
| `heapDelete(heap, pos)` | $O(\log N)$ | Removes an expiration entry from the heap and updates positions. |
| `heapUpdate(heap, pos, len)` | $O(\log N)$ | Restores the heap property by sift-up or sift-down operations. |
