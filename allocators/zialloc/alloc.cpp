// Build: make ALLOCATOR=allocators/<yourname>/youralloc.c run-tests

#include "allocator.h"
#include "types.h"
#include "segments.cpp"
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
    // impl here using mmap, free lists, etc...
}
void Allocator::free(void* ptr) {

}
void* Allocator::realloc(void* ptr, size_t size) {

}
// nmemb is the # of values of size `size`. So calloc(4, sizeof(int)) may produce a 16 butes chunk
void* Allocator::calloc(size_t nmemb, size_t size) {   

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

// Round up to alignment
static inline size_t align_up(size_t size, size_t alignment) {}



// Get memory from OS
static void *os_alloc(size_t size) {}

// Return memory to OS
static void os_free(void *ptr, size_t size) {}

static void *zialloc_malloc(size_t size) {
  return zialloc::Allocator::instance().malloc(size);
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

static bool zialloc_validate_heap(void) {}

static bool zialloc_get_stats(allocator_stats_t *stats) {}

static int zialloc_init(void) {}

static void zialloc_teardown(void) {}

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
