#pragma once

#include <cstdint>

struct Star {
    float x;
    float y;
    float z;
    uint64_t payload_pointer;
}; // 24 bytes

// 64-byte strictly aligned
struct alignas(64) OctreeNode {
    float min_x, min_y, min_z;  // 12 bytes
    float max_x, max_y, max_z;  // 12 bytes

    union {
        OctreeNode* first_child; // 8 bytes

        uint32_t star_indices[8]; // 32 bytes
    }; // 32 bytes

    uint8_t star_count; // 1 byte
    bool is_leaf; // 1 byte
    uint8_t padding[6]; // Explicit padding
};

// Compile-time assertion to guarantee L1 Cache-Line layout
static_assert(sizeof(OctreeNode) == 64, "OctreeNode must be exactly 64 bytes for cache alignment!");
