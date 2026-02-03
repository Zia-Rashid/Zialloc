#ifndef ALLOCATOR_H
#define ALLOCATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  GUARD_NONE = 0,
  GUARD_BEFORE = 1 << 0,
  GUARD_AFTER = 1 << 1,
  GUARD_BOTH = (1 << 0) | (1 << 1)
} guard_location_t;

typedef struct allocator_features {
  bool thread_safe;
  bool per_thread_cache;
  bool huge_page_support;
  bool guard_pages;
  guard_location_t guard_location; // New field
  bool canaries;
  bool quarantine;
  bool zero_on_free;
  size_t min_alignment;
  size_t max_alignment;
} allocator_features_t;

// Runtime statistics
typedef struct allocator_stats {
  size_t bytes_allocated; // Total bytes ever allocated
  size_t bytes_in_use;    // Currently allocated bytes
  size_t bytes_metadata;  // Overhead for bookkeeping
  size_t bytes_mapped;    // Total mmap'd memory
  uint64_t alloc_count;   // Number of malloc calls
  uint64_t free_count;    // Number of free calls
  uint64_t realloc_count; // Number of realloc calls
  uint64_t mmap_count;    // Number of mmap calls
  uint64_t munmap_count;  // Number of munmap calls
} allocator_stats_t;

// Main allocator interface
typedef struct allocator {
  // Required functions
  void *(*malloc)(size_t size);
  void (*free)(void *ptr);
  void *(*realloc)(void *ptr, size_t size);
  void *(*calloc)(size_t nmemb, size_t size);

  // Optional functions (can be NULL)
  void *(*memalign)(size_t alignment, size_t size);
  void *(*aligned_alloc)(size_t alignment, size_t size);
  size_t (*usable_size)(void *ptr);
  void (*free_sized)(void *ptr, size_t size);
  void *(*realloc_array)(void *ptr, size_t nmemb, size_t size);
  void (*bulk_free)(void **ptrs, size_t count);

  // Diagnostics (optional but recommended)
  void (*print_stats)(void);
  bool (*validate_heap)(void);
  bool (*get_stats)(allocator_stats_t *stats);

  // Lifecycle
  int (*init)(void);
  void (*teardown)(void);

  // Metadata
  const char *name;
  const char *author;
  const char *version;
  const char *description;
  const char *memory_backend;

  allocator_features_t features;
} allocator_t;

// Helper macro to check if optional function is implemented
#define ALLOC_HAS(a, fn) ((a)->fn != NULL)

// Version info
#define ALLOC_VERSION_MAJOR 1
#define ALLOC_VERSION_MINOR 0
#define ALLOC_VERSION_PATCH 0

#ifdef __cplusplus
}
#endif

#endif // ALLOCATOR_H
