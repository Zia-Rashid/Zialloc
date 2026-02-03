// test_realistic.c - Realistic Workload Tests
// Tests: WL-REAL-*

#include "allocator.h"
#include "test_harness.h"
#include <stdint.h>
#include <string.h>

// WL-REAL-001: Redis (YCSB) trace simulation
// Pattern: Many small string allocations, varying sizes, significant churn
static test_result_t test_realistic_redis_ycsb(allocator_t *alloc) {
  test_rng_t rng;
  rng_seed(&rng, 0x12341234);

  const size_t num_ops = 100000;
  const size_t max_live = 5000;
  void *ptrs[5000] = {0};
  size_t sizes[5000] = {0};
  size_t count = 0;

  for (size_t i = 0; i < num_ops; i++) {
    bool do_alloc =
        (count == 0) || (rng_next(&rng) % 10 < 6 && count < max_live);

    if (do_alloc) {
      // Redis string sizes: mostly small (8-256), occasionally large (up to
      // 64KB)
      size_t size;
      uint64_t roll = rng_next(&rng) % 100;
      if (roll < 80)
        size = rng_range(&rng, 8, 256);
      else if (roll < 95)
        size = rng_range(&rng, 256, 4096);
      else
        size = rng_range(&rng, 4096, 65536);

      void *ptr = alloc->malloc(size);
      TEST_ASSERT_NOT_NULL(ptr, "Redis-like malloc failed");

      ptrs[count] = ptr;
      sizes[count] = size;
      count++;
    } else {
      size_t idx = rng_next(&rng) % count;
      alloc->free(ptrs[idx]);
      ptrs[idx] = ptrs[count - 1];
      sizes[idx] = sizes[count - 1];
      count--;
    }
  }

  // Cleanup
  for (size_t i = 0; i < count; i++) {
    alloc->free(ptrs[i]);
  }

  return TEST_PASS;
}

// WL-REAL-002: SQLite (TPC-C) trace simulation
// Pattern: Fixed-size page allocations (e.g., 4KB) mixed with small metadata
static test_result_t test_realistic_sqlite_tpcc(allocator_t *alloc) {
  test_rng_t rng;
  rng_seed(&rng, 0x5117E);

  const size_t num_ops = 50000;
  void *pages[2048] = {0};
  void *metadata[4096] = {0};
  size_t page_count = 0;
  size_t meta_count = 0;

  for (size_t i = 0; i < num_ops; i++) {
    uint64_t roll = rng_next(&rng) % 100;

    if (roll < 30) { // Page operation
      if (page_count < 2048 && (rng_next(&rng) % 2 == 0 || page_count == 0)) {
        pages[page_count++] = alloc->malloc(4096);
      } else if (page_count > 0) {
        size_t idx = rng_next(&rng) % page_count;
        alloc->free(pages[idx]);
        pages[idx] = pages[--page_count];
      }
    } else { // Metadata operation
      if (meta_count < 4096 && (rng_next(&rng) % 2 == 0 || meta_count == 0)) {
        metadata[meta_count++] = alloc->malloc(rng_range(&rng, 16, 128));
      } else if (meta_count > 0) {
        size_t idx = rng_next(&rng) % meta_count;
        alloc->free(metadata[idx]);
        metadata[idx] = metadata[--meta_count];
      }
    }
  }

  for (size_t i = 0; i < page_count; i++)
    alloc->free(pages[i]);
  for (size_t i = 0; i < meta_count; i++)
    alloc->free(metadata[i]);

  return TEST_PASS;
}

// WL-REAL-003: Firefox Page Load trace simulation
// Pattern: High churn, mixed sizes, some very large buffers (images/scripts)
static test_result_t test_realistic_firefox_load(allocator_t *alloc) {
  test_rng_t rng;
  rng_seed(&rng, 0xF12EF0);

  const size_t num_ops = 80000;
  void *ptrs[8192] = {0};
  size_t count = 0;

  for (size_t i = 0; i < num_ops; i++) {
    if (count < 8192 && (rng_next(&rng) % 10 < 6 || count == 0)) {
      size_t size;
      uint64_t roll = rng_next(&rng) % 100;
      if (roll < 60)
        size = rng_range(&rng, 16, 512); // Small objects (DOM nodes)
      else if (roll < 90)
        size = rng_range(&rng, 512, 16384); // Scripts, CSS
      else
        size = rng_range(&rng, 16384, 1024 * 1024); // Images, Video chunks

      void *ptr = alloc->malloc(size);
      if (ptr)
        ptrs[count++] = ptr;
    } else if (count > 0) {
      size_t idx = rng_next(&rng) % count;
      alloc->free(ptrs[idx]);
      ptrs[idx] = ptrs[--count];
    }
  }

  for (size_t i = 0; i < count; i++)
    alloc->free(ptrs[i]);
  return TEST_PASS;
}

// WL-REAL-004: Burst Allocations
// Pattern: "Burst" allocations (batching) then bulk freeing
static test_result_t test_realistic_custom_burst(allocator_t *alloc) {
  test_rng_t rng;
  rng_seed(&rng, 0xABCDEF);

  for (int burst = 0; burst < 100; burst++) {
    size_t burst_size = rng_range(&rng, 100, 1000);
    void **ptrs = alloc->malloc(burst_size * sizeof(void *));
    TEST_ASSERT_NOT_NULL(ptrs, "Batch descriptor malloc failed");

    for (size_t i = 0; i < burst_size; i++) {
      ptrs[i] = alloc->malloc(rng_range(&rng, 8, 2048));
      if (ptrs[i])
        memset(ptrs[i], 0x55, 8);
    }

    for (size_t i = 0; i < burst_size; i++) {
      if (ptrs[i])
        alloc->free(ptrs[i]);
    }
    alloc->free(ptrs);
  }

  return TEST_PASS;
}

// WL-REAL-005: Non-Standard Allocations
// Pattern: Allocations with non-standard alignment
static test_result_t
test_realistic_nonstandard_allocations(allocator_t *alloc) {
  test_rng_t rng;
  rng_seed(&rng, 555);

  const size_t num_allocs = 100;
  void *ptrs[num_allocs];
  size_t alignments[] = {32, 64, 128, 256, 512, 1024, 2048, 4096};

  for (size_t i = 0; i < num_allocs; i++) {
    size_t align = alignments[rng_range(&rng, 0, 7)];
    size_t size = rng_range(&rng, 1, 16384);

    if (size % align != 0) {
      size = ((size / align) + 1) * align;
    }

    if (alloc->aligned_alloc) {
      ptrs[i] = alloc->aligned_alloc(align, size);
      TEST_ASSERT_NOT_NULL(ptrs[i], "Aligned allocation failed");
      TEST_ASSERT(((uintptr_t)ptrs[i] % align) == 0,
                  "Alignment verification failed");
    } else if (alloc->memalign) {
      ptrs[i] = alloc->memalign(align, size);
      TEST_ASSERT_NOT_NULL(ptrs[i], "Memalign allocation failed");
      TEST_ASSERT(((uintptr_t)ptrs[i] % align) == 0,
                  "Alignment verification failed");
    } else {
      // Skip if no alignment support
      ptrs[i] = NULL;
    }
  }

  for (size_t i = 0; i < num_allocs; i++) {
    if (ptrs[i])
      alloc->free(ptrs[i]);
  }

  return TEST_PASS;
}

// WL-REAL-006: Pseudo-SIMD allocations
// Pattern: Simulating vector processing with 64-byte (AVX-512) aligned buffers
static test_result_t test_realistic_simd_allocations(allocator_t *alloc) {
  test_rng_t rng;
  rng_seed(&rng, 0x5140);

  const size_t num_vectors = 500;
  void *vectors[500] = {0};
  const size_t vector_size = 64; // AVX-512 vector size

  for (size_t i = 0; i < num_vectors; i++) {
    if (alloc->aligned_alloc) {
      vectors[i] = alloc->aligned_alloc(64, vector_size);
    } else if (alloc->memalign) {
      vectors[i] = alloc->memalign(64, vector_size);
    } else {
      vectors[i] = alloc->malloc(vector_size);
    }

    if (vectors[i]) {
      if (alloc->aligned_alloc || alloc->memalign) {
        TEST_ASSERT(((uintptr_t)vectors[i] % 64) == 0,
                    "SIMD vector not 64-byte aligned");
      }
      memset(vectors[i], 0xAB, vector_size);
    }
  }

  // Process and free
  for (size_t i = 0; i < num_vectors; i++) {
    if (vectors[i])
      alloc->free(vectors[i]);
  }

  return TEST_PASS;
}

test_case_t realistic_tests[] = {
    {"WL-REAL-001", "Redis YCSB workload trace", test_realistic_redis_ycsb},
    {"WL-REAL-002", "SQLite TPC-C trace", test_realistic_sqlite_tpcc},
    {"WL-REAL-003", "Firefox Page load trace", test_realistic_firefox_load},
    {"WL-REAL-004", "Custom Application-burst workload",
     test_realistic_custom_burst},
    {"WL-REAL-005", "Non-Standard Allocations",
     test_realistic_nonstandard_allocations},
    {"WL-REAL-006", "Pseudo-SIMD allocations", test_realistic_simd_allocations},
};

const size_t num_realistic_tests =
    sizeof(realistic_tests) / sizeof(realistic_tests[0]);
