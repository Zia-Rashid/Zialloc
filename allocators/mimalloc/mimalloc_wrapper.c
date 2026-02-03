#include "allocator.h"
#include <mimalloc.h>
#include <stdbool.h>
#include <stdio.h>

// Wrapper for mimalloc
// Link with -lmimalloc

static void *mi_malloc_wrapper(size_t size) { return mi_malloc(size); }

static void mi_free_wrapper(void *ptr) { mi_free(ptr); }

static void *mi_realloc_wrapper(void *ptr, size_t size) {
  return mi_realloc(ptr, size);
}

static void *mi_calloc_wrapper(size_t nmemb, size_t size) {
  return mi_calloc(nmemb, size);
}

static void *mi_memalign_wrapper(size_t alignment, size_t size) {
  return mi_malloc_aligned(size, alignment);
}

static void *mi_aligned_alloc_wrapper(size_t alignment, size_t size) {
  return mi_malloc_aligned(size, alignment);
}

static size_t mi_usable_size_wrapper(void *ptr) {
  return mi_malloc_usable_size(ptr);
}

static void mi_print_stats_wrapper(void) { mi_stats_print(NULL); }

static int mi_init(void) {
  // Optional: mi_version() check
  return 0;
}

static void mi_teardown(void) {
  // nothing
}

// Global allocator instance
allocator_t allocator = {.malloc = mi_malloc_wrapper,
                         .free = mi_free_wrapper,
                         .realloc = mi_realloc_wrapper,
                         .calloc = mi_calloc_wrapper,
                         .memalign = mi_memalign_wrapper, // For Guard Page test
                         .aligned_alloc = mi_aligned_alloc_wrapper,
                         .usable_size = mi_usable_size_wrapper,
                         .print_stats = mi_print_stats_wrapper,
                         .init = mi_init,
                         .teardown = mi_teardown,
                         .name = "mimalloc",
                         .author = "Microsoft",
                         .version = "2.1.7", // Approximate
                         .description = "Compact general purpose allocator",
                         .memory_backend = "mimalloc internals",
                         .features = {.thread_safe = true,
                                      .per_thread_cache = true,
                                      .huge_page_support = true,
                                      .guard_pages = false,
                                      .guard_location = GUARD_NONE,
                                      .canaries = false,
                                      .quarantine = false,
                                      .zero_on_free = false,
                                      .min_alignment = 8, // mimalloc default
                                      .max_alignment = 1024 * 1024}};

allocator_t *get_test_allocator(void) { return &allocator; }
allocator_t *get_bench_allocator(void) { return &allocator; }
