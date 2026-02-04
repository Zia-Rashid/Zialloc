#ifndef TYPES_H
#define TYPES_H

#define PAGE_SIZE 4096  
#define MIN_ALIGNMENT 16
#define MAX_ALIGNMENT 4096

#define KB      1024        // actually KiB
#define MB      (KB*KB)     // ^      ^ MiB
#define GB      (MB*KB)     // ^      ^ GiB

# define ZU(x)  x##ULL
# define ZI(x)  x##LL

#define SMALL_PAGE_SHIFT        (16)                       // 1 << 16          = 64KiB          
#define MEDIUM_PAGE_SHIF        (3 + SMALL_PAGE_SHIFT)     // (1<<16)*(1<<3)   = 512KiB

#define SEGMENT_SIZE            (ZU(1)<<SEGMENT_SHIFT)
#define SEGMENT_ALIGN           SEGMENT_SIZE
#define SEGMENT_MASK            ((uintptr_t)(SEGMENT_ALIGN - 1))
#define SMALL_PAGE_SIZE         (ZU(1)<<SMALL_PAGE_SHIFT)
#define MEDIUM_PAGE_SIZE        (ZU(1)<<MEDIUM_PAGE_SHIFT)

#define SMALL_OBJ_SIZE_MAX      1*KB
#define MEDIUM_OBJ_SIZE_MAX     (128 * KB)
#define LARGE_OBJ_SIZE_MAX      2 * MB   

// Check if power of 2
static inline bool is_power_of_2(size_t n) {}


#endif TYPES_H