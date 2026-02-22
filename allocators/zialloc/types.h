#ifndef TYPES_H
#define TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <cmath>

#define KB      1024        // actually KiB
#define MB      (KB*KB)     // ^      ^ MiB
#define GB      (MB*KB)     // ^      ^ GiB

#define LARGE_PAGE_SIZE     (  4*MB) 
 
#define MIN_ALIGNMENT       (64*KB)
#define MAX_ALIGNMENT       ( 4*MB)

#define ZU(x)  x##ULL
#define ZI(x)  x##LL

#define SMALL_PAGE_SHIFT    (16)                       // 1 << 16          = 64KiB          
#define MEDIUM_PAGE_SHIFT   (3 + SMALL_PAGE_SHIFT)     // (1<<16)*(1<<3)   = 512KiB
#define SEGMENT_SHIFT       (22)

#define SEGMENT_SIZE        (ZU(1)<<SEGMENT_SHIFT)     // should be 4MiB
#define SEGMENT_ALIGN       SEGMENT_SIZE
#define SEGMENT_MASK        ((uintptr_t)(SEGMENT_ALIGN - 1))

#define HEAP_RESERVED_DEFAULT (2ULL * GB)             // ~2 GiB reserved vmem for heap
#define SMALL_PAGE_SIZE     (ZU(1)<<SMALL_PAGE_SHIFT)
#define MEDIUM_PAGE_SIZE    (ZU(1)<<MEDIUM_PAGE_SHIFT)

constexpr static inline bool is_power_of_2(size_t n) {
    return n && !(n & (n - 1));
}

#endif // TYPES_H