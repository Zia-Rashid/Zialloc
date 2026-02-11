// Build: make ALLOCATOR=allocators/<yourname>/youralloc.c run-tests

#include "allocator.h"
#include "types.h"
#include "segments.cpp"
#include "os.cpp"
#include "free.cpp"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

namespace zialloc {

class Allocator {
    public:
        static Allocator& instance() {
            static Allocator alloc;
            return alloc;
        }
        void*   malloc(size_t size);
        void    free(void* ptr); 
        void*   realloc(void* ptr, size_t size);
        void*   calloc(size_t nmemb, size_t size);

        bool    init();
        void    teardown();
        void    print_stats();
        bool    get_stats(allocator_stats_t* stats);
        void    print_features();
        bool    get_feats(allocator_features_t* feats);
        bool    validate_heap();

    private:
        Allocator() = default;
        ~Allocator() = default;
       
        allocator_stats_t stats{};
};

void* Allocator::malloc(size_t size) {
    // TODO: 
    //   1. Determine size class (small / med / large / XL)
    //   2. Fast path: check tcache for a free chunk of this size
    //   3. Slow path: find a page with space via Heap -> Segment -> Page
    //   4. If no page has space, allocate a new segment via os::alloc_segment()
    //   5. Update stats (alloc_count, bytes_in_use, etc.)
    //   6. Return pointer to chunk data
    (void)size;
    return nullptr;
}

void Allocator::free(void* ptr) {
    // TODO: delegate to the free dispatch in free.cpp
    //   zialloc::zialloc_free_dispatch(ptr);
    //   Update stats (free_count, bytes_in_use, etc.)
    (void)ptr;
}

void* Allocator::realloc(void* ptr, size_t size) {
    // TODO:
    //   1. If ptr == nullptr, equivalent to malloc(size)
    //   2. If size == 0, equivalent to free(ptr), return nullptr
    //   3. Find the chunk's current size via Page::get_chunk_size()
    //   4. If new size fits in current chunk, return ptr as-is
    //   5. Otherwise: malloc(size), memcpy, free(ptr)
    //   6. Update stats (realloc_count)
    (void)ptr; (void)size;
    return nullptr;
}

// nmemb is the # of values of size `size`. So calloc(4, sizeof(int)) may produce a 16 bytes chunk
void* Allocator::calloc(size_t nmemb, size_t size) {   
    // TODO:
    //   1. Check for overflow: nmemb * size
    //   2. total = nmemb * size
    //   3. ptr = malloc(total)
    //   4. memset(ptr, 0, total)  — or skip if os_mmap already zeroed it
    //   5. return ptr
    (void)nmemb; (void)size;
    return nullptr;
}


}   

/*
    since I'm going to do all chunk metadata in one system page
    they will not be at the beginning of each chunk. 
    only the free pages will need *next and *prev.
*/


static bool g_initialized = false;

// TODO: Add your allocator state
// static void* g_heap_start = NULL;
// static size_t g_heap_size = 0;
// static chunk_header_t* g_free_list = NULL;

// Statistics tracking
static allocator_stats_t g_stats = {0};

// Round up to alignment — delegate to os.cpp
static inline size_t align_up(size_t size, size_t alignment) {
    // TODO: return zialloc::os::align_up(size, alignment)
    //   or inline: (size + alignment - 1) & ~(alignment - 1)
    (void)size; (void)alignment;
    return 0;
}

// Get memory from OS — delegate to os.cpp
static void *os_alloc(size_t size) {
    // TODO: return zialloc::os::alloc_segment(size)
    (void)size;
    return nullptr;
}

// Return memory to OS — delegate to os.cpp
static void os_free(void *ptr, size_t size) {
    // TODO: zialloc::os::free_segment(ptr, size)
    (void)ptr; (void)size;
}

static void *zialloc_malloc(size_t size) {
  return zialloc::Allocator::instance().malloc(size);
  // fast path should use pages/chunks in tcache
  // slow path is regular allocation
}

static void zialloc_free(void *ptr) {
  return zialloc::Allocator::instance().free(ptr);
}

static void *zialloc_realloc(void *ptr, size_t size) {
    return zialloc::Allocator::instance().realloc(ptr, size);
}

static void *zialloc_calloc(size_t nmemb, size_t size) {
    return zialloc::Allocator::instance().calloc(nmemb, size);
}



// Uncomment and implement these for bonus points

/*
static void* zialloc_memalign(size_t alignment, size_t size) {


}

static void* zialloc_aligned_alloc(size_t alignment, size_t size) {

}

static size_t zialloc_usable_size(void* ptr) {

}

static void zialloc_free_sized(void* ptr, size_t size) {

}

static void* zialloc_realloc_array(void* ptr, size_t nmemb, size_t size) {

}

static void zialloc_bulk_free(void** ptrs, size_t count) {

}
*/

static void zialloc_print_stats(void) {
  printf("  Allocations:   %lu\n", (unsigned long)g_stats.alloc_count);
  printf("  Frees:         %lu\n", (unsigned long)g_stats.free_count);
  printf("  Reallocs:      %lu\n", (unsigned long)g_stats.realloc_count);
  printf("  Bytes in use:  %zu\n", g_stats.bytes_in_use);
  printf("  Bytes mapped:  %zu\n", g_stats.bytes_mapped);
  printf("  mmap calls:    %lu\n", (unsigned long)g_stats.mmap_count);
  printf("  munmap calls:  %lu\n", (unsigned long)g_stats.munmap_count);
}

static bool zialloc_validate_heap(void) {
    // TODO: 
    //   1. Walk all segments via Heap::instance()
    //   2. For each segment, check_canary()
    //   3. For each page, verify used count matches bitmap popcount
    //   4. Return false if any inconsistency found
    return true;
}

static bool zialloc_get_stats(allocator_stats_t *stats) {
    // TODO: copy g_stats into *stats
    if (!stats) return false;
    *stats = g_stats;
    return true;
}

static int zialloc_init(void) {
    // TODO:
    //   1. If already initialized, return 0
    //   2. Allocate initial segments via os::alloc_segment() 
    //      (e.g. 1 small-page segment + 1 medium-page segment)
    //   3. Initialize Heap::instance() with the segments
    //   4. Initialize canaries via generate_canary()
    //   5. Initialize segment keys via Segment::init_key()
    //   6. Set g_initialized = true
    //   7. Return 0 on success, -1 on failure
    if (g_initialized) return 0;
    // ... your init logic here ...
    g_initialized = true;
    return 0;
}

static void zialloc_teardown(void) {
    // TODO:
    //   1. Walk all segments, munmap each via os::free_segment()
    //   2. Reset Heap metadata
    //   3. Zero out g_stats
    //   4. Set g_initialized = false
    g_initialized = false;
}

allocator_t zialloc_allocator = {
    // Required functions
    .malloc = zialloc_malloc,
    .free = zialloc_free,
    .realloc = zialloc_realloc,
    .calloc = zialloc_calloc,

    // Optional functions - set to NULL if not implemented
    .memalign = NULL,      // zialloc_memalign,
    .aligned_alloc = NULL, // zialloc_aligned_alloc,
    .usable_size = NULL,   // zialloc_usable_size,
    .free_sized = NULL,    // zialloc_free_sized,
    .realloc_array = NULL, // zialloc_realloc_array,
    .bulk_free = NULL,     // zialloc_bulk_free,

    // Diagnostics
    .print_stats = zialloc_print_stats,
    .validate_heap = zialloc_validate_heap,
    .get_stats = zialloc_get_stats,

    // Lifecycle
    .init = zialloc_init,
    .teardown = zialloc_teardown,

    // Metadata 
    .name = "zialloc",
    .author = "zia rashid",
    .version = "0.1.0",
    .description = "custom memory allocator",
    .memory_backend = "mmap",

    // Features
    .features =
        {
            .thread_safe = false,
            .per_thread_cache = false,
            .huge_page_support = false,
            .guard_pages = false,
            .canaries = false,
            .quarantine = false,
            .zero_on_free = false,
            .min_alignment = MIN_ALIGNMENT,
            .max_alignment = MAX_ALIGNMENT,
        },
};


allocator_t *get_test_allocator(void) { return &zialloc_allocator; }

allocator_t *get_bench_allocator(void) { return &zialloc_allocator; }
