#ifndef SEGMENTS_H
#define SEGMENTS_H

/*
    |Segment*|||
    segment metadata can be found by taking the page ptr, finding the segment base using the algorithm
    and then using the lower 16 bits as a 'key' to the metadata dict. 
    scratch that. its not a bad idea but not for this. 
    I need to use the upper 16 bits as the key since all of the pages w/i the segment share this value.

*/

#include "types.h"
#include "stdint.h"

typedef struct chunk_header chunk_t;
typedef struct page_s page_t;

enum page_kind_e {
    PAGE_SM,            // small blocks go into 64KiB pages inside a segment
    PAGE_MED,           // medium   ^   ^   ^   512KiB   ^    ^    ^    ^
    PAGE_LG,            // large blocks likely go into a single page, spanning a whole segment
    PAGE_XL             // x-large will either split(unlikely) or default to mmap.
} page_kind_t;

enum segment_kind_e {
    SEGMENT_NORM,       // Most allocations
    SEGMENT_HUGE        // if page kind is "huge"
} segment_kind_t;


// Memory can reside in arena's, direct OS allocated, or statically allocated. The memid keeps track of this.
typedef enum memkind_e {
  MEM_NONE,      // not allocated
  MEM_EXTERNAL,  // not owned by mimalloc but provided externally (via `mi_manage_os_memory` for example)
  MEM_STATIC,    // allocated in a static area and should not be freed (for arena meta data for example)
  MEM_OS,        // allocated from the OS
  MEM_OS_HUGE,   // allocated as huge OS pages (usually 1GiB, pinned to physical memory)
  MEM_OS_REMAP,  // allocated in a remapable area (i.e. using `mremap`)
  MEM_ARENA      // allocated from an arena (the usual case)
} memkind_t;

static inline bool memkind_is_os(memkind_t memkind) {
  return (memkind >= MEM_OS && memkind <= MEM_OS_REMAP);
}

typedef struct memid_os_info {
  void*         base;               // actual base address of the block (used for offset aligned allocations)
  size_t        size;               // full allocation size
} memid_os_info_t;
typedef struct memid_arena_info {
  size_t        block_index;        // index in the arena
  uint8_t       id;                 // arena id (>= 1)
  bool          is_exclusive;       // this arena can only be used for specific arena allocations
} memid_arena_info_t;                           
typedef struct memid_s {
  union {
    memid_os_info_t    os;       // only used for MI_MEM_OS
    memid_arena_info_t arena;    // only used for MI_MEM_ARENA
  } mem;
  bool          is_pinned;          // `true` if we cannot decommit/reset/protect in this memory (e.g. when allocated using large (2Mib) or huge (1GiB) OS pages)
  bool          initially_committed;// `true` if the memory was originally allocated as committed
  bool          initially_zero;     // `true` if the memory was originally zero initialized
} memid_t;  

#endif  SEGMENTS_H

// should I make a heap_s aswell?