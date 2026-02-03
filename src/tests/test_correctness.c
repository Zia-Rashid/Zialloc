// test_correctness.c - Correctness Test Suite
// Tests: TC-BASIC-*, TC-SIZE-*, TC-ALIGN-*, TC-REALLOC-*, TC-CALLOC-*,
// TC-USABLE-*

#include "allocator.h"
#include "test_harness.h"
#include <limits.h>
#include <stdint.h>
#include <string.h>

// TC-BASIC: Basic Operations

// TC-BASIC-001: malloc single allocation
static test_result_t test_basic_001(allocator_t *alloc) {
  void *ptr = alloc->malloc(64);
  TEST_ASSERT_NOT_NULL(ptr, "malloc(64) should return non-NULL");
  alloc->free(ptr);
  return TEST_PASS;
}

// TC-BASIC-002: free single allocation
static test_result_t test_basic_002(allocator_t *alloc) {
  void *ptr = alloc->malloc(64);
  TEST_ASSERT_NOT_NULL(ptr, "malloc(64) should return non-NULL");
  // just hope it doesnt crash
  alloc->free(ptr);
  return TEST_PASS;
}

// TC-BASIC-003: malloc/free cycle
static test_result_t test_basic_003(allocator_t *alloc) {
  for (int i = 0; i < 1000; i++) {
    void *ptr = alloc->malloc(128);
    TEST_ASSERT_NOT_NULL(ptr, "malloc in cycle should succeed");
    fill_pattern(ptr, 128, (uint8_t)i);
    TEST_ASSERT(verify_pattern(ptr, 128, (uint8_t)i),
                "pattern should be preserved");
    alloc->free(ptr);
  }
  return TEST_PASS;
}

// TC-BASIC-004: free(NULL) is no-op
static test_result_t test_basic_004(allocator_t *alloc) {
  // yolo
  alloc->free(NULL);
  alloc->free(NULL);
  alloc->free(NULL);
  return TEST_PASS;
}

// TC-BASIC-005: malloc(0) behavior
static test_result_t test_basic_005(allocator_t *alloc) {
  void *ptr = alloc->malloc(0);
  // Per spec: returns NULL or unique freeable pointer
  // Either is acceptable, but if non-NULL, it must be freeable
  if (ptr != NULL) {
    alloc->free(ptr);
  }
  return TEST_PASS;
}

// TC-SIZE: Size Handling

// TC-SIZE-001: Allocation sizes 1-256 bytes
static test_result_t test_size_001(allocator_t *alloc) {
  for (size_t size = 1; size <= 256; size++) {
    void *ptr = alloc->malloc(size);
    TEST_ASSERT_NOT_NULL(ptr, "small allocation should succeed");
    fill_pattern(ptr, size, (uint8_t)size);
    TEST_ASSERT(verify_pattern(ptr, size, (uint8_t)size), "data integrity");
    alloc->free(ptr);
  }
  return TEST_PASS;
}

// TC-SIZE-002: Allocation sizes 256B-64KB
static test_result_t test_size_002(allocator_t *alloc) {
  size_t sizes[] = {256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
  size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);

  for (size_t i = 0; i < num_sizes; i++) {
    void *ptr = alloc->malloc(sizes[i]);
    TEST_ASSERT_NOT_NULL(ptr, "medium allocation should succeed");
    fill_pattern(ptr, sizes[i], (uint8_t)i);
    TEST_ASSERT(verify_pattern(ptr, sizes[i], (uint8_t)i), "data integrity");
    alloc->free(ptr);
  }
  return TEST_PASS;
}

// TC-SIZE-003: Allocation sizes 64KB-16MB
static test_result_t test_size_003(allocator_t *alloc) {
  size_t sizes[] = {65536, 131072, 262144, 524288, 1048576, 4194304, 16777216};
  size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);

  for (size_t i = 0; i < num_sizes; i++) {
    void *ptr = alloc->malloc(sizes[i]);
    TEST_ASSERT_NOT_NULL(ptr, "large allocation should succeed");
    // Just write to first and last bytes to verify access
    ((uint8_t *)ptr)[0] = 0xAA;
    ((uint8_t *)ptr)[sizes[i] - 1] = 0xBB;
    TEST_ASSERT(((uint8_t *)ptr)[0] == 0xAA, "first byte");
    TEST_ASSERT(((uint8_t *)ptr)[sizes[i] - 1] == 0xBB, "last byte");
    alloc->free(ptr);
  }
  return TEST_PASS;
}

// TC-SIZE-004: Allocation sizes 16MB-256MB
static test_result_t test_size_004(allocator_t *alloc) {
  size_t sizes[] = {16777216, 33554432, 67108864, 134217728, 268435456};
  size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);

  for (size_t i = 0; i < num_sizes; i++) {
    void *ptr = alloc->malloc(sizes[i]);
    // These might fail on memory-constrained systems
    if (ptr == NULL) {
      fprintf(stderr,
              "    [INFO] malloc(%zu) returned NULL (may be expected)\n",
              sizes[i]);
      continue;
    }
    ((uint8_t *)ptr)[0] = 0xAA;
    ((uint8_t *)ptr)[sizes[i] - 1] = 0xBB;
    alloc->free(ptr);
  }
  return TEST_PASS;
}

// TC-SIZE-005: Allocation near SIZE_MAX
static test_result_t test_size_005(allocator_t *alloc) {
  // These should all return NULL
  void *ptr1 = alloc->malloc(SIZE_MAX);
  TEST_ASSERT_NULL(ptr1, "malloc(SIZE_MAX) should return NULL");

  void *ptr2 = alloc->malloc(SIZE_MAX - 4096);
  TEST_ASSERT_NULL(ptr2, "malloc(SIZE_MAX - 4096) should return NULL");

  void *ptr3 = alloc->malloc(SIZE_MAX / 2);
  // if it aint null, free it
  if (ptr3)
    alloc->free(ptr3);

  return TEST_PASS;
}

// TC-ALIGN: Alignment Tests

// TC-ALIGN-001: 16-byte alignment for all sizes
static test_result_t test_align_001(allocator_t *alloc) {
  for (size_t size = 1; size <= 4096; size *= 2) {
    void *ptr = alloc->malloc(size);
    TEST_ASSERT_NOT_NULL(ptr, "allocation should succeed");
    TEST_ASSERT_ALIGNED(ptr, 16, "pointer should be 16-byte aligned");
    alloc->free(ptr);
  }
  // Also test non-power-of-2 sizes
  size_t odd_sizes[] = {1, 7, 13, 31, 65, 127, 257, 1023};
  for (size_t i = 0; i < sizeof(odd_sizes) / sizeof(odd_sizes[0]); i++) {
    void *ptr = alloc->malloc(odd_sizes[i]);
    TEST_ASSERT_NOT_NULL(ptr, "allocation should succeed");
    TEST_ASSERT_ALIGNED(ptr, 16, "odd size should be 16-byte aligned");
    alloc->free(ptr);
  }
  return TEST_PASS;
}

// TC-ALIGN-002: memalign power-of-2 alignments
static test_result_t test_align_002(allocator_t *alloc) {
  TEST_SKIP_IF(!ALLOC_HAS(alloc, memalign), "memalign not implemented");

  size_t alignments[] = {16, 32, 64, 128, 256, 512, 1024, 2048};
  for (size_t i = 0; i < sizeof(alignments) / sizeof(alignments[0]); i++) {
    void *ptr = alloc->memalign(alignments[i], 256);
    TEST_ASSERT_NOT_NULL(ptr, "memalign should succeed");
    TEST_ASSERT_ALIGNED(ptr, alignments[i],
                        "should be aligned to requested alignment");
    alloc->free(ptr);
  }
  return TEST_PASS;
}

// TC-ALIGN-003: memalign large alignments (4KB)
static test_result_t test_align_003(allocator_t *alloc) {
  TEST_SKIP_IF(!ALLOC_HAS(alloc, memalign), "memalign not implemented");

  void *ptr = alloc->memalign(4096, 8192);
  TEST_ASSERT_NOT_NULL(ptr, "memalign(4096, 8192) should succeed");
  TEST_ASSERT_ALIGNED(ptr, 4096, "should be page-aligned");
  alloc->free(ptr);
  return TEST_PASS;
}

// TC-REALLOC: Realloc Tests

// TC-REALLOC-001: Grow allocation
static test_result_t test_realloc_001(allocator_t *alloc) {
  void *ptr = alloc->malloc(64);
  TEST_ASSERT_NOT_NULL(ptr, "initial malloc");
  fill_pattern(ptr, 64, 0x42);

  void *new_ptr = alloc->realloc(ptr, 256);
  TEST_ASSERT_NOT_NULL(new_ptr, "realloc to larger size");
  TEST_ASSERT(verify_pattern(new_ptr, 64, 0x42), "original data preserved");

  alloc->free(new_ptr);
  return TEST_PASS;
}

// TC-REALLOC-002: Shrink allocation
static test_result_t test_realloc_002(allocator_t *alloc) {
  void *ptr = alloc->malloc(256);
  TEST_ASSERT_NOT_NULL(ptr, "initial malloc");
  fill_pattern(ptr, 256, 0x42);

  void *new_ptr = alloc->realloc(ptr, 64);
  TEST_ASSERT_NOT_NULL(new_ptr, "realloc to smaller size");
  TEST_ASSERT(verify_pattern(new_ptr, 64, 0x42),
              "data preserved up to new size");

  alloc->free(new_ptr);
  return TEST_PASS;
}

// TC-REALLOC-003: realloc(NULL, n) == malloc(n)
static test_result_t test_realloc_003(allocator_t *alloc) {
  void *ptr = alloc->realloc(NULL, 128);
  TEST_ASSERT_NOT_NULL(ptr, "realloc(NULL, n) should act like malloc");
  fill_pattern(ptr, 128, 0x55);
  TEST_ASSERT(verify_pattern(ptr, 128, 0x55), "memory usable");
  alloc->free(ptr);
  return TEST_PASS;
}

// TC-REALLOC-004: realloc(ptr, 0) == free(ptr)
static test_result_t test_realloc_004(allocator_t *alloc) {
  void *ptr = alloc->malloc(128);
  TEST_ASSERT_NOT_NULL(ptr, "initial malloc");

  void *result = alloc->realloc(ptr, 0);
  // Per spec: returns NULL (ptr has been freed)
  TEST_ASSERT_NULL(result, "realloc(ptr, 0) should return NULL");
  // ptr is now freed, don't use it
  return TEST_PASS;
}

// TC-REALLOC-005: realloc same size
static test_result_t test_realloc_005(allocator_t *alloc) {
  void *ptr = alloc->malloc(128);
  TEST_ASSERT_NOT_NULL(ptr, "initial malloc");
  fill_pattern(ptr, 128, 0x77);

  void *new_ptr = alloc->realloc(ptr, 128);
  TEST_ASSERT_NOT_NULL(new_ptr, "realloc same size");
  TEST_ASSERT(verify_pattern(new_ptr, 128, 0x77), "data preserved");

  alloc->free(new_ptr);
  return TEST_PASS;
}

// TC-REALLOC-006: Contents preserved on grow
static test_result_t test_realloc_006(allocator_t *alloc) {
  size_t original_size = 100;
  void *ptr = alloc->malloc(original_size);
  TEST_ASSERT_NOT_NULL(ptr, "initial malloc");

  // Fill with specific pattern
  for (size_t i = 0; i < original_size; i++) {
    ((uint8_t *)ptr)[i] = (uint8_t)(i ^ 0xAB);
  }

  void *new_ptr = alloc->realloc(ptr, 500);
  TEST_ASSERT_NOT_NULL(new_ptr, "realloc grow");

  // Verify original data
  for (size_t i = 0; i < original_size; i++) {
    TEST_ASSERT(((uint8_t *)new_ptr)[i] == (uint8_t)(i ^ 0xAB),
                "data corruption on grow");
  }

  alloc->free(new_ptr);
  return TEST_PASS;
}

// TC-REALLOC-007: Contents preserved on shrink
static test_result_t test_realloc_007(allocator_t *alloc) {
  size_t original_size = 500;
  size_t new_size = 100;
  void *ptr = alloc->malloc(original_size);
  TEST_ASSERT_NOT_NULL(ptr, "initial malloc");

  for (size_t i = 0; i < original_size; i++) {
    ((uint8_t *)ptr)[i] = (uint8_t)(i ^ 0xCD);
  }

  void *new_ptr = alloc->realloc(ptr, new_size);
  TEST_ASSERT_NOT_NULL(new_ptr, "realloc shrink");

  for (size_t i = 0; i < new_size; i++) {
    TEST_ASSERT(((uint8_t *)new_ptr)[i] == (uint8_t)(i ^ 0xCD),
                "data corruption on shrink");
  }

  alloc->free(new_ptr);
  return TEST_PASS;
}

// TC-REALLOC-008: Realloc of Aligned Memory
// Verifies that realloc works correctly on pointers returned by memalign.
static test_result_t test_realloc_008(allocator_t *alloc) {
  if (!alloc->memalign)
    return TEST_SKIP;

  size_t align = 4096;
  size_t size = 128;
  void *ptr = alloc->memalign(align, size);
  TEST_ASSERT_NOT_NULL(ptr, "memalign failed");
  TEST_ASSERT(((uintptr_t)ptr % align) == 0, "initial alignment failed");

  memset(ptr, 0xEE, size);

  size_t new_size = 8192;
  void *new_ptr = alloc->realloc(ptr, new_size);
  TEST_ASSERT_NOT_NULL(new_ptr, "realloc of aligned ptr failed");

  unsigned char *p = (unsigned char *)new_ptr;
  for (size_t i = 0; i < size; i++) {
    if (p[i] != 0xEE)
      return TEST_FAIL;
  }

  alloc->free(new_ptr);
  return TEST_PASS;
}

// TC-CALLOC: Calloc Tests

// TC-CALLOC-001: Zero initialization
static test_result_t test_calloc_001(allocator_t *alloc) {
  void *ptr = alloc->calloc(100, 8);
  TEST_ASSERT_NOT_NULL(ptr, "calloc should succeed");
  TEST_ASSERT(is_zeroed(ptr, 800), "memory should be zero-initialized");
  alloc->free(ptr);
  return TEST_PASS;
}

// TC-CALLOC-002: Overflow SIZE_MAX × 2
static test_result_t test_calloc_002(allocator_t *alloc) {
  void *ptr = alloc->calloc(SIZE_MAX, 2);
  TEST_ASSERT_NULL(ptr, "calloc with overflow should return NULL");
  return TEST_PASS;
}

// TC-CALLOC-003: Overflow (SIZE_MAX/2+2) × 2
static test_result_t test_calloc_003(allocator_t *alloc) {
  void *ptr = alloc->calloc(SIZE_MAX / 2 + 2, 2);
  TEST_ASSERT_NULL(ptr, "calloc with overflow should return NULL");
  return TEST_PASS;
}

// TC-CALLOC-004: Large array allocation
static test_result_t test_calloc_004(allocator_t *alloc) {
  void *ptr = alloc->calloc(1024, 1024); // 1 MiB
  TEST_ASSERT_NOT_NULL(ptr, "1 MiB calloc should succeed");
  TEST_ASSERT(is_zeroed(ptr, 1024 * 1024), "should be zeroed");
  alloc->free(ptr);
  return TEST_PASS;
}

// TC-USABLE: usable_size Tests

// TC-USABLE-001: usable_size >= requested
static test_result_t test_usable_001(allocator_t *alloc) {
  TEST_SKIP_IF(!ALLOC_HAS(alloc, usable_size), "usable_size not implemented");

  size_t sizes[] = {1, 7, 16, 64, 100, 256, 1000, 4096};
  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    void *ptr = alloc->malloc(sizes[i]);
    TEST_ASSERT_NOT_NULL(ptr, "malloc should succeed");

    size_t usable = alloc->usable_size(ptr);
    TEST_ASSERT(usable >= sizes[i], "usable_size should be >= requested");

    alloc->free(ptr);
  }
  return TEST_PASS;
}

// TC-USABLE-002: Write to full usable size
static test_result_t test_usable_002(allocator_t *alloc) {
  TEST_SKIP_IF(!ALLOC_HAS(alloc, usable_size), "usable_size not implemented");

  void *ptr = alloc->malloc(100);
  TEST_ASSERT_NOT_NULL(ptr, "malloc should succeed");

  size_t usable = alloc->usable_size(ptr);
  // Write to entire usable size - this should not crash or corrupt
  fill_pattern(ptr, usable, 0x99);
  TEST_ASSERT(verify_pattern(ptr, usable, 0x99),
              "full usable size should be writable");

  alloc->free(ptr);
  return TEST_PASS;
}

// Test Registration

test_case_t correctness_tests[] = {
    // Basic operations
    {"TC-BASIC-001", "malloc single allocation", test_basic_001},
    {"TC-BASIC-002", "free single allocation", test_basic_002},
    {"TC-BASIC-003", "malloc/free cycle", test_basic_003},
    {"TC-BASIC-004", "free(NULL) is no-op", test_basic_004},
    {"TC-BASIC-005", "malloc(0) behavior", test_basic_005},

    // Size handling
    {"TC-SIZE-001", "sizes 1-256 bytes", test_size_001},
    {"TC-SIZE-002", "sizes 256B-64KB", test_size_002},
    {"TC-SIZE-003", "sizes 64KB-16MB", test_size_003},
    {"TC-SIZE-004", "sizes 16MB-256MB", test_size_004},
    {"TC-SIZE-005", "sizes near SIZE_MAX", test_size_005},

    // Alignment
    {"TC-ALIGN-001", "16-byte alignment", test_align_001},
    {"TC-ALIGN-002", "memalign power-of-2", test_align_002},
    {"TC-ALIGN-003", "memalign 4KB page", test_align_003},

    // Realloc
    {"TC-REALLOC-001", "grow allocation", test_realloc_001},
    {"TC-REALLOC-002", "shrink allocation", test_realloc_002},
    {"TC-REALLOC-003", "realloc(NULL, n)", test_realloc_003},
    {"TC-REALLOC-004", "realloc(ptr, 0)", test_realloc_004},
    {"TC-REALLOC-005", "realloc same size", test_realloc_005},
    {"TC-REALLOC-006", "contents preserved grow", test_realloc_006},
    {"TC-REALLOC-007", "contents preserved shrink", test_realloc_007},
    {"TC-REALLOC-008", "realloc aligned memory", test_realloc_008},

    // Calloc
    {"TC-CALLOC-001", "zero initialization", test_calloc_001},
    {"TC-CALLOC-002", "overflow SIZE_MAXx2", test_calloc_002},
    {"TC-CALLOC-003", "overflow (SIZE_MAX/2+2)x2", test_calloc_003},
    {"TC-CALLOC-004", "large array", test_calloc_004},

    // Usable size
    {"TC-USABLE-001", "usable_size >= requested", test_usable_001},
    {"TC-USABLE-002", "write full usable size", test_usable_002},
};

const size_t num_correctness_tests =
    sizeof(correctness_tests) / sizeof(correctness_tests[0]);
