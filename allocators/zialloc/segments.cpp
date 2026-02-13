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
class Chunk {
    private:
        Page*       page;            // page we belong w/i
        uint32_t    data_size : 31;  // usable size of data region (max ~2MiB)
        uint32_t    in_use    : 1;   // packed into the same 8 bytes as data_size
        void*       data;            // body of the chunk (user memory)
        // we don't need to store what segment it belongs to b/c 
        // that can be easily deduced w/ `segment_base = ptr & ~(SEGMENT_SIZE - 1)`

    public:
        Chunk() : page(nullptr), in_use(false), data(nullptr), data_size(0) {}
        Chunk(Page* owning_page, void* data_ptr, size_t size)
            : page(owning_page), in_use(false), data(data_ptr), data_size(size) {}

        void*   get_page()      const { return page; }
        bool    is_in_use()     const { return in_use; }
        void*   get_data()      const { return data; }  // this is what should be returned when "malloc" is called. 
                                                        // this way they aren't overwriting the metadata, only the data
                                                        // but we also have to make sure input is < data_size. 
        size_t  get_data_size() const { return data_size; }
        bool    set_data()      { 
            bool is_changed{false};

        }

        void mark_used()    { in_use = true; }
        void mark_free()    { in_use = false; }
};

// Placed at page boundaries: one before the first chunk, one after the last.
// Page layout: |GuardChunk|chunk|chunk|...|chunk|GuardChunk|
class GuardChunk {
    private:
        int64_t     pattern;    // known canary value, made at page init

    public:
        GuardChunk() : pattern(0) {}
        GuardChunk(int64_t val) : pattern(val) {}

        void set(int64_t val) { pattern = val; }

        // Check if the guard is still intact. Call on free() or during
        // validation to detect buffer overflows from neighboring chunks.
        bool integrity_check(int64_t expected) const {
            // If false, an overflow from an adjacent chunk corrupted the boundary.
            INTEGRITY_CHECK(pattern == expected, "*** canary smashing detected *** ");
            return true;
        }
};

typedef struct tc_page_s {
    Page*       loc;        // where is this page? should this be encoded w/ segment key? YES.
    page_kind_t kind;
    size_t      size;       // obj sizes w/i this page.
    void*       freelist;
} tc_page_t;

typedef struct thread_cache {
    pid_t                   tid;
    thread_local TCache     tcache; // TODO: this isn't being recognized as correct
    bool                    is_active;
    std::vector<tc_page_t*> active; // all the pages (slot sizes) that a tcache can handle
} tc; // tomato cucumber 

// i have to make functions to push to the vector, set tid, and set is_active. could make class if necessary.
// actually 100% should turn this into a class. I want to maximize the multi-threaded capabilities
// and tcache usage so I need to refactor the Pages to support that while still supporting 
// non-thread_local allocation and keeping all of my security and design intentions.

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
16      8     Chunk* free
24      2     uint16_t used
26      6     padding (align size_t to 8)
32      8     size_t chunk_sz
40      8     uint32_t* page_start
-----------------------------------------
Total: 48 bytes... for now.
*/
class Page {
    private:
        tc          owner_tc;          // if this page is handled by a singular thread in a
                                        // multi threaded env, this is that threads metadata
                                        // if owned by a different thread, you shouldn't
                                        // be able to alloc memory from that page.
                                        // idk if this has to be a ptr or not.
        page_kind_t size_class;
        uint32_t    slot_count;         // slots in this page
        uint8_t     is_committed:1;     // 'true' if the page vmem is commited

        uint16_t    num_committed;      // num chunks committed to phys
        uint16_t    reserved;           // num chunks reserved in vmem
        uint16_t    capacity;           // maximum # of chunks in this page. floor(page_size/chunk_size)
        // page_flags?

        std::atomic<bool>   free_bm;    // bitmap of available free blocks for regular allocation 
                                        // all zeros if being used be tcache
        // instead of a bitmap we could do an array of indices(into free list) 
        // and then randomly pick one of those indices. This would likely 
        // be easier to implement.

        std::vector<Chunk*> freelist;   // freelist for tcache to improve locality. used in free's fast path
                                        // if used by slow path, choose random chunk using free_bm
        uint16_t    used;               // num chunks being used
        size_t      chunk_sz;           // size of the chunks in this slot.
        Chunk*      page_start;         // pointer to first chunk (after front guard)

        // Layout: |front_guard|chunk|chunk|...|chunk|back_guard|
        GuardChunk  front_guard;        // sits before first chunk
        GuardChunk  back_guard;         // sits after last chunk

        page_status_t status;           // FULL, ACTIVE, or EMPTY
        uint64_t      prng_state;       // PRNG state for random free-slot selection (non-threaded pages)

    public:
        Page()  = default;
        ~Page() = default;

        // Construct a page with its core layout parameters.
        // guard_canary: value to stamp into front/back guards at init.
        Page(size_t chunk_size, uint16_t max_capacity, int64_t guard_canary)
            : slot_count(0), is_committed(0),
              num_committed(0), reserved(0), capacity(max_capacity),
              free_bm(false), used(0), chunk_sz(chunk_size),
              page_start(nullptr),
              front_guard(guard_canary), back_guard(guard_canary),
              status(EMPTY), prng_state(0) {}

        void* find_space();  // this will likely be moved but my idea is that if we need to use realloc then we'd have to go back to the segment level and see if they have what we need. Could also be used for free() since it should be the segment that controls which pages get freed, not the page itself.

        // ── Free-path accessors (used by free.cpp) ──

        // Get the base address of this page's chunk data region
        Chunk* get_page_start() const {
            // as a security measure we should assert that this pointer is w/i the page
            uintptr_t base = (uintptr_t)this;
            uintptr_t ptr  = (uintptr_t)page_start;
            PTR_IN_BOUNDS((base < ptr && ptr < (base + PAGE_KIND_SIZE(size_class))),
                          "Illegal Pointer - not in bounds");
            return page_start;
        }

        // Set page_start to the address of the first instantiated Chunk.
        void set_page_start(Chunk* first_chunk) {
            uintptr_t base = (uintptr_t)this;
            uintptr_t ptr = (uintptr_t)first_chunk;
            PTR_IN_BOUNDS((base < ptr && ptr < (base + PAGE_KIND_SIZE(size_class))),
                          "Illegal Pointer - not in bounds");
            page_start = first_chunk;
        }

        // called at page init
        void init_guards(int64_t canary_val) {
            front_guard.set(canary_val);
            back_guard.set(canary_val);
        }

        // Will abort if either are corrupted
        bool check_guards(int64_t expected) const {
            return front_guard.integrity_check(expected) 
                 && back_guard.integrity_check(expected);
        }

        GuardChunk&       get_front_guard()       { return front_guard; }
        GuardChunk&       get_back_guard()        { return back_guard; }
        const GuardChunk& get_front_guard() const { return front_guard; }
        const GuardChunk& get_back_guard()  const { return back_guard; }

        size_t get_chunk_size() const {
            return chunk_sz;
        }

        // how many are currently in use
        uint16_t get_used() const {
            return used;
        }

        // decrement used count & update page status 
        void dec_used() {
            used--;
            if (used == 0) status = EMPTY;
            else if (status == FULL) status = ACTIVE;  // was full, now has space
        }
        // inc and dec should have corresponding funcs to update alloc bitmap
        // Increment used count and update page status
        void inc_used() {
            used++;
            if (used == capacity) status = FULL;
            else status = ACTIVE;
        }

        // Check if this page is owned by the calling thread
        bool is_owned_by_current_thread() const {
            return owner_tc.tid == current_tid();
        }

        // Check if this page is used by a tcache (threaded fast-path)
        bool is_tcache_page() const {
            return owner_tc.is_active; // change to pointer if necessary
        }

        // get current page status
        page_status_t get_status() const {
            return status;
        }

        // Compute the slot index from a chunk pointer
        uint16_t slot_index_of(void* chunk_ptr) const {
            return (uintptr_t(chunk_ptr) - uintptr_t(page_start)) / chunk_sz;
        }

        // Get the PRNG state for random allocation (non-threaded pages)
        uint64_t next_prng() { 
            // TODO: xorshift64 on prng_state, return result
            return 0;
        }

        // <type> tc_get_next_free_chunk(); // pops end of freelist
        // <type> choose_rand_free_chunk(); // chooses random idx from free list.
        // issue w/ the above - main thread may have to be an exception
        // otherwise the 2nd function will simply never be used and the 
        // randomization capability will never be utilized.
};


class Segment {
    private:
        page_kind_t            size_class;  // small, medium, large, XL (determines offsets per slot)
        std::vector<Page>      slots;       // metadata for contained pages.
        std::vector<bool>      active;      // bitmap to track the status of a given page so we can free phys mem if necessary.
        uint64_t               canary;
        uint64_t               key;         // segment key for encoding free-list pointers

        Segment()  = default;
        ~Segment() = default;

    public:
        page_kind_t get_size_class();
        void set_chunk_size(int chunk_sz, uint16_t chunk_id); // id is used so we can index. 

        static Segment& instance() {
            static Segment segment;
            return segment;
        }

        // for pointer encoding/decoding
        uint64_t get_key() const {
            return key;
        }

        // call once at segment creation
        void init_key() {
            key = generate_canary();
        }

        // Lookup the page metadata for a given pointer within this segment.
        Page* find_page_for(void* ptr) {
            auto offset = (uintptr_t)ptr - (uintptr_t)this;      // <- segment base
            auto page_index = offset / PAGE_KIND_SIZE(size_class);
            return &slots[page_index];
        }

        // Check tha all pages in this segment are EMPTY (reclaimable to OS)
        bool is_fully_empty() const {
            for (size_t i = 0; i < slots.size(); ++i) {
                page_status_t status = slots.at(i).get_status(); 
                if (status != EMPTY) 
                    return false;      
            }
            return true;
        }

        void mark_page_inactive(uint16_t page_index) {
            active[page_index] = false;
        }

        void mark_page_active(uint16_t page_index) {
            active[page_index] = true;
        }

        bool check_canary(uint64_t expected) const {
            return canary == expected;
        }

        size_t num_pages() const {
            return slots.size();
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
        
        // Find the Segment that contains `ptr`.
        // segment_base = ptr & ~(SEGMENT_SIZE - 1), then look up in layout.
        Segment* find_segment_for(void* ptr) {
            uintptr_t seg_base = (uintptr_t)ptr & ~SEGMENT_MASK;
            uintptr_t heap_base = (uintptr_t)base.get();
            size_t seg_index = (seg_base - heap_base) / SEGMENT_SIZE;
            return &layout[seg_index];
        }
        // idk if this will work as is becasue the start of the heap isn't necessarily 
        // a segment right away - could be metadata

        // Remove a segment from the heap (after full release to OS).
        void remove_segment(uint32_t seg_index) {
            // TODO: update layout, seg_kind, num_segments
            (void)seg_index;
        }   // worry abt this later

        void* get_base() const {
            return base.get();
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
    do this inside of Chunk, that is mainly for the "freed" chunks
    
    when it comes to accessing this page/slot metadata from outside of the corresponding chunk,
    instead of doing next and prev ptrs for the other slots, lets just make a vector corresponding to the segment.
*/

}

/*
    ═══════════════════════════════════════════════════════════════
    WHAT TO DO NEXT (segments.cpp):
    ═══════════════════════════════════════════════════════════════

    New stubs added to the existing classes (marked "Free-path accessors"):

    PAGE:
      - get_page_start(), get_chunk_size(), get_used()     — trivial field returns
      - dec_used() / inc_used()                            — decrement/increment + update status enum
      - is_owned_by_current_thread()                       — compare owner_tid to pthread_self or similar
      - is_tcache_page()                                   — check if freelist is being used by tcache
      - slot_index_of(ptr)                                 — pointer arithmetic: (ptr - page_start) / chunk_sz
      - next_prng()                                        — xorshift64 on prng_state for random alloc

    SEGMENT:
      - get_key() / init_key()                             — segment key for pointer encoding (see free.cpp)
      - find_page_for(ptr)                                 — offset math to find which Page slot a ptr lands in
      - is_fully_empty()                                   — walk slots checking status
      - mark_page_inactive/active(idx)                     — toggle active bitmap
      - check_canary()                                     — integrity verification
      - num_pages()                                        — slots.size()

    HEAP:
      - find_segment_for(ptr)                              — ptr & ~SEGMENT_MASK → index into layout
      - remove_segment(idx)                                — cleanup after full segment release
      - get_base()                                         — return base.get()

    The implementations are all TODO stubs with hints. Start with the 
    trivial getters, then do slot_index_of and find_page_for since 
    those are critical for the free dispatch in free.cpp.
    ═══════════════════════════════════════════════════════════════
*/


// side note: I heard vector<bool> is not real and actually just uses bytes behind the scene 
// so consider switching this to a uint# if size is known, otherwise continue w/ bools.