#ifndef SEGMENTS_H
#define SEGMENTS_H

/*
    Heap-Level:     |Metadata|Segment|guard|Segment|guard|Segment|...| # size =
2GB Segment-Level:  |Metadata|guard|slot|slot|slot|...| # size = 4MB->(32MB*)
(this would mean that about 2048 segments couldfit inside of our heap at any
time which is almost a bit too much so what I will actualy do is set this value
higher meaning each segment is larger. if we use the same pointer access trick
as before and maintain our alignment then we should be able to access any given
page in constant time so it hsouldn't be that big of a deal that we have a
larger segment size. Thus set segment size to say 32MB?) Page-Level:
|Metadata|chunk|chunk|chunk|...|guard|                      # size : small=64KB,
med=512KB, large=4MiB   ---> this means we can fit multiple large pages w/i a
segment, XL allocations will still be handled using MMAP.

    Heap metadata: we should track how many segments are active, their types,
the location of the metadata corresponding to a given chunk so that we can
access it if it contains a size and space we need.
.
    It contains information about the sizes that the pages w/i it support, a
bitmap that tracks the allocation status of the contained pages. This can be
found by querying the metadata of each page and checking if used == 0. Maybe
also track the status of the segment itself; any given segment could be
completely empty, active, or full. But we should probably make sure there is a
minimum number (1sm, 1md) active at all times.


    I don't intend to implement any form of coalescing - much like partition
alloc, I will release memory to OS but keep the vmem reservation

*/
// lets make sure our guard pages are the same size as the chunks so that we can
// handle indexing w/ offsets normally

#include "types.h"
#include <cstdint>
#include <iostream>
#include <mutex> // for multi-threaded allocation
#include <random>
#include <syscall.h>
#include <thread> // for multi-thread
#include <unistd.h>

#define INTEGRITY_CHECK(condition, message)                                    \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Integrity Failure: " << message << " at " << __FILE__      \
                << ":" << __LINE__ << std::endl;                               \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

#define PTR_IN_BOUNDS(condition, message)                                      \
  do {                                                                         \
    if (!(condition)) {                                                        \
      std::cerr << "Illegal Pointer: " << message << " at " << __FILE__ << ":" \
                << __LINE__ << std::endl;                                      \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

// Abort if heap has not been initialized (use only after malloc/calloc have
// run at least once). Use in free, realloc, and any other non-alloc entry.
#define IS_HEAP_INITIALIZED(cond)                                              \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "Heap not initialized: cannot call this function before "   \
                   "malloc or calloc."  << " at " << __FILE__ << ":"           \
                   << __LINE__ << std::endl;                                   \
      std::abort();                                                            \
    }                                                                          \
  } while (0)

typedef enum page_kind_e {
  PAGE_SM,  // small blocks go into 1mib pages inside a segment
  PAGE_MED, // medium   ^   ^   ^   8mib   ^    ^    ^    ^
  PAGE_LG,  // large    ^   ^   ^   16mib     ^    ^    ^    ^
  PAGE_XL   // x-large will either split(unlikely) or default to mmap.
} page_kind_t;

constexpr size_t page_kind_size(int kind) {
    return kind == PAGE_SM  ? (ZU(1) << SMALL_PAGE_SHIFT) :
           kind == PAGE_MED ? (ZU(1) << MEDIUM_PAGE_SHIFT) :
           kind == PAGE_LG  ? LARGE_PAGE_SIZE :
                              SEGMENT_SIZE;
}

typedef enum segment_kind_e {
  SEGMENT_NORM, // most allocations
  SEGMENT_HUGE, // if page kind is "huge"
  SEGMENT_GUARD
} segment_kind_t;

typedef enum page_status_e { FULL, ACTIVE, EMPTY } page_status_t;

// all power aligned (i think), we can fill unused space w/ guard chunks.
// max_chunk <= (usable / 2) - alignment // so, ...
// Compute usable = P - overhead
// Set max_chunk = floor(usable / 2)
typedef enum chunk_max_e {
  // keep per-class max chunk <= ~half page so each page can hold at least 2
  // chunks even after metadata/alignment
  CHUNK_SM = 0x7FFF0,  // 512kib - 16b
  CHUNK_MD = 0x3FFFF0, // 4mib - 16b
  CHUNK_LG = 0x7FFFF0, // 8mib - 16b
  CHUNK_XL             // whatever it wants to be
} chunk_max_t;

// Memory can reside in arena's, direct OS allocated, or statically allocated.
// The memid keeps track of this.
typedef enum memkind_e {
  MEM_NONE,     // not allocated
  MEM_EXTERNAL, // not owned by mimalloc but provided externally (via
                // `mi_manage_os_memory` for example)
  MEM_STATIC,   // allocated in a static area and should not be freed (for arena
                // meta data for example)
  MEM_OS,       // allocated from the OS
  MEM_OS_HUGE,  // allocated as huge OS pages (usually 1GiB, pinned to physical
                // memory)
  MEM_OS_REMAP, // allocated in a remapable area (i.e. using `mremap`)
  MEM_ARENA     // allocated from an arena (the usual case)
} memkind_t;

constexpr static inline bool memkind_is_os(memkind_t memkind) {
  return (memkind >= MEM_OS && memkind <= MEM_OS_REMAP);
}

typedef struct memid_os_info {
  void *base;  // actual base address of the block (used for offset aligned
               // allocations)
  size_t size; // full allocation size
} memid_os_info_t;
typedef struct memid_arena_info {
  size_t block_index; // index in the arena
  uint8_t id;         // arena id (>= 1)
  bool is_exclusive;  // this arena can only be used for specific arena
                      // allocations
} memid_arena_info_t;
typedef struct memid_s {
  union {
    memid_os_info_t os;       // MI_MEM_OS
    memid_arena_info_t arena; // MI_MEM_ARENA
  } mem;
  bool is_pinned; // `true` if we cannot decommit/reset/protect in this memory
  bool initially_committed; 
  bool initially_zero; 
} memid_t;

static inline pid_t current_tid() { return syscall(SYS_gettid); }

static inline uint64_t generate_canary() {
  std::random_device rd;     // Non-deterministic seed
  std::mt19937_64 gen(rd()); // 64-bit generator
  std::uniform_int_distribution<uint64_t> dis;
  return dis(gen);
}

#endif // SEGMENTS_H
