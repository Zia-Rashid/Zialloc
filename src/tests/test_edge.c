// test_edge.c - Edge Case Test Suite
// Tests: TC-EDGE-*

#include "allocator.h"
#include "test_harness.h"
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// TC-EDGE-001: malloc(SIZE_MAX) should return NULL

static test_result_t test_edge_001(allocator_t *alloc) {
  void *ptr = alloc->malloc(SIZE_MAX);
  TEST_ASSERT_NULL(ptr, "malloc(SIZE_MAX) must return NULL");
  return TEST_PASS;
}

// TC-EDGE-002: malloc(SIZE_MAX - 4096) should return NULL

static test_result_t test_edge_002(allocator_t *alloc) {
  void *ptr = alloc->malloc(SIZE_MAX - 4096);
  TEST_ASSERT_NULL(ptr, "malloc(SIZE_MAX - 4096) must return NULL");
  return TEST_PASS;
}

// TC-EDGE-003: 100,000 × 1-byte allocations

static test_result_t test_edge_003(allocator_t *alloc) {
  void **ptrs = alloc->malloc(100000 * sizeof(void *));
  TEST_ASSERT_NOT_NULL(ptrs, "meta allocation");

  for (size_t i = 0; i < 100000; i++) {
    ptrs[i] = alloc->malloc(1);
    TEST_ASSERT_NOT_NULL(ptrs[i], "1-byte allocation should succeed");
    *(uint8_t *)ptrs[i] = (uint8_t)(i & 0xFF);

    if (i % 10000 == 0) {
      fprintf(stderr, "\r    Progress: %zu/100000 1-byte allocs", i);
    }
  }
  fprintf(stderr, "\r    Verifying and freeing...                    ");

  for (size_t i = 0; i < 100000; i++) {
    TEST_ASSERT(*(uint8_t *)ptrs[i] == (uint8_t)(i & 0xFF),
                "1-byte data integrity");
    alloc->free(ptrs[i]);
  }

  alloc->free(ptrs);
  fprintf(stderr, "\r    Completed 100K 1-byte allocations            \n");

  return TEST_PASS;
}

// TC-EDGE-004: Allocations near page boundaries

static test_result_t test_edge_004(allocator_t *alloc) {
  size_t sizes[] = {4095,      4096,      4097,         4094,     4098,
                    4096 - 16, 4096 + 16, 4096 * 2 - 1, 4096 * 2, 4096 * 2 + 1,
                    8191,      8192,      8193};

  for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
    void *ptr = alloc->malloc(sizes[i]);
    TEST_ASSERT_NOT_NULL(ptr, "page boundary allocation");
    TEST_ASSERT_ALIGNED(ptr, alloc->features.min_alignment,
                        "alignment at page boundary");

    ((uint8_t *)ptr)[0] = 0xAA;
    ((uint8_t *)ptr)[sizes[i] - 1] = 0xBB;
    TEST_ASSERT(((uint8_t *)ptr)[0] == 0xAA, "first byte");
    TEST_ASSERT(((uint8_t *)ptr)[sizes[i] - 1] == 0xBB, "last byte");

    alloc->free(ptr);
  }

  return TEST_PASS;
}

// TC-EDGE-005: Rapid init/teardown cycles

static test_result_t test_edge_005(allocator_t *alloc) {
  alloc->teardown();

  for (size_t cycle = 0; cycle < 100; cycle++) {
    int init_result = alloc->init();
    TEST_ASSERT(init_result == 0, "init should succeed");

    void *ptr1 = alloc->malloc(64);
    void *ptr2 = alloc->malloc(256);
    void *ptr3 = alloc->malloc(1024);

    TEST_ASSERT_NOT_NULL(ptr1, "allocation after init");
    TEST_ASSERT_NOT_NULL(ptr2, "allocation after init");
    TEST_ASSERT_NOT_NULL(ptr3, "allocation after init");

    alloc->free(ptr1);
    alloc->free(ptr2);
    alloc->free(ptr3);

    alloc->teardown();
  }

  int final_init = alloc->init();
  TEST_ASSERT(final_init == 0, "final init");

  return TEST_PASS;
}

// TC-EDGE-006: Alternating small/large allocations

static test_result_t test_edge_006(allocator_t *alloc) {
  for (size_t i = 0; i < 1000; i++) {
    void *small = alloc->malloc(16);
    void *large = alloc->malloc(1048576); // 1 MiB

    TEST_ASSERT_NOT_NULL(small, "small in alternating");
    TEST_ASSERT_NOT_NULL(large, "large in alternating");

    fill_pattern(small, 16, 0xAA);
    ((uint8_t *)large)[0] = 0xBB;
    ((uint8_t *)large)[1048575] = 0xCC;

    alloc->free(small);
    alloc->free(large);
  }

  return TEST_PASS;
}

// TC-EDGE-007: Exact power-of-2 sizes

static test_result_t test_edge_007(allocator_t *alloc) {
  for (size_t exp = 0; exp <= 24; exp++) { // 1 to 16 MiB
    size_t size = (size_t)1 << exp;
    void *ptr = alloc->malloc(size);

    if (ptr == NULL && size >= (1 << 20)) {
      fprintf(stderr, "\n    [INFO] malloc(2^%zu = %zu) returned NULL", exp,
              size);
      continue;
    }
    TEST_ASSERT_NOT_NULL(ptr, "power-of-2 allocation");
    TEST_ASSERT_ALIGNED(ptr, alloc->features.min_alignment,
                        "power-of-2 alignment");

    alloc->free(ptr);
  }

  return TEST_PASS;
}

// TC-EDGE-008: Size class boundaries (common allocator thresholds)

static test_result_t test_edge_008(allocator_t *alloc) {
  size_t boundaries[] = {
      7,   8,   9,   15,  16,   17,   31,   32,   33,   47,
      48,  49,  63,  64,  65,   127,  128,  129,  255,  256,
      257, 511, 512, 513, 1023, 1024, 1025, 2047, 2048, 2049,
  };

  for (size_t i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); i++) {
    void *ptr = alloc->malloc(boundaries[i]);
    TEST_ASSERT_NOT_NULL(ptr, "size class boundary allocation");
    fill_pattern(ptr, boundaries[i], (uint8_t)i);
    TEST_ASSERT(verify_pattern(ptr, boundaries[i], (uint8_t)i),
                "boundary data integrity");
    alloc->free(ptr);
  }

  return TEST_PASS;
}

// TC-EDGE-009: Zero-size calloc edge cases

static test_result_t test_edge_009(allocator_t *alloc) {
  void *ptr1 = alloc->calloc(0, 100);
  void *ptr2 = alloc->calloc(100, 0);
  void *ptr3 = alloc->calloc(0, 0);

  if (ptr1)
    alloc->free(ptr1);
  if (ptr2)
    alloc->free(ptr2);
  if (ptr3)
    alloc->free(ptr3);

  return TEST_PASS;
}

// TC-EDGE-010: realloc growth pattern (doubling)

static test_result_t test_edge_010(allocator_t *alloc) {
  size_t size = 1;
  void *ptr = alloc->malloc(size);
  TEST_ASSERT_NOT_NULL(ptr, "initial malloc(1)");

  while (size < 16 * 1024 * 1024) { // Up to 16 MiB
    size_t new_size = size * 2;
    void *new_ptr = alloc->realloc(ptr, new_size);

    if (new_ptr == NULL) {
      fprintf(stderr,
              "\n    [INFO] realloc to %zu failed (expected for large sizes)",
              new_size);
      alloc->free(ptr);
      return TEST_PASS;
    }

    // Write to new portion
    memset((uint8_t *)new_ptr + size, 0xAA, size);

    ptr = new_ptr;
    size = new_size;
  }

  alloc->free(ptr);
  return TEST_PASS;
}

// TC-EDGE-FORK-001: Fork Safety
static test_result_t test_edge_fork_001(allocator_t *alloc) {
  void *ptr = alloc->malloc(64);
  TEST_ASSERT_NOT_NULL(ptr, "parent malloc");

  pid_t pid = fork();
  if (pid < 0)
    return TEST_FAIL;

  if (pid == 0) {
    void *child_ptr = alloc->malloc(128);
    if (!child_ptr)
      _exit(1);

    memset(child_ptr, 0xCC, 128);
    alloc->free(child_ptr);

    alloc->free(ptr);

    _exit(0); // Success
  }

  alloc->free(ptr);

  int status;
  waitpid(pid, &status, 0);

  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
    return TEST_PASS;
  }
  return TEST_FAIL;
}

test_case_t edge_tests[] = {
    {"TC-EDGE-001", "malloc(SIZE_MAX)", test_edge_001},
    {"TC-EDGE-002", "malloc(SIZE_MAX - 4096)", test_edge_002},
    {"TC-EDGE-003", "100K x 1-byte allocations", test_edge_003},
    {"TC-EDGE-004", "page boundary allocations", test_edge_004},
    {"TC-EDGE-005", "init/teardown cycles", test_edge_005},
    {"TC-EDGE-006", "alternating small/large", test_edge_006},
    {"TC-EDGE-007", "exact power-of-2 sizes", test_edge_007},
    {"TC-EDGE-008", "size class boundaries", test_edge_008},
    {"TC-EDGE-009", "zero-size calloc", test_edge_009},
    {"TC-EDGE-010", "realloc doubling pattern", test_edge_010},
    {"TC-EDGE-FORK-001", "fork safety check", test_edge_fork_001},
};

const size_t num_edge_tests = sizeof(edge_tests) / sizeof(edge_tests[0]);
