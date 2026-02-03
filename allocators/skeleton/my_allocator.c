#include "allocator.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Replace these stubs with your own logic.
 */

static int my_init(void) { return 0; }

static void my_teardown(void) {}

static void *my_malloc(size_t size) {
  (void)size;
  return NULL;
}

static void my_free(void *ptr) { (void)ptr; }

static void *my_realloc(void *ptr, size_t size) {
  (void)ptr;
  (void)size;
  return NULL;
}

static void *my_calloc(size_t nmemb, size_t size) {
  (void)nmemb;
  (void)size;
  return NULL;
}

allocator_t allocator = {.malloc = my_malloc,
                         .free = my_free,
                         .realloc = my_realloc,
                         .calloc = my_calloc,
                         .init = my_init,
                         .teardown = my_teardown,
                         .name = "studentv1",
                         .author = "Student Name",
                         .version = "0.1.0",
                         .description = "My first custom allocator",
                         .memory_backend = "none",
                         .features = {.thread_safe = false,
                                      .per_thread_cache = false,
                                      .huge_page_support = false,
                                      .guard_pages = false,
                                      .guard_location = GUARD_NONE,
                                      .min_alignment = 8,
                                      .max_alignment = 4096}};

allocator_t *get_test_allocator(void) { return &allocator; }

allocator_t *get_bench_allocator(void) { return &allocator; }