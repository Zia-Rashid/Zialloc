#define JEMALLOC_NO_DEMANGLE
#include "allocator.h"
#include <jemalloc/jemalloc.h>
#include <stdbool.h>
#include <stdio.h>

// Jemalloc configured with --with-jemalloc-prefix=je_
// Symbols are je_malloc, je_free, etc.

static void *jm_malloc(size_t size) { return je_malloc(size); }
static void jm_free(void *ptr) { je_free(ptr); }
static void *jm_realloc(void *ptr, size_t size) {
  return je_realloc(ptr, size);
}
static void *jm_calloc(size_t nmemb, size_t size) {
  return je_calloc(nmemb, size);
}
static void *jm_memalign(size_t alignment, size_t size) {
  return je_aligned_alloc(alignment, size);
}
static void *jm_aligned_alloc(size_t alignment, size_t size) {
  return je_aligned_alloc(alignment, size);
}
static size_t jm_usable_size(void *ptr) { return je_malloc_usable_size(ptr); }

static void jm_print_stats(void) { je_malloc_stats_print(NULL, NULL, NULL); }

static int jm_init(void) { return 0; }
static void jm_teardown(void) {}

allocator_t allocator = {
    .malloc = jm_malloc,
    .free = jm_free,
    .realloc = jm_realloc,
    .calloc = jm_calloc,
    .memalign = jm_memalign, // Critical for alignment tests
    .aligned_alloc = jm_aligned_alloc,
    .usable_size = jm_usable_size,
    .print_stats = jm_print_stats,
    .init = jm_init,
    .teardown = jm_teardown,
    .name = "jemalloc",
    .author = "Jason Evans",
    .version = "5.3.0",
    .description =
        "General purpose allocator emphasized fragmentation avoidance",
    .memory_backend = "jemalloc",
    .features = {.thread_safe = true,
                 .per_thread_cache = true, // tcache
                 .huge_page_support = true,
                 .guard_pages = false,
                 .guard_location = GUARD_NONE,
                 .canaries = false,
                 .quarantine = false,
                 .zero_on_free = false,
                 .min_alignment = 8,
                 .max_alignment = 1024 * 1024}};

allocator_t *get_test_allocator(void) { return &allocator; }

allocator_t *get_bench_allocator(void) { return &allocator; }
