#include <cstddef>
#include <cstdlib>
#include <unistd.h>
#include <sys/mman.h>

#include <vector>
#include <memory>
#include <atomic>

#include "types.h"
#include "mem.h"

namespace memory {

// chunk and block are synonymous
typedef struct chunk {  
    void* page;  // page we belong w/i
    bool in_use;
    void* data;     // idk what datatype to make this but this is the body of the chunk
    // we don't need to store what segment it belongs to b/c 
    // that can be easily deduced w/ `segment_base = ptr & ~(SEGMENT_SIZE - 1)`
} chunk_t;

typedef struct guard_chunk {
    int64_t guard;  // software tagging mechanism 
    // turn this into a class if necessary, and give it an 'integrity_check()' function.
};

typedef struct tc_page_s {
    Page*       loc;        // where is this page? should this be encoded w/ segment key? YES.
    page_kind_t kind;
    size_t      size;       // obj sizes w/i this page.
    void*       freelist;
} tc_page_t;

typedef struct thread_cache {
    std::vector<tc_page_t*> active; // all the pages (slot sizes) that a tcache can handle
} tc; // tomato cucumber 


// page and slot are synonymous here.
/*
    offset  size  field
-----------------------------------------
0       4     uint32_t slot_count
4       4     uint32_t slot_off
8       1     uint8_t is_committed:1
8       1     uint8_t is_zero_init:1   (same byte)
9       1     padding
10      2     uint16_t capacity
12      2     uint16_t reserved
14      2     padding (align next pointer to 8)
16      8     chunk_t* free
24      2     uint16_t used
26      6     padding (align size_t to 8)
32      8     size_t chunk_sz
40      8     uint32_t* page_start
-----------------------------------------
Total: 48 bytes... for now.
*/
class Page {
    private:
        tc          owner_tid;          // if this page is handled by a singular thread in a
                                        // multi threaded env, this is that threads metadata
                                        // if owned by a different thread, you shouldn't
                                        // be able to alloc memory from that page.
        uint32_t    slot_count;         // slots in this page
        uint32_t    slot_off;           // dist from start of containg page.
        uint8_t     is_committed:1;     // 'true' if the page vmem is commited

        uint16_t    capacity;           // num chunks committed to phys
        uint16_t    reserved;           // num chunks reserved in vmem
        // page_flags?

        std::atomic<bool>   free_bm;    // bitmap of available free blocks for regular allocation 
                                        // all zeros if being used be tcache
        // instead of a bitmap we could do an array of indices(into free list) 
        // and then randomly pick one of those indices. This would likely 
        // be easier to implement.

        std::vector<chunk*> freelist;   // freelist for tcache to improve locality. used in free's fast path
                                        // if used by slow path, choose random chunk using free_bm
        uint16_t    used;               // num chunks being used
        size_t      chunk_sz;           // size of the chunks in this slot.
        chunk_t*    page_start;         // pointer to end of guard page in front chunks
        // uintptr_t   keys[2];            // two random keys to encode the free lists

        Page()  = default;
        ~Page() = default;
    public:
        static Page& instance() {
            static Page page;
            return page; 
        }
        void* find_space();             // this will likely be moved but my idea is that if we need to use realloc then we'd have to go back to the segment level and see if they have what we need. Could also be used for free() since it should be the segment that controls which pages get freed, not the page itself.
        void  tc_free_push();           // push chunk onto thread's free list, 
};


class Segment {
    private:
        page_kind_t            sz_class;    // small, medium, large, XL (determines offsets per slot)
        std::vector<Page>      slots;       // metadata for contained pages.
        std::vector<bool>      active;      // bitmap to track the status of a given page so we can free phys mem if necessary.
        uint64_t               canary;

        Segment()  = default;
        ~Segment() = default;

    public:
        page_kind_t get_size_class();
        void set_chunk_size(int chunk_sz, uint16_t chunk_id); // id is used so we can index. 

        static Segment& instance() {
            static Segment segment;
            return segment;
        }
};


class Heap {
    private:
        memid_t memid;                          // alloc id from OS/Arena
        std::unique_ptr<int> base;              // ptr to base of custom heap. every segment w/i 'layout' should be an offset I can add to this pointer.
        uint32_t num_segments;
        std::vector<Segment> layout;            // |metadata|segment(off)|guard|segment(off)|...
        std::vector<segment_kind_t> seg_kind;   // reflection of layout, representing types.
        memkind_t mem_kind;
        uint64_t canary;

        Heap()   = default;
        ~Heap()  = default;

    public:
        std::vector<segment_kind_t> get_segment_kinds();
        uint32_t get_num_segments();
        bool is_corrupted();

        static Heap& instance() {
            static Heap heap;
            return heap;
        }
};




/*
    Notes:
    I want to make it so that each segment only houses objects/chunks
    of one page type. logically this means this segment may be only small pages,
    so every 64KB, there is a slot(span) for different obj sizes

    as for tracking free chunks. I think we should start by allocating say 3 segments,
    2 for small pages, 1 for medium(or 1small 2med?). at a high level,  

    In the metadata for each segment, we will track every page w/i it once again - if it is allocated and ACTIVE
    then we additionally track its size. I guess a good solution for this would be to track all active pages based on the 
    size of obj they're encapsulating. 
    ^ back to the free pages, scrap what i just said. I think we should also track the size w/i the metadata(for the page)
    and this will remain regardless of free'd status. This should be track to quickly re-allocate pages. <- update: we already
    do this inside of chunk_t, that is mainly for the "freed" chunks
    
    when it comes to accessing this page/slot metadata from outside of the corresponding chunk,
    instead of doing next and prev ptrs for the other slots, lets just make a vector corresponding to the segment.
*/

}