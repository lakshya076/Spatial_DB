#pragma once

#include <cstdlib>
#include <stdexcept>
#include <cstdint>
#include <new>
#include <utility>

class ArenaAllocator {
private:
    char* buffer;          // Giant block of raw memory
    size_t total_size;     // Total capacity in bytes
    size_t offset;         // Current position marker

public:
    ArenaAllocator(size_t bytes) : total_size(bytes), offset(0) {
        buffer = static_cast<char*>(std::malloc(total_size));
        if (!buffer) {
            throw std::bad_alloc();
        }
    }

    ~ArenaAllocator() {
        std::free(buffer);
    }

    // Block copy constructor and assignment operator (prevent accidental arena copying)
    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;

    // Aligned allocation function
    void* allocate(size_t size, size_t alignment = 64) {
        // Calculate the current absolute memory address
        uintptr_t current_address = reinterpret_cast<uintptr_t>(&buffer[offset]);
        
        // Calculate bytes needed to reach the next alignment boundary
        size_t padding = (alignment - (current_address % alignment)) % alignment;
        
        if (offset + padding + size > total_size) {
            throw std::runtime_error("Arena Allocator: Out of memory!");
        }
        
        // Move offset forward by padding to ensure alignment
        offset += padding;
        void* grab_ptr = &buffer[offset];
        
        // Move offset forward by the requested size
        offset += size;
        return grab_ptr;
    }

    template <typename T, typename... Args>
    T* alloc(Args&&... args) {
        void* ptr = allocate(sizeof(T), alignof(T));
        return ::new (ptr) T(std::forward<Args>(args)...);
    }

    // Allocate an array of objects
    template <typename T>
    T* alloc_array(size_t count) {
        void* ptr = allocate(sizeof(T) * count, alignof(T));
        T* arr = static_cast<T*>(ptr);
        for(size_t i = 0; i < count; ++i) {
            ::new (&arr[i]) T(); // default construct each element
        }
        return arr;
    }

    void reset() {
        offset = 0;
    }

    size_t get_used_memory() const {
        return offset;
    }
};
