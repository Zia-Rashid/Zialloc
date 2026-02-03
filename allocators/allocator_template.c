// allocator_template.c - Template for Custom Allocator Implementation
// Copy this file and implement the functions for your allocator.
//
// Usage:
//   1. Copy this file to allocators/<yourname>/youralloc.c
//   2. Implement the TODO sections
//   3. Build: make ALLOCATOR=allocators/<yourname>/youralloc.c run-tests

#define _GNU_SOURCE
#include "allocator.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define PAGE_SIZE 4096
#define MIN_ALIGNMENT 16
#define MAX_ALIGNMENT 4096

// TODO: Add your size classes, thresholds, etc.
// #define SMALL_THRESHOLD   256
// #define LARGE_THRESHOLD   (128 * 1024)

// TODO: Define your metadata structures
// Example:
// typedef struct chunk_header {
//     size_t size;
//     bool in_use;
//     struct chunk_header* next;
//     struct chunk_header* prev;
// } chunk_header_t;

static bool g_initialized = false;

// TODO: Add your allocator state
// static void* g_heap_start = NULL;
// static size_t g_heap_size = 0;
// static chunk_header_t* g_free_list = NULL;

// Statistics tracking
static allocator_stats_t g_stats = {0};

// Round up to alignment
static inline size_t align_up(size_t size, size_t alignment) {}

// Check if power of 2
static inline bool is_power_of_2(size_t n) {}

// Get memory from OS
static void *os_alloc(size_t size) {}

// Return memory to OS
static void os_free(void *ptr, size_t size) {}

static void *myalloc_malloc(size_t size) {}

static void myalloc_free(void *ptr) {}

static void *myalloc_realloc(void *ptr, size_t size) {}

static void *myalloc_calloc(size_t nmemb, size_t size) {}

// Uncomment and implement these for bonus points

/*
static void* myalloc_memalign(size_t alignment, size_t size) {


}

static void* myalloc_aligned_alloc(size_t alignment, size_t size) {

}

static size_t myalloc_usable_size(void* ptr) {

}

static void myalloc_free_sized(void* ptr, size_t size) {

}

static void* myalloc_realloc_array(void* ptr, size_t nmemb, size_t size) {

}

static void myalloc_bulk_free(void** ptrs, size_t count) {

}
*/

static void myalloc_print_stats(void) {
  printf("  Allocations:   %lu\n", (unsigned long)g_stats.alloc_count);
  printf("  Frees:         %lu\n", (unsigned long)g_stats.free_count);
  printf("  Reallocs:      %lu\n", (unsigned long)g_stats.realloc_count);
  printf("  Bytes in use:  %zu\n", g_stats.bytes_in_use);
  printf("  Bytes mapped:  %zu\n", g_stats.bytes_mapped);
  printf("  mmap calls:    %lu\n", (unsigned long)g_stats.mmap_count);
  printf("  munmap calls:  %lu\n", (unsigned long)g_stats.munmap_count);
}

static bool myalloc_validate_heap(void) {}

static bool myalloc_get_stats(allocator_stats_t *stats) {}

static int myalloc_init(void) {}

static void myalloc_teardown(void) {}

allocator_t myalloc_allocator = {
    // Required functions
    .malloc = myalloc_malloc,
    .free = myalloc_free,
    .realloc = myalloc_realloc,
    .calloc = myalloc_calloc,

    // Optional functions - set to NULL if not implemented
    .memalign = NULL,      // myalloc_memalign,
    .aligned_alloc = NULL, // myalloc_aligned_alloc,
    .usable_size = NULL,   // myalloc_usable_size,
    .free_sized = NULL,    // myalloc_free_sized,
    .realloc_array = NULL, // myalloc_realloc_array,
    .bulk_free = NULL,     // myalloc_bulk_free,

    // Diagnostics
    .print_stats = myalloc_print_stats,
    .validate_heap = myalloc_validate_heap,
    .get_stats = myalloc_get_stats,

    // Lifecycle
    .init = myalloc_init,
    .teardown = myalloc_teardown,

    // Metadata - UPDATE THESE!
    .name = "myalloc",
    .author = "Your Name Here",
    .version = "0.1.0",
    .description = "My custom memory allocator",
    .memory_backend = "mmap",

    // Features - If you want to enable a feature, set it to true
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

allocator_t *get_test_allocator(void) { return &myalloc_allocator; }

allocator_t *get_bench_allocator(void) { return &myalloc_allocator; }
