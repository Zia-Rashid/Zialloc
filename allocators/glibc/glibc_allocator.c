// glibc_allocator.c - Wrapper around glibc malloc for testing baseline

#include "allocator.h"
#include <inttypes.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Internal State (for statistics tracking)

static allocator_stats_t glibc_stats = {0};
static bool glibc_initialized = false;

// Wrapper Functions

static void *glibc_malloc(size_t size) {
  void *ptr = malloc(size);
  if (ptr) {
    glibc_stats.alloc_count++;
    glibc_stats.bytes_allocated += size;
    glibc_stats.bytes_in_use += malloc_usable_size(ptr);
  }
  return ptr;
}

static void glibc_free(void *ptr) {
  if (ptr) {
    glibc_stats.free_count++;
    glibc_stats.bytes_in_use -= malloc_usable_size(ptr);
  }
  free(ptr);
}

static void *glibc_realloc(void *ptr, size_t size) {
  size_t old_usable = ptr ? malloc_usable_size(ptr) : 0;
  void *new_ptr = realloc(ptr, size);

  if (new_ptr) {
    glibc_stats.realloc_count++;
    if (ptr) {
      glibc_stats.bytes_in_use -= old_usable;
    }
    glibc_stats.bytes_in_use += malloc_usable_size(new_ptr);
    glibc_stats.bytes_allocated += size;
  }
  return new_ptr;
}

static void *glibc_calloc(size_t nmemb, size_t size) {
  void *ptr = calloc(nmemb, size);
  if (ptr) {
    glibc_stats.alloc_count++;
    glibc_stats.bytes_allocated += nmemb * size;
    glibc_stats.bytes_in_use += malloc_usable_size(ptr);
  }
  return ptr;
}

static void *glibc_memalign(size_t alignment, size_t size) {
  void *ptr = NULL;
  if (posix_memalign(&ptr, alignment, size) == 0) {
    glibc_stats.alloc_count++;
    glibc_stats.bytes_allocated += size;
    glibc_stats.bytes_in_use += malloc_usable_size(ptr);
    return ptr;
  }
  return NULL;
}

static void *glibc_aligned_alloc(size_t alignment, size_t size) {
  // C11 aligned_alloc requires size to be multiple of alignment
  if (size % alignment != 0) {
    return NULL;
  }
  return glibc_memalign(alignment, size);
}

static size_t glibc_usable_size(void *ptr) { return malloc_usable_size(ptr); }

static void glibc_free_sized(void *ptr, size_t size) {
  (void)size; // glibc doesn't use this
  glibc_free(ptr);
}

static void *glibc_realloc_array(void *ptr, size_t nmemb, size_t size) {
  // Check for overflow
  if (nmemb != 0 && size > SIZE_MAX / nmemb) {
    return NULL;
  }
  return glibc_realloc(ptr, nmemb * size);
}

static void glibc_bulk_free(void **ptrs, size_t count) {
  for (size_t i = 0; i < count; i++) {
    glibc_free(ptrs[i]);
  }
}

static void glibc_print_stats(void) {
  printf("  Allocations:   %" PRIu64 "\n", glibc_stats.alloc_count);
  printf("  Frees:         %" PRIu64 "\n", glibc_stats.free_count);
  printf("  Reallocs:      %" PRIu64 "\n", glibc_stats.realloc_count);
  printf("  Bytes in use:  %zu\n", glibc_stats.bytes_in_use);
  printf("  Total alloc'd: %zu\n", glibc_stats.bytes_allocated);
}

static bool glibc_validate_heap(void) {
  // this is a wrapper for glibc, so we just return true, please don't expect me
  // to validate the heap
  return true;
}

static bool glibc_get_stats(allocator_stats_t *stats) {
  if (!stats)
    return false;
  *stats = glibc_stats;
  return true;
}

static int glibc_init(void) {
  if (glibc_initialized)
    return 0;

  memset(&glibc_stats, 0, sizeof(glibc_stats));
  glibc_initialized = true;
  return 0;
}

static void glibc_teardown(void) {
  // Nothing to do for glibc
  glibc_initialized = false;
}

// Allocator Registration

allocator_t glibc_allocator = {
    // Required functions
    .malloc = glibc_malloc,
    .free = glibc_free,
    .realloc = glibc_realloc,
    .calloc = glibc_calloc,

    // Optional functions
    .memalign = glibc_memalign,
    .aligned_alloc = glibc_aligned_alloc,
    .usable_size = glibc_usable_size,
    .free_sized = glibc_free_sized,
    .realloc_array = glibc_realloc_array,
    .bulk_free = glibc_bulk_free,

    // Diagnostics
    .print_stats = glibc_print_stats,
    .validate_heap = glibc_validate_heap,
    .get_stats = glibc_get_stats,

    // Lifecycle
    .init = glibc_init,
    .teardown = glibc_teardown,

    // Metadata
    .name = "glibc",
    .author = "GNU",
    .version = "baseline",
    .description = "Standard glibc malloc wrapper for baseline testing",
    .memory_backend = "glibc-internal",

    .features =
        {
            .thread_safe = true,
            .per_thread_cache = true, // tcache
            .huge_page_support = false,
            .guard_pages = false,
            .guard_location = GUARD_NONE,
            .canaries = false,
            .quarantine = false,
            .zero_on_free = false,
            .min_alignment = 16,
            .max_alignment = 4096,
        },
};

// Accessors for test/bench runners
allocator_t *get_test_allocator(void) { return &glibc_allocator; }
allocator_t *get_bench_allocator(void) { return &glibc_allocator; }
