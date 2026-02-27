#ifndef TYPES_H
#define TYPES_H

#include <stddef.h>
#include <stdint.h>
#include <cmath>

#define KB      1024        // actually KiB
#define MB      (KB*KB)     // ^      ^ MiB
#define GB      (MB*KB)     // ^      ^ GiB

#define ZU(x)  x##ULL
#define ZI(x)  x##LL

#define SMALL_PAGE_SHIFT    (20) // 1 mib
#define MEDIUM_PAGE_SHIFT   (23) // 8 mib
#define LARGE_PAGE_SHIFT    (24) // 16 mib
#define SEGMENT_SHIFT       (27) // 128 mib

#define SMALL_PAGE_SIZE     (ZU(1)<<SMALL_PAGE_SHIFT)
#define MEDIUM_PAGE_SIZE    (ZU(1)<<MEDIUM_PAGE_SHIFT)
#define LARGE_PAGE_SIZE     (ZU(1)<<LARGE_PAGE_SHIFT)

#define SEGMENT_SIZE        (ZU(1)<<SEGMENT_SHIFT)
#define SEGMENT_ALIGN       SEGMENT_SIZE
#define SEGMENT_MASK        ((uintptr_t)(SEGMENT_ALIGN - 1))

#define HEAP_RESERVED_DEFAULT (100ULL * GB)

#define MIN_ALIGNMENT       (64*KB)
#define MAX_ALIGNMENT       LARGE_PAGE_SIZE

constexpr static inline bool is_power_of_2(size_t n) {
    return n && !(n & (n - 1));
}

#endif // TYPES_H
