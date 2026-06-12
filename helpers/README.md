# Protocol Serialization & Networking Helpers

This directory contains the low-level utilities responsible for **serialization, deserialization, buffer management, and protocol framing**. It translates raw TCP byte streams into structured C++ data and encodes application responses back into an efficient binary format (similar to the Redis Serialization Protocol, RESP, but using a compact binary approach).

---

## 1. Core Concepts

### A. Buffer Management
All network communication is built around a dynamic byte array:
```cpp
typedef std::vector<uint8_t> Buffer;
```
This serves as both an incoming parsing buffer (where raw kernel reads are stored until full packets form) and an outgoing serialization buffer (where responses accumulate before being written to the socket).

### B. The Tag-Length-Value (TLV) Protocol
Unlike text-heavy protocols (like HTTP), this system uses a strict binary format to eliminate parsing ambiguity and minimize payload size. Every piece of data is prefixed with a 1-byte **Tag** describing its type.

**Supported Data Tags:**
| Enum | Value | Description |
| :--- | :---: | :--- |
| `TAG_NIL` | `0` | Represents a null/empty response (e.g., key not found). |
| `TAG_ERR` | `1` | An error response containing an error code + message string. |
| `TAG_STR` | `2` | A variable-length string payload. |
| `TAG_INT` | `3` | A 64-bit integer. |
| `TAG_DBL` | `4` | A 64-bit floating point number (double). |
| `TAG_ARR` | `5` | An array container holding $N$ nested elements. |

**Supported Error Codes:**
* `ERR_UNKNOWN (1)`: Unknown command requested.
* `ERR_TOO_BIG (2)`: Outgoing response exceeded `k_max_msg` (32MB).
* `ERR_BAD_TYP (3)`: Unexpected value type (e.g., executing `ZADD` on a String key).
* `ERR_BAD_ARG (4)`: Bad or malformed command arguments.

---

## 2. Response Framing Mechanics

TCP is a streaming protocol, meaning boundaries between messages do not exist natively. To prevent the client from reading partial data, every response is prefixed with a **4-byte total length header**.

Because the final size of a response isn't known until the application finishes processing the command, we use a **Reserve & Backfill pattern**:

```cpp
size_t headerPos;

// 1. Reserve 4 bytes for the length header at the current buffer end
responseBegin(conn->outgoing, &headerPos); 

// 2. Application injects its variable-length data
doRequest(cmd, conn->outgoing); 

// 3. Calculate final size, jump back to headerPos, and overwrite the 4 bytes
responseEnd(conn->outgoing, headerPos); 
```

---

## 3. API Reference & Specifications

### Low-Level Buffer Operations

| Function Signature | Description |
| :--- | :--- |
| `void bufAppend(Buffer &buf, const uint8_t *data, size_t size)` | Appends a raw block of bytes onto the end of the buffer. |
| `void bufAppendU8(Buffer &buf, uint8_t data)` | Pushes a single 8-bit byte. |
| `void bufAppendU32(Buffer &buf, uint32_t data)` | Pushes a 32-bit integer (occupies 4 bytes). |
| `void bufAppendI64(Buffer &buf, int64_t data)` | Pushes a 64-bit integer (occupies 8 bytes). |
| `void bufAppendDbl(Buffer &buf, double data)` | Pushes a 64-bit float (occupies 8 bytes). |
| `void bufConsume(Buffer &buf, size_t n)` | Erases the first `n` bytes from the front of the buffer (used after processing incoming streams or writing to the socket). |

### Response Serializers (App $\to$ Protocol)

| Function Signature | Description |
| :--- | :--- |
| `void outNil(Buffer &out)` | Encodes a `TAG_NIL` response. |
| `void outStr(Buffer &out, const char *s, size_t size)` | Encodes `TAG_STR` + 4-byte string length + raw string bytes. |
| `void outInt(Buffer &out, uint64_t val)` | Encodes `TAG_INT` + 8-byte integer. |
| `void outDbl(Buffer &out, double val)` | Encodes `TAG_DBL` + 8-byte double. |
| `void outArr(Buffer &out, uint32_t n)` | Encodes `TAG_ARR` + 4-byte length representing the array size. |
| `void outErr(Buffer &out, uint32_t code, const std::string &msg)` | Encodes `TAG_ERR` + 4-byte error code + 4-byte msg length + string bytes. |

#### Deferred Array Serialization
Similar to message framing, sometimes the size of an array isn't known upfront (e.g., querying elements sequentially from an AVL tree).

* `size_t outBeginArr(Buffer &out)`: Writes `TAG_ARR`, reserves 4 bytes, and returns the contextual index offset.
* `void outEndArr(Buffer &out, size_t ctx, uint32_t len)`: Backfills the actual array count into the reserved index once processing is complete.

### Input Deserializers (Protocol $\to$ App)

| Function Signature | Description |
| :--- | :--- |
| `bool readU32(const uint8_t *&curr, const uint8_t *end, uint32_t &out)` | Safely parses 4 bytes into a `uint32_t` integer and advances the pointer. Returns `false` if out of bounds. |
| `bool readStr(const uint8_t *&curr, const uint8_t *end, size_t len, std::string &out)` | Safely parses a string of size `len` into an `std::string` and advances the pointer. Returns `false` if out of bounds. |

### Utilities

| Function Signature | Description |
| :--- | :--- |
| `bool str2dbl(const std::string &s, double &out)` | Converts a string to a double gracefully. Returns `true` if successful and valid (not NaN). |
| `bool str2int(const std::string &s, int64_t &out)` | Converts a string to a 64-bit integer gracefully. Returns `true` if completely consumed. |
