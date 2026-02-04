#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <unistd.h>
#include <sys/mman.h>
#include "types.h"
#include "segments.h"

struct chunk_header {  
    void* loc;  // is this dangerous? I should likely do offset instead. then we can calculate pointers dynamically.     
    size_t size;
    bool in_use;
    // we don't need to store what segment it belongs to b/c 
    // that can be easily deduced w/ `segment_base = ptr & ~(SEGMENT_SIZE - 1)`
    int32_t guard;  // software tagging mechanism
};

// page and slot are synonymous here.
struct page_s {
    uint32_t    slot_count;         // slots in this page
    uint32_t    slot_off;           // dist from start of containg page.
    uint8_t     is_committed:1;     // 'true' if the page vmem is commited
    uint8_t     is_zero_init:1;    // 'true' if page is zero initialized

    uint16_t    capacity;           // num chunks committed to phys
    uint16_t    reserved;           // num chunks reserved in vmem
    // page_flags?

    chunk_t*    free;               // list of available free blocks
    uint16_t    used;               // num chunks being used
    size_t      chunk_sz;           // size of the chunks in this slot.
    uint32_t*   page_start;         // pointer to end of guard page in front chunks
    // uintptr_t   keys[2];            // two random keys to encode the free lists
    // any other padding i need for alignment...
};

struct segment_s {
    memid_t     memid;      // memory id for arena/OS allocation
};



// // Good size for allocation
// size_good_size(size_t size) attr_noexcept {
//   if (size <= MEDIUM_OBJ_SIZE_MAX) {
//     return _bin_size(mi_bin(size + PADDING_SIZE));
//   }
//   else {
//     return _align_up(size + PADDING_SIZE,_os_page_size());
//   }
// }


/*
    Notes:
    I want to make it so that each segment only houses objects/chunks
    of one page type. logically this means this segment may be only small pages,
    so every 64KB, there is a slot(span) for different obj sizes

    as for tracking free chunks. I think we should start by allocating say 3 segments,
    2 for small pages, 1 for medium(or 1small 2med?). at a high level, we should be able to track free pages
    w/i these segments and the way we'll track these is a vector<> bitmap so that 1. it can grow overtime
    w/ the addition of new segments, and 2. each bit cna represent the status of a page. 

    In the metadata for each segment, we will track every page w/i it once again - if it is allocated and ACTIVE
    then we additionally track its size. I guess a good solution for this would be to track all active pages based on the 
    size of obj they're encapsulating. 
    ^ back to the free pages, scrap what i just said. I think we should also track the size w/i the metadata(for the page)
    and this will remain regardless of free'd status. This should be track to quickly re-allocate pages. <- update: we already
    do this inside of chunk_t, that is mainly for the "freed" chunks
    
    when it comes to accessing this page/slot metadata from outside of the corresponding chunk,
    instead of doing next and prev ptrs for the other slots, lets just make a vector corresponding to the segment.
*/