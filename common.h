#pragma once

#include <stdint.h>
#include <stddef.h>


// intrusive data structure
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))


//FNV hash function
inline uint64_t strHash(const uint8_t *data, size_t len) {
    // 1. Initialize the accumulator with the FNV Offset Basis (Magic Number).
    // This seeds the hash with a chaotic bit pattern so that empty strings 
    // or strings starting with 0x00 do not accidentally result in a hash of zero.
    uint32_t h = 0x811C9DC5;

    // 2. Loop through every character/byte in the input string sequentially.
    for (size_t i = 0; i < len; i++) {
        // Step A: Stir the current byte directly into our hash accumulator.
        // Step B: Multiply by the FNV Prime (0x01000193, which is 16,777,619 in decimal).
        // 
        // This multiplication acts as a high-speed bit mixer, forcing every new 
        // byte to ripple and cascade across all 32 bits (the "avalanche effect").
        // Any integer overflow here is safe, wrapping around naturally via modulo 2^32.
        h = (h + data[i]) * 0x01000193;
    }

    // 3. Return the fully scrambled 32-bit integer hash value.
    // The compiler automatically zero-extends this 32-bit value into a 64-bit 
    // uint64_t, leaving the upper 32 bits as zeros, matching our HNode definition.
    return h;
}

